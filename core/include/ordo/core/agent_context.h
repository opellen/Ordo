#pragma once

#include <memory>
#include <string_view>

#include <ordo/core/dispatcher.h>
#include <ordo/core/kernel_key.h>

namespace ordo::core {

class Kernel;
class Agent;

// What an Agent may do: send events and look up sibling agents, nothing else.
// Kernel-owned, so a registered Agent's pointer to it never dangles.
class AgentContext {
public:
    // Constructible only by Kernel (KernelKey passkey).
    AgentContext(KernelKey, Kernel& kernel, Dispatcher& dispatcher);

    template <typename EventT>
    void send(const EventT& event) { dispatcher_->dispatch(event); }

    // Returns nullptr if no agent is registered under that name.
    std::shared_ptr<Agent> agent(std::string_view name) const;

    // dynamic_pointer_cast convenience over agent(); nullptr if absent, or if
    // the registered agent is not actually an AgentT.
    template <typename AgentT>
    std::shared_ptr<AgentT> agentAs(std::string_view name) const {
        return std::dynamic_pointer_cast<AgentT>(agent(name));
    }

private:
    Kernel* kernel_;
    Dispatcher* dispatcher_;
};

}  // namespace ordo::core
