#pragma once
// AYEventSystem/Events/AYEventSystem/Events/AYEventSystem/Events/PhysicsEvents.h — shared POD events (physics range 0x0005'xxxx)
//
// Produced by AYPhysics PhysicsEventBridge (E-3) after fetchResults on the
// game thread (PhysicsSubSystem::fixedUpdate). Contact points / impulses
// stay solver-domain only — bus carries body handles + kind.

#include <AYEventSystem/EventPriority.h>

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
