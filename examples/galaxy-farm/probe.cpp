#include "checks.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QImageWriter>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "domain/star_map.h"
#include "galaxylib.h"
#include "infra/job_runner.h"
#include "view/farm_window.h"
#include "view/galaxy_gl_widget.h"
#include "view/jump_presenter.h"

namespace {

// Copies a galaxylib cloud into widget form and frees the DLL buffers.
GalaxyGlWidget::GalaxyCloudData copyAndFreeCloud(GalaxylibCloud& raw) {
    GalaxyGlWidget::GalaxyCloudData cloud;
    const std::size_t n = raw.starCount;
    cloud.orbitA.assign(raw.orbitA, raw.orbitA + n);
    cloud.orbitB.assign(raw.orbitB, raw.orbitB + n);
    cloud.theta0.assign(raw.theta0, raw.theta0 + n);
    cloud.velTheta.assign(raw.velTheta, raw.velTheta + n);
    cloud.tiltAngle.assign(raw.tiltAngle, raw.tiltAngle + n);
    cloud.colors.assign(raw.colors, raw.colors + n * 3);
    cloud.mags.assign(raw.mags, raw.mags + n);
    cloud.kinds.assign(raw.kinds, raw.kinds + n);
    cloud.params.radCoreN = raw.params.radCoreN;
    cloud.params.radFarFieldN = raw.params.radFarFieldN;
    cloud.params.exInner = raw.params.exInner;
    cloud.params.exOuter = raw.params.exOuter;
    cloud.params.angleOffsetN = raw.params.angleOffsetN;
    cloud.params.barRadiusN = raw.params.barRadiusN;
    cloud.params.barEx = raw.params.barEx;
    cloud.params.pertN = raw.params.pertN;
    cloud.params.pertAmp = raw.params.pertAmp;
    cloud.params.dustRenderSize = raw.params.dustRenderSize;
    cloud.params.h2SizeMax = raw.params.h2SizeMax;
    cloud.params.h2Threshold = raw.params.h2Threshold;
    galaxylib_free_cloud(&raw);
    return cloud;
}

}  // namespace

// Widget-only render check (no domain). Writes three PNGs to the working
// directory; nonzero on failure.
int runRenderProbe() {
    GalaxyGlWidget widget;
    widget.resize(1280, 800);
    widget.setWindowTitle(QStringLiteral("Ordo Galaxy Farm -- render probe"));
    widget.show();

    // The GL framebuffer isn't valid until the window is visible.
    if (!pump([&] { return widget.isVisible(); }, 5000)) {
        std::fprintf(stderr, "FAIL: probe window not visible\n");
        return 1;
    }

    constexpr int kSlotCount = 8;
    // One galaxy per type.
    constexpr int kGalaxyCount = GALAXYLIB_TYPE_COUNT;
    constexpr std::uint32_t kStarsPerGalaxy = 12000;

    // Distinct skies so the jump frames show the cross-fade.
    widget.setSectorSky(0, 0, 3, -1);
    widget.beginSector(kSlotCount);
    for (int i = 0; i < kGalaxyCount; ++i) {
        const std::uint64_t seed = static_cast<std::uint64_t>(i + 1);
        const std::uint32_t type = static_cast<std::uint32_t>(i);
        GalaxylibCloud raw{};
        const int rc = galaxylib_generate_galaxy(seed, type, kStarsPerGalaxy, nullptr, nullptr, &raw);
        if (rc != 0) {
            std::fprintf(stderr, "FAIL: galaxylib_generate_galaxy seed=%llu type=%u rc=%d\n",
                         static_cast<unsigned long long>(seed), type, rc);
            return 1;
        }
        const GalaxyGlWidget::GalaxyCloudData cloud = copyAndFreeCloud(raw);
        widget.setGalaxyCloud(seed, i, cloud);
    }

    auto saveFrame = [](GalaxyGlWidget& w, const char* fileName) {
        const QImage frame = w.grabFramebuffer();
        if (frame.isNull()) {
            std::fprintf(stderr, "FAIL: null framebuffer image for %s\n", fileName);
            return false;
        }
        QImageWriter writer(QString::fromUtf8(fileName), "png");
        if (!writer.write(frame)) {
            std::fprintf(stderr, "FAIL: write %s (cwd=%s): %s\n", fileName,
                         qUtf8Printable(QDir::currentPath()), qUtf8Printable(writer.errorString()));
            return false;
        }
        std::printf("wrote %s\n", fileName);
        return true;
    };

    // ---- Frame 1: Idle, default camera -----------------------------------
    pump([] { return false; }, 400);  // let real frames land before the first grab
    if (!saveFrame(widget, "probe-idle.png")) {
        return 1;
    }

    // ---- Frame 2: mid-transit ---------------------------------------------
    widget.setJumpVisual(GalaxyGlWidget::JumpVisual::Jumping, 0.45f);
    widget.setHudLines(QStringLiteral("jumping to sector (3,-1)"), QStringLiteral("building 32 galaxies"));
    pump([] { return false; }, 800);
    if (!saveFrame(widget, "probe-jump-mid.png")) {
        return 1;
    }

    // ---- Frame 3: late transit ----------------------------------------------
    widget.setJumpVisual(GalaxyGlWidget::JumpVisual::Jumping, 0.9f);
    pump([] { return false; }, 800);
    if (!saveFrame(widget, "probe-jump-late.png")) {
        return 1;
    }

    return 0;
}

// GUI probe: one jump, two timed window screenshots, then quit.
// Writes its PNGs to a galaxy-farm-probe folder in the system temp directory.
int runGuiProbe(ordo::core::Kernel& kernel, QApplication& app, app::JobRunner& runner) {
    const QString probeDir = QDir(QDir::tempPath()).filePath(QStringLiteral("galaxy-farm-probe"));
    QDir().mkpath(probeDir);

    // `window` must outlive the ViewHost and its presenter.
    app::FarmWindow window;

    ordo::qt::ViewHost host(kernel);
    auto* presenter =
        host.add<app::JumpPresenter>(window.canvas(), window.laneColumn(), window.progressBar(),
                                      window.runningLabel(), window.queuedLabel(), window.generationLabel(),
                                      window.staleLabel(), &window);

    window.seedEdit()->setText(QStringLiteral("42"));
    window.galaxiesPerSectorSpin()->setValue(12);
    window.starsPerGalaxySpin()->setValue(120000);

    window.connectActions(*presenter, runner);
    window.show();

    // window.grab() drops the nested GL widget's QPainter HUD; patch in grabFramebuffer().
    auto saveWindowFrame = [](app::FarmWindow& w, const QString& path) {
        QPixmap windowPixmap = w.grab();
        if (windowPixmap.isNull()) {
            std::fprintf(stderr, "FAIL: null pixmap for %s\n", qUtf8Printable(path));
            return false;
        }
        QImage windowImage = windowPixmap.toImage();

        const QImage canvasImage = w.canvas()->grabFramebuffer();
        if (!canvasImage.isNull()) {
            const qreal dpr = w.devicePixelRatioF();
            const QPoint logicalTopLeft = w.canvas()->mapTo(&w, QPoint(0, 0));
            const QPoint physicalTopLeft(qRound(logicalTopLeft.x() * dpr), qRound(logicalTopLeft.y() * dpr));

            QImage patchedWindow = windowImage;
            patchedWindow.setDevicePixelRatio(1.0);  // raw physical-pixel coordinates for this patch
            QImage patchedCanvas = canvasImage;
            patchedCanvas.setDevicePixelRatio(1.0);

            QPainter patchPainter(&patchedWindow);
            patchPainter.drawImage(physicalTopLeft, patchedCanvas);
            patchPainter.end();
            windowImage = patchedWindow;
        }

        QImageWriter writer(path, "png");
        if (!writer.write(windowImage)) {
            std::fprintf(stderr, "FAIL: write %s: %s\n", qUtf8Printable(path),
                         qUtf8Printable(writer.errorString()));
            return false;
        }
        std::printf("wrote %s\n", qUtf8Printable(path));
        return true;
    };

    int exitCode = 0;

    // ~2s: mid-transit.
    QTimer::singleShot(2000, &window, [&] {
        if (!saveWindowFrame(window, QDir(probeDir).filePath(QStringLiteral("gui-jump.png")))) {
            exitCode = 1;
            QApplication::quit();
        }
    });

    // ~9s: arrived, past the 4s transit.
    QTimer::singleShot(9000, &window, [&] {
        if (!saveWindowFrame(window, QDir(probeDir).filePath(QStringLiteral("gui-idle.png")))) {
            exitCode = 1;
        }

        // Shrinking Max threads hides the idle lanes above the new limit.
        auto starMap = kernel.agentAs<app::StarMap>(app::StarMap::kName);
        if (!pump([&] { return allWorkersIdle(*starMap); }, 30000)) {
            std::fprintf(stderr, "FAIL: workers not idle before lane-shrink check\n");
            exitCode = 1;
        } else {
            QWidget* laneHost = window.laneColumn()->parentWidget();
            auto visibleLanes = [&] {
                int n = 0;
                for (QWidget* card : laneHost->findChildren<QWidget*>(QStringLiteral("workerLane"))) {
                    n += card->isVisibleTo(&window) ? 1 : 0;
                }
                return n;
            };
            const int before = visibleLanes();
            window.maxThreadsSpin()->setValue(2);
            const int after = visibleLanes();
            if (before < 3 || after != 2) {
                std::fprintf(stderr, "FAIL: visible lanes before=%d after=%d, want >2, 2\n",
                             before, after);
                exitCode = 1;
            } else {
                std::printf("PASS: worker lanes %d -> %d\n", before, after);
            }
            laneHost->grab().save(QDir(probeDir).filePath(QStringLiteral("gui-lanes-shrunk.png")));
        }
        QApplication::quit();
    });

    app.exec();
    return exitCode;
}
