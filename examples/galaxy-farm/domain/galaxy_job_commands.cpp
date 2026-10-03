#include "domain/galaxy_job_commands.h"

#include "domain/star_map.h"

namespace app {

void GalaxyJobStartedCommand::execute(const events::GalaxyJobStarted& event, ordo::core::CommandContext& context) {
    auto starMap = context.agentAs<StarMap>(StarMap::kName);
    if (!starMap) {
        return;
    }
    starMap->markStarted(event.galaxyId, event.epoch, event.slot, event.threadId);
}

void GalaxyJobProgressCommand::execute(const events::GalaxyJobProgress& event, ordo::core::CommandContext& context) {
    auto starMap = context.agentAs<StarMap>(StarMap::kName);
    if (!starMap) {
        return;
    }
    starMap->markProgress(event.galaxyId, event.epoch, event.fraction, event.snapshot);
}

void GalaxyJobFinishedCommand::execute(const events::GalaxyJobFinished& event, ordo::core::CommandContext& context) {
    auto starMap = context.agentAs<StarMap>(StarMap::kName);
    if (!starMap) {
        return;
    }
    starMap->storeGenerated(event.galaxyId, event.epoch, event.slot, event.threadId, event.ok, event.cloud);
}

}  // namespace app
