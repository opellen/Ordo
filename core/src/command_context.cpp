#include "ordo/core/command_context.h"

#include "ordo/core/kernel.h"

namespace ordo::core {

CommandContext::CommandContext(KernelKey, Kernel& kernel, Dispatcher& dispatcher)
    : kernel_(&kernel), dispatcher_(&dispatcher) {}

std::shared_ptr<Agent> CommandContext::agent(std::string_view name) const {
    return kernel_->agent(name);
}

}  // namespace ordo::core
