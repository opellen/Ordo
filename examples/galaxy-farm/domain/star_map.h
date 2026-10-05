#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <ordo/core/agent.h>

#include "domain/cloud_buffers.h"
#include "domain/sector_coord.h"
#include "domain/star_map_changed.h"

namespace app {

// Galaxies on two boards (current_ under the camera, destination_ being
// built by a jump) plus worker status. Every mutation ends in publishSnapshot().
// beginJump bumps epoch_; arrive() does not, so in-flight galaxies stay valid.
class StarMap : public ordo::core::Agent {
public:
    static constexpr const char* kName = "starmap";

    StarMap() : Agent(kName) {}

    // Everything a worker needs, and nothing more.
    struct JumpJob {
        std::uint64_t galaxyId = 0;
        std::uint64_t seed = 0;
        int type = 0;
        int starCount = 0;
    };

    // Picks the destination deterministically and drops any previous one
    // (its late results are caught by the epoch check). Returns the jobs;
    // outEpoch receives the new epoch.
    std::vector<JumpJob> beginJump(std::uint64_t universeSeed, int galaxiesPerSector, int starsPerGalaxy,
                                    std::uint64_t& outEpoch);

    // Promotes destination_ to current_ and returns to Idle. No-op while Idle.
    void arrive();

    void markStarted(std::uint64_t galaxyId, std::uint64_t epoch, int slot, std::uint64_t threadId);

    // Stale progress is dropped silently; it doesn't count toward staleArrivals.
    void markProgress(std::uint64_t galaxyId, std::uint64_t epoch, float fraction,
                       std::shared_ptr<const events::CloudBuffers> snapshot);

    void storeGenerated(std::uint64_t galaxyId, std::uint64_t epoch, int slot, std::uint64_t threadId, bool ok,
                         std::shared_ptr<const events::CloudBuffers> cloud);

    // Latest cloud for galaxyId on either board, or nullptr if none yet.
    const events::CloudBuffers* cloud(std::uint64_t galaxyId) const;

    std::uint64_t epoch() const { return epoch_; }
    events::JumpPhase phase() const { return phase_; }
    int staleArrivals() const { return staleArrivals_; }
    events::SectorCoord currentSector() const { return current_.sector; }
    events::SectorCoord destinationSector() const { return destination_.sector; }

    // Same shape as StarMapChanged's fields.
    std::vector<events::GalaxyStatus> galaxyStatuses() const { return buildGalaxyStatuses(); }
    std::vector<events::WorkerInfo> workerInfos() const { return buildWorkerInfos(); }

private:
    struct Galaxy {
        std::uint64_t galaxyId = 0;
        std::uint64_t seed = 0;
        int type = 0;
        int starCount = 0;
        events::GalaxyState state = events::GalaxyState::Queued;
        float progress = 0.0f;
        std::uint64_t threadId = 0;
        std::shared_ptr<const events::CloudBuffers> cloud;
    };

    struct SectorBoard {
        events::SectorCoord sector;
        std::vector<Galaxy> galaxies;
    };

    Galaxy* findGalaxy(std::uint64_t galaxyId);
    const Galaxy* findGalaxy(std::uint64_t galaxyId) const;

    std::vector<events::GalaxyStatus> buildGalaxyStatuses() const;

    // Ascending by slot.
    std::vector<events::WorkerInfo> buildWorkerInfos() const;

    void publishSnapshot();

    SectorBoard current_;
    SectorBoard destination_;
    std::map<int, events::WorkerInfo> workers_;
    std::uint64_t epoch_ = 0;
    std::uint64_t nextGalaxyId_ = 1;
    int staleArrivals_ = 0;
    events::JumpPhase phase_ = events::JumpPhase::Idle;
};

}  // namespace app
