#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>

#include <ordo/core/command.h>
#include <ordo/core/dispatcher.h>
#include <ordo/core/agent.h>
#include <ordo/core/agent_context.h>
#include <ordo/core/command_context.h>
#include <ordo/core/presenter_context.h>

namespace ordo::core {

// Facade over one Dispatcher plus named Agents and per-event Command
// factories. Instantiable, never a singleton -- two instances are fully isolated.
class Kernel {
public:
    Kernel();

    Dispatcher& dispatcher();

    // Registers (or replaces, by name) an agent: gives it this kernel's
    // AgentContext and calls onRegister(). A replaced agent gets onRemove() first.
    void registerAgent(std::shared_ptr<Agent> agent);

    // Returns nullptr if no agent is registered under that name.
    std::shared_ptr<Agent> agent(std::string_view name);

    // dynamic_pointer_cast convenience over agent(); nullptr if absent, or if the
    // registered agent is not actually an AgentT.
    template <typename AgentT>
    std::shared_ptr<AgentT> agentAs(std::string_view name) {
        return std::dynamic_pointer_cast<AgentT>(agent(name));
    }

    // Calls onRemove() on the agent and drops it. Returns false (no-op) if no
    // agent is registered under that name.
    bool removeAgent(std::string_view name);

    // Runs a fresh CommandT per dispatched EventT; re-registering replaces the
    // old factory. Extra args are captured once -- pass references as std::ref
    // and keep them alive while registered.
    template <typename EventT, typename CommandT, typename... Args>
    void registerCommand(Args&&... args) {
        static_assert(std::is_base_of<Command<EventT>, CommandT>::value,
                      "CommandT must derive from Command<EventT>");
        static_assert(std::is_constructible_v<CommandT, Args&...>,
                      "CommandT must be constructible from the registered arguments");
        removeCommand<EventT>();
        Kernel* kernel = this;
        dispatcher_.subscribe<EventT>(
            commandOwner<EventT>(),
            [kernel, ... captured = std::forward<Args>(args)](const EventT& event) mutable {
                auto command = std::make_unique<CommandT>(captured...);
                command->execute(event, kernel->commandContext_);
            });
    }

    // Unsubscribes the command factory (if any) registered for EventT. Other
    // event types' command factories on this kernel are unaffected.
    template <typename EventT>
    void removeCommand() {
        dispatcher_.unsubscribe(commandOwner<EventT>());
    }

    // Convenience for dispatcher().dispatch(e).
    template <typename EventT>
    void send(const EventT& event) {
        dispatcher_.dispatch(event);
    }

    // Capability views owned by this kernel, each narrower than the kernel.
    CommandContext& commandContext();
    PresenterContext& presenterContext();

private:
    // Per-EventT owner cookie, so removeCommand<EventT>() removes only its own
    // factory (a plain `this` would remove every subscription).
    template <typename EventT>
    const void* commandOwner() {
        return &commandOwnerTokens_[typeid(EventT).hash_code()];
    }

    Dispatcher dispatcher_;
    // Contexts are constructed after dispatcher_ -- member order matters.
    AgentContext agentContext_;
    CommandContext commandContext_;
    PresenterContext presenterContext_;
    std::unordered_map<std::string, std::shared_ptr<Agent>> agents_;
    std::unordered_map<std::size_t, int> commandOwnerTokens_;
};

}  // namespace ordo::core
