#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QImageWriter>
#include <QPixmap>
#include <QSpinBox>
#include <QSurfaceFormat>
#include <QThread>
#include <QTimer>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "domain/bake_job_commands.h"
#include "domain/bake_job_events.h"
#include "domain/mesh_buffers.h"
#include "domain/part_shelf.h"
#include "domain/regenerate_command.h"
#include "domain/regenerate_requested.h"
#include "domain/shelf_changed.h"
#include "infra/job_runner.h"
#include "view/farm_window.h"
#include "view/shelf_presenter.h"

namespace {

// Pumps the Qt event loop until `predicate` is true or `timeoutMs` elapses.
// BakeRelay's callbacks only fire while pumping.
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

// Small read-only helpers over PartShelf's snapshot accessors, cheap enough
// to call every pump() iteration.

int bakedCount(const app::PartShelf& shelf) {
    int count = 0;
    for (const auto& part : shelf.partStatuses()) {
        if (part.state == app::events::PartState::Baked) {
            ++count;
        }
    }
    return count;
}

int totalCount(const app::PartShelf& shelf) { return static_cast<int>(shelf.partStatuses().size()); }

// True once any worker has picked up a job (part Running or worker busy),
// i.e. at least one job is actually in flight, not just queued.
bool anyRunningOrBusy(const app::PartShelf& shelf) {
    for (const auto& part : shelf.partStatuses()) {
        if (part.state == app::events::PartState::Running) {
            return true;
        }
    }
    for (const auto& worker : shelf.workerInfos()) {
        if (worker.busy) {
            return true;
        }
    }
    return false;
}

// Sum of every worker's completedCount, a running total across the whole
// process. Stale (superseded-batch) finishes never inflate it.
int sumCompleted(const app::PartShelf& shelf) {
    int sum = 0;
    for (const auto& worker : shelf.workerInfos()) {
        sum += worker.completedCount;
    }
    return sum;
}

bool allIdle(const app::PartShelf& shelf) {
    for (const auto& worker : shelf.workerInfos()) {
        if (worker.busy) {
            return false;
        }
    }
    return true;
}

// Headless self-check: three RegenerateRequested batches back to back --
// A) plain parallel bake, B) heavy batch superseded while in flight,
// C) small batch fired immediately, proving beginBatch drops the board and
// discards B's stale arrivals.
int runSmoke(ordo::core::Kernel& kernel, const std::shared_ptr<app::PartShelf>& shelf, app::JobRunner& runner) {
    if (!shelf) {
        std::fprintf(stderr, "FAIL: no '%s' agent registered on the kernel\n", app::PartShelf::kName);
        return 1;
    }

    // ---- Batch A -----------------------------------------------------
    QElapsedTimer batchATimer;
    batchATimer.start();
    kernel.send(app::events::RegenerateRequested{42, 8, 4, 16});

    // Proves the AO sweep is actually observable, not just that the bake
    // finishes: catches a mid-sweep PartStatus while the loop itself only
    // waits on bakedCount.
    bool sawPartialProgress = false;
    if (!pump(
            [&] {
                for (const auto& part : shelf->partStatuses()) {
                    if (part.progress > 0.0f && part.progress < 1.0f) {
                        sawPartialProgress = true;
                    }
                }
                return bakedCount(*shelf) == 8;
            },
            30000)) {
        std::fprintf(stderr, "FAIL: batch A did not reach baked==8 within 30s (baked=%d total=%d)\n",
                     bakedCount(*shelf), totalCount(*shelf));
        return 1;
    }
    std::printf("INFO: batch A wall time = %lld ms (8 parts, 4 threads)\n",
                static_cast<long long>(batchATimer.elapsed()));

    if (!sawPartialProgress) {
        std::fprintf(stderr,
                     "FAIL: batch A never observed a PartStatus with 0 < progress < 1 -- no AO-sweep "
                     "progress event arrived before baked==8\n");
        return 1;
    }

    {
        const auto parts = shelf->partStatuses();
        const auto workers = shelf->workerInfos();
        int failed = 0;
        int queued = 0;
        int running = 0;
        for (const auto& part : parts) {
            switch (part.state) {
                case app::events::PartState::Failed: ++failed; break;
                case app::events::PartState::Queued: ++queued; break;
                case app::events::PartState::Running: ++running; break;
                default: break;
            }
        }
        if (totalCount(*shelf) != 8 || failed != 0 || queued != 0 || running != 0 || workers.empty() ||
            !allIdle(*shelf) || sumCompleted(*shelf) != 8) {
            std::fprintf(stderr,
                         "FAIL: batch A end state total=%d failed=%d queued=%d running=%d workers=%d "
                         "allIdle=%d sumCompleted=%d (want 8,0,0,0,>0,1,8)\n",
                         totalCount(*shelf), failed, queued, running, static_cast<int>(workers.size()),
                         allIdle(*shelf) ? 1 : 0, sumCompleted(*shelf));
            return 1;
        }

        // All parts Baked here, so the final mesh must be fully resolved --
        // no -1.0 "not yet computed" sentinel should remain.
        const app::events::MeshBuffers* finalMesh = shelf->mesh(parts.front().partId);
        if (!finalMesh) {
            std::fprintf(stderr, "FAIL: batch A part %llu is Baked but mesh() returned nullptr\n",
                         static_cast<unsigned long long>(parts.front().partId));
            return 1;
        }
        for (float ao : finalMesh->ao) {
            if (ao < 0.0f) {
                std::fprintf(stderr,
                             "FAIL: batch A part %llu's final mesh still carries a -1 AO sentinel\n",
                             static_cast<unsigned long long>(parts.front().partId));
                return 1;
            }
        }
    }

    // ---- Batch B (heavy) -----------------------------------------------
    // Wait for one job to start -- enough to guarantee C below lands on an
    // in-flight, not-yet-finished bake.
    kernel.send(app::events::RegenerateRequested{7, 8, 4, 256});
    if (!pump([&] { return anyRunningOrBusy(*shelf); }, 30000)) {
        std::fprintf(stderr, "FAIL: batch B never started a job within 30s\n");
        return 1;
    }

    // ---- Batch C (immediately) -------------------------------------------
    // beginBatch drops A/B's board wholesale, so C's baked==4 is the entire
    // board. B's queued runnables die on JobRunner's stale-start check on
    // pickup, so C still finishes promptly.
    kernel.send(app::events::RegenerateRequested{99, 4, 4, 16});
    if (!pump([&] { return bakedCount(*shelf) == 4 && allIdle(*shelf); }, 30000)) {
        std::fprintf(stderr,
                     "FAIL: batch C did not reach baked==4 && allIdle within 30s (baked=%d allIdle=%d)\n",
                     bakedCount(*shelf), allIdle(*shelf) ? 1 : 0);
        return 1;
    }

    {
        int failed = 0;
        for (const auto& part : shelf->partStatuses()) {
            if (part.state == app::events::PartState::Failed) {
                ++failed;
            }
        }
        // total==4: board holds only C. discardedArrivals>=1: a B result
        // landed after C's generation bump. sumCompleted==12 = 8 (A) + 4
        // (C) -- stale B finishes bump discardedArrivals, never completedCount.
        if (totalCount(*shelf) != 4 || shelf->discardedArrivals() < 1 || sumCompleted(*shelf) != 12 ||
            failed != 0) {
            std::fprintf(stderr,
                         "FAIL: batch C end state total=%d discardedArrivals=%d sumCompleted=%d failed=%d "
                         "(want 4, >=1, 12, 0)\n",
                         totalCount(*shelf), shelf->discardedArrivals(), sumCompleted(*shelf), failed);
            return 1;
        }
    }

    // ---- Slots are bounded by the pool size ----------------------------------
    for (const auto& worker : shelf->workerInfos()) {
        if (worker.slot < 0 || worker.slot >= 4) {
            std::fprintf(stderr, "FAIL: worker slot %d outside 0..3 with a 4-thread pool\n", worker.slot);
            return 1;
        }
    }

    // ---- Batch D after shrinking the pool to 2 -----------------------------------
    // New jobs must only get slots 0..1, so the view can hide lanes 2..3.
    {
        runner.setMaxThreads(2);
        std::map<int, int> completedBefore;
        for (const auto& worker : shelf->workerInfos()) {
            completedBefore[worker.slot] = worker.completedCount;
        }

        constexpr int kPartsD = 6;
        kernel.send(app::events::RegenerateRequested{123, kPartsD, 4, 16});
        int maxBusySlot = -1;
        if (!pump(
                [&] {
                    for (const auto& worker : shelf->workerInfos()) {
                        if (worker.busy) {
                            maxBusySlot = std::max(maxBusySlot, worker.slot);
                        }
                    }
                    return bakedCount(*shelf) == kPartsD && allIdle(*shelf);
                },
                30000)) {
            std::fprintf(stderr, "FAIL: batch D (2 threads) did not finish within 30s\n");
            return 1;
        }
        int completedInLowSlots = 0;
        for (const auto& worker : shelf->workerInfos()) {
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
        if (maxBusySlot >= 2 || completedInLowSlots != kPartsD) {
            std::fprintf(stderr, "FAIL: batch D maxBusySlot=%d completedInSlots0-1=%d (want <2, %d)\n", maxBusySlot,
                         completedInLowSlots, kPartsD);
            return 1;
        }
    }

    std::printf(
        "PASS: mesh-farm smoke (parallel bake, stale-generation discard, slot-bounded workers after pool "
        "shrink)\n");
    return 0;
}

// GUI probe: builds the window+presenter, lets connectActions fire its
// startup batch, grabs two whole-window screenshots on timers, then quits.
// Screenshots go to C:\tmp\mesh-farm-probe (Controlled Folder Access blocks
// writes under Documents).
int runGuiProbe(ordo::core::Kernel& kernel, QApplication& app, app::JobRunner& runner) {
    QDir().mkpath(QStringLiteral("C:/tmp/mesh-farm-probe"));

    // `window` must outlive the ViewHost (and presenter) handed pointers
    // into it -- declaration order enforces that.
    app::FarmWindow window;

    ordo::qt::ViewHost host(kernel);
    auto* presenter = host.add<app::ShelfPresenter>(window.canvas(), window.laneColumn(), window.progressText(),
                                                      window.progressBar(), window.statsLabel(), &window);

    // connectActions fires one batch before the window is shown, so the
    // probe opens mid-bake instead of on an empty shelf.
    window.connectActions(*presenter, runner);
    window.show();

    // ShelfGlWidget draws no QPainter overlay on its GL frame, so a plain
    // window grab already carries the whole scene.
    auto saveWindowFrame = [](app::FarmWindow& w, const QString& path) {
        const QPixmap windowPixmap = w.grab();
        if (windowPixmap.isNull()) {
            std::fprintf(stderr, "FAIL: grab() returned a null pixmap for %s\n", qUtf8Printable(path));
            return false;
        }
        QImageWriter writer(path, "png");
        if (!writer.write(windowPixmap.toImage())) {
            std::fprintf(stderr, "FAIL: QImageWriter failed for %s error=%s\n", qUtf8Printable(path),
                         qUtf8Printable(writer.errorString()));
            return false;
        }
        std::printf("wrote %s\n", qUtf8Printable(path));
        return true;
    };

    int exitCode = 0;

    // ~2s: mid-bake -- some parts still ghost-gray or mid-sweep, worker
    // lanes busy.
    QTimer::singleShot(2000, &window, [&] {
        if (!saveWindowFrame(window, QStringLiteral("C:/tmp/mesh-farm-probe/gui-baking.png"))) {
            exitCode = 1;
            QApplication::quit();
        }
    });

    // ~13s: comfortably past the batch -- every part Baked, lanes idle.
    QTimer::singleShot(13000, &window, [&] {
        if (!saveWindowFrame(window, QStringLiteral("C:/tmp/mesh-farm-probe/gui-idle.png"))) {
            exitCode = 1;
        }

        // Shrinking Max threads hides the idle lanes above the new limit.
        auto shelf = kernel.agentAs<app::PartShelf>(app::PartShelf::kName);
        if (!pump([&] { return allIdle(*shelf); }, 30000)) {
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
                std::printf("PASS: worker lanes %d -> %d after Max threads 4 -> 2\n", before, after);
            }
            laneHost->grab().save(QStringLiteral("C:/tmp/mesh-farm-probe/gui-lanes-shrunk.png"));
        }
        QApplication::quit();
    });

    app.exec();
    return exitCode;
}

}  // namespace

int main(int argc, char** argv) {
    // Must be set before QApplication brings the platform's GL context up.
    // Set unconditionally (including --smoke): it's just static config.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    // Declaration order matters: reverse destruction tears down `runner`
    // first (its QThreadPool dtor blocks until every in-flight job
    // returns), then `relay`, `kernel`, `app` -- so no worker thread can
    // call into an already-destroyed BakeRelay or Kernel.
    QApplication app(argc, argv);
    ordo::core::Kernel kernel;
    kernel.registerAgent(std::make_shared<app::PartShelf>());
    app::BakeRelay relay;
    app::JobRunner runner(relay);

    // BakeRelay's sinks re-enter the domain loop as ordinary events. These
    // lambdas run on the UI thread (queued delivery), which is why touching
    // `kernel` here is safe.
    relay.onStarted = [&kernel](const app::events::BakeJobStarted& event) { kernel.send(event); };
    relay.onProgress = [&kernel](const app::events::BakeJobProgress& event) { kernel.send(event); };
    relay.onFinished = [&kernel](const app::events::BakeJobFinished& event) { kernel.send(event); };

    // One Command per event type. RegenerateCommand alone takes a
    // constructor argument: starting a batch means both recording it on
    // the shelf and submitting work to the thread pool.
    kernel.registerCommand<app::events::RegenerateRequested, app::RegenerateCommand>(std::ref(runner));
    kernel.registerCommand<app::events::BakeJobStarted, app::BakeJobStartedCommand>();
    kernel.registerCommand<app::events::BakeJobProgress, app::BakeJobProgressCommand>();
    kernel.registerCommand<app::events::BakeJobFinished, app::BakeJobFinishedCommand>();

    // Explicit, so the smoke's parallelism doesn't depend on the machine
    // running it.
    runner.setMaxThreads(4);

    int exitCode = 0;
    if (argc > 1 && std::strcmp(argv[1], "--smoke") == 0) {
        auto shelf = kernel.agentAs<app::PartShelf>(app::PartShelf::kName);
        exitCode = runSmoke(kernel, shelf, runner);
    } else if (argc > 1 && std::strcmp(argv[1], "--gui-probe") == 0) {
        exitCode = runGuiProbe(kernel, app, runner);
    } else {
        // Window's widgets are constructed before ViewHost, so reverse
        // destruction tears ViewHost (and ShelfPresenter) down first --
        // otherwise the presenter could write into freed memory.
        app::FarmWindow window;

        ordo::qt::ViewHost host(kernel);
        auto* presenter = host.add<app::ShelfPresenter>(window.canvas(), window.laneColumn(),
                                                          window.progressText(), window.progressBar(),
                                                          window.statsLabel(), &window);

        // connectActions fires one batch before the window is shown, so the
        // app opens mid-action instead of on an empty shelf.
        window.connectActions(*presenter, runner);

        window.show();
        exitCode = app.exec();
    }

    // Drop every command registration before the objects they reference
    // (`runner`, the shelf agent) go out of scope.
    kernel.removeCommand<app::events::RegenerateRequested>();
    kernel.removeCommand<app::events::BakeJobStarted>();
    kernel.removeCommand<app::events::BakeJobProgress>();
    kernel.removeCommand<app::events::BakeJobFinished>();

    return exitCode;
}
