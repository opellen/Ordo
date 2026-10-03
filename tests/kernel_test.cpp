#include <ordo/core/kernel.h>

#include <ordo/core/app_kernel.h>
#include <ordo/core/command.h>
#include <ordo/core/agent.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using ordo::core::CommandContext;
using ordo::core::Command;
using ordo::core::Agent;

// Event: bump a counter by some amount.
struct PingEvent {
    static constexpr std::string_view eventName = "Ping";
    int value = 0;
};

class CounterStore : public Agent {
public:
    static constexpr const char* kName = "CounterStore";

    CounterStore() : Agent(kName) {}

    int value = 0;
};

class PingCommand : public Command<PingEvent> {
public:
    void execute(const PingEvent& event, CommandContext& context) override {
        auto counter = context.agentAs<CounterStore>(CounterStore::kName);
        if (counter) {
            counter->value += event.value;
        }
    }
};

// These tests exercise the multi-kernel property: nothing registered on one
// Kernel is ever visible from another.

TEST(KernelTest, TwoInstancesHaveIndependentStores) {
    Kernel kernelA;
    Kernel kernelB;

    kernelA.registerAgent(std::make_shared<CounterStore>());

    EXPECT_NE(kernelA.agent(CounterStore::kName), nullptr);
    EXPECT_EQ(kernelB.agent(CounterStore::kName), nullptr);  // invisible to kernel B
}

TEST(KernelTest, TwoInstancesHaveIndependentCommandsAndDispatchers) {
    Kernel kernelA;
    Kernel kernelB;

    kernelA.registerAgent(std::make_shared<CounterStore>());
    kernelB.registerAgent(std::make_shared<CounterStore>());
    kernelA.registerCommand<PingEvent, PingCommand>();
    // Deliberately do NOT register PingCommand on kernelB.

    kernelA.send(PingEvent{5});
    kernelB.send(PingEvent{5});  // no-op: kernel B never registered a PingCommand

    EXPECT_EQ(kernelA.agentAs<CounterStore>(CounterStore::kName)->value, 5);
    EXPECT_EQ(kernelB.agentAs<CounterStore>(CounterStore::kName)->value, 0);
}

TEST(KernelTest, DispatchersAreDistinctPerKernelInstance) {
    Kernel kernelA;
    Kernel kernelB;

    EXPECT_NE(&kernelA.dispatcher(), &kernelB.dispatcher());
}

// The pre-rename spelling keeps compiling through the forwarding header.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static_assert(std::is_same_v<ordo::core::AppKernel, ordo::core::Kernel>);
#pragma GCC diagnostic pop

}  // namespace
