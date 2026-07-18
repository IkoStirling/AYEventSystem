#pragma once
// AYEventSystem/EventBus.h
//
// Engine-wide typed deferred + sync bus.
//
// Thread contract (design §2.2):
//   - post<T>    — any thread, queues until pump.
//   - emit<T>    — main thread only, immediate.
//   - pump       — main thread only, drains Critical→Low.
//   - subscribe  — main preferred; off-main needs external sync w.r.t. pump.
//
// See design.md §3 for the full spec; this is the Phase 1 surface.

#include <ayevent/EventPriority.h>
#include <ayevent/Connection.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ayt::event
{

// -----------------------------------------------------------------------------
// Internal erased listener entry. Per-type invocation goes through invoke().
// Each entry owns its ConnectionId; unsubscribe removes by id alone.
// -----------------------------------------------------------------------------
struct ListenerEntry {
    ConnectionId id = kInvalidConnectionId;
    std::function<void(const void*)> invoke; // takes address of event payload
};

// -----------------------------------------------------------------------------
// EventBus
// -----------------------------------------------------------------------------
class EventBus {
public:
    static EventBus& instance();

    EventBus();
    ~EventBus();

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // ----- Subscribe (typed) --------------------------------------------------
    template<typename TEvent>
    ConnectionId subscribe(std::function<void(const TEvent&)> handler) {
        EventTypeId tid = _typeIdOf<TEvent>();
        ConnectionId id = _nextConnectionId.fetch_add(1, std::memory_order_relaxed);

        auto entry = std::make_unique<ListenerEntry>();
        entry->id = id;
        entry->invoke = [h = std::move(handler)](const void* p) {
            h(*static_cast<const TEvent*>(p));
        };

        std::lock_guard<std::mutex> lk(_bucketsMutex);
        auto& bucket = _bucketFor(tid);
        if (_deliveryDepth > 0) {
            std::lock_guard<std::mutex> mlk(_mutationMutex);
            _pendingAdds.push_back({tid, std::move(entry)});
        } else {
            bucket.listeners.push_back(std::move(entry));
        }
        return id;
    }

    // Type-erased subscribe (for callers that already have an EventTypeId).
    ConnectionId subscribeById(EventTypeId tid,
                               std::function<void(const void*)> erased);

    // Remove a listener by its opaque id. Idempotent; safe during delivery.
    void unsubscribe(ConnectionId id);

    // ----- Synchronous emit (main thread only, design §2.2) -------------------
    template<typename TEvent>
    void emit(const TEvent& event) {
        EventTypeId tid = _typeIdOf<TEvent>();
        _emit(tid, &event);
    }

    // ----- Deferred post (any thread, design §2.2) ----------------------------
    template<typename TEvent>
    void post(TEvent event) {
        EventTypeId tid = _typeIdOf<TEvent>();
        EventPriority prio = EventPriority::Normal;
        if constexpr (requires { TEvent::kPriority; }) {
            prio = TEvent::kPriority;
        }
        // Serialize payload into bytes; queued vector owns its memory.
        std::vector<uint8_t> bytes(sizeof(TEvent));
        std::memcpy(bytes.data(), &event, sizeof(TEvent));
        _post(tid, prio, std::move(bytes));
        (void)event; // event was memcpy'd; prevent UB from moved-from copies.
    }

    // ----- Pump (main thread only, design §2.2) ------------------------------
    void pump();

    // ----- Diagnostics --------------------------------------------------------
    size_t listenerCount(EventTypeId tid) const;
    size_t pendingCount() const;
    bool isMainThread() const;

    // Internal accessors used by ScopedConnection. Not public API.
    void _unsubscribeRaw(ConnectionId id);

private:
    struct ListenerBucket {
        std::vector<std::unique_ptr<ListenerEntry>> listeners;
    };

    void _emit(EventTypeId tid, const void* payload);
    void _post(EventTypeId tid, EventPriority prio, std::vector<uint8_t> bytes);

    template<typename TEvent>
    EventTypeId _typeIdOf() {
        if constexpr (requires { TEvent::kTypeId; }) {
            return TEvent::kTypeId;
        } else {
            EventTypeId tid = 0;
            std::type_index tag(typeid(TEvent));
            {
                std::lock_guard<std::mutex> lk(_bucketsMutex);
                auto it = _typeIdByTypeInfo.find(tag);
                if (it != _typeIdByTypeInfo.end()) {
                    tid = it->second;
                } else {
                    tid = _nextRuntimeTypeId.fetch_add(1, std::memory_order_relaxed);
                    _typeIdByTypeInfo.emplace(tag, tid);
                }
            }
            return tid;
        }
    }

    void _flushPendingMutations();
    ListenerBucket& _bucketFor(EventTypeId tid);

private:
    mutable std::mutex _bucketsMutex;
    std::unordered_map<EventTypeId, std::unique_ptr<ListenerBucket>> _buckets;
    // Side map for runtime-assigned ids keyed by std::type_index.
    std::unordered_map<std::type_index, EventTypeId> _typeIdByTypeInfo;

    // Deferred queue: one vector per priority bucket. Protected by _postMutex
    // so workers may post without holding the listener-bucket mutex.
    mutable std::mutex _postMutex;
    std::vector<std::vector<std::vector<uint8_t>>> _queues; // [priority] -> bytes

    // Mutation backlog: subscribe / unsubscribe / new-bucket registrations
    // made during delivery. Applied by _flushPendingMutations().
    mutable std::mutex _mutationMutex;
    struct PendingAdd {
        EventTypeId tid;
        std::unique_ptr<ListenerEntry> entry;
    };
    struct PendingRemove {
        EventTypeId tid;
        ConnectionId id;
    };
    std::vector<PendingAdd>    _pendingAdds;
    std::vector<PendingRemove> _pendingRemoves;

    // Reentrancy depth tracking: nested emit/pump bump this; mutations are
    // deferred until depth returns to 0.
    int _deliveryDepth = 0;

    // Main thread identity (design §2.2 debug asserts).
    std::atomic<bool> _mainThreadCaptured{false};
    std::atomic<std::thread::id> _mainThreadId{};

    // Counter for unique connection ids.
    std::atomic<uint64_t> _nextConnectionId{1};

    // Counter for runtime-assigned type ids (above kHandAssignedTypeIdMax).
    std::atomic<uint32_t> _nextRuntimeTypeId{kHandAssignedTypeIdMax + 1};

    friend class ScopedConnection;
};

} // namespace ayt::event