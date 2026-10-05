#pragma once

#include <cstdint>
#include <string_view>

namespace app::events {

// ---- Intent: request to perform an action ----------------------------------

// The one intent a user action in this example ever sends: (re)generate a
// whole batch of parts. Firing this while a previous batch is still baking
// discards that batch's in-flight work -- see PartShelf::beginBatch.
struct RegenerateRequested {
    static constexpr std::string_view eventName = "RegenerateRequested";
    std::uint64_t seed = 0;
    int partCount = 0;
    int smoothIters = 0;
    int aoRaysPerVertex = 0;
};

}  // namespace app::events
