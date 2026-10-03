#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "model/todo.h"

namespace app::events {

// Persistence lane events, separate from the TodoMVC feature vocabulary.

// Sent once at bootstrap to ask DbRelay for the table's current rows.
struct LoadTodosRequested {
    static constexpr std::string_view eventName = "LoadTodosRequested";
};

// Internal events: posted by DbRelay, already marshalled onto the UI thread.

// The reply to LoadTodosRequested.
struct TodosLoaded {
    static constexpr std::string_view eventName = "TodosLoaded";
    std::vector<Todo> todos;
    bool ok = false;
    std::string error;
};

// Posted once per queued write once DbWorker finishes it, success or failure alike.
struct PersistCompleted {
    static constexpr std::string_view eventName = "PersistCompleted";
    // Identifies which queued write this completes; carried for diagnostics only.
    std::uint64_t opSeq = 0;
    bool ok = false;
    std::string error;
};

}  // namespace app::events
