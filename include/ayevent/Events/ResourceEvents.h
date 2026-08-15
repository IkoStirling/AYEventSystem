#pragma once
// ResourceEvents.h — shared POD events (resource range 0x0004'xxxx)
//
// Produced by AsyncLoader on worker completion (post → main pump):
//   - ResourceLoadCompleteEvent (ok=true on success; ok=false terminal miss)
//   - ResourceLoadFailedEvent (errorCode 1=cancelled, 2=load failed)
// handle = hash(path). INT-04 alias "resource_ready" maps Complete.

#include <ayevent/EventPriority.h>

#include <cstdint>

namespace ayt::event
{

struct ResourceLoadCompleteEvent {
    static constexpr EventTypeId   kTypeId   = 0x0004'0001;
    static constexpr EventPriority kPriority = EventPriority::Low;

    uint64_t handle   = 0;
    int      refCount = 0;
    bool     ok       = true;
};

struct ResourceLoadFailedEvent {
    static constexpr EventTypeId   kTypeId   = 0x0004'0002;
    static constexpr EventPriority kPriority = EventPriority::High;

    uint64_t    handle = 0;
    // Stable short code; not a heap string (bus payload should stay POD-friendly).
    int         errorCode = 0;
};

} // namespace ayt::event
