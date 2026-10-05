#include <ordo/core/agent.h>

#include <ordo/core/kernel.h>
#include <ordo/core/agent_context.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using ordo::core::Agent;
using ordo::core::AgentContext;

// Toy agent tracking a document title.
class TitleAgent : public Agent {
public:
    static constexpr const char* kName = "TitleAgent";

    TitleAgent() : Agent(kName) {}

    void onRemove() override {
        removed = true;
    }

    bool removed = false;
    std::string title = "Untitled";
};

// Exposes context() to check it throws outside registration.
class ContextProbeAgent : public Agent {
public:
    explicit ContextProbeAgent(const std::string& name) : Agent(name) {}

    void onRemove() override {
        removed = true;
    }

    AgentContext& probeContext() const { return context(); }

    bool removed = false;
};

struct PingEvent {
    static constexpr std::string_view eventName = "Ping";
    int value = 0;
};

class AnnouncingAgent : public Agent {
public:
    static constexpr const char* kName = "AnnouncingAgent";

    AnnouncingAgent() : Agent(kName) {}

    // send() must already work here, before registration returns.
    void onRegister() override { send(PingEvent{1}); }

    void pingAgain(int value) { send(PingEvent{value}); }
};

class CounterAgent : public Agent {
public:
    static constexpr const char* kName = "CounterAgent";

    CounterAgent() : Agent(kName) {}

    int value = 7;
};

class LookupAgent : public Agent {
public:
    static constexpr const char* kName = "LookupAgent";

    LookupAgent() : Agent(kName) {}

    std::shared_ptr<CounterAgent> findCounter() const {
        return context().agentAs<CounterAgent>(CounterAgent::kName);
    }
};

TEST(AgentTest, RegisterRetrieveByNameThenRemove) {
    Kernel kernel;
    auto agent = std::make_shared<TitleAgent>();

    kernel.registerAgent(agent);

    auto found = kernel.agent(TitleAgent::kName);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name(), TitleAgent::kName);

    EXPECT_TRUE(kernel.removeAgent(TitleAgent::kName));
    EXPECT_EQ(kernel.agent(TitleAgent::kName), nullptr);
}

TEST(AgentTest, OnRegisterCanSend) {
    Kernel kernel;
    std::vector<int> received;
    kernel.dispatcher().subscribe<PingEvent>(&received,
                                              [&received](const PingEvent& e) { received.push_back(e.value); });

    kernel.registerAgent(std::make_shared<AnnouncingAgent>());

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], 1);
}

TEST(AgentTest, CanSendAfterRegister) {
    Kernel kernel;
    std::vector<int> received;
    kernel.dispatcher().subscribe<PingEvent>(&received,
                                              [&received](const PingEvent& e) { received.push_back(e.value); });

    auto agent = std::make_shared<AnnouncingAgent>();
    kernel.registerAgent(agent);

    agent->pingAgain(2);

    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[1], 2);
}

TEST(AgentTest, LookUpSiblingAgent) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<CounterAgent>());
    auto lookup = std::make_shared<LookupAgent>();
    kernel.registerAgent(lookup);

    auto counter = lookup->findCounter();
    ASSERT_NE(counter, nullptr);
    EXPECT_EQ(counter->value, 7);
}

TEST(AgentTest, ContextThrowsOutsideRegistration) {
    auto agent = std::make_shared<ContextProbeAgent>("Probe");
    EXPECT_THROW(agent->probeContext(), std::logic_error);

    Kernel kernel;
    kernel.registerAgent(agent);
    EXPECT_NO_THROW(agent->probeContext());

    kernel.removeAgent("Probe");
    EXPECT_THROW(agent->probeContext(), std::logic_error);
}

TEST(AgentTest, OnRemoveFiresOnRemoval) {
    Kernel kernel;
    auto agent = std::make_shared<TitleAgent>();
    kernel.registerAgent(agent);

    EXPECT_FALSE(agent->removed);
    kernel.removeAgent(TitleAgent::kName);
    EXPECT_TRUE(agent->removed);
}

TEST(AgentTest, AgentAsDowncasts) {
    Kernel kernel;
    auto agent = std::make_shared<TitleAgent>();
    kernel.registerAgent(agent);

    auto typed = kernel.agentAs<TitleAgent>(TitleAgent::kName);
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->title, "Untitled");
}

TEST(AgentTest, RemoveMissingReturnsFalse) {
    Kernel kernel;
    EXPECT_FALSE(kernel.removeAgent("DoesNotExist"));
}

TEST(AgentTest, ReplaceRemovesOld) {
    Kernel kernel;
    auto oldAgent = std::make_shared<ContextProbeAgent>("Shared");
    auto newAgent = std::make_shared<ContextProbeAgent>("Shared");

    kernel.registerAgent(oldAgent);
    kernel.registerAgent(newAgent);

    EXPECT_TRUE(oldAgent->removed);
    EXPECT_THROW(oldAgent->probeContext(), std::logic_error);
    EXPECT_NO_THROW(newAgent->probeContext());
    EXPECT_EQ(kernel.agent("Shared"), newAgent);
}

}  // namespace
