#include <ordo/core/dispatcher.h>

#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ordo::core::Dispatcher;

struct TitleChangedEvent {
    static constexpr std::string_view eventName = "TitleChanged";
    std::string newTitle;
};

struct ZoomChangedEvent {
    static constexpr std::string_view eventName = "ZoomChanged";
    double zoomLevel = 1.0;
};

TEST(DispatcherTest, SubscriberGetsPayload) {
    Dispatcher dispatcher;
    int owner = 0;
    std::string received;

    dispatcher.subscribe<TitleChangedEvent>(&owner, [&](const TitleChangedEvent& event) {
        received = event.newTitle;
    });

    dispatcher.dispatch(TitleChangedEvent{"New Title"});

    EXPECT_EQ(received, "New Title");
}

TEST(DispatcherTest, SubscribersFireInOrder) {
    Dispatcher dispatcher;
    int ownerA = 0;
    int ownerB = 0;
    int ownerC = 0;
    std::vector<int> callOrder;

    dispatcher.subscribe<TitleChangedEvent>(&ownerA, [&](const TitleChangedEvent&) { callOrder.push_back(1); });
    dispatcher.subscribe<TitleChangedEvent>(&ownerB, [&](const TitleChangedEvent&) { callOrder.push_back(2); });
    dispatcher.subscribe<TitleChangedEvent>(&ownerC, [&](const TitleChangedEvent&) { callOrder.push_back(3); });

    dispatcher.dispatch(TitleChangedEvent{"x"});

    EXPECT_EQ(callOrder, (std::vector<int>{1, 2, 3}));
}

TEST(DispatcherTest, EventTypesDoNotCrossFire) {
    Dispatcher dispatcher;
    int owner = 0;
    bool titleFired = false;
    bool zoomFired = false;

    dispatcher.subscribe<TitleChangedEvent>(&owner, [&](const TitleChangedEvent&) { titleFired = true; });
    dispatcher.subscribe<ZoomChangedEvent>(&owner, [&](const ZoomChangedEvent&) { zoomFired = true; });

    dispatcher.dispatch(TitleChangedEvent{"x"});

    EXPECT_TRUE(titleFired);
    EXPECT_FALSE(zoomFired);
}

TEST(DispatcherTest, UnsubscribeRemovesOwnerHandlers) {
    Dispatcher dispatcher;
    int owner = 0;
    int otherOwner = 0;
    int titleCount = 0;
    int zoomCount = 0;

    dispatcher.subscribe<TitleChangedEvent>(&owner, [&](const TitleChangedEvent&) { ++titleCount; });
    dispatcher.subscribe<ZoomChangedEvent>(&owner, [&](const ZoomChangedEvent&) { ++zoomCount; });
    dispatcher.subscribe<TitleChangedEvent>(&otherOwner, [&](const TitleChangedEvent&) { ++titleCount; });

    dispatcher.unsubscribe(&owner);

    dispatcher.dispatch(TitleChangedEvent{"x"});
    dispatcher.dispatch(ZoomChangedEvent{2.0});

    EXPECT_EQ(titleCount, 1);
    EXPECT_EQ(zoomCount, 0);
}

TEST(DispatcherTest, SubscribeDuringDispatchWaits) {
    Dispatcher dispatcher;
    int ownerA = 0;
    int ownerB = 0;
    int firstCount = 0;
    int secondCount = 0;

    dispatcher.subscribe<TitleChangedEvent>(&ownerA, [&](const TitleChangedEvent&) {
        ++firstCount;
        dispatcher.subscribe<TitleChangedEvent>(&ownerB, [&](const TitleChangedEvent&) { ++secondCount; });
    });

    dispatcher.dispatch(TitleChangedEvent{"first"});
    EXPECT_EQ(firstCount, 1);
    EXPECT_EQ(secondCount, 0);

    dispatcher.dispatch(TitleChangedEvent{"second"});
    EXPECT_EQ(firstCount, 2);
    EXPECT_EQ(secondCount, 1);
}

TEST(DispatcherTest, UnsubscribeDuringDispatchIsSafe) {
    Dispatcher dispatcher;
    int ownerA = 0;
    int ownerB = 0;
    int ownerC = 0;
    std::vector<int> callOrder;

    // ownerB unsubscribes itself mid-dispatch; ownerC must still be reached.
    dispatcher.subscribe<TitleChangedEvent>(&ownerA, [&](const TitleChangedEvent&) { callOrder.push_back(1); });
    dispatcher.subscribe<TitleChangedEvent>(&ownerB, [&](const TitleChangedEvent&) {
        callOrder.push_back(2);
        dispatcher.unsubscribe(&ownerB);
    });
    dispatcher.subscribe<TitleChangedEvent>(&ownerC, [&](const TitleChangedEvent&) { callOrder.push_back(3); });

    dispatcher.dispatch(TitleChangedEvent{"x"});
    EXPECT_EQ(callOrder, (std::vector<int>{1, 2, 3}));

    // ownerB no longer fires.
    callOrder.clear();
    dispatcher.dispatch(TitleChangedEvent{"y"});
    EXPECT_EQ(callOrder, (std::vector<int>{1, 3}));
}

// Event type with no eventName member.
struct UnnamedEvent {
    int value = 0;
};

TEST(DispatcherTest, ObserverGetsEventName) {
    Dispatcher dispatcher;
    int owner = 0;
    std::vector<ordo::core::DispatchRecord> records;

    dispatcher.setObserver([&](const ordo::core::DispatchRecord& record) { records.push_back(record); });
    dispatcher.subscribe<TitleChangedEvent>(&owner, [](const TitleChangedEvent&) {});

    dispatcher.dispatch(TitleChangedEvent{"first"});
    dispatcher.dispatch(TitleChangedEvent{"second"});

    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].eventName, "TitleChanged");
    EXPECT_EQ(records[1].eventName, "TitleChanged");
    EXPECT_EQ(records[0].typeHash, records[1].typeHash);
}

TEST(DispatcherTest, ObserverGetsSubscriberCount) {
    Dispatcher dispatcher;
    int ownerA = 0;
    int ownerB = 0;
    std::vector<ordo::core::DispatchRecord> records;

    dispatcher.setObserver([&](const ordo::core::DispatchRecord& record) { records.push_back(record); });
    dispatcher.subscribe<TitleChangedEvent>(&ownerA, [](const TitleChangedEvent&) {});
    dispatcher.subscribe<TitleChangedEvent>(&ownerB, [](const TitleChangedEvent&) {});

    dispatcher.dispatch(TitleChangedEvent{"x"});

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].subscriberCount, 2u);
}

TEST(DispatcherTest, ObserverGetsEmptyNameWithoutEventName) {
    Dispatcher dispatcher;
    std::vector<ordo::core::DispatchRecord> records;

    dispatcher.setObserver([&](const ordo::core::DispatchRecord& record) { records.push_back(record); });

    dispatcher.dispatch(UnnamedEvent{42});

    ASSERT_EQ(records.size(), 1u);
    EXPECT_TRUE(records[0].eventName.empty());
}

TEST(DispatcherTest, ObserverCalledWithoutSubscribers) {
    Dispatcher dispatcher;
    std::vector<ordo::core::DispatchRecord> records;

    dispatcher.setObserver([&](const ordo::core::DispatchRecord& record) { records.push_back(record); });

    dispatcher.dispatch(TitleChangedEvent{"nobody home"});

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].subscriberCount, 0u);
    EXPECT_EQ(records[0].eventName, "TitleChanged");
}

TEST(DispatcherTest, ClearedObserverStops) {
    Dispatcher dispatcher;
    int callCount = 0;

    dispatcher.setObserver([&](const ordo::core::DispatchRecord&) { ++callCount; });
    dispatcher.dispatch(TitleChangedEvent{"one"});
    EXPECT_EQ(callCount, 1);

    dispatcher.setObserver({});
    dispatcher.dispatch(TitleChangedEvent{"two"});
    EXPECT_EQ(callCount, 1);
}

}  // namespace
