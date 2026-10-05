#include "checks.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <ordo/core/kernel.h>

#include "domain/cloud_buffers.h"
#include "domain/jump_events.h"
#include "domain/sector_coord.h"
#include "domain/star_map.h"
#include "domain/star_map_changed.h"
#include "infra/job_runner.h"

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

bool allWorkersIdle(const app::StarMap& starMap) {
    for (const auto& worker : starMap.workerInfos()) {
        if (worker.busy) {
            return false;
        }
    }
    return true;
}

namespace {

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

}  // namespace

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
        std::fprintf(stderr, "FAIL: jump not ready==%d within 30s (ready=%d total=%d)\n",
                     kGalaxiesPerSector, readyCount(*starMap),
                     static_cast<int>(starMap->galaxyStatuses().size()));
        return 1;
    }
    std::printf("INFO: jump wall time = %lld ms (%d galaxies, 4 threads)\n",
                static_cast<long long>(jumpTimer.elapsed()), kGalaxiesPerSector);

    if (!sawPartialSector) {
        std::fprintf(stderr, "FAIL: ready never strictly between 0 and %d\n", kGalaxiesPerSector);
        return 1;
    }
    if (!sawPartialCloud) {
        std::fprintf(stderr, "FAIL: no galaxy with 0 < progress < 1 before ready==%d\n", kGalaxiesPerSector);
        return 1;
    }

    // ---- Arrival -----------------------------------------------------------
    kernel.send(app::events::JumpArrivalReached{});

    if (starMap->phase() != app::events::JumpPhase::Idle) {
        std::fprintf(stderr, "FAIL: phase not Idle after arrival\n");
        return 1;
    }
    if (starMap->currentSector() != destinationDuringTransit) {
        std::fprintf(stderr, "FAIL: currentSector (%d,%d) != transit destination (%d,%d)\n",
                     starMap->currentSector().x, starMap->currentSector().y, destinationDuringTransit.x,
                     destinationDuringTransit.y);
        return 1;
    }

    if (!pump([&] { return allWorkersIdle(*starMap); }, 30000)) {
        std::fprintf(stderr, "FAIL: workers not idle within 30s after arrival\n");
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
            std::fprintf(stderr, "FAIL: staleArrivals=%d, want 0\n",
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
            std::fprintf(stderr, "FAIL: cloud() is null for Ready galaxyId=%llu\n",
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
            std::fprintf(stderr, "FAIL: STAR-kind count=%zu, want %d\n", starKindCount,
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
        std::fprintf(stderr, "FAIL: jump B started no job within 30s\n");
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
        std::fprintf(stderr, "FAIL: jump C not done within 30s (destinationReady=%d/%d allIdle=%d)\n",
                     readyCountOnBoard(*starMap, app::events::Board::Destination), kGalaxiesPerSectorC,
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
                         "FAIL: jump C destination total=%d ready=%d failed=%d, want %d, %d, 0\n",
                         destinationTotal, destinationReady, destinationFailed, kGalaxiesPerSectorC,
                         kGalaxiesPerSectorC);
            return 1;
        }

        // At least one jump B bake must arrive stale.
        if (starMap->staleArrivals() < 1) {
            std::fprintf(stderr, "FAIL: staleArrivals=%d, want >= 1\n", starMap->staleArrivals());
            return 1;
        }

        // Exactly jump A's + jump C's; stale finishes never count.
        const int wantCompleted = kGalaxiesPerSector + kGalaxiesPerSectorC;
        const int sum = sumCompleted(*starMap);
        if (sum != wantCompleted) {
            std::fprintf(stderr, "FAIL: completedCount sum=%d, want %d\n", sum, wantCompleted);
            return 1;
        }

        if (!sawPartialCloudC) {
            std::fprintf(stderr, "FAIL: no jump C galaxy with 0 < progress < 1\n");
            return 1;
        }
    }

    // ---- Slots are bounded by the pool size ------------------------------------
    for (const auto& worker : starMap->workerInfos()) {
        if (worker.slot < 0 || worker.slot >= 4) {
            std::fprintf(stderr, "FAIL: worker slot %d outside 0..3\n", worker.slot);
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
            std::fprintf(stderr, "FAIL: jump D not done within 30s\n");
            return 1;
        }
        int completedInLowSlots = 0;
        for (const auto& worker : starMap->workerInfos()) {
            const int delta = worker.completedCount - completedBefore[worker.slot];
            if (worker.slot >= 2 && delta != 0) {
                std::fprintf(stderr, "FAIL: slot %d completed %d jobs after pool shrink\n", worker.slot,
                             delta);
                return 1;
            }
            if (worker.slot < 2) {
                completedInLowSlots += delta;
            }
        }
        if (maxBusySlot >= 2 || completedInLowSlots != kGalaxiesPerSectorD) {
            std::fprintf(stderr, "FAIL: jump D maxBusySlot=%d completedInSlots0-1=%d, want <2, %d\n", maxBusySlot,
                         completedInLowSlots, kGalaxiesPerSectorD);
            return 1;
        }
    }

    std::printf("PASS: galaxy-farm smoke\n");
    return 0;
}
