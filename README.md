# AYEventSystem

Engine-wide typed event bus (`post` / `emit` / `pump`) for cross-module
notifications. Spec: [`design.md`](design.md). Host patterns:
[`docs/host_examples.md`](docs/host_examples.md).

## Who posts engine events (production)

| Producer | Event type(s) | When | Notes |
|----------|---------------|------|-------|
| **AYDevice** (`DeviceSubSystem`) | `WindowResizeEvent`, `WindowCloseEvent` | Each `update()` after `pollEvents` | Already shipped (INT-03) |
| **AYDevice** (`DeviceSubSystem`) | `DeviceActionEvent` | Action just-pressed / just-released | `actionId` = FNV-1a of action name; **axes stay off-bus** |
| **AYResource** (`AsyncLoader`) | `ResourceLoadCompleteEvent`, `ResourceLoadFailedEvent` | Worker load finish | `handle` = `hash(path)`; Failed `errorCode` 1=cancel, 2=load fail |
| **AYApplication** (`bindBuiltinHostServices`) | `TaskCompleteEvent` | Via AYTask completion hook | AYTask itself does **not** link EventSystem |
| **AYApplication** (`SceneLifecycleEventBridge`) | `SceneCurrentChangedEvent`, `SceneBeginPlayEvent`, `SceneEndPlayEvent` | SceneManager lifecycle | AYScene stays EventSystem-free |
| **AYPhysics** (`PhysicsEventBridge` / E-3) | `PhysicsCollisionEvent` | After `fetchResults` in `fixedUpdate` | kind 0/1/2; Box2D sensor 3/4 → 0/2; invalid handles skipped |
| **AYGameLoop** | Frame / `SimTickEvent` | Loop tick | See `AYGameLoop/GameLoopEvents.h` |
| **AYScript** (INT-04 / INT-04b) | Alias emit of catalog types | Logia `event.emit` / EventHandler `run()` | Does **not** route S4.1 `signal`/`emit` |

## Catalog only (not production-posted yet)

| Header | Intent |
|--------|--------|
| `DeviceAxisEvent` | Explicitly **not** on the bus (continuous input) |
| `AYEventSystem/Events/ScriptTestEvents.h` | INT-04 unit tests |

## Logia string aliases (INT-04 / INT-04b)

| Alias | C++ type | Notes |
|-------|----------|-------|
| `window_resize` / `window_close` | Window* | |
| `resource_ready` | `ResourceLoadCompleteEvent` | |
| `script_test_ping` | test | |
| `device_action` | `DeviceActionEvent` | |
| `task_complete` | `TaskCompleteEvent` | |
| `physics_collision` | `PhysicsCollisionEvent` | |
| `scene_current_changed` / `scene_begin_play` / `scene_end_play` | Scene* | **subscribe-only**; Lua gets opaque lightuserdata |

## Thread rule

- `post` — any thread; delivered on main at next `pump`
- `emit` / `subscribe` / `pump` — main thread

GameLoop already `pump`s the process bus each frame.

## Subscription lifetime

`ayt::event::SubscriptionScope` groups typed subscriptions and releases them
with an explicit `disconnect()`. Its destructor intentionally does not access
the bus, so application, renderer, and device owners can tear down listeners
before process-wide static destruction begins.
