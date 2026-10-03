#pragma once

#include <ordo/core/command.h>

#include "infra/db_worker.h"
#include "model/persistence_events.h"

namespace app {

// Persistence lane: the mutating commands in todo_commands.h write through
// to DbWorker, and the commands here handle DbRelay's replies coming back.

// Sent once at bootstrap; asks DbWorker to read the table back. Reply comes
// back as TodosLoaded, below.
class LoadTodosCommand : public ordo::core::Command<events::LoadTodosRequested> {
public:
    explicit LoadTodosCommand(DbWorker& db) : db_(db) {}

    void execute(const events::LoadTodosRequested&, ordo::core::CommandContext&) override;

private:
    DbWorker& db_;
};

// DbRelay posts this on the UI thread once DbWorker::load() finishes.
// Forwards to the agent, which owns what "loaded" means.
class TodosLoadedCommand : public ordo::core::Command<events::TodosLoaded> {
public:
    void execute(const events::TodosLoaded& event, ordo::core::CommandContext& context) override;
};

// DbRelay posts this on the UI thread once a queued write finishes.
// Forwards to the agent, which owns the pending-writes count and last error.
class PersistCompletedCommand : public ordo::core::Command<events::PersistCompleted> {
public:
    void execute(const events::PersistCompleted& event, ordo::core::CommandContext& context) override;
};

}  // namespace app
