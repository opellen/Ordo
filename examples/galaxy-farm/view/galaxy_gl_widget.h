#pragma once

// Qt-only on purpose: no <ordo/...> or app:: includes. The widget draws what
// the presenter hands it.

#include <cstdint>
#include <memory>
#include <vector>

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>
#include <QVector3D>
#include <QtGlobal>

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QPainter;

// Orbit-camera viewer for one sector of galaxy point clouds, plus the jump
// transit visual. No signals, so no Q_OBJECT.
class GalaxyGlWidget : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    // Mirror of GalaxylibGalaxyParams, restated to keep this header Qt-only.
    struct GalaxyRenderParams {
        float radCoreN = 0.3f;
        float radFarFieldN = 2.0f;
        float exInner = 0.85f;
        float exOuter = 0.95f;
        float angleOffsetN = 5.2f;  // radians per normalized radius
        float barRadiusN = 0.0f;    // 0 = no bar
        float barEx = 0.5f;
        int pertN = 0;
        float pertAmp = 0.0f;       // divisor -- smaller = stronger distortion
        float dustRenderSize = 90.0f;
        float h2SizeMax = 100.0f;
        float h2Threshold = 1.2f;
    };

    // One galaxy's per-point orbit data; positions are computed in the vertex shader.
    struct GalaxyCloudData {
        std::vector<float> orbitA;     // semi-major axis, galaxy-local (radius = 1)
        std::vector<float> orbitB;     // semi-minor axis
        std::vector<float> theta0;     // initial parametric angle, degrees
        std::vector<float> velTheta;   // angular velocity, degrees per time unit
        std::vector<float> tiltAngle;  // ellipse tilt, radians
        std::vector<float> colors;     // rgb per point, 0..1
        std::vector<float> mags;       // brightness/size magnitude
        std::vector<quint8> kinds;     // GalaxylibKind, non-decreasing (kind-contiguity)
        GalaxyRenderParams params;
    };

    // Mirror of GalaxylibKind; values must match. HAZE is never emitted.
    enum GalaxyPointKind {
        kKindHaze = 0,
        kKindStar = 1,
        kKindDust = 2,
        kKindFilament = 3,
        kKindH2 = 4,
        kKindH2Core = 5,
    };
    static constexpr int kKindCount = 6;

    enum class JumpVisual { Idle, Jumping };

    explicit GalaxyGlWidget(QWidget* parent = nullptr);
    ~GalaxyGlWidget() override;

    // Starts a fresh sector; the current board becomes the departing one.
    // reframeCamera=false keeps the user's pose (jump launches).
    // layoutSeed keys the slot arrangement; 0 is a valid seed.
    void beginSector(int slotCount, bool reframeCamera = true, quint64 layoutSeed = 0);

    // Copies `data`; uploaded on the next paintGL, replacing any same-id cloud.
    void setGalaxyCloud(std::uint64_t galaxyId, int slotIndex, const GalaxyCloudData& data);

    // transit01 is clamped to [0, 1]. Camera input is ignored while Jumping.
    void setJumpVisual(JumpVisual v, float transit01);

    // Per-sector procedural sky; while Idle both pairs should match.
    // Unchanged coordinates are a no-op.
    void setSectorSky(int currentX, int currentY, int destX, int destY);

    // Top-right HUD text; both empty hides it.
    void setHudLines(const QString& line1, const QString& line2);

    // Live camera state for the toolbar readout (polled).
    QVector3D cameraEyePos() const { return cameraEye(); }
    QVector3D cameraTargetPos() const { return orbitTarget_; }
    float cameraDistance() const { return distance_; }

    // Glides the orbit target back to the sector center, keeping zoom and angles.
    void recenterTarget();
    // Full smooth reset to the sector's default framing (Space key).
    void resetCamera();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    // GL buffers are created/destroyed only from paintGL. unique_ptr keeps
    // this movable (QOpenGLVertexArrayObject is a QObject).
    struct GpuGalaxy {
        std::unique_ptr<QOpenGLVertexArrayObject> vao;
        std::unique_ptr<QOpenGLBuffer> vbo;
        int starCount = 0;
        int slotIndex = 0;
        std::uint64_t galaxyId = 0;
        // [first, first+count) vertex range per point kind.
        int kindFirst[kKindCount] = {};
        int kindCount[kKindCount] = {};
        GalaxyRenderParams params;

        void destroyGl() {
            if (vao) vao->destroy();
            if (vbo) vbo->destroy();
        }
    };

    struct PendingUpload {
        std::uint64_t galaxyId = 0;
        int slotIndex = 0;
        GalaxyCloudData data;
    };

    struct GpuStreaks {
        std::unique_ptr<QOpenGLVertexArrayObject> vao;
        std::unique_ptr<QOpenGLBuffer> vbo;
        int vertexCount = 0;

        void destroyGl() {
            if (vao) vao->destroy();
            if (vbo) vbo->destroy();
        }
    };

    // Deterministic sky inputs for one sector coordinate.
    struct SkyParams {
        QVector3D colorA;
        QVector3D colorB;
        quint64 seed = 0;  // root of every random choice in the sky bake
    };

    static SkyParams skyParamsFor(int sectorX, int sectorY);

    void releaseDeferred();
    void drainPendingUploads();
    GpuGalaxy uploadGalaxy(const PendingUpload& pending);
    void regenerateStreaks();

    void drawSky();
    // ---- Sky cubemap bake -------------------------------------------------
    void ensureSkyCubemaps();
    // Restores the GL state it touches.
    void bakeSkyCubemap(GLuint cubemapTex, const SkyParams& params);
    void bakeSkyStars(const QMatrix4x4 views[6], const QMatrix4x4& projection, GLuint cubemapTex,
                       const SkyParams& params);
    void bakeSkyNebulae(const QMatrix4x4 views[6], const QMatrix4x4& projection, GLuint cubemapTex,
                         const SkyParams& params);
    void bakeSkySmudges(const QMatrix4x4 views[6], const QMatrix4x4& projection, GLuint cubemapTex,
                         const SkyParams& params);
    // Draws galaxies shifted by worldOffset; the caller owns blend state.
    void drawOneGalaxy(const GpuGalaxy& galaxy, const QMatrix4x4& view, const QVector3D& worldOffset,
                        float refDist, bool departing);
    void drawGalaxySet(const std::vector<GpuGalaxy>& set, const QMatrix4x4& view,
                        const QVector3D& worldOffset, float refDist, bool departing);
    void drawStreakLayer(const QMatrix4x4& view);
    void drawHud(QPainter& painter);

    // ---- Bloom post-process ----------------------------------------------
    // Sizes are device pixels; no-op when unchanged.
    void ensureBloomTargets(int pixelWidth, int pixelHeight);
    void drawBloomExtract();
    void drawBloomBlurPass(GLuint sourceTex, QOpenGLFramebufferObject& target, bool horizontal);
    // Draws into the currently bound framebuffer.
    void drawBloomComposite();

    // `departing` selects the receding board's layout keys instead of the active one's.
    QVector3D slotPosition(int slotIndex, bool departing = false) const;
    QVector3D basePosition(int slotIndex, float outerRadius, bool departing) const;
    float slotYaw(int slotIndex, bool departing = false) const;
    float slotPitch(int slotIndex, bool departing = false) const;
    // Per-galaxy world-radius multiplier (0.7x .. 1.5x).
    float slotScale(int slotIndex, bool departing = false) const;
    QVector3D sectorCenter() const;
    QVector3D cameraEye() const;

    QOpenGLShaderProgram starProgram_;
    QOpenGLShaderProgram streakProgram_;
    QOpenGLShaderProgram skyProgram_;

    QOpenGLShaderProgram skyNebulaBakeProgram_;
    QOpenGLShaderProgram skyStarBakeProgram_;
    QOpenGLShaderProgram skySmudgeBakeProgram_;

    QOpenGLShaderProgram bloomExtractProgram_;
    QOpenGLShaderProgram bloomBlurProgram_;
    QOpenGLShaderProgram bloomCompositeProgram_;

    // Fullscreen triangle for the sky and bloom passes.
    QOpenGLVertexArrayObject voidQuadVao_;
    QOpenGLBuffer voidQuadVbo_{QOpenGLBuffer::VertexBuffer};

    QOpenGLVertexArrayObject skyCubeVao_;
    QOpenGLBuffer skyCubeVbo_{QOpenGLBuffer::VertexBuffer};

    // Recreated on resize (no resize() on QOpenGLFramebufferObject).
    // sceneFbo_ is full-res RGBA16F with depth; the rest are quarter-res.
    std::unique_ptr<QOpenGLFramebufferObject> sceneFbo_;
    std::unique_ptr<QOpenGLFramebufferObject> brightFbo_;
    std::unique_ptr<QOpenGLFramebufferObject> blurPingFbo_;
    std::unique_ptr<QOpenGLFramebufferObject> blurPongFbo_;
    int bloomFboWidth_ = 0;
    int bloomFboHeight_ = 0;

    std::vector<GpuGalaxy> galaxies_;
    // The board being left during a jump; released on arrival or the next jump.
    std::vector<GpuGalaxy> departingGalaxies_;
    std::vector<PendingUpload> pendingUploads_;
    std::vector<GpuGalaxy> deferredRelease_;

    GpuStreaks streaks_;
    // Built on the first paintGL and never reseeded.
    bool streaksDirty_ = true;
    quint64 streakSeed_ = 1;

    int slotCount_ = 0;
    quint64 layoutSeed_ = 0;  // keys the ACTIVE board's slot arrangement
    int departingSlotCount_ = 0;
    quint64 departingLayoutSeed_ = 0;

    QMatrix4x4 projection_;
    float aspect_ = 1.0f;

    // Orbit camera, degrees / world units.
    float azimuth_ = 35.0f;
    float elevation_ = 25.0f;
    float distance_ = 20.0f;
    QVector3D orbitTarget_{0.0f, 0.0f, 0.0f};
    // Smooth-zoom end state: input edits these, paintGL eases toward them.
    float targetDistance_ = 20.0f;
    QVector3D targetOrbit_{0.0f, 0.0f, 0.0f};
    bool panning_ = false;  // middle-drag view-plane pan
    bool dragging_ = false;
    QPoint lastMousePos_;

    JumpVisual jumpVisual_ = JumpVisual::Idle;
    float transit01_ = 0.0f;

    // Jump journey, derived from transit01; jumpSpeed_ is a 0->1->0 bell.
    QVector3D jumpAxis_{0.0f, 0.0f, -1.0f};
    // Per-jump sideways bow direction, perpendicular to jumpAxis_.
    QVector3D jumpLatDir_{0.0f, 0.0f, 0.0f};
    quint32 jumpCurveSeed_ = 0;
    float jumpTravel01_ = 0.0f;
    float jumpSpeed_ = 0.0f;

    // Streak field clock; streakAxis_ eases toward the flight direction.
    QElapsedTimer streakClock_;
    float streakScroll_ = 0.0f;
    QVector3D streakAxis_{0.0f, 0.0f, -1.0f};

    // Global orbital clock for the galaxy vertex shader.
    QElapsedTimer galaxyClock_;
    float galaxyTime_ = 0.0f;

    int skyCurrentX_ = 0, skyCurrentY_ = 0;
    int skyDestX_ = 0, skyDestY_ = 0;
    SkyParams skyCurrent_;
    SkyParams skyDest_;

    GLuint skyCubemapCurrent_ = 0;
    GLuint skyCubemapDest_ = 0;
    GLuint skyBakeFbo_ = 0;
    bool skyCubemapsReady_ = false;
    bool skyCurrentDirty_ = true;
    bool skyDestDirty_ = true;
    // (0,0) is both the default and a legal sector, so the first call can't be skipped.
    bool skySectorInitialized_ = false;

    QString hudLine1_;
    QString hudLine2_;
};
