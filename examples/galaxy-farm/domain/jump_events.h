#pragma once

#include <cstdint>
#include <string_view>

namespace app::events {

// Starts a jump; supersedes any jump still in transit.
struct JumpRequested {
    static constexpr std::string_view eventName = "JumpRequested";
    std::uint64_t universeSeed = 0;
    int galaxiesPerSector = 0;
    int starsPerGalaxy = 0;
};

// Sent when the transit animation completes; the domain has no clock.
struct JumpArrivalReached {
    static constexpr std::string_view eventName = "JumpArrivalReached";
};

}  // namespace app::events
