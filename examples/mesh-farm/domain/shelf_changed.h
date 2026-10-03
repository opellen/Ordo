#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace app::events {

// Lifecycle of one part on the shelf, mirrored 1:1 into ShelfChanged's
// PartStatus. A superseded batch's parts leave the board wholesale when
// the next batch begins.
enum class PartState { Queued, Running, Baked, Failed };

// Per-part status broadcast in ShelfChanged. threadId is only meaningful
// while state == Running: it names the worker that most recently touched
// this part.
struct PartStatus {
    std::uint64_t partId = 0;
    PartState state = PartState::Queued;
    std::uint64_t threadId = 0;
    // 0..1 while Running, 1.0 once Baked; always 0.0 for Queued/Failed.
    float progress = 0.0f;
};

// Per-worker status, keyed by concurrency slot; threadId is the OS thread
// that last ran in it.
struct WorkerInfo {
    int slot = 0;
    std::uint64_t threadId = 0;
    bool busy = false;
    std::uint64_t currentPartId = 0;
    int completedCount = 0;
};

// PartShelf's one fact, broadcast after every state change. Carries no
// mesh buffers; a view pulls geometry from the agent as needed.
struct ShelfChanged {
    static constexpr std::string_view eventName = "ShelfChanged";
    std::uint64_t generation = 0;
    int queued = 0;
    int running = 0;
    int baked = 0;
    int failed = 0;
    int total = 0;
    // Count of stale-generation arrivals dropped so far. Diagnostic only.
    int discardedArrivals = 0;
    std::vector<PartStatus> parts;
    std::vector<WorkerInfo> workers;
};

}  // namespace app::events
