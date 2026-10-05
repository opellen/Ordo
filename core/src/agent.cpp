#include <ordo/core/agent.h>

#include <stdexcept>

namespace ordo::core {

Agent::Agent(const std::string& name) : name_(name) {}

Agent::~Agent() = default;

const std::string& Agent::name() const {
    return name_;
}

AgentContext& Agent::context() const {
    if (!context_) {
        throw std::logic_error("Agent '" + name_ + "' is not registered with a kernel");
    }
    return *context_;
}

}  // namespace ordo::core
