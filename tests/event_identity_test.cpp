#include <gtest/gtest.h>

#include "event_identity_lanes.h"

namespace {

using ordo::core::Dispatcher;
using ordo::core::Kernel;

TEST(EventIdentity, DispatcherDeliversOnlyToMatchingType) {
    Dispatcher dispatcher;
    std::vector<std::string> log;
    int ownerA = 0;
    int ownerB = 0;
    lane_a::subscribe(dispatcher, &ownerA, log);
    lane_b::subscribe(dispatcher, &ownerB, log);

    lane_a::dispatch(dispatcher);
    EXPECT_EQ(log, std::vector<std::string>({"A:42"}));

    lane_b::dispatch(dispatcher);
    EXPECT_EQ(log, (std::vector<std::string>{"A:42", "B:hello"}));
}

TEST(EventIdentity, SecondCommandDoesNotEvictFirst) {
    Kernel kernel;
    lane_a::commandRuns() = 0;
    lane_b::commandRuns() = 0;
    lane_a::registerCommand(kernel);
    lane_b::registerCommand(kernel);

    lane_a::send(kernel);
    EXPECT_EQ(lane_a::commandRuns(), 1);
    EXPECT_EQ(lane_b::commandRuns(), 0);

    lane_b::send(kernel);
    EXPECT_EQ(lane_a::commandRuns(), 1);
    EXPECT_EQ(lane_b::commandRuns(), 1);
}

TEST(EventIdentity, RemoveCommandLeavesOtherTypeFiring) {
    Kernel kernel;
    lane_a::commandRuns() = 0;
    lane_b::commandRuns() = 0;
    lane_a::registerCommand(kernel);
    lane_b::registerCommand(kernel);

    lane_b::removeCommand(kernel);

    lane_a::send(kernel);
    lane_b::send(kernel);
    EXPECT_EQ(lane_a::commandRuns(), 1);
    EXPECT_EQ(lane_b::commandRuns(), 0);
}

#if defined(__GLIBCXX__)
// Pins the precondition: this standard library gives two distinct
// anonymous-namespace types the same hash_code, so the tests above exercise
// the collision.
TEST(EventIdentity, KitCollidesOnHashCode) {
    EXPECT_EQ(lane_a::pingType().hash_code(), lane_b::pingType().hash_code());
    EXPECT_TRUE(lane_a::pingType() != lane_b::pingType());
}
#endif

}  // namespace
