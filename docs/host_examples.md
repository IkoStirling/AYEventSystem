# AYEventSystem — Host Integration Examples (Phase 3 §8)

Thin reference patterns for cross-module event publishing. These examples show
how `AYDevice`, `AYResource`, and `AYTask` modules wire their notifications
through `ayt::event::EventBus::instance()`. The bus itself does not bind
handlers to layer-specific pools (design §1.3 anti-goal); it is a typed
deferred + sync channel with main-thread delivery.

## Thread model reminder

| API            | Allowed threads      | Listener runs on |
|----------------|----------------------|------------------|
| `post<T>`      | any                  | main, on next `pump()` |
| `emit<T>`      | main only (debug assert) | main, immediate |
| `pump`         | main only            | main             |
| `subscribe`    | main preferred       | n/a              |

Recommended host frame order (illustrative):

1. Device poll → discrete actions may `emit` (main) or `post` (any thread).
2. `EventBus::pump()` — drain deferred queue before simulation/UI that depends on it.
3. GameLoop simulation.
4. UI update / render.

---

## A. AYDevice — discrete actions / button edges

**On the bus**: discrete button edges (`Jump pressed`, `Pause pressed`).
**Off the bus**: continuous axes / per-frame mouse deltas (those stay on the
input → UI/game direct path, design §6 "AYDevice" row).

```cpp
// AYDevice module (e.g. AYKeyboardDevice.cpp / AYGamepadDevice.cpp)
namespace ayt::device {

void KeyboardDevice::onEdge(int actionId, bool pressed) {
    auto& bus = ayt::event::EventBus::instance();
    bus.post(DeviceActionEvent{ actionId, pressed });
    // Or: bus.emit(...) if you want synchronous fan-out on the main thread.
}

// Listener in the host / game module
bus.subscribe<DeviceActionEvent>([&](const DeviceActionEvent& e) {
    if (e.actionId == kActionJump && e.pressed) player.jump();
});
} // namespace
```

The device module picks the priority — `DeviceActionEvent::kPriority = High`
so it drains before gameplay audio cues. Window/system events should be
`Critical` (e.g. shutdown, fatal).

## B. AYResource — worker finish → main thread notifies dependents

**On the bus**: resource load completion / progress / failure.
**Off the bus**: streaming bytes / IO hot path.

```cpp
// AYResource worker (running on a job / IO thread)
namespace ayt::resource {

void ResourceLoader::onComplete(uint64_t handle, int refCount) {
    auto& bus = ayt::event::EventBus::instance();
    bus.post(ResourceLoadCompleteEvent{ handle, refCount });
    // For streaming progress with many intermediate updates, use coalesce:
    //   bus.post_coalesce(handle, ResourceProgressEvent{ handle, pct });
    // → only the latest progress per handle survives until pump.
}

// Dependents in the main thread listen for completion
bus.subscribe<ResourceLoadCompleteEvent>(
    [&](const ResourceLoadCompleteEvent& e) {
        assetRegistry.bind(e.handle, e.refCount);
        ui.refresh(); // safe — main thread
    });
} // namespace
```

`ResourceLoadCompleteEvent::kPriority = Low` — gameplay frames should not
stall on resource readiness notifications; coalesce on `handle` so progress
events collapse per-resource.

## C. AYTask — job completion callback → bus

```cpp
// AYTask scheduler integration
namespace ayt::task {

void TaskScheduler::onTaskComplete(uint64_t taskId, bool ok) {
    auto& bus = ayt::event::EventBus::instance();
    bus.post(TaskCompleteEvent{ taskId, ok });
}

// In a job-system worker, after a heavy compute:
void HeavyJob::run() {
    Result r = compute();
    ayt::task::TaskScheduler::onTaskComplete(this->id, r.success);
}

// In the game / UI module, on the main thread:
bus.subscribe<TaskCompleteEvent>([&](const TaskCompleteEvent& e) {
    if (e.ok) {
        gameplay.handleResult(e.taskId);
    } else {
        log.error("task %llu failed", (unsigned long long)e.taskId);
    }
});
```

This satisfies the design §5 contract:

> When results must reach game/UI/renderer state → worker (or completing
> task) calls `EventBus::post(...)`. Main loop calls `EventBus::pump()`;
> listeners mutate engine state on the main thread.

## D. Script INT-04 — string aliases

Logia's ambient `event.emit("window_resize", w, h)` / `event.subscribe(...)`
(shipped in AYScript `AYLogiaEventBridge`) has no compile-time EventTypeId.
Use the alias map:

```cpp
// At host bootstrap (also done inside installLogiaEventAmbient):
auto& bus = ayt::event::EventBus::instance();
bus.registerAlias<WindowResizeEvent>("window_resize");
bus.registerAlias<WindowCloseEvent>("window_close");
bus.registerAlias<ResourceLoadCompleteEvent>("resource_ready");
bus.registerAlias<ScriptTestPingEvent>("script_test_ping");

// C++ listener:
bus.subscribeByAlias<WindowResizeEvent>("window_resize",
    [](const WindowResizeEvent& e) { /* … */ });

// Logia:
//   event.subscribe("window_resize", on_resize)
//   event.emit("window_resize", 1280, 720)
```

If the alias was never registered, `emitByAlias` / `postByAlias` return
`false` — Script bridge logs and skips (do not crash). Unknown
`event.subscribe` returns connection id `0`.

## E. Shutdown — `unsubscribeAll()`

At game shutdown, drop every listener in one shot:

```cpp
void GameHost::shutdown() {
    auto& bus = ayt::event::EventBus::instance();
    bus.unsubscribeAll();   // Phase 3 §8 — single-call listener wipe
    // bus itself stays alive (singleton); next session can re-subscribe.
}
```

Do NOT call `unsubscribeAll` while a `pump` or `emit` is in flight; use it
between frames or during teardown.