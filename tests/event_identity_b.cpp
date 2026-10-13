#include "event_identity_lanes.h"

namespace {

struct Ping {
    int lane = 2;
    std::string text = "hello";
};

class PingCommand : public ordo::core::Command<Ping> {
public:
    void execute(const Ping& event, ordo::core::CommandContext&) override {
        // Count only a Ping of this lane; a foreign one is reported by the test.
        if (event.lane == 2) {
            ++runs;
        }
    }
    static int runs;
};
int PingCommand::runs = 0;

}  // namespace

namespace lane_b {

void subscribe(ordo::core::Dispatcher& dispatcher, const void* owner, std::vector<std::string>& log) {
    dispatcher.subscribe<Ping>(owner, [&log](const Ping& event) {
        if (event.lane != 2) {
            log.push_back("B-handler got foreign lane " + std::to_string(event.lane));
            return;
        }
        log.push_back("B:" + event.text);
    });
}

void dispatch(ordo::core::Dispatcher& dispatcher) { dispatcher.dispatch(Ping{}); }
void registerCommand(ordo::core::Kernel& kernel) { kernel.registerCommand<Ping, PingCommand>(); }
void removeCommand(ordo::core::Kernel& kernel) { kernel.removeCommand<Ping>(); }
void send(ordo::core::Kernel& kernel) { kernel.send(Ping{}); }
int& commandRuns() { return PingCommand::runs; }
const std::type_info& pingType() { return typeid(Ping); }

}  // namespace lane_b
