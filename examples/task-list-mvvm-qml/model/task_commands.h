#pragma once

#include <string>

#include <ordo/core/command.h>

#include "model/task_events.h"
#include "model/task_list.h"
#include "model/task_output.h"

namespace app {

// Validates the title and reports outcome via the output port (1:1);
// the agent's fact broadcast (1:N) is separate and never touches the port.
class AddTaskCommand : public ordo::core::Command<events::AddTaskRequested> {
public:
    explicit AddTaskCommand(TaskOutput& output) : output_(output) {}

    void execute(const events::AddTaskRequested& event, ordo::core::CommandContext& context) override;

private:
    static std::string trimmed(const std::string& s);

    TaskOutput& output_;
};

// Broadcast-only: no output port, only the agent's TaskToggled fact.
class ToggleTaskCommand : public ordo::core::Command<events::ToggleTaskRequested> {
public:
    void execute(const events::ToggleTaskRequested& event, ordo::core::CommandContext& context) override;
};

// Broadcast-only: no output port, only the agent's TaskRemoved fact.
class RemoveTaskCommand : public ordo::core::Command<events::RemoveTaskRequested> {
public:
    void execute(const events::RemoveTaskRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace app
