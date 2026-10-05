#pragma once

#include <ordo/core/command.h>

#include "domain/bake_job_events.h"

namespace app {

// Re-entry for BakeRelay's three sinks: thin forwards from a worker
// thread's result into a PartShelf mutation.

// A worker thread picked up a job; shows the part as Running.
class BakeJobStartedCommand : public ordo::core::Command<events::BakeJobStarted> {
public:
    void execute(const events::BakeJobStarted& event, ordo::core::CommandContext& context) override;
};

// A worker thread hit an AO checkpoint mid-sweep.
class BakeJobProgressCommand : public ordo::core::Command<events::BakeJobProgress> {
public:
    void execute(const events::BakeJobProgress& event, ordo::core::CommandContext& context) override;
};

// A worker thread finished, success or failure.
class BakeJobFinishedCommand : public ordo::core::Command<events::BakeJobFinished> {
public:
    void execute(const events::BakeJobFinished& event, ordo::core::CommandContext& context) override;
};

}  // namespace app
