#pragma once

#include <ordo/core/command.h>

#include "infra/db_worker.h"
#include "model/todo_events.h"

namespace app {

// Each user intent is its own event type, bound to its own Command in main.cpp.

// A blank (after trimming) title is ignored; the agent stays a plain state
// owner and this command holds the policy instead.
class AddTodoCommand : public ordo::core::Command<events::AddTodoRequested> {
public:
    explicit AddTodoCommand(DbWorker& db) : db_(db) {}

    void execute(const events::AddTodoRequested& event, ordo::core::CommandContext& context) override;

private:
    DbWorker& db_;
};

class ToggleTodoCommand : public ordo::core::Command<events::ToggleTodoRequested> {
public:
    explicit ToggleTodoCommand(DbWorker& db) : db_(db) {}

    void execute(const events::ToggleTodoRequested& event, ordo::core::CommandContext& context) override;

private:
    DbWorker& db_;
};

class DestroyTodoCommand : public ordo::core::Command<events::DestroyTodoRequested> {
public:
    explicit DestroyTodoCommand(DbWorker& db) : db_(db) {}

    void execute(const events::DestroyTodoRequested& event, ordo::core::CommandContext& context) override;

private:
    DbWorker& db_;
};

// Editing a title down to blank (after trimming) destroys the todo instead
// of leaving a blank one behind.
class EditTodoCommand : public ordo::core::Command<events::EditTodoRequested> {
public:
    explicit EditTodoCommand(DbWorker& db) : db_(db) {}

    void execute(const events::EditTodoRequested& event, ordo::core::CommandContext& context) override;

private:
    DbWorker& db_;
};

class ClearCompletedCommand : public ordo::core::Command<events::ClearCompletedRequested> {
public:
    explicit ClearCompletedCommand(DbWorker& db) : db_(db) {}

    void execute(const events::ClearCompletedRequested&, ordo::core::CommandContext& context) override;

private:
    DbWorker& db_;
};

class ToggleAllCommand : public ordo::core::Command<events::ToggleAllRequested> {
public:
    explicit ToggleAllCommand(DbWorker& db) : db_(db) {}

    void execute(const events::ToggleAllRequested& event, ordo::core::CommandContext& context) override;

private:
    DbWorker& db_;
};

// The filter is not persisted; it resets to All on every launch.
class SetFilterCommand : public ordo::core::Command<events::SetFilterRequested> {
public:
    void execute(const events::SetFilterRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace app
