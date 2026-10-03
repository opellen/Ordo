#include "ordo/core/presenter_context.h"

#include "ordo/core/kernel.h"

namespace ordo::core {

PresenterContext::PresenterContext(KernelKey, Kernel& kernel, Dispatcher& dispatcher)
    : kernel_(&kernel), dispatcher_(&dispatcher) {}

void PresenterContext::unsubscribe(const void* owner) {
    dispatcher_->unsubscribe(owner);
}

std::shared_ptr<Agent> PresenterContext::agent(std::string_view name) const {
    return kernel_->agent(name);
}

}  // namespace ordo::core
