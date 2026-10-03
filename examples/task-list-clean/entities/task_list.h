#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <ordo/core/agent.h>

#include "entities/task.h"
#include "entities/task_added.h"

namespace app::entities {

// Owns the tasks. Mutates on request from a Command, then publishes the fact.
class TaskList : public ordo::core::Agent {
public:
    static constexpr const char* kName = "tasks";

    TaskList() : Agent(kName) {}

    std::uint64_t add(const std::string& title) {
        const std::uint64_t newId = nextId_++;
        tasks_.push_back(Task{newId, title});
        context().send(TaskAdded{.id = newId, .title = title});
        return newId;
    }

    const std::vector<Task>& tasks() const { return tasks_; }

private:
    std::uint64_t nextId_ = 1;
    std::vector<Task> tasks_;
};

}  // namespace app::entities
