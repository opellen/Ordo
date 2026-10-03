#pragma once

#include <memory>
#include <string_view>

#include <ordo/core/dispatcher.h>
#include <ordo/core/kernel_key.h>

namespace ordo::core {

class Kernel;
class Agent;

// Narrow capability view handed to a Command per execute() call (least
// privilege): a Command may publish events and look up agents -- nothing
// else. Owned by the kernel; lifetime == kernel lifetime.
class CommandContext {
public:
    // Constructible only by Kernel (KernelKey passkey).
    CommandContext(KernelKey, Kernel& kernel, Dispatcher& dispatcher);

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
