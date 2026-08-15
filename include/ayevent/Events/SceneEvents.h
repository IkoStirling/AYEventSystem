#pragma once
// SceneEvents.h — Scene lifecycle notifications (module range 0x0002'xxxx)
//
// Emitted by AYApplication's SceneLifecycleEventBridge when SceneManager
// reports beginPlay / endPlay / setCurrent. Subscribe via host->eventBus()
// (or EventBus::instance() outside Host scope).

#include <ayevent/EventPriority.h>

namespace ayt::scene
{
class Scene;
}

namespace ayt::event
{

struct SceneCurrentChangedEvent {
    static constexpr EventTypeId kTypeId = 0x0002'0001;
    static constexpr EventPriority kPriority = EventPriority::High;

    ayt::scene::Scene* current = nullptr;
};

struct SceneBeginPlayEvent {
    static constexpr EventTypeId kTypeId = 0x0002'0002;
    static constexpr EventPriority kPriority = EventPriority::High;

    ayt::scene::Scene* play = nullptr;
};

struct SceneEndPlayEvent {
    static constexpr EventTypeId kTypeId = 0x0002'0003;
    static constexpr EventPriority kPriority = EventPriority::High;

    /// Edit Scene restored as current (may be nullptr).
    ayt::scene::Scene* edit = nullptr;
};

} // namespace ayt::event
