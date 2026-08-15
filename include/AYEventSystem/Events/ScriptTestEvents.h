#pragma once
// AYEventSystem/Events/AYEventSystem/Events/AYEventSystem/Events/ScriptTestEvents.h — INT-04 test / script ping (script range 0x0006'xxxx)

#include <AYEventSystem/EventPriority.h>

#include <cstdint>

namespace ayt::event
{

/// Minimal typed event for INT-04 ambient round-trip tests.
struct ScriptTestPingEvent {
    static constexpr EventTypeId   kTypeId   = 0x0006'0001;
    static constexpr EventPriority kPriority = EventPriority::Normal;

    int value = 0;
};

} // namespace ayt::event
