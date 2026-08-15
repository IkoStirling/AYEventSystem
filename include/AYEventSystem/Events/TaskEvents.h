#pragma once
// AYEventSystem/Events/AYEventSystem/Events/AYEventSystem/Events/TaskEvents.h — shared POD events (task range 0x0007'xxxx)
//
// Posted by Application's Task→EventBus completion hook (AYTask stays
// EventSystem-free; see AYTask/TaskCompletionHook.h).

#include <AYEventSystem/EventPriority.h>

#include <cstdint>

namespace ayt::event
{

struct TaskCompleteEvent {
    static constexpr EventTypeId   kTypeId   = 0x0007'0001;
    static constexpr EventPriority kPriority = EventPriority::Normal;

    /// Opaque task identity (pointer bits of ITask* at completion time).
    uint64_t taskId = 0;
    bool     ok     = true;  // false when cancelled
};

} // namespace ayt::event
