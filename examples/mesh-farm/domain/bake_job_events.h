#pragma once

// Events posted by BakeRelay (on the UI thread) when a worker-thread bake
// starts, checkpoints, or finishes.

#include <cstdint>
#include <memory>
#include <string_view>

#include "domain/mesh_buffers.h"

namespace app::events {

// `slot` is the job's concurrency slot, 0..maxThreads-1 (lowest free at start).
struct BakeJobStarted {
    static constexpr std::string_view eventName = "BakeJobStarted";
    std::uint64_t partId = 0;
    std::uint64_t generation = 0;
    int slot = 0;
    std::uint64_t threadId = 0;
};

struct BakeJobFinished {
    static constexpr std::string_view eventName = "BakeJobFinished";
    std::uint64_t partId = 0;
    std::uint64_t generation = 0;
    int slot = 0;
    std::uint64_t threadId = 0;
    bool ok = false;
    // shared_ptr: Dispatcher hands Commands a const ref, so this can't be moved out.
    std::shared_ptr<const MeshBuffers> buffers;
};

// A worker thread's bake reached an AO checkpoint. `snapshot` is a partial
// mesh: its `ao` carries the -1.0 "not yet computed" sentinel.
struct BakeJobProgress {
    static constexpr std::string_view eventName = "BakeJobProgress";
    std::uint64_t partId = 0;
    std::uint64_t generation = 0;
    std::uint64_t threadId = 0;
    float fraction = 0.0f;
    std::shared_ptr<const MeshBuffers> snapshot;
};

}  // namespace app::events
