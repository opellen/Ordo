#include "view/todo_view_model.h"

namespace app {

void TodoViewModel::onRegister() { subscribe<events::TodosFiltered>(&TodoViewModel::onTodosFiltered); }

void TodoViewModel::addTodo(const QString& title) { context().send(events::AddTodoRequested{title.toStdString()}); }
void TodoViewModel::toggleTodo(quint64 id) { context().send(events::ToggleTodoRequested{id}); }
void TodoViewModel::destroyTodo(quint64 id) { context().send(events::DestroyTodoRequested{id}); }
void TodoViewModel::editTodo(quint64 id, const QString& title) {
    context().send(events::EditTodoRequested{id, title.toStdString()});
}
void TodoViewModel::clearCompleted() { context().send(events::ClearCompletedRequested{}); }
void TodoViewModel::toggleAll(bool completed) { context().send(events::ToggleAllRequested{completed}); }
void TodoViewModel::setFilter(app::events::Filter filter) { context().send(events::SetFilterRequested{filter}); }

void TodoViewModel::onTodosFiltered(const events::TodosFiltered& e) {
    items_.clear();
    for (const auto& todo : e.todos) {
        items_.push_back(TodoItem{todo.id, QString::fromStdString(todo.title), todo.completed});
    }
    total_ = e.totalCount;
    active_ = e.activeCount;
    completed_ = e.completedCount;
    filter_ = e.filter;

    emit itemsChanged();
    emit countsChanged(active_, completed_, total_);
    emit filterChanged(filter_);

    // Compare-before-emit: TodosFiltered fires on every mutation, but these
    // fields change less often, so each is guarded against re-emitting an
    // unchanged value.
    const bool wasLoaded = loaded_;
    loaded_ = e.loaded;
    if (loaded_ != wasLoaded) {
        emit loadedChanged(loaded_);
    }

    const int previousPendingWrites = pendingWrites_;
    pendingWrites_ = e.pendingWrites;
    if (pendingWrites_ != previousPendingWrites) {
        emit pendingWritesChanged(pendingWrites_);
    }

    const QString previousPersistError = persistError_;
    persistError_ = QString::fromStdString(e.lastPersistError);
    if (persistError_ != previousPersistError) {
        emit persistErrorChanged(persistError_);
    }
}

}  // namespace app
