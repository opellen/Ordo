#include "controller/todo_commands.h"

#include <string>

#include "model/todo_list.h"

namespace app {

namespace detail {

// Shared by AddTodoCommand and EditTodoCommand.
std::string trimmed(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return {};
    }
    return s.substr(begin, s.find_last_not_of(" \t") - begin + 1);
}

}  // namespace detail

void AddTodoCommand::execute(const events::AddTodoRequested& event, ordo::core::CommandContext& context) {
    const std::string title = detail::trimmed(event.title);
    if (title.empty()) {
        return;
    }

    auto todos = context.agentAs<TodoListAgent>(TodoListAgent::kName);
    if (!todos) {
        return;  // the todo list is always registered in this app
    }
    const std::uint64_t id = todos->add(title);
    db_.add(id, title);   // same id, same (trimmed) title handed to the agent above
    todos->notePersistQueued();
}

void ToggleTodoCommand::execute(const events::ToggleTodoRequested& event, ordo::core::CommandContext& context) {
    auto todos = context.agentAs<TodoListAgent>(TodoListAgent::kName);
    if (!todos) {
        return;
    }
    todos->toggle(event.id);

    // Reads the flipped value back off the agent rather than having toggle()
    // report it; an unknown id is simply absent, which skips the DB op below.
    for (const auto& todo : todos->todos()) {
        if (todo.id == event.id) {
            db_.setCompleted(todo.id, todo.completed);
            todos->notePersistQueued();
            break;
        }
    }
}

void DestroyTodoCommand::execute(const events::DestroyTodoRequested& event, ordo::core::CommandContext& context) {
    auto todos = context.agentAs<TodoListAgent>(TodoListAgent::kName);
    if (!todos) {
        return;
    }
    todos->destroy(event.id);
    db_.remove(event.id);
    todos->notePersistQueued();
}

void EditTodoCommand::execute(const events::EditTodoRequested& event, ordo::core::CommandContext& context) {
    auto todos = context.agentAs<TodoListAgent>(TodoListAgent::kName);
    if (!todos) {
        return;
    }

    const std::string title = detail::trimmed(event.title);
    if (title.empty()) {
        todos->destroy(event.id);   // edit-to-empty == destroy
        db_.remove(event.id);
        todos->notePersistQueued();
        return;
    }
    todos->edit(event.id, title);

    // Same skip-if-absent guard as ToggleTodoCommand: edit() is a silent
    // no-op for an unknown id.
    for (const auto& todo : todos->todos()) {
        if (todo.id == event.id) {
            db_.setTitle(event.id, title);
            todos->notePersistQueued();
            break;
        }
    }
}

void ClearCompletedCommand::execute(const events::ClearCompletedRequested&, ordo::core::CommandContext& context) {
    auto todos = context.agentAs<TodoListAgent>(TodoListAgent::kName);
    if (!todos) {
        return;
    }
    todos->clearCompleted();
    db_.removeCompleted();
    todos->notePersistQueued();
}

void ToggleAllCommand::execute(const events::ToggleAllRequested& event, ordo::core::CommandContext& context) {
    auto todos = context.agentAs<TodoListAgent>(TodoListAgent::kName);
    if (!todos) {
        return;
    }
    todos->toggleAll(event.completed);
    db_.setAllCompleted(event.completed);
    todos->notePersistQueued();
}

void SetFilterCommand::execute(const events::SetFilterRequested& event, ordo::core::CommandContext& context) {
    auto todos = context.agentAs<TodoListAgent>(TodoListAgent::kName);
    if (!todos) {
        return;
    }
    todos->setFilter(event.filter);
}

}  // namespace app
