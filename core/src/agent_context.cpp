#include <ordo/core/agent_context.h>

#include <ordo/core/kernel.h>

namespace ordo::core {

AgentContext::AgentContext(KernelKey, Kernel& kernel, Dispatcher& dispatcher)
    : kernel_(&kernel), dispatcher_(&dispatcher) {}

std::shared_ptr<Agent> AgentContext::agent(std::string_view name) const {
    return kernel_->agent(name);
}

}  // namespace ordo::core
