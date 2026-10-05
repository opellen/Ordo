#include <ordo/core/kernel.h>

namespace ordo::core {

Kernel::Kernel()
    : agentContext_(KernelKey{}, *this, dispatcher_),
      commandContext_(KernelKey{}, *this, dispatcher_),
      presenterContext_(KernelKey{}, *this, dispatcher_) {}

CommandContext& Kernel::commandContext() {
    return commandContext_;
}

PresenterContext& Kernel::presenterContext() {
    return presenterContext_;
}

Dispatcher& Kernel::dispatcher() {
    return dispatcher_;
}

void Kernel::registerAgent(std::shared_ptr<Agent> agent) {
    Agent& ref = *agent;
    const std::string key = ref.name();
    auto existing = agents_.find(key);
    if (existing != agents_.end()) {
        existing->second->onRemove();
        existing->second->setContext(KernelKey{}, nullptr);
    }
    agents_[key] = agent;
    ref.setContext(KernelKey{}, &agentContext_);
    ref.onRegister();
}

std::shared_ptr<Agent> Kernel::agent(std::string_view name) {
    auto it = agents_.find(std::string(name));
    return it == agents_.end() ? nullptr : it->second;
}

bool Kernel::removeAgent(std::string_view name) {
    auto it = agents_.find(std::string(name));
    if (it == agents_.end()) {
        return false;
    }
    it->second->onRemove();
    it->second->setContext(KernelKey{}, nullptr);
    agents_.erase(it);
    return true;
}

}  // namespace ordo::core
