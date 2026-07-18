// AYEventSystem/src/AYEventBus.cpp
//
// Implementation per design.md §3-§4 (Phase 1).

#include <ayevent/EventBus.h>

#include <algorithm>
#include <cassert>
#include <unordered_set>

namespace ayt::event
{

// -----------------------------------------------------------------------------
// Singleton (process default). Tests construct local EventBus instances.
// -----------------------------------------------------------------------------
EventBus& EventBus::instance() {
    static EventBus s_instance;
    return s_instance;
}

EventBus::EventBus() {
    _queues.resize(static_cast<size_t>(EventPriority::Count));
}

EventBus::~EventBus() = default;

// -----------------------------------------------------------------------------
// Diagnostics
// -----------------------------------------------------------------------------
size_t EventBus::listenerCount(EventTypeId tid) const {
    std::lock_guard<std::mutex> lk(_bucketsMutex);
    auto it = _buckets.find(tid);
    if (it == _buckets.end()) return 0;
    return it->second->listeners.size();
}

size_t EventBus::pendingCount() const {
    std::lock_guard<std::mutex> lk(_postMutex);
    size_t n = 0;
    for (auto& q : _queues) n += q.size();
    return n;
}

bool EventBus::isMainThread() const {
    if (!_mainThreadCaptured.load(std::memory_order_acquire)) return false;
    return std::this_thread::get_id() == _mainThreadId.load(std::memory_order_acquire);
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
EventBus::ListenerBucket& EventBus::_bucketFor(EventTypeId tid) {
    auto it = _buckets.find(tid);
    if (it == _buckets.end()) {
        auto bucket = std::make_unique<ListenerBucket>();
        auto& ref = *bucket;
        _buckets.emplace(tid, std::move(bucket));
        return ref;
    }
    return *it->second;
}

// -----------------------------------------------------------------------------
// Type-erased subscribe (id already known; for advanced callers).
// -----------------------------------------------------------------------------
ConnectionId EventBus::subscribeById(EventTypeId tid,
                                     std::function<void(const void*)> erased) {
    ConnectionId id = _nextConnectionId.fetch_add(1, std::memory_order_relaxed);
    auto entry = std::make_unique<ListenerEntry>();
    entry->id = id;
    entry->invoke = std::move(erased);

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

// -----------------------------------------------------------------------------
// Unsubscribe by id. Idempotent. Safe during emit/pump delivery.
// -----------------------------------------------------------------------------
void EventBus::unsubscribe(ConnectionId id) {
    if (id == kInvalidConnectionId) return;

    bool inDelivery = false;
    {
        // Peek delivery depth under buckets lock for consistency with other ops.
        std::lock_guard<std::mutex> lk(_bucketsMutex);
        inDelivery = (_deliveryDepth > 0);
    }
    if (inDelivery) {
        // We don't know which type owns this id without searching; defer.
        // The deferred remove runs against every bucket on flush.
        std::lock_guard<std::mutex> mlk(_mutationMutex);
        _pendingRemoves.push_back({0 /* sentinel: search all */, id});
        return;
    }
    _unsubscribeRaw(id);
}

void EventBus::_unsubscribeRaw(ConnectionId id) {
    if (id == kInvalidConnectionId) return;
    std::lock_guard<std::mutex> lk(_bucketsMutex);
    for (auto& kv : _buckets) {
        auto& bucket = *kv.second;
        auto it = std::find_if(bucket.listeners.begin(), bucket.listeners.end(),
            [id](const std::unique_ptr<ListenerEntry>& e) { return e->id == id; });
        if (it != bucket.listeners.end()) {
            bucket.listeners.erase(it);
            return;
        }
    }
    // Also try pending adds: a subscribe during emit may have pushed the id
    // there. Drain into a snapshot and look for it.
    std::lock_guard<std::mutex> mlk(_mutationMutex);
    for (auto& a : _pendingAdds) {
        if (a.entry && a.entry->id == id) {
            a.entry.reset(); // mark for removal on flush
            return;
        }
    }
}

// -----------------------------------------------------------------------------
// Synchronous emit
// -----------------------------------------------------------------------------
void EventBus::_emit(EventTypeId tid, const void* payload) {
    // Capture main-thread identity on first emit (lazily).
    if (!_mainThreadCaptured.load(std::memory_order_acquire)) {
        _mainThreadId.store(std::this_thread::get_id(), std::memory_order_release);
        _mainThreadCaptured.store(true, std::memory_order_release);
    }
#ifndef NDEBUG
    assert(_mainThreadId.load(std::memory_order_acquire) == std::this_thread::get_id()
           && "EventBus::emit called off main thread");
#endif

    // Snapshot listeners under lock; release lock before invoking.
    std::vector<ListenerEntry*> snapshot;
    {
        std::lock_guard<std::mutex> lk(_bucketsMutex);
        auto it = _buckets.find(tid);
        if (it == _buckets.end()) return;
        snapshot.reserve(it->second->listeners.size());
        for (auto& e : it->second->listeners) snapshot.push_back(e.get());
    }

    _deliveryDepth++;
    // Invoke in registration order. Per design §3.5, nested post/emit is
    // allowed; mutations made by handlers are deferred until deliveryDepth
    // returns to zero.
    for (auto* e : snapshot) {
        // A handler may have been unsubscribed concurrently in a multi-thread
        // scenario via pending-removes that flushed; the entry pointer is
        // still valid here because _unsubscribeRaw would only erase entries
        // outside delivery, and we hold the snapshot. (Pending removes are
        // applied in _flushPendingMutations after delivery.)
        e->invoke(payload);
    }
    _deliveryDepth--;
    if (_deliveryDepth == 0) _flushPendingMutations();
}

// -----------------------------------------------------------------------------
// Post — queue for next pump. Safe from any thread.
// -----------------------------------------------------------------------------
void EventBus::_post(EventTypeId tid, EventPriority prio, std::vector<uint8_t> bytes) {
    // Tag the bytes with the type id so pump can route without a typeid lookup.
    struct Envelope {
        EventTypeId tid;
        uint32_t    payloadSize;
        // payload follows contiguously
    };
    size_t idx = static_cast<size_t>(prio);
    if (idx >= _queues.size()) idx = static_cast<size_t>(EventPriority::Normal);

    // Prepend Envelope header.
    std::vector<uint8_t> packet;
    packet.resize(sizeof(Envelope) + bytes.size());
    Envelope env{ tid, static_cast<uint32_t>(bytes.size()) };
    std::memcpy(packet.data(), &env, sizeof(Envelope));
    if (!bytes.empty()) {
        std::memcpy(packet.data() + sizeof(Envelope), bytes.data(), bytes.size());
    }

    std::lock_guard<std::mutex> lk(_postMutex);
    _queues[idx].push_back(std::move(packet));
}

// -----------------------------------------------------------------------------
// Pump — drain Critical→Low, drain nested posts in same call.
// -----------------------------------------------------------------------------
void EventBus::pump() {
    if (!_mainThreadCaptured.load(std::memory_order_acquire)) {
        _mainThreadId.store(std::this_thread::get_id(), std::memory_order_release);
        _mainThreadCaptured.store(true, std::memory_order_release);
    }
#ifndef NDEBUG
    assert(_mainThreadId.load(std::memory_order_acquire) == std::this_thread::get_id()
           && "EventBus::pump called off main thread");
#endif

    // Safety cap to prevent infinite re-entrant posting cycles in tests.
    constexpr int kMaxPumpIterations = 1024;

    for (int iter = 0; iter < kMaxPumpIterations; ++iter) {
        bool anyDrained = false;

        for (size_t prio = 0; prio < _queues.size(); ++prio) {
            std::vector<std::vector<uint8_t>> batch;
            {
                std::lock_guard<std::mutex> lk(_postMutex);
                if (_queues[prio].empty()) continue;
                batch.swap(_queues[prio]);
            }
            anyDrained = true;

            for (auto& packet : batch) {
                if (packet.size() < sizeof(EventTypeId) + sizeof(uint32_t)) continue;
                EventTypeId tid;
                uint32_t payloadSize;
                std::memcpy(&tid, packet.data(), sizeof(EventTypeId));
                std::memcpy(&payloadSize, packet.data() + sizeof(EventTypeId), sizeof(uint32_t));
                const void* payload = packet.data() + sizeof(EventTypeId) + sizeof(uint32_t);
                _emit(tid, payload);
            }
        }
        if (!anyDrained) break;
    }
#ifndef NDEBUG
    // If we exited via cap, at least one bucket had items.
    if (pendingCount() > 0) {
        // Note: this is a soft signal; not assert in production.
        // assert(false && "EventBus::pump hit iteration cap — possible runaway cycle");
    }
#endif
}

// -----------------------------------------------------------------------------
// Apply mutations deferred during delivery.
// Lock order: _bucketsMutex BEFORE _mutationMutex.
// -----------------------------------------------------------------------------
void EventBus::_flushPendingMutations() {
    // Lock order matters: buckets first, then mutation list.
    std::lock_guard<std::mutex> blk(_bucketsMutex);
    std::lock_guard<std::mutex> mlk(_mutationMutex);

    for (auto& add : _pendingAdds) {
        if (!add.entry) continue; // canceled by _unsubscribeRaw
        auto& bucket = _bucketFor(add.tid);
        bucket.listeners.push_back(std::move(add.entry));
    }
    _pendingAdds.clear();

    // Pending removes: tid==0 means "search all buckets" (we don't track
    // owning type for unsubscribes that happened during delivery).
    for (auto& rem : _pendingRemoves) {
        if (rem.tid != 0) {
            auto it = _buckets.find(rem.tid);
            if (it != _buckets.end()) {
                auto& bucket = *it->second;
                auto fit = std::find_if(bucket.listeners.begin(), bucket.listeners.end(),
                    [&rem](const std::unique_ptr<ListenerEntry>& e) { return e->id == rem.id; });
                if (fit != bucket.listeners.end()) bucket.listeners.erase(fit);
            }
        } else {
            for (auto& kv : _buckets) {
                auto& bucket = *kv.second;
                auto fit = std::find_if(bucket.listeners.begin(), bucket.listeners.end(),
                    [&rem](const std::unique_ptr<ListenerEntry>& e) { return e->id == rem.id; });
                if (fit != bucket.listeners.end()) {
                    bucket.listeners.erase(fit);
                    break;
                }
            }
        }
    }
    _pendingRemoves.clear();
}

// -----------------------------------------------------------------------------
// ScopedConnection::reset — defined out-of-line to access bus internals.
// -----------------------------------------------------------------------------
void ScopedConnection::reset() {
    if (_bus && _id != kInvalidConnectionId) {
        _bus->unsubscribe(_id);
        _id = kInvalidConnectionId;
        _bus = nullptr;
    }
}

} // namespace ayt::event