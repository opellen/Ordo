#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace app::events {

// Intents.
struct AddTaskRequested {
    static constexpr std::string_view eventName = "AddTaskRequested";
    std::string title;
};

struct ToggleTaskRequested {
    static constexpr std::string_view eventName = "ToggleTaskRequested";
    std::uint64_t id = 0;
};

struct RemoveTaskRequested {
    static constexpr std::string_view eventName = "RemoveTaskRequested";
    std::uint64_t id = 0;
};

// Facts.
struct TaskAdded {
    static constexpr std::string_view eventName = "TaskAdded";
    std::uint64_t id = 0;
    std::string title;
};

struct TaskToggled {
    static constexpr std::string_view eventName = "TaskToggled";
    std::uint64_t id = 0;
    bool done = false;
};

struct TaskRemoved {
    static constexpr std::string_view eventName = "TaskRemoved";
    std::uint64_t id = 0;
};

}  // namespace app::events
