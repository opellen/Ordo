#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "domain/cloud_buffers.h"

namespace app::events {

// ---- Worker job notifications, sent by GenRelay on the UI thread ----------

// `slot` is the job's concurrency slot, 0..maxThreads-1 (lowest free at start).
struct GalaxyJobStarted {
    static constexpr std::string_view eventName = "GalaxyJobStarted";
    std::uint64_t galaxyId = 0;
    std::uint64_t epoch = 0;
    int slot = 0;
    std::uint64_t threadId = 0;
};

// `snapshot` is always a prefix of the final cloud.
struct GalaxyJobProgress {
    static constexpr std::string_view eventName = "GalaxyJobProgress";
    std::uint64_t galaxyId = 0;
    std::uint64_t epoch = 0;
    std::uint64_t threadId = 0;
    float fraction = 0.0f;
    std::shared_ptr<const CloudBuffers> snapshot;
};

struct GalaxyJobFinished {
    static constexpr std::string_view eventName = "GalaxyJobFinished";
    std::uint64_t galaxyId = 0;
    std::uint64_t epoch = 0;
    int slot = 0;
    std::uint64_t threadId = 0;
    bool ok = false;
    // shared_ptr: commands receive a const event, so buffers can't be moved out.
    std::shared_ptr<const CloudBuffers> cloud;
};

}  // namespace app::events
