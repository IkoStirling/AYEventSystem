#include <AYEventSystem/EventBus.h>
#include <algorithm>
#include <cassert>
#include <iterator>

namespace ayt::event {
EventBus& EventBus::instance() { static EventBus bus; return bus; }
EventBus::EventBus() : _queues(static_cast<size_t>(EventPriority::Count)) {}
EventBus::~EventBus() = default;
void EventBus::_assertOwner() const {
    assert((!_mainThreadCaptured.load(std::memory_order_acquire) || isMainThread()) && "EventBus owner-thread operation");
}
void EventBus::_captureOwner() {
    if (!_mainThreadCaptured.load(std::memory_order_acquire)) {
        _mainThreadId.store(std::this_thread::get_id(),std::memory_order_release);
        _mainThreadCaptured.store(true,std::memory_order_release);
    }
    _assertOwner();
}
bool EventBus::isMainThread() const {
    return _mainThreadCaptured.load(std::memory_order_acquire) && _mainThreadId.load(std::memory_order_acquire)==std::this_thread::get_id();
}
size_t EventBus::listenerCount(EventTypeId tid) const {
    std::lock_guard lk(_bucketsMutex);
    auto it=_buckets.find(tid); return it==_buckets.end()?0:it->second.size();
}
size_t EventBus::observerCount() const { std::lock_guard lk(_bucketsMutex); return _observers.size(); }
size_t EventBus::pendingCount() const {
    std::lock_guard lk(_postMutex); size_t n=0; for (const auto& q:_queues) n+=q.size(); return n;
}
ConnectionId EventBus::_subscribe(EventTypeId tid,bool observer,EventFilter filter,std::function<void(const EventView&)> fn) {
    _assertOwner();
    if (!fn) return kInvalidConnectionId;
    auto id=_nextConnectionId.fetch_add(1,std::memory_order_relaxed);
    auto entry=std::make_unique<ListenerEntry>(ListenerEntry{id,std::move(filter),std::move(fn)});
    std::lock_guard lk(_bucketsMutex);
    if (_deliveryDepth) _pendingAdds.push_back({tid,observer,std::move(entry)});
    else (observer?_observers:_buckets[tid]).push_back(std::move(entry));
    return id;
}
ConnectionId EventBus::subscribeById(EventTypeId tid,std::function<void(const void*)> fn) {
    if (!fn) return kInvalidConnectionId;
    return _subscribe(tid,false,{},[h=std::move(fn)](const EventView& v){h(v.payload);});
}
ConnectionId EventBus::observe(EventFilter filter,std::function<void(const EventView&)> fn) {
    return _subscribe(0,true,std::move(filter),std::move(fn));
}
void EventBus::unsubscribe(ConnectionId id) {
    _assertOwner();
    if (!id) return;
    { std::lock_guard lk(_bucketsMutex); if (_deliveryDepth) { _pendingRemoves.push_back(id); return; } }
    _unsubscribeRaw(id);
}
void EventBus::_unsubscribeRaw(ConnectionId id) {
    std::lock_guard lk(_bucketsMutex);
    auto remove=[id](auto& entries) { std::erase_if(entries,[id](const auto& e){return e->id==id;}); };
    remove(_observers); for (auto& [tid,bucket]:_buckets) remove(bucket);
    std::erase_if(_pendingAdds,[id](const auto& a){return a.entry->id==id;});
}
void EventBus::unsubscribeAll() {
    _assertOwner(); std::lock_guard lk(_bucketsMutex);
    assert(_deliveryDepth==0 && "unsubscribeAll during delivery");
    _buckets.clear(); _observers.clear(); _pendingAdds.clear(); _pendingRemoves.clear();
}
EventTypeId EventBus::aliasToTypeId(std::string_view alias) const {
    std::lock_guard lk(_aliasMutex); auto it=_aliases.find(std::string(alias)); return it==_aliases.end()?0:it->second;
}
void EventBus::_emit(const EventView& view) {
    _captureOwner();
    std::vector<ListenerEntry*> snapshot;
    {
        std::lock_guard lk(_bucketsMutex);
        auto add=[&](auto& bucket){for (auto& e:bucket) snapshot.push_back(e.get());};
        auto it=_buckets.find(view.typeId); if (it!=_buckets.end()) add(it->second);
        add(_observers);
        if (snapshot.empty()) return;
        ++_deliveryDepth;
    }
    try {
        bool delivered=false;
        for (auto* e:snapshot) if (e->filter.matches(view.metadata)) {e->invoke(view); delivered=true;}
        if (delivered) _totalDelivered.fetch_add(1,std::memory_order_relaxed);
    } catch (...) { _finishDelivery(); throw; }
    _finishDelivery();
}
void EventBus::_finishDelivery() {
    bool flush;
    {std::lock_guard lk(_bucketsMutex); flush=--_deliveryDepth==0;}
    if (flush) _flushPendingMutations();
}
void EventBus::_flushPendingMutations() {
    std::lock_guard lk(_bucketsMutex);
    for (auto& a:_pendingAdds) (a.observer?_observers:_buckets[a.tid]).push_back(std::move(a.entry));
    _pendingAdds.clear();
    for (auto id:_pendingRemoves) {
        auto remove=[id](auto& entries){std::erase_if(entries,[id](const auto& e){return e->id==id;});};
        remove(_observers); for (auto& [tid,bucket]:_buckets) remove(bucket);
    }
    _pendingRemoves.clear();
}
void EventBus::_post(EventPriority priority,Packet packet) {
    size_t idx=static_cast<size_t>(priority);
    assert(idx<_queues.size() && "Invalid event priority");
    if (idx>=_queues.size()) idx=static_cast<size_t>(EventPriority::Normal);
    std::lock_guard lk(_postMutex);
    auto& bucket=_queues[idx];
    if (packet.coalesce) for (auto& old:bucket) {
        if (old.coalesce && old.tid==packet.tid && old.key==packet.key &&
            old.metadata.channel==packet.metadata.channel && old.metadata.scope==packet.metadata.scope && old.metadata.source==packet.metadata.source) {
            old=std::move(packet); _totalCoalesced.fetch_add(1,std::memory_order_relaxed); return;
        }
    }
    bucket.push_back(std::move(packet)); _totalPosted.fetch_add(1,std::memory_order_relaxed);
    uint64_t depth=0; for (const auto& q:_queues) depth+=q.size();
    auto peak=_peakQueueDepth.load(std::memory_order_relaxed);
    while (depth>peak && !_peakQueueDepth.compare_exchange_weak(peak,depth,std::memory_order_relaxed)) {}
}
void EventBus::pump() {
    _captureOwner();
    for (int iteration=0;iteration<1024;++iteration) {
        bool drained=false;
        for (size_t priority=0;priority<_queues.size();++priority) {
            std::vector<Packet> batch;
            { std::lock_guard lk(_postMutex); batch.swap(_queues[priority]); }
            if (!batch.empty()) drained=true;
            for (size_t i=0;i<batch.size();++i) {
                const auto& p=batch[i];
                try { _emit({p.tid,p.metadata,p.payloadType,p.payload.get()}); }
                catch (...) {
                    // Failed delivery is not replayed; preserve all unstarted
                    // events ahead of posts made by the failing callback.
                    std::lock_guard lk(_postMutex);
                    auto& q=_queues[priority];
                    q.insert(q.begin(),std::make_move_iterator(batch.begin()+i+1),std::make_move_iterator(batch.end()));
                    uint64_t depth=0; for (const auto& pending:_queues) depth+=pending.size();
                    auto peak=_peakQueueDepth.load(std::memory_order_relaxed);
                    while (depth>peak && !_peakQueueDepth.compare_exchange_weak(peak,depth,std::memory_order_relaxed)) {}
                    throw;
                }
            }
        }
        if (!drained) break;
    }
}
void ScopedConnection::reset() {
    if (_bus && _id!=kInvalidConnectionId) {_bus->unsubscribe(_id); _id=kInvalidConnectionId; _bus=nullptr;}
}
}
