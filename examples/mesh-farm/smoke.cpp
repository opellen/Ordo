#include "checks.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <ordo/core/kernel.h>

#include "domain/bake_job_events.h"
#include "domain/mesh_buffers.h"
#include "domain/part_shelf.h"
#include "domain/regenerate_requested.h"
#include "domain/shelf_changed.h"
#include "infra/job_runner.h"

// Pumps the Qt event loop until `predicate` is true or `timeoutMs` elapses;
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

namespace {

// Read-only helpers over PartShelf's snapshot accessors.

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

// True once any job is in flight (a part Running or a worker busy).
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

// Sum of every worker's completedCount; stale finishes are not counted.
int sumCompleted(const app::PartShelf& shelf) {
    int sum = 0;
    for (const auto& worker : shelf.workerInfos()) {
        sum += worker.completedCount;
    }
    return sum;
}
}  // namespace

bool allIdle(const app::PartShelf& shelf) {
    for (const auto& worker : shelf.workerInfos()) {
        if (worker.busy) {
            return false;
        }
    }
    return true;
}

// Headless self-check: A) plain parallel bake, B) heavy batch superseded while
// in flight by C, D) a batch after the pool shrinks to 2 threads.
int runSmoke(ordo::core::Kernel& kernel, const std::shared_ptr<app::PartShelf>& shelf, app::JobRunner& runner) {
    if (!shelf) {
        std::fprintf(stderr, "FAIL: no '%s' agent\n", app::PartShelf::kName);
        return 1;
    }

    // ---- Batch A -----------------------------------------------------
    QElapsedTimer batchATimer;
    batchATimer.start();
    kernel.send(app::events::RegenerateRequested{42, 8, 4, 16});

    // Records whether any PartStatus was seen mid-sweep while waiting.
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
        std::fprintf(stderr, "FAIL: batch A timed out baked=%d total=%d\n", bakedCount(*shelf),
                     totalCount(*shelf));
        return 1;
    }
    std::printf("INFO: batch A %lld ms\n", static_cast<long long>(batchATimer.elapsed()));

    if (!sawPartialProgress) {
        std::fprintf(stderr, "FAIL: batch A saw no partial progress\n");
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
                         "allIdle=%d sumCompleted=%d\n",
                         totalCount(*shelf), failed, queued, running, static_cast<int>(workers.size()),
                         allIdle(*shelf) ? 1 : 0, sumCompleted(*shelf));
            return 1;
        }

        // All parts are Baked, so no -1.0 AO sentinel may remain.
        const app::events::MeshBuffers* finalMesh = shelf->mesh(parts.front().partId);
        if (!finalMesh) {
            std::fprintf(stderr, "FAIL: batch A part %llu has no mesh\n",
                         static_cast<unsigned long long>(parts.front().partId));
            return 1;
        }
        for (float ao : finalMesh->ao) {
            if (ao < 0.0f) {
                std::fprintf(stderr, "FAIL: batch A part %llu has an AO sentinel\n",
                             static_cast<unsigned long long>(parts.front().partId));
                return 1;
            }
        }
    }

    // ---- Batch B (heavy) -----------------------------------------------
    // Wait for one job to start so C lands on an in-flight bake.
    kernel.send(app::events::RegenerateRequested{7, 8, 4, 256});
    if (!pump([&] { return anyRunningOrBusy(*shelf); }, 30000)) {
        std::fprintf(stderr, "FAIL: batch B never started\n");
        return 1;
    }

    // ---- Batch C (immediately) -------------------------------------------
    // beginBatch drops the previous board; B's queued jobs are skipped by the
    // stale-start check.
    kernel.send(app::events::RegenerateRequested{99, 4, 4, 16});
    if (!pump([&] { return bakedCount(*shelf) == 4 && allIdle(*shelf); }, 30000)) {
        std::fprintf(stderr, "FAIL: batch C timed out baked=%d allIdle=%d\n", bakedCount(*shelf),
                     allIdle(*shelf) ? 1 : 0);
        return 1;
    }

    {
        int failed = 0;
        for (const auto& part : shelf->partStatuses()) {
            if (part.state == app::events::PartState::Failed) {
                ++failed;
            }
        }
        // Expect total 4 (only C), discardedArrivals >= 1 (stale B), sumCompleted 12 (A 8 + C 4).
        if (totalCount(*shelf) != 4 || shelf->discardedArrivals() < 1 || sumCompleted(*shelf) != 12 ||
            failed != 0) {
            std::fprintf(stderr, "FAIL: batch C end state total=%d discardedArrivals=%d sumCompleted=%d failed=%d\n",
                         totalCount(*shelf), shelf->discardedArrivals(), sumCompleted(*shelf), failed);
            return 1;
        }
    }

    // ---- Slots are bounded by the pool size ----------------------------------
    for (const auto& worker : shelf->workerInfos()) {
        if (worker.slot < 0 || worker.slot >= 4) {
            std::fprintf(stderr, "FAIL: worker slot %d outside 0..3\n", worker.slot);
            return 1;
        }
    }

    // ---- Batch D after shrinking the pool to 2 -----------------------------------
    // New jobs must only use slots 0..1.
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
            std::fprintf(stderr, "FAIL: batch D timed out\n");
            return 1;
        }
        int completedInLowSlots = 0;
        for (const auto& worker : shelf->workerInfos()) {
            const int delta = worker.completedCount - completedBefore[worker.slot];
            if (worker.slot >= 2 && delta != 0) {
                std::fprintf(stderr, "FAIL: slot %d completed %d jobs in batch D\n", worker.slot, delta);
                return 1;
            }
            if (worker.slot < 2) {
                completedInLowSlots += delta;
            }
        }
        if (maxBusySlot >= 2 || completedInLowSlots != kPartsD) {
            std::fprintf(stderr, "FAIL: batch D maxBusySlot=%d completedInSlots0-1=%d expected=%d\n", maxBusySlot,
                         completedInLowSlots, kPartsD);
            return 1;
        }
    }

    std::printf("PASS: mesh-farm smoke\n");
    return 0;
}
