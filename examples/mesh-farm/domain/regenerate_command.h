#pragma once

#include <ordo/core/command.h>

#include "domain/regenerate_requested.h"
#include "infra/job_runner.h"

namespace app {

// Kicks off a new batch: records it on the shelf and submits work to the
// thread pool via JobRunner.
class RegenerateCommand : public ordo::core::Command<events::RegenerateRequested> {
public:
    explicit RegenerateCommand(JobRunner& runner) : runner_(runner) {}

    void execute(const events::RegenerateRequested& event, ordo::core::CommandContext& context) override;

private:
    JobRunner& runner_;
};

}  // namespace app
