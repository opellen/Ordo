#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace app::events {

// Intent: request to perform an action.
struct AddTaskRequested {
    static constexpr std::string_view eventName = "AddTaskRequested";
    std::string title;
};

// Fact: notification that state has changed.
struct TaskAdded {
    static constexpr std::string_view eventName = "TaskAdded";
    std::uint64_t id = 0;
    std::string title;
};

}  // namespace app::events
