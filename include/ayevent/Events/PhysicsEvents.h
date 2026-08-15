#pragma once
// PhysicsEvents.h — shared POD events (physics range 0x0005'xxxx)
//
// Catalog only: PhysicsEventBridge (E-3) may emit later. Host/INT-04 may
// registerAlias without requiring Physics production wiring in this PR.

#include <ayevent/EventPriority.h>

#include <cstdint>

namespace ayt::event
{

struct PhysicsCollisionEvent {
    static constexpr EventTypeId   kTypeId   = 0x0005'0001;
    static constexpr EventPriority kPriority = EventPriority::Normal;

    uint32_t bodyA = 0;
    uint32_t bodyB = 0;
    /// 0=enter, 1=stay, 2=exit (mirrors PhysCollisionEvent kind).
    uint8_t  kind  = 0;
};

} // namespace ayt::event
