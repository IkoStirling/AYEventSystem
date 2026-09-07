#include <AYEventSystem/SubscriptionScope.h>
#include <AYTest.h>

#include <utility>

namespace ayt::event::test
{

struct ScopeEvent {
    int payload = 0;
    static constexpr EventTypeId kTypeId = 0x0A00'9001;
    static constexpr EventPriority kPriority = EventPriority::Normal;
};

TEST_SUITE(SubscriptionScope)

TEST_CASE(SubscribeAndDisconnect)
{
    auto& bus = EventBus::instance();
    SubscriptionScope scope;
    int received = 0;

    scope.subscribe<ScopeEvent>([&received](const ScopeEvent& event) {
        received = event.payload;
    });

    bus.emit(ScopeEvent{42});
    CHECK_INT_EQ(received, 42);
    CHECK_INT_EQ(static_cast<int>(scope.size()), 1);

    scope.disconnect();
    bus.emit(ScopeEvent{7});
    CHECK_INT_EQ(received, 42);
    CHECK(scope.empty());

    scope.disconnect();
    CHECK(scope.empty());
}

TEST_CASE(DestructorDoesNotTouchBus)
{
    auto& bus = EventBus::instance();
    ConnectionId leaked = kInvalidConnectionId;
    {
        SubscriptionScope scope;
        leaked = scope.subscribe<ScopeEvent>([](const ScopeEvent&) {});
    }

    CHECK_INT_EQ(static_cast<int>(bus.listenerCount(ScopeEvent::kTypeId)), 1);
    bus.unsubscribe(leaked);
    CHECK_INT_EQ(static_cast<int>(bus.listenerCount(ScopeEvent::kTypeId)), 0);
}

TEST_CASE(MoveAndDetachTransferOwnership)
{
    auto& bus = EventBus::instance();
    SubscriptionScope source;
    source.subscribe<ScopeEvent>([](const ScopeEvent&) {});
    source.subscribe<ScopeEvent>([](const ScopeEvent&) {});

    SubscriptionScope moved(std::move(source));
    CHECK(source.empty());
    CHECK_INT_EQ(static_cast<int>(moved.size()), 2);

    SubscriptionScope detached = moved.detach();
    CHECK(moved.empty());
    CHECK_INT_EQ(static_cast<int>(detached.size()), 2);

    detached.disconnect();
    CHECK_INT_EQ(static_cast<int>(bus.listenerCount(ScopeEvent::kTypeId)), 0);
}

TEST_SUITE_END

} // namespace ayt::event::test
