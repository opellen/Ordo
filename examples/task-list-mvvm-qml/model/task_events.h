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

// Intent: request to perform an action.
struct ToggleTaskRequested {
    static constexpr std::string_view eventName = "ToggleTaskRequested";
    std::uint64_t id = 0;
};

// Intent: request to perform an action.
struct RemoveTaskRequested {
    static constexpr std::string_view eventName = "RemoveTaskRequested";
    std::uint64_t id = 0;
};

// Fact: notification that state has changed.
struct TaskAdded {
    static constexpr std::string_view eventName = "TaskAdded";
    std::uint64_t id = 0;
    std::string title;
};

// Fact: notification that state has changed.
struct TaskToggled {
    static constexpr std::string_view eventName = "TaskToggled";
    std::uint64_t id = 0;
    bool done = false;
};

// Fact: notification that state has changed.
struct TaskRemoved {
    static constexpr std::string_view eventName = "TaskRemoved";
    std::uint64_t id = 0;
};

}  // namespace app::events
