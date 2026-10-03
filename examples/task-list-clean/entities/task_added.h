#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace app::entities {

// Fact broadcast by TaskList when a task is created; any view may subscribe (1:N).
struct TaskAdded {
    static constexpr std::string_view eventName = "TaskAdded";
    std::uint64_t id = 0;
    std::string title;
};

}  // namespace app::entities
