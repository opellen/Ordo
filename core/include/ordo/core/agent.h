#pragma once

#include <string>

#include <ordo/core/agent_context.h>

namespace ordo::core {

class Kernel;

// Base class for named domain data holders (the Proxy role). Concrete agents
// subclass this and are registered with a Kernel under a unique name.
class Agent {
public:
    explicit Agent(const std::string& name);
    virtual ~Agent();

    const std::string& name() const;

    // Called by Kernel::registerAgent() once this agent is added to a
    // kernel; context() is already available when this runs.
    virtual void onRegister() {}

    // Called by Kernel::removeAgent() just before this agent is dropped.
    virtual void onRemove() {}

    // Kernel-only (KernelKey passkey): wires/clears this agent's context around
    // registration. Pass nullptr to clear.
    void setContext(KernelKey, AgentContext* context) noexcept { context_ = context; }

protected:
    // Available from just before onRegister() until just after onRemove().
    // Throws std::logic_error if the agent is not currently registered.
    AgentContext& context() const;

    // Convenience for context().send(event).
    template <typename EventT>
    void send(const EventT& event) { context().send(event); }

private:
    std::string name_;
    AgentContext* context_ = nullptr;
};

}  // namespace ordo::core
