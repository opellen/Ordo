#pragma once

#include <cstdint>
#include <string>

namespace app::entities {

// Entity: plain data. TaskList owns the rule for creating one.
struct Task {
    std::uint64_t id = 0;
    std::string title;
};

}  // namespace app::entities
