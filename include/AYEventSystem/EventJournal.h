#pragma once
#include <AYEventSystem/EventRoute.h>
#include <filesystem>
#include <functional>
#include <memory>

namespace ayt::event {
enum class JournalStatus { Ok, Duplicate, Unsupported, Invalid, Conflict, Limit, Unavailable };
struct JournalReceipt {
    JournalStatus status = JournalStatus::Unavailable;
    std::int64_t sequence = 0;
    explicit operator bool() const noexcept { return status==JournalStatus::Ok || status==JournalStatus::Duplicate; }
};
struct EventSnapshot {
    std::string typeName; // caller-owned stable name, not RTTI or runtime type ID
    int schemaVersion = 1;
    EventMetadata metadata;
    std::string payload; // owned JSON, produced by an explicit codec
};
struct JournalRecord { std::int64_t sequence=0; EventSnapshot snapshot; };
struct JournalQuery {
    EventFilter filter;
    std::vector<std::string> typeNames;
    std::int64_t afterSequence=0;
    std::int64_t throughSequence=0; // zero = unbounded; use a saved upper cursor for a stable scan
    std::size_t limit=128;
    std::size_t maxBytes=4*1024*1024; // combined serialized snapshots per page
};
struct JournalPage {
    JournalStatus status=JournalStatus::Unavailable;
    std::vector<JournalRecord> records;
    std::int64_t nextSequence=0;
    bool more=false;
    explicit operator bool() const noexcept { return status==JournalStatus::Ok; }
};
/// Optional append-only journal using AYStorage SQLite + AYSerializer validation.
/// Not event sourcing or a transactional outbox. Capture is synchronous, owner
/// callback payloads never escape. No automatic listener, replay or state writes.
/// All operations may run concurrently; codecs execute outside the storage lock.
class EventJournal {
public:
    EventJournal();
    ~EventJournal();
    EventJournal(const EventJournal&)=delete;
    EventJournal& operator=(const EventJournal&)=delete;
    bool open(const std::filesystem::path& path);
    /// Register one immutable codec per C++ type. Unknown types return Unsupported.
    /// Conversion exceptions become Invalid; return values contain no payload/error text.
    template<class T>
    bool registerCodec(std::string typeName,int schemaVersion,std::function<std::string(const T&)> codec) {
        if (!codec) return false;
        return _register(typeid(T),std::move(typeName),schemaVersion,
            [c=std::move(codec)](const EventView& view){return c(*view.get<T>());});
    }
    JournalReceipt capture(const EventView& view);
    /// Nonempty eventId deduplicates globally within this journal. Identical
    /// snapshot bytes return Duplicate; conflicting reuse returns Conflict.
    JournalReceipt append(EventSnapshot snapshot);
    /// SQL filters precede paging. Stable sequence order is independent of caller
    /// timestamps and nested callback order. No implicit emit of historical rows.
    JournalPage query(const JournalQuery& query) const;
private:
    bool _register(std::type_index,std::string,int,std::function<std::string(const EventView&)>);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
