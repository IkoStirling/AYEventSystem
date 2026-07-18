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

struct CoalesceEvent {
    int payload = 0;
    static constexpr EventTypeId   kTypeId   = 0x0A00'0010;
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

// -----------------------------------------------------------------------------
// Phase 2 §4.2 / §8 — coalesce + counters
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_Coalesce)

// Singleton key=0: successive post_coalesce(0, ...) collapses into one
// delivery with the latest payload (matches §4.2 "WindowResizeEvent" example).
TEST_CASE(CoalesceSingletonKey) {
    EventBus bus;
    std::vector<int> seen;
    bus.subscribe<CoalesceEvent>([&](const CoalesceEvent& e) {
        seen.push_back(e.payload);
    });
    bus.post_coalesce<CoalesceEvent>(0, CoalesceEvent{1});
    bus.post_coalesce<CoalesceEvent>(0, CoalesceEvent{2});
    bus.post_coalesce<CoalesceEvent>(0, CoalesceEvent{3});
    CHECK_INT_EQ(static_cast<int>(bus.pendingCount()), 1);
    bus.pump();
    CHECK_INT_EQ(static_cast<int>(seen.size()), 1);
    CHECK_INT_EQ(seen[0], 3); // latest wins
    CHECK_INT_EQ(static_cast<int>(bus.totalCoalesced()), 2); // 2 prior replaced
}

TEST_CASE(CoalesceDistinctKeys) {
    EventBus bus;
    std::vector<std::pair<uint64_t, int>> seen;
    bus.subscribe<CoalesceEvent>([&](const CoalesceEvent& e) {
        // Payload encodes the original key in the high 16 bits for this test.
        seen.push_back({static_cast<uint64_t>(e.payload) >> 16,
                        e.payload & 0xFFFF});
    });
    // Two distinct keys → two pending items.
    bus.post_coalesce<CoalesceEvent>(100, CoalesceEvent{100u << 16 | 1});
    bus.post_coalesce<CoalesceEvent>(200, CoalesceEvent{200u << 16 | 1});
    bus.post_coalesce<CoalesceEvent>(100, CoalesceEvent{100u << 16 | 2});
    CHECK_INT_EQ(static_cast<int>(bus.pendingCount()), 2);
    bus.pump();
    // key=100 collapsed to latest; key=200 unchanged.
    CHECK_INT_EQ(static_cast<int>(seen.size()), 2);
    // Order: key=100 was posted first → drained first → payload 2.
    CHECK_TRUE(seen[0].first == 100u);
    CHECK_INT_EQ(static_cast<int>(seen[0].second), 2);
    CHECK_TRUE(seen[1].first == 200u);
    CHECK_INT_EQ(static_cast<int>(seen[1].second), 1);
    CHECK_INT_EQ(static_cast<int>(bus.totalCoalesced()), 1);
}

TEST_CASE(CoalesceDoesNotMatchPlainPost) {
    EventBus bus;
    int count = 0;
    bus.subscribe<CoalesceEvent>([&](const CoalesceEvent&) { ++count; });
    // Mixed: one plain post + one coalesce(0). They MUST coexist (different
    // buckets in our header layout: coalesce flag distinguishes them).
    bus.post(CoalesceEvent{1});
    bus.post_coalesce<CoalesceEvent>(0, CoalesceEvent{2});
    CHECK_INT_EQ(static_cast<int>(bus.pendingCount()), 2);
    bus.pump();
    CHECK_INT_EQ(count, 2);
}

TEST_CASE(CoalesceKeyZeroIsValid) {
    // Regression: 0 is a real coalesce key, not a sentinel for "no coalesce".
    EventBus bus;
    int seen = 0;
    bus.subscribe<CoalesceEvent>([&](const CoalesceEvent& e) { seen = e.payload; });
    bus.post_coalesce<CoalesceEvent>(0, CoalesceEvent{42});
    bus.post_coalesce<CoalesceEvent>(0, CoalesceEvent{99});
    bus.pump();
    CHECK_INT_EQ(seen, 99);
}

TEST_CASE(CoalesceCrossThread) {
    EventBus bus;
    std::atomic<int> count{0};
    bus.subscribe<CoalesceEvent>([&](const CoalesceEvent&) { count.fetch_add(1); });
    // 100 workers each posting 10 coalesce items with the SAME key →
    // exactly one delivery total.
    constexpr int kWorkers = 4;
    constexpr int kPerWorker = 25;
    std::vector<std::thread> ts;
    std::atomic<bool> startGate{false};
    for (int w = 0; w < kWorkers; ++w) {
        ts.emplace_back([&] {
            while (!startGate.load(std::memory_order_acquire)) {}
            for (int i = 0; i < kPerWorker; ++i) {
                bus.post_coalesce<CoalesceEvent>(7, CoalesceEvent{i});
            }
        });
    }
    startGate.store(true, std::memory_order_release);
    for (auto& t : ts) t.join();
    CHECK_INT_EQ(static_cast<int>(bus.pendingCount()), 1);
    bus.pump();
    CHECK_INT_EQ(count.load(), 1);
}

TEST_SUITE_END

// -----------------------------------------------------------------------------
// Phase 2 §8 counters
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_Counters)

TEST_CASE(TotalPostedTracksPlainPost) {
    EventBus bus;
    CHECK_INT_EQ(static_cast<int>(bus.totalPosted()), 0);
    bus.post(SimpleEvent{1});
    bus.post(SimpleEvent{2});
    bus.post(SimpleEvent{3});
    CHECK_INT_EQ(static_cast<int>(bus.totalPosted()), 3);
    CHECK_INT_EQ(static_cast<int>(bus.totalCoalesced()), 0);
}

TEST_CASE(TotalCoalescedTracksReplacements) {
    EventBus bus;
    bus.subscribe<CoalesceEvent>([](const CoalesceEvent&) {});
    bus.post_coalesce<CoalesceEvent>(0, CoalesceEvent{1}); // posted
    bus.post_coalesce<CoalesceEvent>(0, CoalesceEvent{2}); // coalesced
    bus.post_coalesce<CoalesceEvent>(0, CoalesceEvent{3}); // coalesced
    CHECK_INT_EQ(static_cast<int>(bus.totalPosted()), 1);
    CHECK_INT_EQ(static_cast<int>(bus.totalCoalesced()), 2);
}

TEST_CASE(TotalDeliveredTracksPump) {
    EventBus bus;
    int got = 0;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent& e) { got = e.payload; });
    bus.post(SimpleEvent{7});
    bus.pump();
    CHECK_INT_EQ(static_cast<int>(bus.totalDelivered()), 1);
    CHECK_INT_EQ(got, 7);
    bus.emit(SimpleEvent{8});
    CHECK_INT_EQ(static_cast<int>(bus.totalDelivered()), 2);
    CHECK_INT_EQ(got, 8);
}

TEST_CASE(PeakQueueDepth) {
    EventBus bus;
    CHECK_INT_EQ(static_cast<int>(bus.peakQueueDepth()), 0);
    bus.post(SimpleEvent{1});
    bus.post(SimpleEvent{2});
    bus.post(SimpleEvent{3});
    CHECK_INT_EQ(static_cast<int>(bus.peakQueueDepth()), 3);
    bus.pump();
    CHECK_INT_EQ(static_cast<int>(bus.pendingCount()), 0);
    // Peak must persist (it's a "highest ever observed" metric).
    CHECK_INT_EQ(static_cast<int>(bus.peakQueueDepth()), 3);
}

TEST_CASE(ResetCounters) {
    EventBus bus;
    bus.post(SimpleEvent{1});
    bus.subscribe<SimpleEvent>([](const SimpleEvent&) {});
    bus.emit(SimpleEvent{2});
    CHECK_TRUE(bus.totalPosted() > 0 || bus.totalDelivered() > 0);
    bus.resetCounters();
    CHECK_INT_EQ(static_cast<int>(bus.totalPosted()), 0);
    CHECK_INT_EQ(static_cast<int>(bus.totalCoalesced()), 0);
    CHECK_INT_EQ(static_cast<int>(bus.totalDelivered()), 0);
    CHECK_INT_EQ(static_cast<int>(bus.peakQueueDepth()), 0);
}

TEST_SUITE_END

// -----------------------------------------------------------------------------
// Phase 3 §8 — string aliases (Script INT-04 bridge) + unsubscribeAll
// -----------------------------------------------------------------------------
TEST_SUITE(EventBus_Bridges)

struct AliasedEvent {
    int payload = 0;
    static constexpr EventTypeId   kTypeId   = 0x0A00'0100;
    static constexpr EventPriority kPriority = EventPriority::Normal;
};

TEST_CASE(RegisterAndLookupAlias) {
    EventBus bus;
    bus.registerAlias<AliasedEvent>("aliased_event");
    EventTypeId tid = bus.aliasToTypeId("aliased_event");
    CHECK_TRUE(tid != 0);
    CHECK_INT_EQ(static_cast<int>(tid), 0x0A00'0100);
}

TEST_CASE(UnknownAliasReturnsZero) {
    EventBus bus;
    CHECK_INT_EQ(static_cast<int>(bus.aliasToTypeId("never_registered")), 0);
}

TEST_CASE(SubscribeByAliasDelivers) {
    EventBus bus;
    int got = 0;
    bus.subscribeByAlias<AliasedEvent>("aliased_event",
        [&](const AliasedEvent& e) { got = e.payload; });
    bus.emitByAlias<AliasedEvent>("aliased_event", AliasedEvent{99});
    CHECK_INT_EQ(got, 99);
}

TEST_CASE(PostByAliasDeliversOnPump) {
    EventBus bus;
    int got = 0;
    bus.subscribeByAlias<AliasedEvent>("aliased_event",
        [&](const AliasedEvent& e) { got = e.payload; });
    bool ok = bus.postByAlias<AliasedEvent>("aliased_event", AliasedEvent{42});
    CHECK_TRUE(ok);
    bus.pump();
    CHECK_INT_EQ(got, 42);
}

TEST_CASE(EmitByAliasUnknownReturnsFalse) {
    EventBus bus;
    bool ok = bus.emitByAlias<AliasedEvent>("never_registered", AliasedEvent{1});
    CHECK_FALSE(ok);
}

TEST_CASE(UnsubscribeAllDropsEveryListener) {
    EventBus bus;
    int a = 0, b = 0, c = 0;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { ++a; });
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { ++b; });
    bus.subscribe<OtherEvent>([&](const OtherEvent&) { ++c; });
    bus.emit(SimpleEvent{0});
    bus.emit(OtherEvent{"x", {}});
    CHECK_INT_EQ(a, 1);
    CHECK_INT_EQ(b, 1);
    CHECK_INT_EQ(c, 1);

    bus.unsubscribeAll();

    bus.emit(SimpleEvent{0});
    bus.emit(OtherEvent{"y", {}});
    CHECK_INT_EQ(a, 1); // unchanged
    CHECK_INT_EQ(b, 1);
    CHECK_INT_EQ(c, 1);
}

TEST_CASE(UnsubscribeAllClearsPendingAdds) {
    // Simulate "subscribe during emit" then unsubscribeAll mid-delivery. The
    // pending add must be cleared so the listener does not appear later.
    EventBus bus;
    int delivered = 0;
    bus.subscribe<SimpleEvent>([&](const SimpleEvent&) {
        bus.subscribe<SimpleEvent>([&](const SimpleEvent&) { ++delivered; });
    });
    bus.unsubscribeAll();
    bus.emit(SimpleEvent{0});
    CHECK_INT_EQ(delivered, 0);
}

TEST_SUITE_END

// -----------------------------------------------------------------------------
// Phase 3 §8 — host integration examples (Device / Resource / AYTask patterns)
// -----------------------------------------------------------------------------
// These tests simulate the wiring described in docs/host_examples.md. They
// exercise the bus's role as a deferred delivery channel between worker
// threads and the main game thread; the actual Device / Resource / AYTask
// modules are separate repos and link against AYEventSystem for the real
// integration. Here we verify the bus's contract that downstream callers
// rely on.

TEST_SUITE(EventBus_HostExamples)

// Pattern A: AYDevice discrete action → main → post → pump → listener.
struct DeviceActionEvent {
    int actionId = 0;
    bool pressed = false;
    static constexpr EventTypeId   kTypeId   = 0x0A00'0200;
    static constexpr EventPriority kPriority = EventPriority::High;
};
TEST_CASE(DeviceActionPattern) {
    EventBus bus;
    std::atomic<int> received{0};
    int lastAction = 0;
    bool lastPressed = false;
    bus.subscribe<DeviceActionEvent>([&](const DeviceActionEvent& e) {
        received.fetch_add(1);
        lastAction = e.actionId;
        lastPressed = e.pressed;
    });
    // Simulate Device thread posting a discrete button edge.
    std::thread dev([&] {
        bus.post(DeviceActionEvent{42, true});
        bus.post(DeviceActionEvent{42, false});
    });
    dev.join();
    bus.pump();
    CHECK_INT_EQ(received.load(), 2);
    CHECK_INT_EQ(lastAction, 42);
    CHECK_FALSE(lastPressed); // last write wins
}

// Pattern B: AYResource worker → post complete → main pump notifies dependents.
struct ResourceLoadCompleteEvent {
    uint64_t handle = 0;
    int      refCount = 0;
    static constexpr EventTypeId   kTypeId   = 0x0A00'0201;
    static constexpr EventPriority kPriority = EventPriority::Low;
};
TEST_CASE(ResourceWorkerPattern) {
    EventBus bus;
    std::atomic<int> dependentsNotified{0};
    bus.subscribe<ResourceLoadCompleteEvent>(
        [&](const ResourceLoadCompleteEvent&) { dependentsNotified.fetch_add(1); });

    // Simulate 3 workers loading 3 different resources concurrently.
    constexpr int kWorkers = 3;
    std::vector<std::thread> ts;
    for (int i = 0; i < kWorkers; ++i) {
        ts.emplace_back([&, i] {
            bus.post(ResourceLoadCompleteEvent{static_cast<uint64_t>(i), 1});
        });
    }
    for (auto& t : ts) t.join();
    bus.pump();
    CHECK_INT_EQ(dependentsNotified.load(), kWorkers);
}

// Pattern C: AYTask completion callback → post → main pump.
struct TaskCompleteEvent {
    uint64_t taskId = 0;
    bool     ok = false;
    static constexpr EventTypeId   kTypeId   = 0x0A00'0202;
    static constexpr EventPriority kPriority = EventPriority::Normal;
};
TEST_CASE(TaskCompletionPattern) {
    EventBus bus;
    std::atomic<int> okCount{0};
    bus.subscribe<TaskCompleteEvent>([&](const TaskCompleteEvent& e) {
        if (e.ok) okCount.fetch_add(1);
    });

    // Simulate a job system firing completions from worker threads.
    constexpr int kTasks = 50;
    std::thread worker([&] {
        for (int i = 0; i < kTasks; ++i) {
            bus.post(TaskCompleteEvent{static_cast<uint64_t>(i), i % 2 == 0});
        }
    });
    worker.join();
    bus.pump();
    // half succeeded
    CHECK_INT_EQ(okCount.load(), kTasks / 2);
}

TEST_SUITE_END