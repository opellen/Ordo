#include "controller/persistence_commands.h"

#include "model/todo_list.h"

namespace app {

void LoadTodosCommand::execute(const events::LoadTodosRequested&, ordo::core::CommandContext&) { db_.load(); }

void TodosLoadedCommand::execute(const events::TodosLoaded& event, ordo::core::CommandContext& context) {
    auto todos = context.agentAs<TodoListAgent>(TodoListAgent::kName);
    if (!todos) {
        return;
    }
    todos->adoptLoaded(event.todos, event.ok, event.error);
}

void PersistCompletedCommand::execute(const events::PersistCompleted& event, ordo::core::CommandContext& context) {
    auto todos = context.agentAs<TodoListAgent>(TodoListAgent::kName);
    if (!todos) {
        return;
    }
    todos->notePersistCompleted(event.ok, event.error);
}

}  // namespace app
