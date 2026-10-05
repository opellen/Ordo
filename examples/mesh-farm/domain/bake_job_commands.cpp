#include "domain/bake_job_commands.h"

#include "domain/part_shelf.h"

namespace app {

void BakeJobStartedCommand::execute(const events::BakeJobStarted& event, ordo::core::CommandContext& context) {
    auto shelf = context.agentAs<PartShelf>(PartShelf::kName);
    if (!shelf) {
        return;
    }
    shelf->markStarted(event.partId, event.generation, event.slot, event.threadId);
}

void BakeJobProgressCommand::execute(const events::BakeJobProgress& event, ordo::core::CommandContext& context) {
    auto shelf = context.agentAs<PartShelf>(PartShelf::kName);
    if (!shelf) {
        return;
    }
    shelf->markProgress(event.partId, event.generation, event.fraction, event.snapshot);
}

void BakeJobFinishedCommand::execute(const events::BakeJobFinished& event, ordo::core::CommandContext& context) {
    auto shelf = context.agentAs<PartShelf>(PartShelf::kName);
    if (!shelf) {
        return;
    }
    shelf->storeBaked(event.partId, event.generation, event.slot, event.threadId, event.ok, event.buffers);
}

}  // namespace app
