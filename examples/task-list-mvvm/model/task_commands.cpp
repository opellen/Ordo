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

}  // namespace app
