#include "view/shelf_gl_widget.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <QMouseEvent>
#include <QWheelEvent>

namespace {

// Grid spacing between adjacent part slots, on the XZ plane.
constexpr float kSpacing = 1.5f;

constexpr float kMinDistance = 2.0f;
constexpr float kMaxDistance = 60.0f;
constexpr float kElevationLimit = 85.0f;
constexpr float kOrbitSensitivity = 0.4f;   // degrees per pixel of drag
constexpr double kZoomFactorPerNotch = 0.999;  // exponential zoom base

const char* kVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in float inAo;

uniform mat4 mvp;
uniform mat4 model;

out vec3 vNormal;
out float vAo;

void main() {
    gl_Position = mvp * vec4(inPosition, 1.0);
    // model is translation-only (scaling happens at upload), so mat3(model)
    // transforms normals correctly.
    vNormal = mat3(model) * inNormal;
    vAo = inAo;
}
)";

const char* kFragmentShaderSource = R"(#version 330 core
in vec3 vNormal;
in float vAo;

uniform vec3 lightDir;
uniform vec3 baseColor;

out vec4 fragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(lightDir);
    // vAo < 0.0: AO not computed yet, draw flat gray.
    if (vAo < 0.0) {
        fragColor = vec4(vec3(0.30, 0.32, 0.36) * (0.55 + 0.45 * max(dot(N, L), 0.0)), 1.0);
    } else {
        float diffuse = 0.25 + 0.75 * max(dot(N, L), 0.0);
        float aoTerm = mix(0.35, 1.0, vAo);
        fragColor = vec4(baseColor * diffuse * aoTerm, 1.0);
    }
}
)";

// HSV -> RGB, s and v in [0, 1], h in [0, 1) (wraps).
QVector3D hsvToRgb(double h, double s, double v) {
    const double hh = (h - std::floor(h)) * 6.0;
    const int sector = static_cast<int>(hh);
    const double f = hh - sector;
    const double p = v * (1.0 - s);
    const double q = v * (1.0 - s * f);
    const double t = v * (1.0 - s * (1.0 - f));

    double r = v, g = v, b = v;
    switch (sector) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return QVector3D(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
}

}  // namespace

ShelfGlWidget::ShelfGlWidget(QWidget* parent) : QOpenGLWidget(parent) {}

ShelfGlWidget::~ShelfGlWidget() {
    // Every GpuPart's buffers must be freed before the context goes away,
    // so bracket the deletes in a makeCurrent/doneCurrent pair.
    makeCurrent();
    for (GpuPart& part : parts_) {
        part.destroyGl();
    }
    for (GpuPart& part : deferredRelease_) {
        part.destroyGl();
    }
    doneCurrent();
}

void ShelfGlWidget::beginGeneration(int slotCount) {
    for (GpuPart& part : parts_) {
        deferredRelease_.push_back(std::move(part));
    }
    parts_.clear();

    // Anything still queued targets a slot from the old grid, so it's
    // dropped along with the generation it belonged to.
    pendingUploads_.clear();

    slotCount_ = slotCount;
    columns_ = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(std::max(slotCount, 1)))));

    update();
}

void ShelfGlWidget::setPartMesh(std::uint64_t partId, int slotIndex, const PartMeshData& data) {
    pendingUploads_.push_back(PendingUpload{partId, slotIndex, data});
    update();
}

void ShelfGlWidget::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.13f, 0.14f, 0.16f, 1.0f);

    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSource);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSource);
    program_.link();
}

void ShelfGlWidget::resizeGL(int w, int h) {
    const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
    projection_.setToIdentity();
    // 45-degree vertical FOV. Far plane (100) must clear the worst case:
    // kMaxDistance (60) plus an 8x8 shelf's grid radius.
    projection_.perspective(45.0f, aspect, 0.1f, 100.0f);
}

void ShelfGlWidget::paintGL() {
    // Only place GL objects are created/destroyed (context guaranteed
    // current here). Release before upload, so both generations' GPU
    // memory is never held at once.
    releaseDeferred();
    drainPendingUploads();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (parts_.empty() || !program_.isLinked()) {
        return;
    }

    QMatrix4x4 view;
    view.lookAt(cameraEye(), gridCenter(), QVector3D(0.0f, 1.0f, 0.0f));

    program_.bind();
    // Fixed key light -- no lighting UI in this example.
    program_.setUniformValue("lightDir", QVector3D(0.4f, 0.8f, 0.5f));

    for (const GpuPart& part : parts_) {
        QMatrix4x4 model;
        model.translate(gridPosition(part.slotIndex));
        const QMatrix4x4 mvp = projection_ * view * model;

        program_.setUniformValue("mvp", mvp);
        program_.setUniformValue("model", model);
        program_.setUniformValue("baseColor", part.baseColor);

        part.vao->bind();
        glDrawElements(GL_TRIANGLES, part.indexCount, GL_UNSIGNED_INT, nullptr);
        part.vao->release();
    }

    program_.release();
}

void ShelfGlWidget::releaseDeferred() {
    for (GpuPart& part : deferredRelease_) {
        part.destroyGl();
    }
    deferredRelease_.clear();
}

void ShelfGlWidget::drainPendingUploads() {
    // Must REPLACE an existing part's GpuPart, not accumulate a second one,
    // or superseded GpuParts leak their GL buffers. The old one moves to
    // deferredRelease_ for the next releaseDeferred() pass.
    for (const PendingUpload& pending : pendingUploads_) {
        const auto existing =
            std::find_if(parts_.begin(), parts_.end(),
                         [&pending](const GpuPart& part) { return part.partId == pending.partId; });
        if (existing != parts_.end()) {
            deferredRelease_.push_back(std::move(*existing));
            parts_.erase(existing);
        }
        parts_.push_back(uploadPart(pending));
    }
    pendingUploads_.clear();
}

ShelfGlWidget::GpuPart ShelfGlWidget::uploadPart(const PendingUpload& pending) {
    const PartMeshData& data = pending.data;
    const std::size_t vertexCount = data.ao.size();

    // Normalize at upload time: center and uniformly scale by the AABB so
    // every part fits a unit cube, regardless of worklib's raw scale.
    QVector3D minP(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max());
    QVector3D maxP(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest());
    for (std::size_t v = 0; v < vertexCount; ++v) {
        const QVector3D p(data.positions[v * 3 + 0], data.positions[v * 3 + 1], data.positions[v * 3 + 2]);
        minP.setX(std::min(minP.x(), p.x()));
        minP.setY(std::min(minP.y(), p.y()));
        minP.setZ(std::min(minP.z(), p.z()));
        maxP.setX(std::max(maxP.x(), p.x()));
        maxP.setY(std::max(maxP.y(), p.y()));
        maxP.setZ(std::max(maxP.z(), p.z()));
    }
    const QVector3D center = (minP + maxP) * 0.5f;
    const QVector3D extent = maxP - minP;
    const float maxExtent = std::max({extent.x(), extent.y(), extent.z(), 1e-6f});
    const float scale = 1.0f / maxExtent;

    std::vector<float> interleaved(vertexCount * 7);
    for (std::size_t v = 0; v < vertexCount; ++v) {
        const QVector3D p((data.positions[v * 3 + 0] - center.x()) * scale,
                           (data.positions[v * 3 + 1] - center.y()) * scale,
                           (data.positions[v * 3 + 2] - center.z()) * scale);
        float* out = &interleaved[v * 7];
        out[0] = p.x();
        out[1] = p.y();
        out[2] = p.z();
        out[3] = data.normals[v * 3 + 0];
        out[4] = data.normals[v * 3 + 1];
        out[5] = data.normals[v * 3 + 2];
        out[6] = data.ao[v];
    }

    GpuPart part;
    part.vao = std::make_unique<QOpenGLVertexArrayObject>();
    part.vao->create();
    part.vao->bind();

    part.vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    part.vbo->create();
    part.vbo->bind();
    part.vbo->allocate(interleaved.data(), static_cast<int>(interleaved.size() * sizeof(float)));

    part.ibo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer);
    part.ibo->create();
    part.ibo->bind();
    part.ibo->allocate(data.indices.data(), static_cast<int>(data.indices.size() * sizeof(quint32)));

    constexpr int kStride = static_cast<int>(7 * sizeof(float));
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3, kStride);
    program_.enableAttributeArray(1);
    program_.setAttributeBuffer(1, GL_FLOAT, static_cast<int>(3 * sizeof(float)), 3, kStride);
    program_.enableAttributeArray(2);
    program_.setAttributeBuffer(2, GL_FLOAT, static_cast<int>(6 * sizeof(float)), 1, kStride);

    // Element-array binding is VAO state -- VAO must still be bound when
    // the IBO releases below, so release the VAO first.
    part.vao->release();
    part.vbo->release();
    part.ibo->release();

    part.indexCount = static_cast<int>(data.indices.size());
    part.slotIndex = pending.slotIndex;
    part.partId = pending.partId;
    part.baseColor = colorForPartId(pending.partId);
    return part;
}

QVector3D ShelfGlWidget::gridPosition(int slotIndex) const {
    const int cols = std::max(columns_, 1);
    const int col = slotIndex % cols;
    const int row = slotIndex / cols;
    return QVector3D(static_cast<float>(col) * kSpacing, 0.0f, static_cast<float>(row) * kSpacing);
}

QVector3D ShelfGlWidget::gridCenter() const {
    const int cols = std::max(columns_, 1);
    const int rows = std::max((slotCount_ + cols - 1) / cols, 1);
    return QVector3D(static_cast<float>(cols - 1) * 0.5f * kSpacing, 0.0f,
                      static_cast<float>(rows - 1) * 0.5f * kSpacing);
}

QVector3D ShelfGlWidget::cameraEye() const {
    const float azRad = qDegreesToRadians(azimuth_);
    const float elRad = qDegreesToRadians(elevation_);
    const QVector3D offset(distance_ * std::cos(elRad) * std::sin(azRad), distance_ * std::sin(elRad),
                            distance_ * std::cos(elRad) * std::cos(azRad));
    return gridCenter() + offset;
}

QVector3D ShelfGlWidget::colorForPartId(std::uint64_t partId) {
    // Golden-ratio hue rotation: successive partIds land far apart around
    // the hue wheel, so neighboring parts read as visually distinct.
    constexpr double kGoldenRatioConjugate = 0.6180339887498949;
    const double hue = std::fmod(static_cast<double>(partId) * kGoldenRatioConjugate, 1.0);
    return hsvToRgb(hue, 0.55, 0.95);
}

void ShelfGlWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        lastMousePos_ = event->position().toPoint();
    }
}

void ShelfGlWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) {
        return;
    }
    const QPoint currentPos = event->position().toPoint();
    const QPoint delta = currentPos - lastMousePos_;
    lastMousePos_ = currentPos;

    azimuth_ += static_cast<float>(delta.x()) * kOrbitSensitivity;
    elevation_ = std::clamp(elevation_ - static_cast<float>(delta.y()) * kOrbitSensitivity, -kElevationLimit,
                             kElevationLimit);
    update();
}

void ShelfGlWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
    }
}

void ShelfGlWidget::wheelEvent(QWheelEvent* event) {
    // Exponential zoom: scales distance_ by a fixed ratio per notch, so
    // zoom feels the same rate near or far.
    const double notches = event->angleDelta().y();
    const double factor = std::pow(kZoomFactorPerNotch, notches);
    distance_ = std::clamp(static_cast<float>(distance_ * factor), kMinDistance, kMaxDistance);
    update();
}
