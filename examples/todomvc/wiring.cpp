#include "wiring.h"

#include <functional>

#include "controller/persistence_commands.h"
#include "controller/todo_commands.h"
#include "model/persistence_events.h"
#include "model/todo_events.h"

void registerCommands(ordo::core::Kernel& kernel, app::DbWorker& dbWorker) {
    kernel.registerCommand<app::events::AddTodoRequested, app::AddTodoCommand>(std::ref(dbWorker));
    kernel.registerCommand<app::events::ToggleTodoRequested, app::ToggleTodoCommand>(std::ref(dbWorker));
    kernel.registerCommand<app::events::DestroyTodoRequested, app::DestroyTodoCommand>(std::ref(dbWorker));
    kernel.registerCommand<app::events::EditTodoRequested, app::EditTodoCommand>(std::ref(dbWorker));
    kernel.registerCommand<app::events::ClearCompletedRequested, app::ClearCompletedCommand>(std::ref(dbWorker));
    kernel.registerCommand<app::events::ToggleAllRequested, app::ToggleAllCommand>(std::ref(dbWorker));
    kernel.registerCommand<app::events::SetFilterRequested, app::SetFilterCommand>();
    kernel.registerCommand<app::events::LoadTodosRequested, app::LoadTodosCommand>(std::ref(dbWorker));
    kernel.registerCommand<app::events::TodosLoaded, app::TodosLoadedCommand>();
    kernel.registerCommand<app::events::PersistCompleted, app::PersistCompletedCommand>();
}

void removeCommands(ordo::core::Kernel& kernel) {
    kernel.removeCommand<app::events::AddTodoRequested>();
    kernel.removeCommand<app::events::ToggleTodoRequested>();
    kernel.removeCommand<app::events::DestroyTodoRequested>();
    kernel.removeCommand<app::events::EditTodoRequested>();
    kernel.removeCommand<app::events::ClearCompletedRequested>();
    kernel.removeCommand<app::events::ToggleAllRequested>();
    kernel.removeCommand<app::events::SetFilterRequested>();
    kernel.removeCommand<app::events::LoadTodosRequested>();
    kernel.removeCommand<app::events::TodosLoaded>();
    kernel.removeCommand<app::events::PersistCompleted>();
}
