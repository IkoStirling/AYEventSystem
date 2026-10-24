#include <AYEventSystem/EventJournal.h>
#include <AYEventSystem/EventBus.h>
#include <AYStorage/ISqlite.h>
#include <AYTest.h>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
using namespace ayt::event;
namespace {
struct Fact {int value;};
struct Unregistered {int value;};
struct DbFile {
    std::filesystem::path path;
    DbFile(){static std::atomic<int> n=0;path=std::filesystem::temp_directory_path()/("ay-event-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(n++)+".sqlite");}
    ~DbFile(){std::error_code ec;std::filesystem::remove(path,ec);}
};
EventSnapshot snapshot(std::string id="",std::string channel="story.fact",std::string scope="a") {
    EventSnapshot s;s.typeName="test.fact";s.payload="{\"value\":7}";
    s.metadata.channel=std::move(channel);s.metadata.scope=std::move(scope);s.metadata.source="sim";
    s.metadata.eventId=std::move(id);s.metadata.correlation="turn-1";s.metadata.occurredAtMs=100;s.metadata.tags={"visible","committed"};return s;
}
}
TEST_SUITE(EventJournal)
TEST_CASE(FilteredBusCaptureSurvivesReopenWithoutEmittingHistory) {
    DbFile file;
    {EventJournal journal;CHECK(journal.open(file.path));
        CHECK(journal.registerCodec<Fact>("test.fact",2,[](const auto& f){return "{\"value\":"+std::to_string(f.value)+"}";}));
        EventBus bus;int captured=0;EventFilter filter;filter.channelTree="story";
        ScopedConnection link(bus,bus.observe(filter,[&](const EventView& v){auto receipt=journal.capture(v);CHECK(receipt);++captured;}));
        auto m=snapshot("first").metadata;bus.post(Fact{42},m);bus.post(Fact{99},snapshot("hidden","system.trace").metadata);bus.pump();
        CHECK_INT_EQ(captured,1);
    }
    EventJournal reopened;CHECK(reopened.open(file.path));auto page=reopened.query({});CHECK(page);CHECK_INT_EQ(page.records.size(),1);
    const auto& s=page.records.at(0).snapshot;CHECK(s.payload=="{\"value\":42}");CHECK(s.typeName=="test.fact");CHECK_INT_EQ(s.schemaVersion,2);
    CHECK(s.metadata.eventId=="first");CHECK(s.metadata.correlation=="turn-1");CHECK_INT_EQ(s.metadata.occurredAtMs,100);
}
TEST_CASE(SqlFilteringPrecedesPagingAndTreeNamesAreLiteral) {
    DbFile file;EventJournal journal;CHECK(journal.open(file.path));
    CHECK(journal.append(snapshot("noise","system.trace")));CHECK(journal.append(snapshot("wrong-scope","story.fact","b")));
    auto a=journal.append(snapshot("one"));CHECK(a);auto b=journal.append(snapshot("two","story.other"));CHECK(b);
    CHECK(journal.append(snapshot("boundary","storyline")));
    JournalQuery q;q.filter.channelTree="story";q.filter.scopes={"a"};q.filter.sources={"sim"};q.filter.requiredTags={"visible","committed"};q.typeNames={"test.fact"};q.limit=1;
    auto first=journal.query(q);CHECK(first);CHECK_INT_EQ(first.records.size(),1);CHECK(first.records.at(0).snapshot.metadata.eventId=="one");CHECK(first.more);
    q.afterSequence=first.nextSequence;q.throughSequence=b.sequence;
    auto second=journal.query(q);CHECK(second);CHECK(second.records.at(0).snapshot.metadata.eventId=="two");CHECK(!second.more);
    q.filter.channels={"story.other"};q.afterSequence=0;CHECK_INT_EQ(journal.query(q).records.size(),1);
    q.filter.requiredTags.push_back("absent");CHECK(journal.query(q).records.empty());
    CHECK(journal.append(snapshot("literal","s%_'.child")));q={};q.filter.channelTree="s%_'";CHECK_INT_EQ(journal.query(q).records.size(),1);
    CHECK(journal.append(snapshot("unicode","世界.发现")));CHECK(journal.append(snapshot("unicode-boundary","世界/发现")));
    q={};q.filter.channelTree="世界";auto unicode=journal.query(q);CHECK(unicode);CHECK_INT_EQ(unicode.records.size(),1);
    CHECK(unicode.records.at(0).snapshot.metadata.eventId=="unicode");
}
TEST_CASE(IdentityDeduplicationAndConflictsAreAtomicAcrossConnections) {
    DbFile file;EventJournal a,b;CHECK(a.open(file.path));CHECK(b.open(file.path));
    JournalReceipt x,y;
    std::thread t1([&]{x=a.append(snapshot("same"));});std::thread t2([&]{y=b.append(snapshot("same"));});t1.join();t2.join();
    CHECK(x);CHECK(y);CHECK_INT_EQ(x.sequence,y.sequence);
    CHECK((x.status==JournalStatus::Ok && y.status==JournalStatus::Duplicate)||(y.status==JournalStatus::Ok && x.status==JournalStatus::Duplicate));
    auto altered=snapshot("same");altered.payload="{}";CHECK(a.append(altered).status==JournalStatus::Conflict);
    CHECK_INT_EQ(a.query({}).records.size(),1);
    CHECK(a.append(snapshot()));CHECK(a.append(snapshot()));CHECK_INT_EQ(b.query({}).records.size(),3);
}
TEST_CASE(ExplicitCodecsAndBoundedValidationFailClosed) {
    DbFile file;EventJournal journal;CHECK(journal.open(file.path));
    CHECK(journal.registerCodec<Fact>("test.fact",1,[](const auto&)->std::string{throw std::runtime_error("private payload");}));
    CHECK(!journal.registerCodec<Fact>("other",1,[](const auto&){return "{}";}));
    CHECK(!journal.registerCodec<Unregistered>("test.fact",1,[](const auto&){return "{}";}));
    EventBus bus;int errors=0;bus.observe({},[&](const EventView& v){
        auto r=journal.capture(v);CHECK(r.status==(v.get<Fact>()?JournalStatus::Invalid:JournalStatus::Unsupported));++errors;
    });bus.emit(Fact{1});bus.emit(Unregistered{2});CHECK_INT_EQ(errors,2);
    auto s=snapshot();s.payload="{\"x\":1,\"x\":2}";CHECK(journal.append(s).status==JournalStatus::Invalid);
    s=snapshot();s.payload=std::string(1024*1024+1,'x');CHECK(journal.append(s).status==JournalStatus::Limit);
    s=snapshot();s.metadata.tags.resize(65,"tag");CHECK(journal.append(s).status==JournalStatus::Invalid);
    s=snapshot();s.metadata.occurredAtMs=-1;CHECK(journal.append(s).status==JournalStatus::Invalid);
    CHECK(journal.query({}).records.empty());
    JournalQuery q;q.limit=0;CHECK(journal.query(q).status==JournalStatus::Invalid);
}
TEST_CASE(ByteBudgetAndFutureStorageVersionAreExplicit) {
    DbFile file;
    {EventJournal journal;CHECK(journal.open(file.path));CHECK(journal.append(snapshot()));
        JournalQuery q;q.maxBytes=1;CHECK(journal.query(q).status==JournalStatus::Limit);
    }
    {auto db=ayt::storage::ISqlite::create();auto p=file.path.u8string();CHECK(db->open({reinterpret_cast<const char*>(p.data()),p.size()}));CHECK(db->execute("UPDATE ay_event_meta SET version=999"));}
    EventJournal future;CHECK(!future.open(file.path));CHECK(future.query({}).status==JournalStatus::Unavailable);
}
TEST_CASE(PartialBytePagesAdvanceAndFailedReopenPreservesOpenArchive) {
    DbFile file;EventJournal journal;
    CHECK(journal.append(snapshot()).status==JournalStatus::Unavailable);
    CHECK(journal.open(file.path));auto first=journal.append(snapshot("first"));CHECK(first);
    auto larger=snapshot("second");larger.payload="\""+std::string(1000,'x')+"\"";CHECK(journal.append(larger));
    JournalQuery q;q.maxBytes=512;
    auto page=journal.query(q);CHECK(page);CHECK_INT_EQ(page.records.size(),1);CHECK(page.more);CHECK_INT_EQ(page.nextSequence,first.sequence);
    q.afterSequence=page.nextSequence;CHECK(journal.query(q).status==JournalStatus::Limit);
    q.maxBytes=4096;auto next=journal.query(q);CHECK(next);CHECK_INT_EQ(next.records.size(),1);CHECK(!next.more);
    CHECK(next.records.at(0).snapshot.metadata.eventId=="second");
    CHECK(!journal.open(file.path/"nonexistent-child"));CHECK_INT_EQ(journal.query({}).records.size(),2);
    q={};q.afterSequence=2;q.throughSequence=1;CHECK(journal.query(q).status==JournalStatus::Invalid);
    q={};q.filter.channelTree="STORY";CHECK(journal.query(q).records.empty());
}
TEST_SUITE_END
