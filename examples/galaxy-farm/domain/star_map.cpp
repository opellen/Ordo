#include "domain/star_map.h"

#include "galaxylib.h"

namespace app {

namespace detail {

// One splitmix64 step (Sebastiano Vigna, public domain).
inline std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

inline std::uint64_t combine(std::uint64_t a, std::uint64_t b) { return splitmix64(a ^ splitmix64(b)); }

// The 8 neighbor steps; order is fixed so picks are reproducible.
constexpr int kStepDx[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
constexpr int kStepDy[8] = {-1, 0, 1, -1, 1, -1, 0, 1};

// Hashes (universeSeed, epoch) into one of the 8 neighbors of `from`.
events::SectorCoord stepDestination(const events::SectorCoord& from, std::uint64_t universeSeed,
                                     std::uint64_t epoch) {
    const std::uint64_t h = combine(universeSeed, epoch);
    const int index = static_cast<int>(h % 8);
    return events::SectorCoord{from.x + kStepDx[index], from.y + kStepDy[index]};
}

// Coordinates hash as uint32 bit patterns (no sign extension).
std::uint64_t sectorSeed(std::uint64_t universeSeed, events::SectorCoord coord) {
    const auto ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x));
    const auto uy = static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.y));
    return combine(universeSeed, combine(ux, uy));
}

std::uint64_t galaxySeed(std::uint64_t sectorSeedValue, int index) {
    return combine(sectorSeedValue, static_cast<std::uint64_t>(index));
}

// Weighted type pick from the galaxy seed.
int galaxyType(std::uint64_t galaxySeedValue) {
    const std::uint64_t roll = splitmix64(galaxySeedValue) % 100;
    if (roll < 26) {
        return GALAXYLIB_TYPE_SPIRAL;                    //  0..25 (26%)
    }
    if (roll < 48) {
        return GALAXYLIB_TYPE_BARRED;                     // 26..47 (22%)
    }
    if (roll < 62) {
        return GALAXYLIB_TYPE_ELLIPTICAL;                 // 48..61 (14%)
    }
    if (roll < 70) {
        return GALAXYLIB_TYPE_IRREGULAR;                  // 62..69 (8%)
    }
    if (roll < 84) {
        return GALAXYLIB_TYPE_STARBURST;                  // 70..83 (14%)
    }
    return GALAXYLIB_TYPE_DUST_BELT_LENTICULAR;           // 84..99 (16%)
}

}  // namespace detail

std::vector<StarMap::JumpJob> StarMap::beginJump(std::uint64_t universeSeed, int galaxiesPerSector,
                                                  int starsPerGalaxy, std::uint64_t& outEpoch) {
    ++epoch_;
    destination_.galaxies.clear();

    destination_.sector = detail::stepDestination(current_.sector, universeSeed, epoch_);
    const std::uint64_t sectorSeedValue = detail::sectorSeed(universeSeed, destination_.sector);

    std::vector<JumpJob> jobs;
    jobs.reserve(static_cast<std::size_t>(galaxiesPerSector));
    for (int index = 0; index < galaxiesPerSector; ++index) {
        const std::uint64_t galaxyId = nextGalaxyId_++;  // never reused
        const std::uint64_t seed = detail::galaxySeed(sectorSeedValue, index);
        const int type = detail::galaxyType(seed);

        Galaxy galaxy;
        galaxy.galaxyId = galaxyId;
        galaxy.seed = seed;
        galaxy.type = type;
        galaxy.starCount = starsPerGalaxy;
        galaxy.state = events::GalaxyState::Queued;
        destination_.galaxies.push_back(galaxy);

        jobs.push_back(JumpJob{galaxyId, seed, type, starsPerGalaxy});
    }

    phase_ = events::JumpPhase::Jumping;
    outEpoch = epoch_;
    publishSnapshot();
    return jobs;
}

void StarMap::arrive() {
    if (phase_ != events::JumpPhase::Jumping) {
        return;
    }
    current_ = std::move(destination_);
    destination_ = SectorBoard{};
    phase_ = events::JumpPhase::Idle;
    publishSnapshot();
}

void StarMap::markStarted(std::uint64_t galaxyId, std::uint64_t epoch, int slot, std::uint64_t threadId) {
    if (epoch != epoch_) {
        // Stale; JobRunner usually skips these first, but can race.
        return;
    }

    Galaxy* galaxy = findGalaxy(galaxyId);
    if (!galaxy) {
        return;  // defensive: shouldn't happen for a live epoch
    }
    galaxy->state = events::GalaxyState::Generating;
    galaxy->threadId = threadId;

    events::WorkerInfo& worker = workers_[slot];
    worker.slot = slot;
    worker.threadId = threadId;
    worker.busy = true;
    worker.currentGalaxyId = galaxyId;

    publishSnapshot();
}

void StarMap::markProgress(std::uint64_t galaxyId, std::uint64_t epoch, float fraction,
                            std::shared_ptr<const events::CloudBuffers> snapshot) {
    if (epoch != epoch_) {
        return;
    }

    Galaxy* galaxy = findGalaxy(galaxyId);
    if (!galaxy) {
        return;  // defensive: shouldn't happen for a live epoch
    }
    galaxy->progress = fraction;
    galaxy->cloud = std::move(snapshot);

    publishSnapshot();
}

void StarMap::storeGenerated(std::uint64_t galaxyId, std::uint64_t epoch, int slot, std::uint64_t threadId, bool ok,
                              std::shared_ptr<const events::CloudBuffers> cloud) {
    events::WorkerInfo& worker = workers_[slot];
    worker.slot = slot;
    worker.threadId = threadId;
    worker.busy = false;
    worker.currentGalaxyId = 0;

    if (epoch != epoch_) {
        // Stale: its board is gone. The worker still goes idle.
        ++staleArrivals_;
        publishSnapshot();
        return;
    }

    Galaxy* galaxy = findGalaxy(galaxyId);
    if (!galaxy) {
        publishSnapshot();  // defensive: shouldn't happen for a live epoch
        return;
    }

    galaxy->state = ok ? events::GalaxyState::Ready : events::GalaxyState::Failed;
    galaxy->threadId = threadId;
    if (ok) {
        galaxy->cloud = std::move(cloud);
        galaxy->progress = 1.0f;
    }
    ++worker.completedCount;

    publishSnapshot();
}

const events::CloudBuffers* StarMap::cloud(std::uint64_t galaxyId) const {
    const Galaxy* galaxy = findGalaxy(galaxyId);
    if (!galaxy || !galaxy->cloud) {
        return nullptr;
    }
    return galaxy->cloud.get();
}

StarMap::Galaxy* StarMap::findGalaxy(std::uint64_t galaxyId) {
    // A galaxy is on exactly one board.
    for (Galaxy& galaxy : destination_.galaxies) {
        if (galaxy.galaxyId == galaxyId) {
            return &galaxy;
        }
    }
    for (Galaxy& galaxy : current_.galaxies) {
        if (galaxy.galaxyId == galaxyId) {
            return &galaxy;
        }
    }
    return nullptr;
}

const StarMap::Galaxy* StarMap::findGalaxy(std::uint64_t galaxyId) const {
    return const_cast<StarMap*>(this)->findGalaxy(galaxyId);
}

std::vector<events::GalaxyStatus> StarMap::buildGalaxyStatuses() const {
    std::vector<events::GalaxyStatus> statuses;
    statuses.reserve(current_.galaxies.size() + destination_.galaxies.size());
    for (const Galaxy& galaxy : current_.galaxies) {
        statuses.push_back(
            events::GalaxyStatus{galaxy.galaxyId, events::Board::Current, galaxy.state, galaxy.progress,
                                  galaxy.threadId});
    }
    for (const Galaxy& galaxy : destination_.galaxies) {
        statuses.push_back(
            events::GalaxyStatus{galaxy.galaxyId, events::Board::Destination, galaxy.state, galaxy.progress,
                                  galaxy.threadId});
    }
    return statuses;
}

std::vector<events::WorkerInfo> StarMap::buildWorkerInfos() const {
    std::vector<events::WorkerInfo> infos;
    infos.reserve(workers_.size());
    for (const auto& [slot, info] : workers_) {
        infos.push_back(info);
    }
    return infos;
}

void StarMap::publishSnapshot() {
    const SectorBoard& scope = (phase_ == events::JumpPhase::Jumping) ? destination_ : current_;

    int queued = 0;
    int generating = 0;
    int ready = 0;
    int failed = 0;
    for (const Galaxy& galaxy : scope.galaxies) {
        switch (galaxy.state) {
            case events::GalaxyState::Queued: ++queued; break;
            case events::GalaxyState::Generating: ++generating; break;
            case events::GalaxyState::Ready: ++ready; break;
            case events::GalaxyState::Failed: ++failed; break;
        }
    }

    context().send(events::StarMapChanged{
        .epoch = epoch_,
        .phase = phase_,
        .currentSector = current_.sector,
        .destinationSector = destination_.sector,
        .queued = queued,
        .generating = generating,
        .ready = ready,
        .failed = failed,
        .total = static_cast<int>(scope.galaxies.size()),
        .staleArrivals = staleArrivals_,
        .galaxies = buildGalaxyStatuses(),
        .workers = buildWorkerInfos(),
    });
}

}  // namespace app
