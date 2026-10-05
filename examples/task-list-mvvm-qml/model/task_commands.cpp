#include "model/task_commands.h"

namespace app {

void AddTaskCommand::execute(const events::AddTaskRequested& event, ordo::core::CommandContext& context) {
    const std::string title = trimmed(event.title);
    if (title.empty()) {
        output_.taskRejected("title is empty");   // 1:1 -- nobody else hears this
        return;                                   // no mutation, no fact
    }

    auto tasks = context.agentAs<TaskList>(TaskList::kName);
    if (!tasks) {
        output_.taskRejected("task list unavailable");
        return;
    }

    const auto id = tasks->add(title);            // agent sends TaskAdded (1:N)
    output_.taskAccepted(id, title);              // 1:1 echo to the caller
}

std::string AddTaskCommand::trimmed(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return {};
    }
    return s.substr(begin, s.find_last_not_of(" \t") - begin + 1);
}

void ToggleTaskCommand::execute(const events::ToggleTaskRequested& event, ordo::core::CommandContext& context) {
    auto tasks = context.agentAs<TaskList>(TaskList::kName);
    if (!tasks) {
        return;  // no output port -- nothing to report to
    }

    // Unknown ids can only come from a stale view -- silence, not an error.
    tasks->toggle(event.id);  // agent sends TaskToggled on success (1:N)
}

void RemoveTaskCommand::execute(const events::RemoveTaskRequested& event, ordo::core::CommandContext& context) {
    auto tasks = context.agentAs<TaskList>(TaskList::kName);
    if (!tasks) {
        return;  // no output port -- nothing to report to
    }

    // Unknown ids can only come from a stale view -- silence, not an error.
    tasks->remove(event.id);  // agent sends TaskRemoved on success (1:N)
}

}  // namespace app
