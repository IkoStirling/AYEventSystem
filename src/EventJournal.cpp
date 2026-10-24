#include <AYEventSystem/EventJournal.h>
#include <AYStorage/ISqlite.h>
#include <AYSerializer/JsonSchema.h>
#include <nlohmann/json.hpp>
#include <mutex>
#include <unordered_map>

namespace ayt::event {
namespace {
using namespace ayt::storage;
using Json=nlohmann::json;
constexpr std::size_t maxSnapshot=1024*1024;
bool text(const std::string& s) { return s.size()<=256 && s.find('\0')==std::string::npos; }
bool list(const std::vector<std::string>& v) {
    return v.size()<=64 && std::all_of(v.begin(),v.end(),text);
}
bool valid(const EventMetadata& m) {
    return text(m.channel)&&text(m.scope)&&text(m.source)&&text(m.correlation)&&text(m.eventId)&&m.occurredAtMs>=0&&list(m.tags);
}
Json encode(const EventSnapshot& s) {
    const auto& m=s.metadata;
    return {{"type",s.typeName},{"version",s.schemaVersion},{"payload",s.payload},
        {"channel",m.channel},{"scope",m.scope},{"source",m.source},{"correlation",m.correlation},
        {"eventId",m.eventId},{"occurredAtMs",m.occurredAtMs},{"tags",m.tags}};
}
EventSnapshot decode(const std::string& body) {
    auto j=Json::parse(body);
    EventSnapshot s;
    s.typeName=j.at("type").get<std::string>(); s.schemaVersion=j.at("version").get<int>(); s.payload=j.at("payload").get<std::string>();
    auto& m=s.metadata;
    m.channel=j.at("channel").get<std::string>(); m.scope=j.at("scope").get<std::string>(); m.source=j.at("source").get<std::string>();
    m.correlation=j.at("correlation").get<std::string>(); m.eventId=j.at("eventId").get<std::string>();
    m.occurredAtMs=j.at("occurredAtMs").get<std::int64_t>(); m.tags=j.at("tags").get<std::vector<std::string>>();
    if (s.typeName.empty()||!text(s.typeName)||s.schemaVersion<1||!valid(m)) throw std::invalid_argument("snapshot");
    return s;
}
struct Transaction { ISqlite& db; bool committed=false; ~Transaction(){if(!committed) db.rollback();} };
}
struct EventJournal::Impl {
    mutable std::mutex mutex;
    std::unique_ptr<ISqlite> db;
    struct Codec {std::string name; int version; std::function<std::string(const EventView&)> encode;};
    std::unordered_map<std::type_index,Codec> codecs;
};
EventJournal::EventJournal():impl_(std::make_unique<Impl>()) {}
EventJournal::~EventJournal()=default;
bool EventJournal::open(const std::filesystem::path& path) {
    std::lock_guard guard(impl_->mutex);
    auto candidate=ISqlite::create();
    auto utf8=path.u8string();
    if(!candidate->open({reinterpret_cast<const char*>(utf8.data()),utf8.size()}) || !candidate->execute("PRAGMA synchronous=FULL") || !candidate->execute("BEGIN IMMEDIATE")) return false;
    Transaction txn{*candidate};
    if(!candidate->execute("CREATE TABLE IF NOT EXISTS ay_event_meta(version INTEGER NOT NULL);"
        "INSERT INTO ay_event_meta SELECT 1 WHERE NOT EXISTS(SELECT 1 FROM ay_event_meta);")) return false;
    auto version=candidate->prepare("SELECT version FROM ay_event_meta");
    if(!version||version->stepChecked()!=SqliteStepResult::Row||version->getInt64(0)!=1||version->stepChecked()!=SqliteStepResult::Done) return false;
    version.reset();
    if(!candidate->execute("CREATE TABLE IF NOT EXISTS ay_events("
        "seq INTEGER PRIMARY KEY AUTOINCREMENT,type TEXT NOT NULL,channel TEXT NOT NULL,scope TEXT NOT NULL,source TEXT NOT NULL,"
        "event_id TEXT NOT NULL,body TEXT NOT NULL);"
        "CREATE UNIQUE INDEX IF NOT EXISTS ay_events_identity ON ay_events(event_id) WHERE event_id<>'';"
        "CREATE INDEX IF NOT EXISTS ay_events_channel ON ay_events(channel,seq);"
        "CREATE INDEX IF NOT EXISTS ay_events_scope ON ay_events(scope,seq);"
        "CREATE TABLE IF NOT EXISTS ay_event_tags(seq INTEGER NOT NULL,tag TEXT NOT NULL,PRIMARY KEY(seq,tag));"
        "CREATE INDEX IF NOT EXISTS ay_event_tags_lookup ON ay_event_tags(tag,seq);")) return false;
    if(!candidate->commit()) return false;
    txn.committed=true; impl_->db=std::move(candidate); return true;
}
bool EventJournal::_register(std::type_index type,std::string name,int version,std::function<std::string(const EventView&)> codec) {
    if(name.empty()||!text(name)||version<1) return false;
    std::lock_guard guard(impl_->mutex);
    // Stable names also identify exactly one C++ payload codec in this instance.
    for(const auto& [key,c]:impl_->codecs) if(c.name==name) return false;
    return impl_->codecs.emplace(type,Impl::Codec{std::move(name),version,std::move(codec)}).second;
}
JournalReceipt EventJournal::capture(const EventView& view) {
    Impl::Codec codec;
    {std::lock_guard guard(impl_->mutex); auto it=impl_->codecs.find(view.payloadType);
        if(it==impl_->codecs.end()) return {JournalStatus::Unsupported}; codec=it->second;}
    if(!view.payload) return {JournalStatus::Invalid};
    try {return append({codec.name,codec.version,view.metadata,codec.encode(view)});}
    catch(...) {return {JournalStatus::Invalid};}
}
JournalReceipt EventJournal::append(EventSnapshot snapshot) {
    if(snapshot.typeName.empty()||!text(snapshot.typeName)||snapshot.schemaVersion<1||!valid(snapshot.metadata)) return {JournalStatus::Invalid};
    if(snapshot.payload.size()>maxSnapshot) return {JournalStatus::Limit};
    if(!serializer::validateJsonDocument(snapshot.payload,"{}")) return {JournalStatus::Invalid};
    auto& m=snapshot.metadata;
    std::sort(m.tags.begin(),m.tags.end()); m.tags.erase(std::unique(m.tags.begin(),m.tags.end()),m.tags.end());
    std::string body;
    try {body=encode(snapshot).dump();} catch(...) {return {JournalStatus::Invalid};}
    if(body.size()>maxSnapshot) return {JournalStatus::Limit};
    std::lock_guard guard(impl_->mutex);
    if(!impl_->db||!impl_->db->execute("BEGIN IMMEDIATE")) return {JournalStatus::Unavailable};
    auto& db=*impl_->db; Transaction txn{db};
    if(!m.eventId.empty()) {
        auto existing=db.prepare("SELECT seq,body FROM ay_events WHERE event_id=?");
        if(!existing) return {JournalStatus::Unavailable}; existing->bind(1,m.eventId);
        auto state=existing->stepChecked();
        if(state==SqliteStepResult::Error) return {JournalStatus::Unavailable};
        if(state==SqliteStepResult::Row) return {existing->getText(1)==body?JournalStatus::Duplicate:JournalStatus::Conflict,existing->getInt64(0)};
    }
    auto insert=db.prepare("INSERT INTO ay_events(type,channel,scope,source,event_id,body) VALUES(?,?,?,?,?,?)");
    if(!insert) return {JournalStatus::Unavailable};
    insert->bind(1,snapshot.typeName);insert->bind(2,m.channel);insert->bind(3,m.scope);insert->bind(4,m.source);insert->bind(5,m.eventId);insert->bind(6,body);
    if(insert->stepChecked()!=SqliteStepResult::Done) return {JournalStatus::Unavailable};
    auto seq=db.lastInsertRowid(); insert.reset();
    for(const auto& tag:m.tags) {
        auto t=db.prepare("INSERT INTO ay_event_tags(seq,tag) VALUES(?,?)"); if(!t) return {JournalStatus::Unavailable};
        t->bind(1,seq); t->bind(2,tag); if(t->stepChecked()!=SqliteStepResult::Done) return {JournalStatus::Unavailable};
    }
    if(!db.commit()) return {JournalStatus::Unavailable}; txn.committed=true;
    return {JournalStatus::Ok,seq};
}
JournalPage EventJournal::query(const JournalQuery& q) const {
    const auto& f=q.filter;
    if(!list(f.channels)||!list(f.scopes)||!list(f.sources)||!list(f.requiredTags)||!text(f.channelTree)||!list(q.typeNames)||
        q.afterSequence<0||q.throughSequence<0||(q.throughSequence && q.throughSequence<q.afterSequence)||!q.limit||q.limit>1000||!q.maxBytes||q.maxBytes>16*maxSnapshot) return {JournalStatus::Invalid};
    std::lock_guard guard(impl_->mutex);
    if(!impl_->db) return {JournalStatus::Unavailable};
    std::string sql="SELECT e.seq,length(CAST(e.body AS BLOB)),e.body FROM ay_events e WHERE e.seq>? AND (?=0 OR e.seq<=?) ";
    std::vector<std::string> bindings;
    auto values=[&](const char* column,const auto& entries){if(entries.empty()) return;
        sql+="AND ";sql+=column;sql+=" IN (";
        for(size_t i=0;i<entries.size();++i){sql+=i?",?":"?";bindings.push_back(entries[i]);}sql+=") ";};
    values("e.channel",f.channels);values("e.scope",f.scopes);values("e.source",f.sources);values("e.type",q.typeNames);
    if(!f.channelTree.empty()) {
        // BINARY range for the literal '.' subtree; '/' is the next ASCII byte.
        // Unlike LIKE/substr this preserves case and permits the channel index.
        sql+="AND (e.channel=? OR (e.channel>=? AND e.channel<?)) ";
        bindings.push_back(f.channelTree);bindings.push_back(f.channelTree+".");bindings.push_back(f.channelTree+"/");
    }
    for(const auto& tag:f.requiredTags){sql+="AND EXISTS(SELECT 1 FROM ay_event_tags t WHERE t.seq=e.seq AND t.tag=?) ";bindings.push_back(tag);}
    sql+="ORDER BY e.seq LIMIT ?";
    auto stmt=impl_->db->prepare(sql.c_str()); if(!stmt) return {JournalStatus::Unavailable};
    stmt->bind(1,q.afterSequence);stmt->bind(2,q.throughSequence);stmt->bind(3,q.throughSequence);
    int index=4;for(const auto& b:bindings) stmt->bind(index++,b);stmt->bind(index,static_cast<int>(q.limit+1));
    JournalPage page{JournalStatus::Ok};page.nextSequence=q.afterSequence;
    size_t bytes=0;SqliteStepResult state;
    while((state=stmt->stepChecked())==SqliteStepResult::Row) {
        auto size=stmt->getInt64(1);
        if(size<0||size>static_cast<std::int64_t>(maxSnapshot)) return {JournalStatus::Invalid};
        if(page.records.size()==q.limit || static_cast<std::uint64_t>(size)>q.maxBytes-bytes) {
            if(page.records.empty()) return {JournalStatus::Limit};
            page.more=true;return page;
        }
        try {page.records.push_back({stmt->getInt64(0),decode(stmt->getText(2))});}
        catch(...) {return {JournalStatus::Invalid};}
        page.nextSequence=page.records.back().sequence;bytes+=static_cast<size_t>(size);
    }
    if(state==SqliteStepResult::Error) return {JournalStatus::Unavailable};
    return page;
}
}
