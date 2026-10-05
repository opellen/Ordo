#include <ordo/core/command.h>

#include <ordo/core/kernel.h>
#include <ordo/core/agent.h>

#include <memory>
#include <functional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using ordo::core::CommandContext;
using ordo::core::Command;
using ordo::core::Agent;

struct RenameDocumentEvent {
    static constexpr std::string_view eventName = "RenameDocument";
    std::string newName;
};

struct SelectToolEvent {
    static constexpr std::string_view eventName = "SelectTool";
    std::string toolId;
};

class ResultsAgent : public Agent {
public:
    static constexpr const char* kName = "ResultsAgent";

    ResultsAgent() : Agent(kName) {}

    std::string lastRenameSeenByV1;
    int renameV1ExecuteCount = 0;
    std::string lastRenameSeenByV2;
    std::string lastToolSelected;
};

class RenameDocumentCommand : public Command<RenameDocumentEvent> {
public:
    void execute(const RenameDocumentEvent& event, CommandContext& context) override {
        auto results = context.agentAs<ResultsAgent>(ResultsAgent::kName);
        results->lastRenameSeenByV1 = event.newName;
        ++results->renameV1ExecuteCount;
    }
};

// Replacement for RenameDocumentCommand on the same event.
class RenameDocumentCommandV2 : public Command<RenameDocumentEvent> {
public:
    void execute(const RenameDocumentEvent& event, CommandContext& context) override {
        auto results = context.agentAs<ResultsAgent>(ResultsAgent::kName);
        results->lastRenameSeenByV2 = event.newName;
    }
};

class SelectToolCommand : public Command<SelectToolEvent> {
public:
    void execute(const SelectToolEvent& event, CommandContext& context) override {
        auto results = context.agentAs<ResultsAgent>(ResultsAgent::kName);
        results->lastToolSelected = event.toolId;
    }
};

class CommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<ResultsAgent>());
        results = kernel.agentAs<ResultsAgent>(ResultsAgent::kName);
    }

    Kernel kernel;
    std::shared_ptr<ResultsAgent> results;
};

TEST_F(CommandTest, DispatchRunsCommand) {
    kernel.registerCommand<RenameDocumentEvent, RenameDocumentCommand>();

    kernel.send(RenameDocumentEvent{"Deck.skp"});

    EXPECT_EQ(results->lastRenameSeenByV1, "Deck.skp");
    EXPECT_EQ(results->renameV1ExecuteCount, 1);
}

TEST_F(CommandTest, ReRegisterReplaces) {
    kernel.registerCommand<RenameDocumentEvent, RenameDocumentCommand>();
    kernel.registerCommand<RenameDocumentEvent, RenameDocumentCommandV2>();

    kernel.send(RenameDocumentEvent{"Roof.skp"});

    EXPECT_TRUE(results->lastRenameSeenByV1.empty());   
    EXPECT_EQ(results->lastRenameSeenByV2, "Roof.skp");  
}

TEST_F(CommandTest, RemoveCommandStopsExecution) {
    kernel.registerCommand<RenameDocumentEvent, RenameDocumentCommand>();
    kernel.removeCommand<RenameDocumentEvent>();

    kernel.send(RenameDocumentEvent{"Wall.skp"});

    EXPECT_EQ(results->renameV1ExecuteCount, 0);
}

TEST_F(CommandTest, CommandsAreIndependent) {
    kernel.registerCommand<RenameDocumentEvent, RenameDocumentCommand>();
    kernel.registerCommand<SelectToolEvent, SelectToolCommand>();

    kernel.send(SelectToolEvent{"eraser"});

    EXPECT_EQ(results->lastToolSelected, "eraser");
    EXPECT_TRUE(results->lastRenameSeenByV1.empty());

    kernel.removeCommand<SelectToolEvent>();
    kernel.send(RenameDocumentEvent{"Beam.skp"});

    EXPECT_EQ(results->lastRenameSeenByV1, "Beam.skp");
}


// Injected through registerCommand argument forwarding.
struct RenameRecorder {
    int calls = 0;
    std::string lastName;
};

class RecordingRenameCommand : public Command<RenameDocumentEvent> {
public:
    explicit RecordingRenameCommand(RenameRecorder& recorder) : recorder_(recorder) {}

    void execute(const RenameDocumentEvent& event, CommandContext&) override {
        ++recorder_.calls;
        recorder_.lastName = event.newName;
    }

private:
    RenameRecorder& recorder_;
};

TEST_F(CommandTest, ArgumentsReachEachConstruction) {
    RenameRecorder recorder;
    kernel.registerCommand<RenameDocumentEvent, RecordingRenameCommand>(std::ref(recorder));

    kernel.send(RenameDocumentEvent{"first"});
    kernel.send(RenameDocumentEvent{"second"});

    EXPECT_EQ(recorder.calls, 2);
    EXPECT_EQ(recorder.lastName, "second");

    // Lifetime rule: drop the registration before the recorder dies.
    kernel.removeCommand<RenameDocumentEvent>();
}

}  // namespace
