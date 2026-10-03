#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <ordo/core/agent.h>

#include "model/task_events.h"

namespace app {

struct Task {
    std::uint64_t id = 0;
    std::string title;
    bool done = false;
};

// Owns the tasks. Mutates on request from a Command, then publishes the fact.
class TaskList : public ordo::core::Agent {
public:
    static constexpr const char* kName = "tasks";

    TaskList() : Agent(kName) {}

    std::uint64_t add(const std::string& title);

    // Flips done, returns the new state, or nullopt if id is unknown.
    std::optional<bool> toggle(std::uint64_t id);

    // Erases the task, returns false if id is unknown.
    bool remove(std::uint64_t id);

    const std::vector<Task>& tasks() const { return tasks_; }

private:
    std::uint64_t nextId_ = 1;
    std::vector<Task> tasks_;
};

}  // namespace app
