#pragma once

#include <string>

#include <ordo/core/command.h>

#include "entities/task_list.h"
#include "use_cases/add_task_request.h"
#include "use_cases/add_task_response.h"
#include "use_cases/task_output.h"

namespace app::use_cases {

// Business policy lives here. Uses both result channels: the output port
// for caller-directed outcomes (1:1), the agent's fact broadcast for
// anything observable (1:N). The agent never sees the port.
class AddTaskCommand : public ordo::core::Command<AddTaskRequest> {
public:
    explicit AddTaskCommand(TaskOutput& output) : output_(output) {}

    void execute(const AddTaskRequest& event, ordo::core::CommandContext& context) override {
        const std::string title = trimmed(event.title);
        if (title.empty()) {
            output_.presentRejected("title is empty");  // 1:1 -- nobody else hears this
            return;                                     // no mutation, no fact
        }

        auto tasks = context.agentAs<entities::TaskList>(entities::TaskList::kName);
        if (!tasks) {
            output_.presentRejected("task list unavailable");
            return;
        }

        const auto id = tasks->add(title);                 // agent sends TaskAdded (1:N)
        output_.presentAdded(AddTaskResponse{id, title});   // 1:1 echo to the caller
    }

private:
    static std::string trimmed(const std::string& s) {
        const auto begin = s.find_first_not_of(" \t");
        if (begin == std::string::npos) {
            return {};
        }
        return s.substr(begin, s.find_last_not_of(" \t") - begin + 1);
    }

    TaskOutput& output_;
};

}  // namespace app::use_cases
