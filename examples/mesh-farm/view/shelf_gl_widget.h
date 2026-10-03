#pragma once

// Qt-only: no <ordo/...> and no app:: includes reach this file.
// ShelfPresenter repackages MeshBuffers as PartMeshData below first.

#include <cstdint>
#include <memory>
#include <vector>

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>
#include <QtGlobal>

class QMouseEvent;
class QWheelEvent;

// Orbit-camera viewer for one generation of baked parts, laid out on a grid.
// No Q_OBJECT: ShelfPresenter drives it through plain method calls only.
class ShelfGlWidget : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    // One part's mesh, plain Qt types only -- lets shelf_presenter.h build
    // one from app::events::MeshBuffers without this header seeing that type.
    struct PartMeshData {
        std::vector<float> positions;  // xyz per vertex
        std::vector<float> normals;    // xyz per vertex
        std::vector<float> ao;         // one scalar per vertex, 0..1
        std::vector<quint32> indices;  // 3 per triangle
    };

    explicit ShelfGlWidget(QWidget* parent = nullptr);
    ~ShelfGlWidget() override;

    // Starts a fresh generation: drops every part on screen. Their GL
    // buffers move to a deferred-release list paintGL drains once its
    // context is current. Also stores the grid column count for setPartMesh.
    void beginGeneration(int slotCount);

    // Queues partId's mesh for upload into slotIndex; copies `data`, so the
    // caller's buffers need not outlive this call. paintGL drains the queue
    // on the GL-context thread.
    void setPartMesh(std::uint64_t partId, int slotIndex, const PartMeshData& data);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // One part's uploaded GL state; buffers only ever created/destroyed
    // from paintGL. unique_ptr since QOpenGLVertexArrayObject is a QObject
    // (no copy/move), making GpuPart itself move-only.
    struct GpuPart {
        std::unique_ptr<QOpenGLVertexArrayObject> vao;
        std::unique_ptr<QOpenGLBuffer> vbo;
        std::unique_ptr<QOpenGLBuffer> ibo;
        int indexCount = 0;
        int slotIndex = 0;
        // A part uploads many times (one per AO checkpoint); partId is the
        // key drainPendingUploads uses to replace, not duplicate, a GpuPart.
        std::uint64_t partId = 0;
        QVector3D baseColor;

        void destroyGl() {
            if (vao) vao->destroy();
            if (vbo) vbo->destroy();
            if (ibo) ibo->destroy();
        }
    };

    // A copy of setPartMesh's arguments, sitting in line for paintGL's next
    // drain pass.
    struct PendingUpload {
        std::uint64_t partId = 0;
        int slotIndex = 0;
        PartMeshData data;
    };

    void releaseDeferred();
    void drainPendingUploads();
    GpuPart uploadPart(const PendingUpload& pending);
    QVector3D gridPosition(int slotIndex) const;
    QVector3D gridCenter() const;
    QVector3D cameraEye() const;
    static QVector3D colorForPartId(std::uint64_t partId);

    QOpenGLShaderProgram program_;
    std::vector<GpuPart> parts_;
    std::vector<PendingUpload> pendingUploads_;
    std::vector<GpuPart> deferredRelease_;

    int columns_ = 1;
    int slotCount_ = 0;

    QMatrix4x4 projection_;

    // Orbit camera state, in degrees/world-units. Initial pose is elevated
    // and pulled back to see a 4x4 (16-part) shelf whole.
    float azimuth_ = 35.0f;
    float elevation_ = 30.0f;
    float distance_ = 9.0f;
    bool dragging_ = false;
    QPoint lastMousePos_;
};
