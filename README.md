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
| **AYGameLoop** | Frame / `SimTickEvent` | Loop tick | See `AYGameLoopEvents.h` |
| **AYScript** (INT-04) | Alias emit of catalog types | Logia `event.emit` | Does **not** route S4.1 `signal`/`emit` |

## Catalog only (not production-posted yet)

| Header | Intent |
|--------|--------|
| `PhysicsEvents.h` | Collision bridge (E-3) — pending |
| `DeviceAxisEvent` | Explicitly **not** on the bus (continuous input) |
| `ScriptTestEvents.h` | INT-04 unit tests |

## Thread rule

- `post` — any thread; delivered on main at next `pump`
- `emit` / `subscribe` / `pump` — main thread

GameLoop already `pump`s the process bus each frame.
