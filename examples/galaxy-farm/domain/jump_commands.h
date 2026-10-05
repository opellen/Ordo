#pragma once

#include <ordo/core/command.h>

#include "domain/jump_events.h"
#include "infra/job_runner.h"

namespace app {

// Starts a jump on StarMap and submits its jobs to the runner.
class JumpCommand : public ordo::core::Command<events::JumpRequested> {
public:
    explicit JumpCommand(JobRunner& runner) : runner_(runner) {}

    void execute(const events::JumpRequested& event, ordo::core::CommandContext& context) override;

private:
    JobRunner& runner_;
};

// Transit animation is over; forwards to StarMap::arrive().
class ArrivalCommand : public ordo::core::Command<events::JumpArrivalReached> {
public:
    void execute(const events::JumpArrivalReached& event, ordo::core::CommandContext& context) override;
};

}  // namespace app
