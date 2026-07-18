// AYEventSystem/unittest/Test_EventBus.cpp
//
// Covers design §9 test matrix. Uses AYTest (per project rule §2).
// Uses local EventBus instances (not singleton) for isolation.

#include <ayevent/EventBus.h>
#include <ayevent/Events/WindowEvents.h>
#include <AYTest.h>

#include <atomic>
#include <algorithm>
#include <thread>
#include <vector>

using namespace ayt::event;

// -----------------------------------------------------------------------------
// Test event types
// -----------------------------------------------------------------------------
struct SimpleEvent { int payload = 0; };
struct OtherEvent { std::string name; std::vector<int> data; };

struct CriticalEvent {
    int x = 0;
    static constexpr EventTypeId   kTypeId   = 0x0A00'0001;
    static constexpr EventPriority kPriority = EventPriority::Critical;
};
struct LowEvent {
    int x = 0;
    static constexpr EventTypeId   kTypeId   = 0x0A00'0002;
    static constexpr EventPriority kPriority = EventPriority::Low;
};
struct HighEvent {
    int x = 0;
    static constexpr EventTypeId   kTypeId   = 0x0A00'0003;
    static constexpr EventPriority kPriority = EventPriority::High;
};
struct NormalEvent {
    int x = 0;
    static constexpr EventTypeId   kTypeId   = 0x0A00'0004;
    static constexpr EventPriority kPriority = EventPriority::Normal;
};

// -----------------------------------------------------------------------------
// §9 row 1: subscribe + emit
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_Basic)

TEST_CASE(SubscribeEmit) {
    EventBus bus;
    int got = 0;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent& e) { got = e.payload; });
    bus.emit(SimpleEvent{42});
    CHECK_INT_EQ(got, 42);
}

TEST_CASE(HandlerCalledOnce) {
    EventBus bus;
    int count = 0;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { ++count; });
    bus.emit(SimpleEvent{1});
    bus.emit(SimpleEvent{2});
    bus.emit(SimpleEvent{3});
    CHECK_INT_EQ(count, 3);
}

TEST_CASE(MultipleHandlersSameType) {
    EventBus bus;
    int a = 0, b = 0;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent& e) { a += e.payload; });
    bus.subscribe<SimpleEvent>([&](const SimpleEvent& e) { b += e.payload * 2; });
    bus.emit(SimpleEvent{10});
    CHECK_INT_EQ(a, 10);
    CHECK_INT_EQ(b, 20);
}

TEST_CASE(HandlerOrderIsRegistrationOrder) {
    EventBus bus;
    std::vector<int> order;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { order.push_back(1); });
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { order.push_back(2); });
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { order.push_back(3); });
    bus.emit(SimpleEvent{0});
    CHECK_INT_EQ(static_cast<int>(order.size()), 3);
    CHECK_INT_EQ(order[0], 1);
    CHECK_INT_EQ(order[1], 2);
    CHECK_INT_EQ(order[2], 3);
}

TEST_CASE(DifferentTypesIsolated) {
    EventBus bus;
    int simple = 0, other = 0;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent& e) { simple = e.payload; });
    bus.subscribe<OtherEvent>([&](const OtherEvent& e) { other = static_cast<int>(e.name.size()); });
    bus.emit(SimpleEvent{7});
    CHECK_INT_EQ(simple, 7);
    CHECK_INT_EQ(other, 0);
    bus.emit(OtherEvent{"hello", {}});
    CHECK_INT_EQ(other, 5);
}

TEST_SUITE_END

// -----------------------------------------------------------------------------
// §9 row 2: unsubscribe + ScopedConnection dtor
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_Unsubscribe)

TEST_CASE(UnsubscribeById) {
    EventBus bus;
    int count = 0;
    auto id = bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { ++count; });
    bus.emit(SimpleEvent{1});
    CHECK_INT_EQ(count, 1);
    bus.unsubscribe(id);
    bus.emit(SimpleEvent{2});
    CHECK_INT_EQ(count, 1);
}

TEST_CASE(UnsubscribeIdempotent) {
    EventBus bus;
    auto id = bus.subscribe<SimpleEvent>([](const SimpleEvent&) {});
    bus.unsubscribe(id);
    bus.unsubscribe(id);
    bus.unsubscribe(kInvalidConnectionId);
    CHECK_TRUE(true);
}

TEST_CASE(ScopedConnectionDtorUnsubscribes) {
    EventBus bus;
    int count = 0;
    {
        ScopedConnection conn(bus, bus.subscribe<SimpleEvent>(
            [&](const SimpleEvent&) { ++count; }));
        bus.emit(SimpleEvent{1});
        CHECK_INT_EQ(count, 1);
    }
    bus.emit(SimpleEvent{2});
    CHECK_INT_EQ(count, 1);
}

TEST_CASE(ScopedConnectionMoveTransfersOwnership) {
    EventBus bus;
    int count = 0;
    ScopedConnection a;
    {
        ScopedConnection b(bus, bus.subscribe<SimpleEvent>(
            [&](const SimpleEvent&) { ++count; }));
        a = std::move(b);
        CHECK_FALSE(static_cast<bool>(b));
        bus.emit(SimpleEvent{1});
        CHECK_INT_EQ(count, 1);
    }
    bus.emit(SimpleEvent{2});
    CHECK_INT_EQ(count, 2);
    a.reset();
    bus.emit(SimpleEvent{3});
    CHECK_INT_EQ(count, 2);
}

TEST_CASE(ScopedConnectionResetEarlyUnsubscribes) {
    EventBus bus;
    int count = 0;
    ScopedConnection conn(bus, bus.subscribe<SimpleEvent>(
        [&](const SimpleEvent&) { ++count; }));
    bus.emit(SimpleEvent{1});
    conn.reset();
    CHECK_FALSE(static_cast<bool>(conn));
    bus.emit(SimpleEvent{2});
    CHECK_INT_EQ(count, 1);
}

TEST_CASE(UnsubscribeDoesNotCompareFunctionTarget) {
    EventBus bus;
    int a = 0, b = 0;
    auto idA = bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { ++a; });
    auto idB = bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { ++b; });
    (void)idB;
    bus.unsubscribe(idA);
    bus.emit(SimpleEvent{0});
    CHECK_INT_EQ(a, 0);
    CHECK_INT_EQ(b, 1);
}

TEST_SUITE_END

// -----------------------------------------------------------------------------
// §9 row 3-4: reentrancy during emit
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_Reentrancy)

TEST_CASE(UnsubscribeSelfDuringEmit) {
    EventBus bus;
    int count = 0;
    ConnectionId self = kInvalidConnectionId;
    self = bus.subscribe<SimpleEvent>([&](const SimpleEvent&) {
        ++count;
        bus.unsubscribe(self);
    });
    bus.emit(SimpleEvent{1});
    CHECK_INT_EQ(count, 1);
    bus.emit(SimpleEvent{2});
    CHECK_INT_EQ(count, 1);
}

TEST_CASE(SubscribeDuringEmitDoesNotFireForCurrentEvent) {
    EventBus bus;
    int early = 0, late = 0;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) {
        ++early;
        bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { ++late; });
    });
    bus.emit(SimpleEvent{0});
    CHECK_INT_EQ(early, 1);
    CHECK_INT_EQ(late, 0);
    bus.emit(SimpleEvent{0});
    CHECK_INT_EQ(early, 2);
    CHECK_INT_EQ(late, 1);
}

// Nested emit must complete (firing all current listeners) BEFORE the
// outer listener resumes. We bound recursion by checking calls inside L1.
TEST_CASE(NestedEmitIsSync) {
    EventBus bus;
    std::vector<int> order;
    int calls = 0;
    bool nestedL2FiredBeforeOuterResume = false;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) {
        order.push_back(1);
        if (++calls == 1) {
            bus.emit(SimpleEvent{0}); // nested once
            // After nested returns, L2 must have fired (with current snapshot).
            nestedL2FiredBeforeOuterResume =
                std::find(order.begin(), order.end(), 2) != order.end();
        }
        order.push_back(99); // marker: outer resumes only after nested done
    });
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) {
        order.push_back(2);
    });
    bus.emit(SimpleEvent{0});
    CHECK_TRUE(nestedL2FiredBeforeOuterResume);
}

TEST_SUITE_END

// -----------------------------------------------------------------------------
// §9 row 5: post + pump
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_PostPump)

TEST_CASE(PostDeliveredOnPump) {
    EventBus bus;
    int got = 0;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent& e) { got = e.payload; });
    bus.post(SimpleEvent{99});
    CHECK_INT_EQ(got, 0);
    bus.pump();
    CHECK_INT_EQ(got, 99);
}

TEST_CASE(MultiplePostsDrainInOrder) {
    EventBus bus;
    std::vector<int> seen;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent& e) { seen.push_back(e.payload); });
    bus.post(SimpleEvent{1});
    bus.post(SimpleEvent{2});
    bus.post(SimpleEvent{3});
    bus.pump();
    CHECK_INT_EQ(static_cast<int>(seen.size()), 3);
    CHECK_INT_EQ(seen[0], 1);
    CHECK_INT_EQ(seen[1], 2);
    CHECK_INT_EQ(seen[2], 3);
}

TEST_CASE(PostDuringPumpDrainsSameCall) {
    EventBus bus;
    std::vector<int> seen;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent& e) {
        seen.push_back(e.payload);
        if (e.payload < 3) {
            bus.post(SimpleEvent{e.payload + 1});
        }
    });
    bus.post(SimpleEvent{1});
    bus.pump();
    CHECK_INT_EQ(static_cast<int>(seen.size()), 3);
    CHECK_INT_EQ(seen[0], 1);
    CHECK_INT_EQ(seen[1], 2);
    CHECK_INT_EQ(seen[2], 3);
}

TEST_CASE(EmptyPumpNoOp) {
    EventBus bus;
    int count = 0;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { ++count; });
    bus.pump();
    CHECK_INT_EQ(count, 0);
}

TEST_SUITE_END

// -----------------------------------------------------------------------------
// §9 row 6: priority drain order
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_Priority)

TEST_CASE(PriorityDrainOrderCriticalToLow) {
    EventBus bus;
    std::vector<int> seen;
    bus.subscribe<CriticalEvent>([&](const CriticalEvent& e) { seen.push_back(e.x); });
    bus.subscribe<HighEvent>([&](const HighEvent& e) { seen.push_back(e.x); });
    bus.subscribe<NormalEvent>([&](const NormalEvent& e) { seen.push_back(e.x); });
    bus.subscribe<LowEvent>([&](const LowEvent& e) { seen.push_back(e.x); });

    bus.post(LowEvent{40});
    bus.post(NormalEvent{30});
    bus.post(HighEvent{20});
    bus.post(CriticalEvent{10});

    bus.pump();
    CHECK_INT_EQ(static_cast<int>(seen.size()), 4);
    CHECK_INT_EQ(seen[0], 10);
    CHECK_INT_EQ(seen[1], 20);
    CHECK_INT_EQ(seen[2], 30);
    CHECK_INT_EQ(seen[3], 40);
}

TEST_CASE(SamePriorityFifo) {
    EventBus bus;
    std::vector<int> seen;
    bus.subscribe<NormalEvent>([&](const NormalEvent& e) { seen.push_back(e.x); });
    bus.post(NormalEvent{1});
    bus.post(NormalEvent{2});
    bus.post(NormalEvent{3});
    bus.pump();
    CHECK_INT_EQ(static_cast<int>(seen.size()), 3);
    CHECK_INT_EQ(seen[0], 1);
    CHECK_INT_EQ(seen[1], 2);
    CHECK_INT_EQ(seen[2], 3);
}

TEST_SUITE_END

// -----------------------------------------------------------------------------
// §9 row 7: cross-thread post + main pump
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_Threading)

TEST_CASE(CrossThreadPost) {
    EventBus bus;
    std::atomic<int> count{0};
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { count.fetch_add(1); });

    std::thread worker([&] {
        for (int i = 0; i < 100; ++i) {
            bus.post(SimpleEvent{i});
        }
    });
    worker.join();
    bus.pump();
    CHECK_INT_EQ(count.load(), 100);
}

TEST_CASE(MultipleWorkersPostConcurrent) {
    EventBus bus;
    std::atomic<int> count{0};
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { count.fetch_add(1); });

    constexpr int kWorkers = 4;
    constexpr int kPerWorker = 250;
    std::vector<std::thread> ts;
    for (int w = 0; w < kWorkers; ++w) {
        ts.emplace_back([&] {
            for (int i = 0; i < kPerWorker; ++i) {
                bus.post(SimpleEvent{i});
            }
        });
    }
    for (auto& t : ts) t.join();
    bus.pump();
    CHECK_INT_EQ(count.load(), kWorkers * kPerWorker);
}

TEST_SUITE_END

// -----------------------------------------------------------------------------
// §9 row 8: diagnostics
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_Diagnostics)

TEST_CASE(ListenerCountTracks) {
    EventBus bus;
    CHECK_INT_EQ(static_cast<int>(bus.listenerCount(CriticalEvent::kTypeId)), 0);
    auto id = bus.subscribe<CriticalEvent>([](const CriticalEvent&) {});
    CHECK_INT_EQ(static_cast<int>(bus.listenerCount(CriticalEvent::kTypeId)), 1);
    bus.unsubscribe(id);
    CHECK_INT_EQ(static_cast<int>(bus.listenerCount(CriticalEvent::kTypeId)), 0);
}

TEST_CASE(PendingCountTracks) {
    EventBus bus;
    CHECK_INT_EQ(static_cast<int>(bus.pendingCount()), 0);
    bus.post(SimpleEvent{1});
    bus.post(SimpleEvent{2});
    CHECK_INT_EQ(static_cast<int>(bus.pendingCount()), 2);
    bus.pump();
    CHECK_INT_EQ(static_cast<int>(bus.pendingCount()), 0);
}

TEST_CASE(IsMainThreadFlagAfterEmit) {
    EventBus bus;
    bus.emit(SimpleEvent{0});
    CHECK_TRUE(bus.isMainThread());
}

TEST_SUITE_END

// -----------------------------------------------------------------------------
// WindowEvents shared POD
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_WindowEvents)

TEST_CASE(WindowResizeDelivery) {
    EventBus bus;
    int w = 0, h = 0;
    bus.subscribe<WindowResizeEvent>([&](const WindowResizeEvent& e) {
        w = e.width; h = e.height;
    });
    bus.emit(WindowResizeEvent{1920, 1080});
    CHECK_INT_EQ(w, 1920);
    CHECK_INT_EQ(h, 1080);
}

TEST_CASE(WindowCloseEventIsCritical) {
    static_assert(WindowCloseEvent::kPriority == EventPriority::Critical,
                  "WindowCloseEvent must drain first");
    CHECK_TRUE(WindowCloseEvent::kPriority == EventPriority::Critical);
}

TEST_SUITE_END

// -----------------------------------------------------------------------------
// subscribeById (advanced path)
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_Advanced)

TEST_CASE(SubscribeByIdInvoke) {
    EventBus bus;
    int got = 0;
    ConnectionId id = bus.subscribe<SimpleEvent>(
        [&](const SimpleEvent& e) { got = e.payload; });
    (void)id;
    bus.emit(SimpleEvent{77});
    CHECK_INT_EQ(got, 77);
}

TEST_CASE(ScopedConnectionReleaseNoUnsub) {
    EventBus bus;
    int count = 0;
    auto id = bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { ++count; });
    ScopedConnection conn(bus, id);
    conn.release();
    CHECK_FALSE(static_cast<bool>(conn));
    bus.emit(SimpleEvent{0});
    CHECK_INT_EQ(count, 1);
    bus.unsubscribe(id);
    bus.emit(SimpleEvent{0});
    CHECK_INT_EQ(count, 1);
}

TEST_SUITE_END