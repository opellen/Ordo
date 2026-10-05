#include "domain/jump_commands.h"

#include <algorithm>

#include "domain/star_map.h"

namespace app {

void JumpCommand::execute(const events::JumpRequested& event, ordo::core::CommandContext& context) {
    // Clamp the dials; 1000000 is galaxylib's hard cap.
    const int galaxiesPerSector = std::clamp(event.galaxiesPerSector, 1, 128);
    const int starsPerGalaxy = std::clamp(event.starsPerGalaxy, 1, 1000000);

    auto starMap = context.agentAs<StarMap>(StarMap::kName);
    if (!starMap) {
        return;
    }

    std::uint64_t epoch = 0;
    auto jobs = starMap->beginJump(event.universeSeed, galaxiesPerSector, starsPerGalaxy, epoch);
    runner_.enqueue(epoch, jobs);
}

void ArrivalCommand::execute(const events::JumpArrivalReached&, ordo::core::CommandContext& context) {
    auto starMap = context.agentAs<StarMap>(StarMap::kName);
    if (!starMap) {
        return;
    }
    starMap->arrive();
}

}  // namespace app
