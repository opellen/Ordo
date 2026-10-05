#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include <ordo/core/dispatcher.h>
#include <ordo/core/kernel_key.h>

namespace ordo::core {

class Kernel;
class Agent;

// What a Presenter may do: subscribe, send, and look up agents.
// Kernel-owned, so a registered Presenter's pointer to it never dangles.
class PresenterContext {
public:
    // Constructible only by Kernel (KernelKey passkey).
    PresenterContext(KernelKey, Kernel& kernel, Dispatcher& dispatcher);

    // owner is the subscriber's own address (bulk removal cookie).
    template <typename EventT>
    void subscribe(const void* owner, std::function<void(const EventT&)> handler) {
        dispatcher_->subscribe<EventT>(owner, std::move(handler));
    }

    // Removes every subscription registered with this owner.
    void unsubscribe(const void* owner);

    template <typename EventT>
    void send(const EventT& event) { dispatcher_->dispatch(event); }

    // Returns nullptr if no agent is registered under that name.
    std::shared_ptr<Agent> agent(std::string_view name) const;

    template <typename AgentT>
    std::shared_ptr<AgentT> agentAs(std::string_view name) const {
        return std::dynamic_pointer_cast<AgentT>(agent(name));
    }

private:
    Kernel* kernel_;
    Dispatcher* dispatcher_;
};

}  // namespace ordo::core
