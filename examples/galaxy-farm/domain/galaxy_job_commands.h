#pragma once

#include <ordo/core/command.h>

#include "domain/galaxy_job_events.h"

namespace app {

// Forward worker job notifications (already on the UI thread) to StarMap.

class GalaxyJobStartedCommand : public ordo::core::Command<events::GalaxyJobStarted> {
public:
    void execute(const events::GalaxyJobStarted& event, ordo::core::CommandContext& context) override;
};

class GalaxyJobProgressCommand : public ordo::core::Command<events::GalaxyJobProgress> {
public:
    void execute(const events::GalaxyJobProgress& event, ordo::core::CommandContext& context) override;
};

class GalaxyJobFinishedCommand : public ordo::core::Command<events::GalaxyJobFinished> {
public:
    void execute(const events::GalaxyJobFinished& event, ordo::core::CommandContext& context) override;
};

}  // namespace app
