#pragma once
// AYEventSystem/SubscriptionScope.h
//
// Explicit lifetime container for a group of EventBus subscriptions.
// The destructor deliberately does not touch the bus: owners must call
// disconnect() while the bus is alive and on a thread allowed to unsubscribe.

#include <AYEventSystem/EventBus.h>

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace ayt::event
{

class SubscriptionScope {
public:
    SubscriptionScope() = default;

    SubscriptionScope(SubscriptionScope&& other) noexcept;
    SubscriptionScope& operator=(SubscriptionScope&& other) noexcept;
    SubscriptionScope(const SubscriptionScope&) = delete;
    SubscriptionScope& operator=(const SubscriptionScope&) = delete;

    // Explicit disconnect avoids reaching into a process-wide bus during
    // static destruction when owner and bus destruction order is unknown.
    ~SubscriptionScope() = default;

    template <typename EventType>
    ConnectionId subscribe(std::function<void(const EventType&)> listener)
    {
        const ConnectionId id =
            EventBus::instance().subscribe<EventType>(std::move(listener));
        _ids.push_back(id);
        return id;
    }

    void disconnect();

    [[nodiscard]] std::size_t size() const noexcept { return _ids.size(); }
    [[nodiscard]] bool empty() const noexcept { return _ids.empty(); }

    // Transfer all tracked subscriptions to a child scope. The source remains
    // usable and empty.
    [[nodiscard]] SubscriptionScope detach();

private:
    std::vector<ConnectionId> _ids;
};

// This type replaces the former Application-owned subscription scope in
// public subsystem layouts. Keep its representation vector-only so that the
// migration does not change the size or alignment of those subsystem types.
static_assert(sizeof(SubscriptionScope) ==
              sizeof(std::vector<ConnectionId>));
static_assert(alignof(SubscriptionScope) ==
              alignof(std::vector<ConnectionId>));

} // namespace ayt::event
