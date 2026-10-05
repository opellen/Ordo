#include "model/todo_list.h"

#include <algorithm>

#include "model/todo_events.h"

namespace app {

std::uint64_t TodoListAgent::add(const std::string& title) {
    const std::uint64_t newId = nextId_++;
    todos_.push_back(events::Todo{newId, title, false});
    publishSnapshot();
    return newId;
}

void TodoListAgent::toggle(std::uint64_t id) {
    for (auto& todo : todos_) {
        if (todo.id == id) {
            todo.completed = !todo.completed;
            break;
        }
    }
    publishSnapshot();
}

void TodoListAgent::destroy(std::uint64_t id) {
    std::erase_if(todos_, [id](const events::Todo& todo) { return todo.id == id; });
    publishSnapshot();
}

void TodoListAgent::edit(std::uint64_t id, const std::string& title) {
    for (auto& todo : todos_) {
        if (todo.id == id) {
            todo.title = title;
            break;
        }
    }
    publishSnapshot();
}

void TodoListAgent::clearCompleted() {
    std::erase_if(todos_, [](const events::Todo& todo) { return todo.completed; });
    publishSnapshot();
}

void TodoListAgent::toggleAll(bool completed) {
    for (auto& todo : todos_) {
        todo.completed = completed;
    }
    publishSnapshot();
}

void TodoListAgent::setFilter(events::Filter filter) {
    filter_ = filter;
    publishSnapshot();
}

void TodoListAgent::adoptLoaded(std::vector<events::Todo> todos, bool ok, const std::string& error) {
    loaded_ = true;
    if (ok) {
        todos_ = std::move(todos);
        std::uint64_t maxId = 0;
        for (const auto& todo : todos_) {
            maxId = std::max(maxId, todo.id);
        }
        // Continues from the DB's ids so add() never mints one that collides with disk.
        nextId_ = maxId + 1;
    } else {
        lastPersistError_ = error;
    }
    publishSnapshot();
}

void TodoListAgent::notePersistQueued() {
    ++pendingWrites_;
    publishSnapshot();
}

void TodoListAgent::notePersistCompleted(bool ok, const std::string& error) {
    if (pendingWrites_ > 0) {
        --pendingWrites_;
    }
    if (!ok) {
        lastPersistError_ = error;
    }
    publishSnapshot();
}

void TodoListAgent::publishSnapshot() {
    int active = 0;
    int completed = 0;
    for (const auto& todo : todos_) {
        if (todo.completed) {
            ++completed;
        } else {
            ++active;
        }
    }

    std::vector<events::Todo> filtered;
    for (const auto& todo : todos_) {
        const bool include = filter_ == events::Filter::All ||
                              (filter_ == events::Filter::Active && !todo.completed) ||
                              (filter_ == events::Filter::Completed && todo.completed);
        if (include) {
            filtered.push_back(todo);
        }
    }

    context().send(events::TodosFiltered{
        .todos = std::move(filtered),
        .totalCount = static_cast<int>(todos_.size()),
        .activeCount = active,
        .completedCount = completed,
        .filter = filter_,
        .loaded = loaded_,
        .pendingWrites = pendingWrites_,
        .lastPersistError = lastPersistError_,
    });
}

}  // namespace app
