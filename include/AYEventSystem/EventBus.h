#pragma once
#include <AYEventSystem/EventRoute.h>
#include <AYEventSystem/Connection.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace ayt::event {
/// Typed sync/deferred bus. emit/pump/subscriptions belong to one owner thread;
/// post is thread safe. Mutations during delivery apply after outermost delivery.
/// Channels select existing deliveries; they create no extra queue or thread.
class EventBus {
public:
    static EventBus& instance();
    EventBus();
    ~EventBus();
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    template<typename TEvent>
    ConnectionId subscribe(std::function<void(const TEvent&)> handler) {
        return subscribeById(_typeIdOf<TEvent>(), [h=std::move(handler)](const void* p) {
            h(*static_cast<const TEvent*>(p));
        });
    }
    /// Filter a typed subscription. Legacy subscriptions receive every channel.
    template<typename TEvent>
    ConnectionId subscribe(EventFilter filter,
            std::function<void(const TEvent&, const EventMetadata&)> handler) {
        return _subscribe(_typeIdOf<TEvent>(),false,std::move(filter),
            [h=std::move(handler)](const EventView& v) { h(*static_cast<const TEvent*>(v.payload),v.metadata); });
    }
    ConnectionId subscribeById(EventTypeId tid, std::function<void(const void*)> erased);
    /// Observe multiple payload types on this same bus. Called after typed
    /// listeners, in observer registration order. Retain a ScopedConnection.
    ConnectionId observe(EventFilter filter, std::function<void(const EventView&)> handler);
    void unsubscribe(ConnectionId id);
    /// Owner thread only, outside delivery. Does not discard queued events.
    void unsubscribeAll();
    /// Deliver immediately on the designated owner thread using the default route.
    /// Callback exceptions propagate after deferred subscription cleanup.
    template<typename TEvent>
    void emit(const TEvent& event) { emit(event,{}); }
    template<typename TEvent>
    void emit(const TEvent& event, EventMetadata metadata) {
        _emit({_typeIdOf<TEvent>(),metadata,typeid(TEvent),&event});
    }
    template<typename TEvent>
    void post(TEvent event) { post(std::move(event),{}); }
    /// Owns both metadata and an aligned payload copy until pump. The existing
    /// trivially-copyable payload restriction remains for queued engine events.
    template<typename TEvent>
    void post(TEvent event, EventMetadata metadata) {
        _queue<TEvent>(std::move(event),std::move(metadata),0,false);
    }
    template<typename TEvent>
    void post_coalesce(uint64_t key, TEvent event) { post_coalesce(key,std::move(event),{}); }
    /// Coalescing identity: type + priority + channel + scope + source + key.
    /// Replaces the entire metadata/payload, retaining the first FIFO position.
    template<typename TEvent>
    void post_coalesce(uint64_t key, TEvent event, EventMetadata metadata) {
        _queue<TEvent>(std::move(event),std::move(metadata),key,true);
    }
    /// Drain priorities Critical to Low on the owner, including nested posts
    /// (bounded to 1024 passes). Callback failure preserves unstarted packets.
    void pump();
    size_t listenerCount(EventTypeId tid) const;
    size_t observerCount() const;
    size_t pendingCount() const;
    bool isMainThread() const;
    uint64_t totalPosted() const { return _totalPosted.load(std::memory_order_relaxed); }
    uint64_t totalCoalesced() const { return _totalCoalesced.load(std::memory_order_relaxed); }
    uint64_t totalDelivered() const { return _totalDelivered.load(std::memory_order_relaxed); }
    uint64_t peakQueueDepth() const { return _peakQueueDepth.load(std::memory_order_relaxed); }
    void resetCounters() { _totalPosted=0; _totalCoalesced=0; _totalDelivered=0; _peakQueueDepth=0; }
    template<typename TEvent>
    void registerAlias(std::string_view alias) {
        auto tid = _typeIdOf<TEvent>();
        std::lock_guard lk(_aliasMutex);
        _aliases.emplace(std::string(alias),tid);
    }
    EventTypeId aliasToTypeId(std::string_view alias) const;
    template<typename TEvent>
    ConnectionId subscribeByAlias(std::string_view alias, std::function<void(const TEvent&)> handler) {
        auto id=subscribe<TEvent>(std::move(handler)); registerAlias<TEvent>(alias); return id;
    }
    template<typename TEvent>
    bool emitByAlias(std::string_view alias, const TEvent& event) {
        if (!aliasToTypeId(alias)) return false;
        emit(event); return true;
    }
    template<typename TEvent>
    bool postByAlias(std::string_view alias, TEvent event) {
        if (!aliasToTypeId(alias)) return false;
        post(std::move(event)); return true;
    }
    void _unsubscribeRaw(ConnectionId id);
private:
    struct ListenerEntry {
        ConnectionId id;
        EventFilter filter;
        std::function<void(const EventView&)> invoke;
    };
    using ListenerBucket = std::vector<std::unique_ptr<ListenerEntry>>;
    struct PendingAdd { EventTypeId tid; bool observer; std::unique_ptr<ListenerEntry> entry; };
    struct Packet {
        EventTypeId tid;
        std::type_index payloadType{typeid(void)};
        std::shared_ptr<const void> payload;
        EventMetadata metadata;
        uint64_t key;
        bool coalesce;
    };
    template<typename TEvent>
    void _queue(TEvent event, EventMetadata metadata, uint64_t key, bool coalesce) {
        static_assert(std::is_trivially_copyable_v<TEvent>,"Queued events must be trivially_copyable; use emit for owning payloads.");
        EventPriority priority=EventPriority::Normal;
        if constexpr (requires { TEvent::kPriority; }) priority=TEvent::kPriority;
        _post(priority,{_typeIdOf<TEvent>(),typeid(TEvent),std::make_shared<TEvent>(std::move(event)),std::move(metadata),key,coalesce});
    }
    template<typename TEvent>
    EventTypeId _typeIdOf() {
        if constexpr (requires { TEvent::kTypeId; }) return TEvent::kTypeId;
        else {
            std::lock_guard lk(_bucketsMutex);
            auto [it,inserted]=_typeIdByTypeInfo.try_emplace(typeid(TEvent),0);
            if (inserted) it->second=_nextRuntimeTypeId.fetch_add(1,std::memory_order_relaxed);
            return it->second;
        }
    }
    ConnectionId _subscribe(EventTypeId, bool, EventFilter, std::function<void(const EventView&)>);
    void _emit(const EventView& view);
    void _post(EventPriority priority, Packet packet);
    void _captureOwner();
    void _assertOwner() const;
    void _finishDelivery();
    void _flushPendingMutations();
    mutable std::mutex _bucketsMutex;
    std::unordered_map<EventTypeId,ListenerBucket> _buckets;
    ListenerBucket _observers;
    std::unordered_map<std::type_index,EventTypeId> _typeIdByTypeInfo;
    std::vector<PendingAdd> _pendingAdds;
    std::vector<ConnectionId> _pendingRemoves;
    int _deliveryDepth=0;
    mutable std::mutex _postMutex;
    std::vector<std::vector<Packet>> _queues;
    std::atomic<bool> _mainThreadCaptured{false};
    std::atomic<std::thread::id> _mainThreadId{};
    std::atomic<uint64_t> _nextConnectionId{1};
    std::atomic<uint32_t> _nextRuntimeTypeId{kHandAssignedTypeIdMax+1};
    std::atomic<uint64_t> _totalPosted{0},_totalCoalesced{0},_totalDelivered{0},_peakQueueDepth{0};
    mutable std::mutex _aliasMutex;
    std::unordered_map<std::string,EventTypeId> _aliases;
    friend class ScopedConnection;
};
}
