#pragma once
// AYEventSystem/EventPriority.h
//
// Drain-order priority for deferred events (post + pump).
// Synchronous emit ignores this enum; it only affects pump bucket order.
//
// Per design §3.1: not for listener ordering, not for thread routing.
// Per design §8.1: do not mix with listener-priority.

#include <cstdint>

namespace ayt::event
{

using EventTypeId = uint32_t;

enum class EventPriority : uint8_t {
    Critical = 0, // shutdown / fatal — drain first
    High     = 1, // window / discrete input actions
    Normal   = 2, // gameplay / audio cues (default)
    Low      = 3, // resource / background notifications
    Count    = 4
};

// Compile-time / runtime fallback for events without a hand-assigned id.
// User code should prefer a per-module range kTypeId (see design §3.2).
// Bus-assigned ids start above 0x8000'0000 to leave room for module ranges.
constexpr EventTypeId kHandAssignedTypeIdMax = 0x7FFF'FFFFu;

} // namespace ayt::event