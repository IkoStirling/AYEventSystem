#pragma once
// AYEventSystem/Events/AYEventSystem/Events/AYEventSystem/Events/AYEventSystem/Events/WindowEvents.h
//
// Optional shared POD events (design §7 — Events/ subdir). Grow over time.
// All events here use hand-assigned kTypeId per module range
// (window: 0x0001'xxxx — see design §3.2).

#include <AYEventSystem/EventPriority.h>

namespace ayt::event
{

struct WindowResizeEvent {
    static constexpr EventTypeId   kTypeId   = 0x0001'0001;
    static constexpr EventPriority kPriority = EventPriority::High;

    int width  = 0;
    int height = 0;
};

struct WindowCloseEvent {
    static constexpr EventTypeId   kTypeId   = 0x0001'0002;
    static constexpr EventPriority kPriority = EventPriority::Critical;

    // No payload — marker event for "user requested window close".
};

} // namespace ayt::event