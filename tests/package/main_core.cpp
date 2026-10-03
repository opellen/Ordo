// Consumer smoke test for ordo::core, reached only through find_package(ordo
// CONFIG). Wires an Agent + Command through a real Kernel dispatch and
// checks the mutation happened. Prints PASS/FAIL and exits 0/1.
#include <ordo/core/agent.h>
#include <ordo/core/kernel.h>
#include <ordo/core/command.h>
#include <ordo/core/command_context.h>
#include <ordo/core/version.h>

#include <cstdio>
#include <memory>
#include <string_view>

namespace {

using ordo::core::Agent;
using ordo::core::Kernel;
using ordo::core::Command;
using ordo::core::CommandContext;

struct PingEvent {
    static constexpr std::string_view eventName = "Ping";
    int value = 0;
};

// Toy agent holding one int, mutated only through its Command.
class CounterAgent : public Agent {
public:
    static constexpr const char* kName = "Counter";

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

}  // namespace

int main() {
    std::printf("ordo::core::versionString() = %s\n", ordo::core::versionString());

    Kernel kernel;
    auto counter = std::make_shared<CounterAgent>();
    kernel.registerAgent(counter);
    kernel.registerCommand<PingEvent, PingCommand>();

    kernel.send(PingEvent{5});

    if (counter->value != 5) {
        std::fprintf(stderr, "FAIL: expected CounterAgent::value == 5, got %d\n", counter->value);
        return 1;
    }

    std::printf("PASS: smoke_core (ordo::core via find_package)\n");
    return 0;
}
