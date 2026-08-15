#pragma once
// DeviceEvents.h — shared POD events (device range 0x0003'xxxx)
//
// Catalog only: modules may emit these later. INT-04 / Host may registerAlias
// without requiring Device production wiring in this PR.

#include <ayevent/EventPriority.h>

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
