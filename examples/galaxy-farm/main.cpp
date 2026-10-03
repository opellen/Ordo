#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QImageWriter>
#include <QLineEdit>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSpinBox>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QThread>
#include <QTimer>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "domain/cloud_buffers.h"
#include "domain/galaxy_job_commands.h"
#include "domain/galaxy_job_events.h"
#include "domain/sector_coord.h"
#include "domain/star_map.h"
#include "domain/star_map_changed.h"
#include "domain/jump_commands.h"
#include "domain/jump_events.h"
#include "galaxylib.h"
#include "infra/job_runner.h"
#include "view/farm_window.h"
#include "view/galaxy_gl_widget.h"
#include "view/jump_presenter.h"

namespace {

// Pumps events until `predicate` holds or `timeoutMs` elapses; GenRelay
// callbacks arrive queued, so plain polling would never see them.
bool pump(const std::function<bool()>& predicate, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!timer.hasExpired(timeoutMs)) {
        QCoreApplication::processEvents();
        if (predicate()) {
            return true;
        }
        QThread::msleep(5);
    }
    return false;
}

// ---- Read-only helpers over StarMap snapshots. ----

int readyCount(const app::StarMap& starMap) {
    int count = 0;
    for (const auto& galaxy : starMap.galaxyStatuses()) {
        if (galaxy.state == app::events::GalaxyState::Ready) {
            ++count;
        }
    }
    return count;
}

bool allWorkersIdle(const app::StarMap& starMap) {
    for (const auto& worker : starMap.workerInfos()) {
        if (worker.busy) {
            return false;
        }
    }
    return true;
}

// True once a job is actually running, not just queued.
bool anyGeneratingOrBusy(const app::StarMap& starMap) {
    for (const auto& galaxy : starMap.galaxyStatuses()) {
        if (galaxy.state == app::events::GalaxyState::Generating) {
            return true;
        }
    }
    for (const auto& worker : starMap.workerInfos()) {
        if (worker.busy) {
            return true;
        }
    }
    return false;
}

// Ready count scoped to one board.
int readyCountOnBoard(const app::StarMap& starMap, app::events::Board board) {
    int count = 0;
    for (const auto& galaxy : starMap.galaxyStatuses()) {
        if (galaxy.board == board && galaxy.state == app::events::GalaxyState::Ready) {
            ++count;
        }
    }
    return count;
}

// Process-wide total; stale finishes are not counted.
int sumCompleted(const app::StarMap& starMap) {
    int sum = 0;
    for (const auto& worker : starMap.workerInfos()) {
        sum += worker.completedCount;
    }
    return sum;
}

// Headless self-check: jump A streams in and arrives; heavy jump B is
// superseded mid-flight by small jump C (destination drop, stale discard).
int runSmoke(ordo::core::Kernel& kernel, const std::shared_ptr<app::StarMap>& starMap, app::JobRunner& runner) {
    if (!starMap) {
        std::fprintf(stderr, "FAIL: no '%s' agent registered on the kernel\n", app::StarMap::kName);
        return 1;
    }

    constexpr int kGalaxiesPerSector = 6;
    constexpr int kStarsPerGalaxy = 3000;

    // ---- Jump ------------------------------------------------------------
    QElapsedTimer jumpTimer;
    jumpTimer.start();
    kernel.send(app::events::JumpRequested{42, kGalaxiesPerSector, kStarsPerGalaxy});

    // Galaxies complete one at a time, and each one streams incrementally.
    bool sawPartialSector = false;
    bool sawPartialCloud = false;
    app::events::SectorCoord destinationDuringTransit{};
    if (!pump(
            [&] {
                const int ready = readyCount(*starMap);
                if (ready > 0 && ready < kGalaxiesPerSector) {
                    sawPartialSector = true;
                }
                for (const auto& galaxy : starMap->galaxyStatuses()) {
                    if (galaxy.progress > 0.0f && galaxy.progress < 1.0f) {
                        sawPartialCloud = true;
                    }
                }
                if (starMap->phase() == app::events::JumpPhase::Jumping) {
                    destinationDuringTransit = starMap->destinationSector();
                }
                return ready == kGalaxiesPerSector;
            },
            30000)) {
        std::fprintf(stderr, "FAIL: jump did not reach ready==%d within 30s (ready=%d total=%d)\n",
                     kGalaxiesPerSector, readyCount(*starMap),
                     static_cast<int>(starMap->galaxyStatuses().size()));
        return 1;
    }
    std::printf("INFO: jump wall time = %lld ms (%d galaxies, 4 threads)\n",
                static_cast<long long>(jumpTimer.elapsed()), kGalaxiesPerSector);

    if (!sawPartialSector) {
        std::fprintf(stderr,
                     "FAIL: never observed ready strictly between 0 and %d -- destination streaming not "
                     "observed\n",
                     kGalaxiesPerSector);
        return 1;
    }
    if (!sawPartialCloud) {
        std::fprintf(stderr,
                     "FAIL: never observed a galaxy with 0 < progress < 1 -- no GalaxyJobProgress arrived "
                     "before ready==%d\n",
                     kGalaxiesPerSector);
        return 1;
    }

    // ---- Arrival -----------------------------------------------------------
    kernel.send(app::events::JumpArrivalReached{});

    if (starMap->phase() != app::events::JumpPhase::Idle) {
        std::fprintf(stderr, "FAIL: phase is not Idle after JumpArrivalReached\n");
        return 1;
    }
    if (starMap->currentSector() != destinationDuringTransit) {
        std::fprintf(stderr, "FAIL: currentSector (%d,%d) != destinationSector observed during transit (%d,%d)\n",
                     starMap->currentSector().x, starMap->currentSector().y, destinationDuringTransit.x,
                     destinationDuringTransit.y);
        return 1;
    }

    if (!pump([&] { return allWorkersIdle(*starMap); }, 30000)) {
        std::fprintf(stderr, "FAIL: workers never went idle within 30s after arrival\n");
        return 1;
    }

    {
        const auto galaxies = starMap->galaxyStatuses();
        int total = 0;
        int ready = 0;
        for (const auto& galaxy : galaxies) {
            if (galaxy.board == app::events::Board::Current) {
                ++total;
                if (galaxy.state == app::events::GalaxyState::Ready) {
                    ++ready;
                }
            }
        }
        if (total != kGalaxiesPerSector || ready != kGalaxiesPerSector) {
            std::fprintf(stderr, "FAIL: current board total=%d ready=%d (want %d, %d)\n", total, ready,
                         kGalaxiesPerSector, kGalaxiesPerSector);
            return 1;
        }
        if (starMap->staleArrivals() != 0) {
            std::fprintf(stderr, "FAIL: staleArrivals=%d (want 0 -- single jump, nothing should ever be stale)\n",
                         starMap->staleArrivals());
            return 1;
        }

        const app::events::CloudBuffers* cloudBuffers = nullptr;
        std::uint64_t sampledGalaxyId = 0;
        for (const auto& galaxy : galaxies) {
            if (galaxy.board == app::events::Board::Current && galaxy.state == app::events::GalaxyState::Ready) {
                sampledGalaxyId = galaxy.galaxyId;
                cloudBuffers = starMap->cloud(galaxy.galaxyId);
                break;
            }
        }
        if (!cloudBuffers) {
            std::fprintf(stderr, "FAIL: a Ready galaxy's cloud() returned nullptr (galaxyId=%llu)\n",
                         static_cast<unsigned long long>(sampledGalaxyId));
            return 1;
        }
        // Arrays hold every kind; only the STAR count is controlled here.
        if (cloudBuffers->kinds.size() != cloudBuffers->orbitA.size()) {
            std::fprintf(stderr, "FAIL: cloud kinds.size()=%zu != orbitA.size()=%zu\n",
                         cloudBuffers->kinds.size(), cloudBuffers->orbitA.size());
            return 1;
        }
        if (cloudBuffers->mags.size() != cloudBuffers->kinds.size()) {
            std::fprintf(stderr, "FAIL: cloud mags.size()=%zu != kinds.size()=%zu\n",
                         cloudBuffers->mags.size(), cloudBuffers->kinds.size());
            return 1;
        }
        constexpr std::uint8_t kStarKind = 1;  // GALAXYLIB_KIND_STAR, galaxylib.h
        std::size_t starKindCount = 0;
        for (std::uint8_t kind : cloudBuffers->kinds) {
            if (kind == kStarKind) {
                ++starKindCount;
            }
        }
        if (starKindCount != static_cast<std::size_t>(kStarsPerGalaxy)) {
            std::fprintf(stderr, "FAIL: cloud STAR-kind count=%zu (want %d)\n", starKindCount,
                         kStarsPerGalaxy);
            return 1;
        }
    }

    // ---- Jump B (heavy) -----------------------------------------------------
    // Heavy enough that jump C lands while these bakes are still running.
    constexpr int kGalaxiesPerSectorB = 6;
    constexpr int kStarsPerGalaxyB = 200000;
    kernel.send(app::events::JumpRequested{7, kGalaxiesPerSectorB, kStarsPerGalaxyB});
    if (!pump([&] { return anyGeneratingOrBusy(*starMap); }, 30000)) {
        std::fprintf(stderr, "FAIL: jump B never started a job within 30s\n");
        return 1;
    }

    // ---- Jump C (immediately) -------------------------------------------------
    // Drops jump B's destination board. B's running bakes finish stale; its
    // queued jobs are skipped at start.
    constexpr int kGalaxiesPerSectorC = 4;
    constexpr int kStarsPerGalaxyC = 3000;
    kernel.send(app::events::JumpRequested{99, kGalaxiesPerSectorC, kStarsPerGalaxyC});

    // Destination-only, so a stray jump-B checkpoint can't fake it.
    bool sawPartialCloudC = false;
    if (!pump(
            [&] {
                for (const auto& galaxy : starMap->galaxyStatuses()) {
                    if (galaxy.board == app::events::Board::Destination && galaxy.progress > 0.0f &&
                        galaxy.progress < 1.0f) {
                        sawPartialCloudC = true;
                    }
                }
                return readyCountOnBoard(*starMap, app::events::Board::Destination) == kGalaxiesPerSectorC &&
                       allWorkersIdle(*starMap);
            },
            30000)) {
        std::fprintf(stderr,
                     "FAIL: jump C did not reach destination ready==%d && allWorkersIdle within 30s "
                     "(destinationReady=%d allIdle=%d)\n",
                     kGalaxiesPerSectorC, readyCountOnBoard(*starMap, app::events::Board::Destination),
                     allWorkersIdle(*starMap) ? 1 : 0);
        return 1;
    }

    {
        int destinationTotal = 0;
        int destinationReady = 0;
        int destinationFailed = 0;
        for (const auto& galaxy : starMap->galaxyStatuses()) {
            if (galaxy.board == app::events::Board::Destination) {
                ++destinationTotal;
                if (galaxy.state == app::events::GalaxyState::Ready) {
                    ++destinationReady;
                } else if (galaxy.state == app::events::GalaxyState::Failed) {
                    ++destinationFailed;
                }
            }
        }
        // Destination holds only jump C's galaxies.
        if (destinationTotal != kGalaxiesPerSectorC || destinationReady != kGalaxiesPerSectorC ||
            destinationFailed != 0) {
            std::fprintf(stderr,
                         "FAIL: jump C destination board total=%d ready=%d failed=%d (want %d, %d, 0)\n",
                         destinationTotal, destinationReady, destinationFailed, kGalaxiesPerSectorC,
                         kGalaxiesPerSectorC);
            return 1;
        }

        // At least one jump B bake must arrive stale.
        if (starMap->staleArrivals() < 1) {
            std::fprintf(stderr,
                         "FAIL: staleArrivals=%d (want >=1 -- jump B's in-flight bakes must arrive stale "
                         "after jump C's epoch bump)\n",
                         starMap->staleArrivals());
            return 1;
        }

        // Exactly jump A's + jump C's; stale finishes never count.
        const int wantCompleted = kGalaxiesPerSector + kGalaxiesPerSectorC;
        const int sum = sumCompleted(*starMap);
        if (sum != wantCompleted) {
            std::fprintf(stderr,
                         "FAIL: sum of workers' completedCount=%d (want %d -- jump A's %d + jump C's %d; "
                         "jump B's stale finishes must bump staleArrivals, never completedCount)\n",
                         sum, wantCompleted, kGalaxiesPerSector, kGalaxiesPerSectorC);
            return 1;
        }

        if (!sawPartialCloudC) {
            std::fprintf(stderr,
                         "FAIL: never observed a jump C galaxy with 0 < progress < 1 -- streaming did not "
                         "survive the supersede\n");
            return 1;
        }
    }

    // ---- Slots are bounded by the pool size ------------------------------------
    for (const auto& worker : starMap->workerInfos()) {
        if (worker.slot < 0 || worker.slot >= 4) {
            std::fprintf(stderr, "FAIL: worker slot %d outside 0..3 with a 4-thread pool\n", worker.slot);
            return 1;
        }
    }

    // ---- Jump D after shrinking the pool to 2 -----------------------------------
    // New jobs must only get slots 0..1, so the view can hide lanes 2..3.
    {
        runner.setMaxThreads(2);
        std::map<int, int> completedBefore;
        for (const auto& worker : starMap->workerInfos()) {
            completedBefore[worker.slot] = worker.completedCount;
        }

        constexpr int kGalaxiesPerSectorD = 6;
        kernel.send(app::events::JumpRequested{123, kGalaxiesPerSectorD, 3000});
        int maxBusySlot = -1;
        if (!pump(
                [&] {
                    for (const auto& worker : starMap->workerInfos()) {
                        if (worker.busy) {
                            maxBusySlot = std::max(maxBusySlot, worker.slot);
                        }
                    }
                    return readyCountOnBoard(*starMap, app::events::Board::Destination) == kGalaxiesPerSectorD &&
                           allWorkersIdle(*starMap);
                },
                30000)) {
            std::fprintf(stderr, "FAIL: jump D (2 threads) did not finish within 30s\n");
            return 1;
        }
        int completedInLowSlots = 0;
        for (const auto& worker : starMap->workerInfos()) {
            const int delta = worker.completedCount - completedBefore[worker.slot];
            if (worker.slot >= 2 && delta != 0) {
                std::fprintf(stderr, "FAIL: slot %d completed %d jobs after shrinking the pool to 2\n", worker.slot,
                             delta);
                return 1;
            }
            if (worker.slot < 2) {
                completedInLowSlots += delta;
            }
        }
        if (maxBusySlot >= 2 || completedInLowSlots != kGalaxiesPerSectorD) {
            std::fprintf(stderr, "FAIL: jump D maxBusySlot=%d completedInSlots0-1=%d (want <2, %d)\n", maxBusySlot,
                         completedInLowSlots, kGalaxiesPerSectorD);
            return 1;
        }
    }

    std::printf(
        "PASS: galaxy-farm smoke (streaming jump, arrival, parallel bake, stale-generation discard, "
        "slot-bounded workers after pool shrink)\n");
    return 0;
}

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

// Widget-only render check (no domain). Writes three PNGs to the working
// directory; nonzero on failure.
int runRenderProbe() {
    GalaxyGlWidget widget;
    widget.resize(1280, 800);
    widget.setWindowTitle(QStringLiteral("Ordo Galaxy Farm -- render probe"));
    widget.show();

    // The GL framebuffer isn't valid until the window is visible.
    if (!pump([&] { return widget.isVisible(); }, 5000)) {
        std::fprintf(stderr, "FAIL: probe window never became visible\n");
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
            std::fprintf(stderr, "FAIL: galaxylib_generate_galaxy(seed=%llu, type=%u) returned %d\n",
                         static_cast<unsigned long long>(seed), type, rc);
            return 1;
        }
        const GalaxyGlWidget::GalaxyCloudData cloud = copyAndFreeCloud(raw);
        widget.setGalaxyCloud(seed, i, cloud);
    }

    auto saveFrame = [](GalaxyGlWidget& w, const char* fileName) {
        const QImage frame = w.grabFramebuffer();
        if (frame.isNull()) {
            std::fprintf(stderr, "FAIL: grabFramebuffer() returned a null image for %s\n", fileName);
            return false;
        }
        QImageWriter writer(QString::fromUtf8(fileName), "png");
        if (!writer.write(frame)) {
            std::fprintf(stderr, "FAIL: QImageWriter failed for %s (cwd=%s) error=%s\n", fileName,
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
// Writes to C:\tmp: Controlled Folder Access blocks writes under Documents.
int runGuiProbe(ordo::core::Kernel& kernel, QApplication& app, app::JobRunner& runner) {
    QDir().mkpath(QStringLiteral("C:/tmp/galaxy-farm-probe"));

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
            std::fprintf(stderr, "FAIL: grab() returned a null pixmap for %s\n", qUtf8Printable(path));
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
            std::fprintf(stderr, "FAIL: QImageWriter failed for %s error=%s\n", qUtf8Printable(path),
                         qUtf8Printable(writer.errorString()));
            return false;
        }
        std::printf("wrote %s\n", qUtf8Printable(path));
        return true;
    };

    int exitCode = 0;

    // ~2s: mid-transit.
    QTimer::singleShot(2000, &window, [&] {
        if (!saveWindowFrame(window, QStringLiteral("C:/tmp/galaxy-farm-probe/gui-jump.png"))) {
            exitCode = 1;
            QApplication::quit();
        }
    });

    // ~9s: arrived, past the 4s transit.
    QTimer::singleShot(9000, &window, [&] {
        if (!saveWindowFrame(window, QStringLiteral("C:/tmp/galaxy-farm-probe/gui-idle.png"))) {
            exitCode = 1;
        }

        // Shrinking Max threads hides the idle lanes above the new limit.
        auto starMap = kernel.agentAs<app::StarMap>(app::StarMap::kName);
        if (!pump([&] { return allWorkersIdle(*starMap); }, 30000)) {
            std::fprintf(stderr, "FAIL: workers never went idle before the lane-shrink check\n");
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
                std::fprintf(stderr, "FAIL: visible worker lanes before=%d after shrink to 2=%d (want >2, 2)\n",
                             before, after);
                exitCode = 1;
            } else {
                std::printf("PASS: worker lanes %d -> %d after Max threads 6 -> 2\n", before, after);
            }
            laneHost->grab().save(QStringLiteral("C:/tmp/galaxy-farm-probe/gui-lanes-shrunk.png"));
        }
        QApplication::quit();
    });

    app.exec();
    return exitCode;
}

}  // namespace

int main(int argc, char** argv) {
    // Must be set before QApplication is constructed.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    // Declaration order matters: `runner` is destroyed first (waiting for its
    // jobs), so no worker can call into a dead GenRelay or Kernel.
    QApplication app(argc, argv);

    // Dark theme; must be set before FarmWindow is constructed.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(0x14, 0x14, 0x14));
    darkPalette.setColor(QPalette::WindowText, QColor(0xe0, 0xe0, 0xe0));
    darkPalette.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
    darkPalette.setColor(QPalette::AlternateBase, QColor(0x14, 0x14, 0x14));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(0xe0, 0xe0, 0xe0));
    darkPalette.setColor(QPalette::ToolTipText, QColor(0xe0, 0xe0, 0xe0));
    darkPalette.setColor(QPalette::Text, QColor(0xe0, 0xe0, 0xe0));
    darkPalette.setColor(QPalette::Button, QColor(0x1e, 0x1e, 0x1e));
    darkPalette.setColor(QPalette::ButtonText, QColor(0xe0, 0xe0, 0xe0));
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(0x2a, 0x7f, 0xff));
    darkPalette.setColor(QPalette::Highlight, QColor(0x2a, 0x7f, 0xff));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x78, 0x78, 0x78));
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x78, 0x78, 0x78));
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x78, 0x78, 0x78));
    QApplication::setPalette(darkPalette);

    ordo::core::Kernel kernel;
    kernel.registerAgent(std::make_shared<app::StarMap>());
    app::GenRelay relay;
    app::JobRunner runner(relay);

    // Job callbacks re-enter the kernel as events. They run on the UI thread
    // (queued delivery), which is what makes touching `kernel` legal.
    relay.onStarted = [&kernel](const app::events::GalaxyJobStarted& event) { kernel.send(event); };
    relay.onProgress = [&kernel](const app::events::GalaxyJobProgress& event) { kernel.send(event); };
    relay.onFinished = [&kernel](const app::events::GalaxyJobFinished& event) { kernel.send(event); };

    // One Command per event type.
    kernel.registerCommand<app::events::JumpRequested, app::JumpCommand>(std::ref(runner));
    kernel.registerCommand<app::events::JumpArrivalReached, app::ArrivalCommand>();
    kernel.registerCommand<app::events::GalaxyJobStarted, app::GalaxyJobStartedCommand>();
    kernel.registerCommand<app::events::GalaxyJobProgress, app::GalaxyJobProgressCommand>();
    kernel.registerCommand<app::events::GalaxyJobFinished, app::GalaxyJobFinishedCommand>();

    // Matches the spinbox default, which only applies on later changes.
    runner.setMaxThreads(6);

    int exitCode = 0;
    if (argc > 1 && std::strcmp(argv[1], "--smoke") == 0) {
        // Fixed, and matches runSmoke's "4 threads" output.
        runner.setMaxThreads(4);
        auto starMap = kernel.agentAs<app::StarMap>(app::StarMap::kName);
        exitCode = runSmoke(kernel, starMap, runner);
    } else if (argc > 1 && std::strcmp(argv[1], "--render-probe") == 0) {
        exitCode = runRenderProbe();
    } else if (argc > 1 && std::strcmp(argv[1], "--gui-probe") == 0) {
        exitCode = runGuiProbe(kernel, app, runner);
    } else {
        // Declared before the ViewHost so the presenter dies before its widgets.
        app::FarmWindow window;

        ordo::qt::ViewHost host(kernel);
        auto* presenter = host.add<app::JumpPresenter>(
            window.canvas(), window.laneColumn(), window.progressBar(), window.runningLabel(),
            window.queuedLabel(), window.generationLabel(), window.staleLabel(), &window);

        // Also fires the first jump, so the app opens mid-jump.
        window.connectActions(*presenter, runner);

        window.show();
        exitCode = app.exec();
    }

    // Remove commands before the objects they reference go out of scope.
    kernel.removeCommand<app::events::JumpRequested>();
    kernel.removeCommand<app::events::JumpArrivalReached>();
    kernel.removeCommand<app::events::GalaxyJobStarted>();
    kernel.removeCommand<app::events::GalaxyJobProgress>();
    kernel.removeCommand<app::events::GalaxyJobFinished>();

    return exitCode;
}
