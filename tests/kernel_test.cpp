#include <ordo/core/kernel.h>

#include <ordo/core/command.h>
#include <ordo/core/agent.h>

#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using ordo::core::CommandContext;
using ordo::core::Command;
using ordo::core::Agent;

struct PingEvent {
    static constexpr std::string_view eventName = "Ping";
    int value = 0;
};

class CounterAgent : public Agent {
public:
    static constexpr const char* kName = "CounterAgent";

    CounterAgent() : Agent(kName) {}

    int value = 0;
};

class PingCommand : public Command<PingEvent> {
public:
    void execute(const PingEvent& event, CommandContext& context) override {
        auto counter = context.agentAs<CounterAgent>(CounterAgent::kName);
        if (counter) {
            counter->value += event.value;
        }
    }
};

// Kernels share no state.

TEST(KernelTest, InstancesHaveIndependentAgents) {
    Kernel kernelA;
    Kernel kernelB;

    kernelA.registerAgent(std::make_shared<CounterAgent>());

    EXPECT_NE(kernelA.agent(CounterAgent::kName), nullptr);
    EXPECT_EQ(kernelB.agent(CounterAgent::kName), nullptr);
}

TEST(KernelTest, InstancesHaveIndependentCommands) {
    Kernel kernelA;
    Kernel kernelB;

    kernelA.registerAgent(std::make_shared<CounterAgent>());
    kernelB.registerAgent(std::make_shared<CounterAgent>());
    kernelA.registerCommand<PingEvent, PingCommand>();
    // PingCommand is deliberately not registered on kernelB.

    kernelA.send(PingEvent{5});
    kernelB.send(PingEvent{5});

    EXPECT_EQ(kernelA.agentAs<CounterAgent>(CounterAgent::kName)->value, 5);
    EXPECT_EQ(kernelB.agentAs<CounterAgent>(CounterAgent::kName)->value, 0);
}

TEST(KernelTest, DispatchersAreDistinct) {
    Kernel kernelA;
    Kernel kernelB;

    EXPECT_NE(&kernelA.dispatcher(), &kernelB.dispatcher());
}

}  // namespace
