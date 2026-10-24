#include <AYEventSystem/EventBus.h>
#include <AYTest.h>
#include <stdexcept>
#include <thread>
using namespace ayt::event;
namespace {
struct RouteEvent {int value;};
struct RouteOther {int value;};
struct alignas(64) AlignedEvent {int value; int padding[15]{};};
EventMetadata route(std::string channel="story.fact",std::string scope="actor-1",std::string source="sim") {
    EventMetadata m;m.channel=std::move(channel);m.scope=std::move(scope);m.source=std::move(source);m.tags={"visible","committed"};return m;
}
}
TEST_SUITE(EventRoute)
TEST_CASE(CombinedFiltersHaveExactTreeAndAllTagSemantics) {
    EventFilter f;f.channelTree="story";f.scopes={"actor-1","actor-2"};f.sources={"sim"};f.requiredTags={"visible","committed"};
    CHECK(f.matches(route()));CHECK(f.matches(route("story")));CHECK(!f.matches(route("storyline")));
    CHECK(!f.matches(route("Story.fact")));CHECK(!f.matches(route("story.fact","actor-3")));
    CHECK(!f.matches(route("story.fact","actor-1","network")));
    auto m=route();m.tags.pop_back();CHECK(!f.matches(m));
    f.channels={"story.special"};CHECK(!f.matches(route()));CHECK(f.matches(route("story.special")));
    CHECK(EventFilter{}.matches({}));
}
TEST_CASE(TypedAndCrossTypeSubscriptionsShareOneDelivery) {
    EventBus bus;std::vector<int> order;int legacy=0;
    bus.subscribe<RouteEvent>([&](const auto&){++legacy;order.push_back(1);});
    EventFilter f;f.channelTree="story";
    bus.subscribe<RouteEvent>(f,[&](const auto& e,const auto& m){CHECK_INT_EQ(e.value,7);CHECK(m.channel=="story.fact");order.push_back(2);});
    ScopedConnection observer(bus,bus.observe(f,[&](const EventView& v){
        CHECK((v.get<RouteEvent>()!=nullptr)!=(v.get<RouteOther>()!=nullptr));order.push_back(3);
    }));
    bus.emit(RouteEvent{7},route());bus.emit(RouteOther{9},route());bus.emit(RouteEvent{0});
    CHECK_INT_EQ(legacy,2);CHECK(order==std::vector<int>({1,2,3,3,1}));CHECK_INT_EQ(bus.observerCount(),1);
    observer.reset();CHECK_INT_EQ(bus.observerCount(),0);
}
TEST_CASE(WorkerQueueOwnsMetadataAndPreservesPayloadAlignment) {
    EventBus bus;int seen=0;
    bus.observe({},[&](const EventView& v){
        auto* e=v.get<AlignedEvent>();CHECK(e!=nullptr);
        CHECK_INT_EQ(reinterpret_cast<std::uintptr_t>(e)%alignof(AlignedEvent),0);
        CHECK_INT_EQ(e->value,42);CHECK(v.metadata.scope=="actor-1");CHECK(v.metadata.tags.size()==2);++seen;
    });
    std::thread worker([&]{auto m=route();bus.post(AlignedEvent{42},m);m.scope="changed";m.tags.clear();});worker.join();
    CHECK_INT_EQ(seen,0);bus.pump();CHECK_INT_EQ(seen,1);
}
TEST_CASE(CoalescingDoesNotCrossChannelScopeOrSource) {
    EventBus bus;std::vector<int> got;std::string id;
    bus.observe({},[&](const EventView& v){got.push_back(v.get<RouteEvent>()->value);if(got.size()==1) id=v.metadata.eventId;});
    auto m=route();bus.post_coalesce(0,RouteEvent{1},m);
    bus.post_coalesce(0,RouteEvent{2},route("story.other"));
    bus.post_coalesce(0,RouteEvent{3},route("story.fact","actor-2"));
    bus.post_coalesce(0,RouteEvent{4},route("story.fact","actor-1","other"));
    m.eventId="latest";bus.post_coalesce(0,RouteEvent{5},m);
    CHECK_INT_EQ(bus.pendingCount(),4);CHECK_INT_EQ(bus.totalCoalesced(),1);bus.pump();
    CHECK(got==std::vector<int>({5,2,3,4}));CHECK(id=="latest");
}
TEST_CASE(ObserverMutationsAreDeferredAcrossNestedDelivery) {
    EventBus bus;int first=0,late=0;ConnectionId id=0;
    id=bus.observe({},[&](const EventView& v){++first;if(v.get<RouteEvent>()->value==1){
        bus.unsubscribe(id);bus.observe({},[&](const auto&){++late;});bus.emit(RouteEvent{2});
    }});
    bus.emit(RouteEvent{1});CHECK_INT_EQ(first,2);CHECK_INT_EQ(late,0);
    bus.emit(RouteEvent{3});CHECK_INT_EQ(first,2);CHECK_INT_EQ(late,1);
    bus.unsubscribeAll();CHECK_INT_EQ(bus.observerCount(),0);
}
TEST_CASE(ThrowingListenerRestoresMutationsAndKeepsUnstartedQueue) {
    EventBus bus;int received=0;ConnectionId failing=0;
    failing=bus.observe({},[&](const auto&){
        bus.unsubscribe(failing);bus.observe({},[&](const auto&){++received;});throw std::runtime_error("expected");
    });
    bus.post(RouteEvent{1});bus.post(RouteEvent{2});bus.post(RouteEvent{3});
    bool threw=false;try {bus.pump();}catch(const std::runtime_error&){threw=true;}
    CHECK(threw);CHECK_INT_EQ(bus.pendingCount(),2);CHECK_INT_EQ(bus.observerCount(),1);
    bus.pump();CHECK_INT_EQ(received,2);CHECK_INT_EQ(bus.pendingCount(),0);
}
TEST_SUITE_END
