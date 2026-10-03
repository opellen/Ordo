#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "domain/sector_coord.h"

namespace app::events {

// No "discarded" state: a superseded destination's galaxies leave the board.
enum class GalaxyState { Queued, Generating, Ready, Failed };

enum class Board { Current, Destination };

enum class JumpPhase { Idle, Jumping };

// `threadId` is meaningful only while state == Generating.
struct GalaxyStatus {
    std::uint64_t galaxyId = 0;
    Board board = Board::Current;
    GalaxyState state = GalaxyState::Queued;
    float progress = 0.0f;
    std::uint64_t threadId = 0;
};

// Keyed by concurrency slot; threadId is the OS thread that last ran in it.
struct WorkerInfo {
    int slot = 0;
    std::uint64_t threadId = 0;
    bool busy = false;
    std::uint64_t currentGalaxyId = 0;
    int completedCount = 0;
};

// Broadcast after every StarMap change. Carries no point data; pull clouds
// from StarMap::cloud.
struct StarMapChanged {
    static constexpr std::string_view eventName = "StarMapChanged";
    std::uint64_t epoch = 0;
    JumpPhase phase = JumpPhase::Idle;
    SectorCoord currentSector;
    SectorCoord destinationSector;
    // Counts cover the destination while Jumping, the current board once Idle.
    // `galaxies` always carries both boards, tagged.
    int queued = 0;
    int generating = 0;
    int ready = 0;
    int failed = 0;
    int total = 0;
    // Finished jobs dropped for a stale epoch; diagnostic only.
    int staleArrivals = 0;
    std::vector<GalaxyStatus> galaxies;
    std::vector<WorkerInfo> workers;
};

}  // namespace app::events
