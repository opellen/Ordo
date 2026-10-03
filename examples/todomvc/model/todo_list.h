#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <ordo/core/agent.h>

#include "model/todo.h"

namespace app {

// Owns the todos and the current filter. Memory-authoritative: mutations
// enqueue a mirroring DbWorker write but this agent stays the source of
// truth. Every mutation ends by calling publishSnapshot().
class TodoListAgent : public ordo::core::Agent {
public:
    static constexpr const char* kName = "todos";

    TodoListAgent() : Agent(kName) {}

    std::uint64_t add(const std::string& title);

    // Flips one todo's completed flag. An id that matches nothing is a no-op.
    void toggle(std::uint64_t id);

    void destroy(std::uint64_t id);

    void edit(std::uint64_t id, const std::string& title);

    void clearCompleted();

    // Sets every todo's completed flag to the same value.
    void toggleAll(bool completed);

    void setFilter(events::Filter filter);

    const std::vector<events::Todo>& todos() const { return todos_; }

    // Reply to LoadTodosRequested. loaded_ flips to true even on failure;
    // the error surfaces through lastPersistError_ instead.
    void adoptLoaded(std::vector<events::Todo> todos, bool ok, const std::string& error);

    void notePersistQueued();

    void notePersistCompleted(bool ok, const std::string& error);

private:
    void publishSnapshot();

    std::vector<events::Todo> todos_;
    events::Filter filter_ = events::Filter::All;
    std::uint64_t nextId_ = 1;
    bool loaded_ = false;
    int pendingWrites_ = 0;
    std::string lastPersistError_;
};

}  // namespace app
