#pragma once
// AYEventSystem/Events/AYEventSystem/Events/AYEventSystem/Events/DeviceEvents.h — shared POD events (device range 0x0003'xxxx)
//
// Produced by DeviceSubSystem:
//   - DeviceActionEvent on InputMapping just-pressed / just-released
//     (actionId = FNV-1a of action name). Continuous axes stay off-bus.
// Window resize/close use AYEventSystem/Events/AYEventSystem/Events/AYEventSystem/Events/WindowEvents.h (also posted by DeviceSubSystem).

#include <AYEventSystem/EventPriority.h>

#include <cstdint>

namespace ayt::event
{

struct DeviceActionEvent {
    static constexpr EventTypeId   kTypeId   = 0x0003'0001;
    static constexpr EventPriority kPriority = EventPriority::High;

    int  actionId = 0;
    bool pressed  = false;
};

struct DeviceAxisEvent {
    static constexpr EventTypeId   kTypeId   = 0x0003'0002;
    static constexpr EventPriority kPriority = EventPriority::Normal;

    int   axisId = 0;
    float value  = 0.0f;
};

} // namespace ayt::event
