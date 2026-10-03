#pragma once

#include <string>

#include <ordo/core/command.h>

#include "model/task_events.h"
#include "model/task_list.h"
#include "model/task_output.h"

namespace app {

// Uses both result channels: the output port for caller-directed outcomes
// (1:1), the agent's fact broadcast for shared state (1:N). The agent never
// sees the port.
class AddTaskCommand : public ordo::core::Command<events::AddTaskRequested> {
public:
    explicit AddTaskCommand(TaskOutput& output) : output_(output) {}

    void execute(const events::AddTaskRequested& event, ordo::core::CommandContext& context) override;

private:
    static std::string trimmed(const std::string& s);

    TaskOutput& output_;
};

}  // namespace app
