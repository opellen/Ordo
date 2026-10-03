#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "model/todo.h"

namespace app::events {

// The TodoMVC feature's vocabulary: its seven user intents and the one fact
// they produce.

// ---- Intents: requests to perform an action --------------------------------

struct AddTodoRequested {
    static constexpr std::string_view eventName = "AddTodoRequested";
    std::string title;
};

struct ToggleTodoRequested {
    static constexpr std::string_view eventName = "ToggleTodoRequested";
    std::uint64_t id = 0;
};

struct DestroyTodoRequested {
    static constexpr std::string_view eventName = "DestroyTodoRequested";
    std::uint64_t id = 0;
};

struct EditTodoRequested {
    static constexpr std::string_view eventName = "EditTodoRequested";
    std::uint64_t id = 0;
    std::string title;
};

struct ClearCompletedRequested {
    static constexpr std::string_view eventName = "ClearCompletedRequested";
};

struct ToggleAllRequested {
    static constexpr std::string_view eventName = "ToggleAllRequested";
    bool completed = false;
};

struct SetFilterRequested {
    static constexpr std::string_view eventName = "SetFilterRequested";
    Filter filter = Filter::All;
};

// ---- Fact: notification that state has changed -----------------------------
// todos is already filtered to the current `filter`.
struct TodosFiltered {
    static constexpr std::string_view eventName = "TodosFiltered";
    std::vector<Todo> todos;
    int totalCount = 0;
    int activeCount = 0;
    int completedCount = 0;
    Filter filter = Filter::All;
    // False until the first TodosLoaded arrives (success or failure).
    bool loaded = false;
    // DB writes queued but not yet confirmed by a PersistCompleted.
    int pendingWrites = 0;
    // Most recent failed write's message, empty if the last write succeeded.
    std::string lastPersistError;
};

}  // namespace app::events
