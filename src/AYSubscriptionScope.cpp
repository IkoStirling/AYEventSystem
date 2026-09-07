#include <AYEventSystem/SubscriptionScope.h>

namespace ayt::event
{

SubscriptionScope::SubscriptionScope(SubscriptionScope&& other) noexcept
    : _ids(std::move(other._ids))
{
}

SubscriptionScope& SubscriptionScope::operator=(
    SubscriptionScope&& other) noexcept
{
    if (this != &other) {
        // Preserve the explicit-cleanup contract: move assignment does not
        // unsubscribe connections already held by the target.
        _ids = std::move(other._ids);
    }
    return *this;
}

void SubscriptionScope::disconnect()
{
    auto& bus = EventBus::instance();
    while (!_ids.empty()) {
        bus.unsubscribe(_ids.back());
        _ids.pop_back();
    }
}

SubscriptionScope SubscriptionScope::detach()
{
    SubscriptionScope child;
    child._ids = std::move(_ids);
    return child;
}

} // namespace ayt::event
