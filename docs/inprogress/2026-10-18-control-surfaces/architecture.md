# Control surfaces: architecture

| | |
|---|---|
| **Date** | 2026-10-18 |
| **Status** | Design, for review |
| **Requirements** | [requirements.md](requirements.md) |
| **Extends** | [controller manager architecture](../2026-10-17-controller-manager/architecture.md) (§3 library, §7 pipeline, §9 instances, §11 threads) |

## Contents

- [1. Vocabulary](#1-vocabulary)
- [2. Placement](#2-placement)
- [3. The core action registry](#3-the-core-action-registry)
- [4. Gates: who may do what](#4-gates-who-may-do-what)
- [5. The router's part](#5-the-routers-part)
- [6. Contexts and the followed instance](#6-contexts-and-the-followed-instance)
- [7. The TTD published cursor and readouts](#7-the-ttd-published-cursor-and-readouts)
- [8. The surface hub](#8-the-surface-hub)
- [9. Haptic feedback](#9-haptic-feedback)
- [10. Flows](#10-flows)
- [11. Threads and lifetime](#11-threads-and-lifetime)
- [12. Determinism](#12-determinism)
- [13. Code placement](#13-code-placement)

## 1. Vocabulary

| Term | Meaning |
|:--|:--|
| Control surface | A device whose controls drive host actions and that may give feedback: a Surface Dial (host device), a Stream Deck behind `streamdeck-bridge` (companion process). |
| Action | A host command by id, with a value kind, a scope rule, availability with a reason. |
| Actions worker | One thread per instance that runs action executors in order. |
| Context | The instance's current mode (`ttd`, `ttd.timecode`, `debug.disasm`), one string. |
| Context layer | A layer of a debug profile named `@<context>`, active while the context matches. |
| Readout | A value a companion subscribes to (`ttd.position`, `run.state`), pushed on change. |
| Mark | A point on the recorded timeline a seek crossed (second, 10 s, edge, barrier, bookmark). |

## 2. Placement

```mermaid
flowchart TB
    subgraph QT["unreal-qt"]
        HC["HostControllers<br/>owns HostInputService · bridges router ⇄ registry"]
        GUIACT["GUI actions (dbg.open)"]
    end
    subgraph LIB["core/controllers (headers of core only, does not link it)"]
        BE["IOHID backend<br/>+ SurfaceDial driver (writer thread)"]
        RT["InputRouter<br/>Encoder · context layers · own value type"]
    end
    subgraph CORE["core/src"]
        AR["emulator/actions/ActionRegistry<br/>+ ActionsWorker per instance"]
        CTX["emulator/actions/ContextState<br/>NC_SURFACE_CONTEXT"]
        RC["emulator/execution/RunControl"]
        TTD["debugger/ttd/TTDControl<br/>+ published cursor (ITimeTravelHooks)"]
    end
    subgraph AUTO["core/automation/webapi"]
        HUB["SurfaceHub · topic surface of /api/v1/ws"]
        REST["/api/v1/controllers/actions"]
    end
    BE --> RT
    RT -- "ActionEvent (library value)" --> HC
    HC -- "Submit (core value, origin host:…)" --> AR
    GUIACT --> AR
    REST --> AR
    HUB --> AR
    AR --> RC & TTD & CTX
    CTX -- "NC_SURFACE_CONTEXT" --> HC
    HC -- "SetContext(instance, ctx)" --> RT
    CTX --> HUB
    TTD --> HUB
    AR -- "result" --> HC
    HC -- "marks → Output" --> RT
    RT -- "haptic · steps" --> BE
```

- The registry, the context state, `RunControl` and the cursor are core, built with or without `ENABLE_AUTOMATION`,
  present headless.
- The controllers library keeps its rule: core headers only, no link to core (`core/controllers/CMakeLists.txt`),
  no Qt. It gets the context pushed by its owner (`InputRouter::SetContext`) and emits its own `ActionEvent` value;
  the owner (today `HostControllers`) converts to the core's `ActionValue` and back.
- `HostInputService` keeps its owner, the Qt client. Headless instances have actions, contexts, readouts and the
  protocol, not the Dial (Q-3).

## 3. The core action registry

```cpp
namespace actions {

enum class ValueKind : uint8_t { Press, Delta, Text };

struct ActionValue
{
    ValueKind kind = ValueKind::Press;
    bool pressed = true;              // Press: down / up (held actions see both)
    double delta = 0;                 // Delta: signed steps, fractional allowed
    double rate = 0;                  // Delta: steps per second
    std::string text;                 // Text: ctx.set
    StateNode params;                 // ttd.seek_by: unit, min, max, rate_lo, rate_hi
};

struct ActionOrigin
{
    execution::Origin origin;         // Gui, WebApi, Mcp, Cli, Lua, Python, Companion, + HostDevice (new)
    std::string identity;             // the client / session id for automation, the cid for a companion,
                                      // the HostDeviceId for a host device; the op log's source
    std::string session;              // a protocol session id (coalescing never crosses sessions)
};

struct ActionResult
{
    bool ok = false;
    std::string error;                // as the route answers: "Conflict", "Bad Request", "Not Found",
                                      // "Run-control held", "companion_forbidden", "held_by_session", "not_now"
    std::string message;              // one sentence a user can act on
    StateNode body;                   // the route's body; ttd.seek_by adds "marks"
};

struct ActionInfo
{
    std::string id, label, group;
    ValueKind kind = ValueKind::Press;
    bool gui = false;                 // registered by the Qt client; absent headless
};

class ActionRegistry
{
public:
    /// Core actions at core start-up; the Qt client's GUI actions with the handle it keeps until it closes
    RegistrationHandle Register(ActionInfo info, Executor run, Rules rules);
    std::vector<ActionInfo> List() const;
    /// "" when it can run now, else the reason (the gates of §4 included)
    std::string Unavailable(std::string_view id, const std::string& instanceId, const ActionOrigin& origin,
                            const ActionValue& value) const;
    /// Queued on the instance's actions worker; done() is called on that worker
    void Submit(std::string_view id, ActionValue value, std::string instanceId, ActionOrigin origin,
                std::function<void(ActionResult)> done);
};

}  // namespace actions
```

- `Rules` gives per action: the companion scope **as a function of the value and the machine** (`run.pause_toggle`
  pausing needs none, resuming needs `ScopeRun`, as `/pause` is free and `/resume` is not,
  `core/src/emulator/execution/automationgate.cpp`), whether it needs a pause (as `CompanionNeedsPause`: steps,
  instruction steps), whether it changes the machine (for the automation gate), and the availability check.
- **Executors call control objects**:
  - `TTDControl` (seek, steps, bookmarks, record);
  - `RunControl`, new: the logic of the lifecycle and debug routes moved out of them (pause through
    `EmulatorManager::PauseEmulatorWithResult` with its pending / busy outcomes, resume through `ResumeEmulator`
    with the run-control claim check, `core/automation/webapi/src/api/lifecycle_api.cpp`; step in / over / out and
    frame step as `debug_api.cpp` does); the routes call it too;
  - `ContextState`.
  The Qt debugger slots stop being executors; the debugger window follows the machine as it does for WebAPI steps.
- **The worker** runs one executor at a time, in submission order. Executors may block (`TTDControl::Seek` pauses
  the machine and waits up to 1 s, `PauseAndConfirm`); nothing else waits for them.
- **Op-log source**: the worker sets `SourceScope` (`core/src/emulator/coordinator/machineop.h`) from the origin
  around each executor, since the source is thread-local.
- **Coalescing**: a `Delta` merges only into the **last** queued job, and only when id, origin identity, session and
  `params` are equal (the bridge merges into its queue's back the same way, `streamdeck-bridge/src/runtime.cpp`); the
  merged job keeps the higher rate. A merged call's `done` is called with the merged job's result, so every `rid` is
  answered. A `ctx.set` between two deltas therefore stays between them.
- **The companion catalogue** (`core/src/emulator/execution/companionactions.cpp`) stays the scope view of
  `GET /companions/me` (`rewind`, `tape`, `memory`, ...); registry actions name their scope rule, the two agree, no
  aliasing.

### 3.1 As built (S1, 2026-10-20)

`core/src/emulator/actions/actionregistry.h`, `builtinactions.h`, `core/src/emulator/execution/runcontrol.h`.
Differences from the sketch above:

- `ValueKind::Level` (0..1, 0 = released) beside Press, Delta, Text: the analog scrub of the Qt client. `ActionValue`
  carries `level`; `ActionOrigin` adds `client`, `peer` and `lease` (the implicit session's lease) and names the
  carried session `session`.
- `Register` returns a `RegistrationHandle` for every action (the core keeps its own); a newer registration of an id
  replaces the older, whose handle then removes nothing. `ActionRules` has `scope(emulator, value)`, `needsPause`,
  `unavailable(emulator, value)` and the error code of that answer.
- `Statuses(instance, origin)` (one row per action with `available`, `error`, `reason`) instead of a bare `List()`
  with availability: a dry run of the gates that opens no session. `Run()` is Submit plus a bounded wait, for the CLI
  and the scripts.
- `done` is called at once on the calling thread for an unknown action or instance, a value of the wrong kind and the
  release of a Press that is not held; everything else runs on the worker.
- A release that only ends what a press started (a held action's release, a level of 0) passes every gate.
- The worker thread starts with the instance's first action and ends at `EmulatorManager::RemoveEmulator` (queued
  jobs answer "Not Found"; the running one is waited for up to 2 s). Idle instances have no thread. The process's
  registry is never destroyed (the static `EmulatorManager` drops instances in its own destructor at exit).
- Origins `WebApi`, `Mcp`, `Cli` pass the automation gate with the identity of the routes (`webapi|host|client`,
  `cli|peer`); `Lua`, `Python` call `execution::script::Check`; a companion is looked up by id in
  `CompanionRegistry` (a revoked one: `companion_unknown`).
- `RunControl` holds pause, resume, step in / over / out, frame step, run to interrupt / scanline, N scanlines,
  reset, NMI and turbo; `POST /pause`, `/resume`, `/step`, `/stepover`, `/stepout`, `/run_frame`,
  `/run_to_interrupt`, `/run_to_scanline`, `/run_scanlines` call it (same bodies and statuses as before).
- The GUI executors post to the GUI thread and answer at once; the analog scrub's timer submits `ttd.jog_frames`,
  so the GUI thread never waits for a seek.

## 4. Gates: who may do what

Every origin goes through the gate it goes through today on REST, inside the registry:

| Origin | Gate |
|:--|:--|
| WebAPI, MCP (through the WebAPI), CLI, Lua, Python | The automation gate (owner decision 2026-10-11, `automationgate.h`): a changing action opens the implicit run-control session (`EmulatorContext::EnterImplicitRunControl`) for that client, exactly as the REST route would. `/api/v1/controllers/actions/{id}` is outside the gate's `/api/v1/emulator/` prefix today (`automation-webapi.cpp`), so the registry applies it itself. |
| Companion (the surface socket, `POST …/actions/{id}` with a token) | `CheckAction(companion, required, claimed, holder, takeBackHint, needsPause, paused)` with a `MachineView` (`companionregistry.h`): `companion_forbidden`, `held_by_session`, `not_now`. An agent token is refused `companion_not_for_agents`, as on HTTP and the events socket. |
| Host device (the Dial), GUI | The user's own hands, like the GUI: no session is opened; but while another surface holds run control (`IsRunControlClaimed`, a GDB client, an agent session), a changing action is refused "Run-control held", as `/resume` refuses a caller that does not hold control. |

`execution::Origin` (`executionarbiter.h`) gets `HostDevice` so a Dial's pause and resume are attributed in the
arbiter and the op log.

## 5. The router's part

- The library's `ActionEvent` gains a value of its own type (`controllers::ActionValue`: press, delta + rate, text,
  params); Press bindings are unchanged.
- An `Encoder` control bound to a `Delta` action sends one event per router pass with the coalesced delta and the
  speed ([controls-and-feedback.md §2](controls-and-feedback.md#2-encoder)); bound to a `Press` action it fires one
  press per step, at most 16 per pass.
- `InputRouter::SetContext(instance, context)` is called by the owner; the router makes the debug profile's layer
  `@<context>` (or the longest `@<prefix>.*` match) active above `debug` ([profiles.md](profiles.md)).
- Results come back through the owner, which turns marks into `Output(haptic)` for the device that moved
  ([§9](#9-haptic-feedback)).

### 5.1 As built (S3, 2026-10-20)

- `ActionEvent` carries `turn`, `rate` (steps/s) and `params` (the binding's `curve` object, `BindingOptions::params`).
  The router sends one event per control event (the Dial: one per input report), not one per pass; a Press action
  bound to an encoder gets one press per step from the Qt owner (`GuiActions::Trigger`, at most 16).
- `EncoderRate` (`inputrouter.h`): the bridge's rule, with every interval counted as at least 7.5 ms (the shortest BLE
  connection interval) so two reports of one bunch do not read as a spin; the rate counts every turn, bound or not.
  To retune by feel in the owner test.
- Context layers: `SetContext(instance, context)` per instance; a device's instance is its pinned one, else the
  focused one. The context layer goes right above `debug` (a shift layer raised over `debug` stays on top); it is a
  context layer of the device's bindings or of its scope's surface haptics. A change of the layers up releases what
  is held (epoch, stale controls), as a layer switch. A binding cannot raise a context layer with a layer key.
- Feel: the router keeps each device's last `StepsOut` and queues a new one when it changes (after an evaluation, a
  context change, new surface haptics); `Dispatch` sends it to the output sink outside the lock. A device whose
  scope has no surface haptics and never got a feel is never asked. Without a layer feel: clicks off, the steps as
  they were. `Feel(device)` answers the top layer's `LayerHaptics` (the marks' patterns, the interval).
- Q-8: `DeviceMatch::role` (`"role"` in `devices.json`, the Dial `debugger-remote`); `HostInputService::OnDeviceAdded`
  calls `InputRouter::SetDefaultRole`, which `SetRole` overrides (`RoleSet`); the Qt client's former rule (a non-player
  family becomes a remote in `assignPlayers`) is gone. `RemoteByNature` (`profilestore.cpp`) asks the family's role.
  There is no role editor in the dialog yet (U6's Advanced mode), so `SetRole` has no GUI caller besides the window
  keyboard.
- Not done: the TargetState of SD4 (the router follows the focused instance); a Dial on Linux / Windows (no writer:
  `Outputs` 0 there).

## 6. Contexts and the followed instance

- `ContextState` (core, per instance) holds one string, `play` at start. `ctx.set` stores it and posts
  `NC_SURFACE_CONTEXT {instance, context, origin}`.
- Setters: the Stream Deck (a page that names a context), the Qt client (debugger pane focus, the TTD panel), any
  automation plane. The last one wins (Q-5).
- Followers: the Qt owner (pushes it into the router), the surface hub (`ctx` to sessions).
- **The followed instance** is the Stream Deck integration's `TargetState`
  ([targeting.md](../2026-10-19-streamdeck-integration/targeting.md)): the active instance of the window the user
  focused last (headless: the selected instance, else the sole one). Each surface follows it or pins a view or an
  instance (`join.target`, the `pin` op). The Qt client does not set the selected instance on focus: it reports its
  windows as views. (Erratum 2026-10-20: the first version had the Qt client set the selected instance when the
  focused one changed, making the GUI and automation fight over one global.)

### 6.1 As built (S2, 2026-10-20)

- `actions::ContextState` (`core/src/emulator/actions/contextstate.h`), process-wide, keyed by instance id; dropped
  with the instance (`EmulatorManager`). A stored name or the default (Q-7: `ttd` while the TTD cursor says there is
  a history, else `play`); the default follows the history without a notification. Names: lowercase letters, digits,
  `.`, `_`, `-`, starting with a letter, at most 64.
- `ctx.set` (kind text, no scope): the last setter wins (Q-5); `""` returns to the default. `NC_SURFACE_CONTEXT`
  (`SurfaceContextPayload {emulatorId, context, previous, by}`) is posted when the context in force changes, not
  when the same one is set again. `by`: `companion:<id>`, `host-device:<id>`, `webapi:<client>`, `gui`, `cli:cli`.
- Read on every plane: `GET /api/v1/surfaces/context` (`/api/v1/controllers/context` was taken: it is the host
  controllers' own verb), MCP `actions` `context`, CLI `action context`, Lua / Python `context()`. The `ctx` push is
  the `surface` topic (S4).
- Not in S2: the Qt client as a setter (pane focus, the TTD panel) and as a follower (`InputRouter::SetContext`):
  S3 with the `@` layers. The instance a surface acts on stays the plane's own rule (the WebAPI: `instance`, else the
  selected, else the only one); `TargetState` (Stream Deck SD4) is not a prerequisite of S2: the context is keyed by
  the instance whichever rule picked it.

### 6.2 As built (Stream Deck SD4, 2026-10-20)

`actions::TargetState` (`core/src/emulator/actions/targetstate.h`): views registered by unreal-qt's main window and
the video wall, the focus order, the followed instance, surface sessions with pins, `NC_SURFACE_TARGET`; the WebAPI's
"no id" is the followed instance (T-1). As built and the hook the `surface` topic uses:
[targeting.md §10](../2026-10-19-streamdeck-integration/targeting.md#10-as-built-sd4-2026-10-20). The router of the
Dial and the pads still follows its window's bound instance (the same instance while a process has one window).

### 6.3 As built (S5, 2026-10-29): every surface follows the context

The context is the one state all surfaces share (owner 2026-10-29: the Dial is one controller among many; a pad
scrubs and debugs as well): the Stream Deck, the Dial, the ShuttlePRO, the pads and the GUI read and set the same
`ContextState`, and none keeps a mode of its own beside it.

- `InputRouter::SetContext(instance, context, set)`: `set` is false for the default. A player device is a debugger
  remote while its instance's context `FollowsIntoDebug`: `debug` and `debug.*` always, `ttd` and `ttd.*` when
  somebody set it. A history alone (the default `ttd`) keeps the pads in the game; the Stream Deck's Time page
  (`context: ttd` in the product scenario), the TTD panel focused or a `ctx.set ttd` take them into the history with
  the context's layer above `debug` (`pad-default`: the d-pad by frames in `@ttd`, by seconds in `@ttd.timecode`).
  `InputRouter::SetDebugFocus` is gone: the debugger window focused sets `debug` instead.
- The owner pushes every instance's context, not only the bound one's: a device pinned to an instance follows that
  instance (`HostControllers::followContext`, on `NC_SURFACE_CONTEXT` and the 250 ms poll). When the bound
  instance's pads go into the debugger or come back, their debug profiles are applied or dropped
  (`ProfileContext::debugLayer` = the Debug layer key switch or the context; the host verb `context` reports it as
  `debug_layer`) and the status bar says so.
- A pad's layer key is a setter: its own `debug` layer going up or down reaches the owner through
  `InputRouter::SetLayerSink`; up sets `debug` (by `host-device:<id>`) unless the context has the pads in the
  debugger already, down restores what it replaced unless another setter changed it meanwhile. A pad gone with the
  key held counts as down.
- The GUI: the debugger window focused sets `debug` (a pane `debug.<pane>`; a `debug.*` somebody set stays), the
  TTD panel focused `ttd`; the focus in another window of the application returns what the GUI set to the default.
  The TTD panel shown no longer sets `ttd` (a panel open beside a game keeps the pads in it).
- Tests: `InputRouter_Test.Context_*` (the rule, a pad following its instance and back, a pinned pad following its
  own, the layer key reported once each way and on removal); `acceptance.py` ACC-2 checks the pads follow the deck's
  Debugger and Time pages and come back home.
- Left: actions of a device pinned to another instance still run on the window's bound instance (the GUI's action
  dispatch); headless there is no owner of `HostInputService` (Q-3).
- One client owns the devices (owner 2026-10-29, after a Dial buzzing in the debugger: three unreal-qt processes
  had it open, one of them in `ttd` with its `edge` / `barrier` buzz marks). Every windowed client opens the same
  devices, so the one the user activated last owns them: `ClientQueue` is the list of process ids, the last
  activated first, in `<temp>/unreal-ng-controllers-<user>.owners` (a `QLockFile` around each edit, a `QSaveFile`
  write); activating the application claims, quitting leaves, the 250 ms poll follows it, and a queue with no live
  process is taken by the first client to see it. A windowless session (offscreen, minimal) never claims (and opens no devices unless `UNREAL_WINDOWLESS_DEVICES` lets it).
  `HostInputService::SetOwner(false)` suspends the router (`InputRouter::SetSuspended`: the controls are let go, the
  held actions released, the events dropped, the feel followed but not sent), stops the motors and writes nothing to
  a device (rumble, patterns, clicks, steps, lights; a light keeps the colour asked for); the owner again, each device
  gets its feel and its light back. The host verb `context` reports it as `owner`. Tests:
  `InputRouter_Test.Suspended_ReleasesHeldAndDropsEvents`, `ClientQueue_Test.*`,
  `Surfaces_Test.Service_NotTheOwnerWritesNothingAndTakesTheFeelBack`.

## 7. The TTD published cursor and readouts

`GetPublishedSessionInfo` copies a struct under a mutex at most every 100 ms and only after an observer asked
(`timetravelmanager.h`, `kPublishIntervalMs`); `TTDSessionInfo` has no cursor. Added to `ITimeTravelHooks`
(`core/src/debugger/ttd/timetravelhooks.h`), so both backends (`TimeTravelController`, the default, and
`TimeTravelManager`) provide it:

```cpp
struct TTDCursor { uint64_t frame; uint32_t tinframe; uint64_t first; uint64_t end; uint8_t state; bool paused; };
/// Written by the backend on every seek, step, recorded frame and state change; read by anyone without a lock
/// (a seqlock: readers retry while the sequence is odd or changed)
virtual TTDCursor PublishedCursor() const = 0;
```

- Frames are `uint64_t`, as in the TTD (`ttdcheckpoint.h`). A seqlock, not a 16-byte atomic (not lock-free on MSVC,
  libatomic on gcc).
- **Frame rate**: `fps = emulatorState.base_z80_frequency / EmulatorContext::GetBaseFrameTStates()`, the unscaled
  pair (`GetFrameTStates()` grows with the turbo multiplier, `emulatorcontext.h`), so turbo does not change the time
  code (Pentagon 3.5 MHz / 71680 T = 48.83 Hz).
- Readouts in v1: `ttd.position` = the cursor plus `fps`; `run.state` = `running` / `paused` and the speed.
- The hub reads them at the highest `max_hz` any session asked for (≤ 50) and sends a session the values that
  changed since its last message. No subscriber: no tick.

### 7.1 As built (S2, 2026-10-20)

- `TTDCursor` (`timetravelhooks.h`) carries `frame`, `tinframe`, `first`, `end`, `endTinframe`, `state`, `history`
  and `paused`: the end's T-state (a paused or stopped recording ends inside its last frame) and `history` (first
  and end are meaningful) were added. `paused` is read live (`Emulator::IsPaused`), not published.
- The cell is `ttd::TTDCursorCell` (`ttdcursorcell.h`): a seqlock over relaxed atomics; writers are serialized by
  a mutex (the machine's thread and a control thread ending an operation may meet), readers never take it. An equal
  cursor does not move `Sequence()`, so a readout hub (S4) can tell a change by one load.
- Both backends publish at every frame boundary (a guard at the top of `OnFrameBoundary`, whichever way it returns;
  not a control thread's throwaway replays) and in every `GetSessionInfo`, which the end of every changing
  operation calls: a seek, a step, a start, a stop, a load leave the cursor where they left the machine. A debugger
  step inside a frame does not publish (the next boundary or operation does): `tinframe` may lag there.
- `ttd.position` is the TTD `position` verb (`ttdcontrol.cpp`) on every plane: the live position, the bounds from the
  cursor, `history`, `first_frame`, `paused`, `fps` (`ttd::TTDFrameRate`). It no longer calls `SessionEndPosition`,
  so it never takes a session operation (which held the CPU for a detached machine that ran).

## 8. The surface hub

`SurfaceHub` (core/automation/webapi, beside `CompanionEventHub`) serves the `surface` topic of `/api/v1/ws` (D7;
erratum 2026-10-20: no own route `/api/v1/surfaces/ws`):

| Part | What |
|:--|:--|
| Sessions | The authenticated companion, the `join` capabilities, the bound instance, subscriptions. |
| Instance binding | A `TargetState` session per `join`: `join.target` (a pin) if given, otherwise the followed instance, following it ([§6](#6-contexts-and-the-followed-instance)); `target` on every change. |
| Calls | `action` → `ActionRegistry::Submit` with origin Companion; `result` on completion. |
| Push | `ctx` on `NC_SURFACE_CONTEXT` for the bound instance; `readout` on the tick; `revoked` then close when the companion is revoked (as the events socket). |

The protocol is in [protocol.md](protocol.md).

### 8.1 As built (S4, 2026-10-20)

- `SurfaceHub` is the `surface` topic's handler inside `WsHub` (`core/automation/webapi/src/ws-hub.*`,
  `surface-hub.*`), the shared hub of `/api/v1/ws`; both are drogon-free (a connection is a send and a close
  function), so `WsHub_Test` drives them with an inline loop and a fake clock.
- Binding: `follow` is the WebAPI's rule (the selected instance, else the only one) until `TargetState` (SD4) gives
  the focused view; `{"instance": …}` pins. The hub re-evaluates on `NC_EMULATOR_INSTANCE_CREATED` / `DESTROYED` /
  `NC_EMULATOR_SELECTION_CHANGED`.
- The hub tick of §11 is not a thread: one `runEvery` timer on the WebAPI's main loop, armed while somebody
  subscribes (§7's rule: the highest `max_hz`, nothing without a subscriber).
- The default context flip (Q-7) posts `NC_SURFACE_CONTEXT` by `default` (`ContextState::HistoryChanged`, called by
  both TTD backends when the cursor's `history` flips), so the Qt follower of S3 gets it too.
- Details and what is not built: [protocol.md §10](protocol.md#10-as-built-s4-2026-10-20).

## 9. Haptic feedback

```mermaid
flowchart LR
    R["seek result<br/>(actions worker)"] --> M["marks → patterns<br/>(active context layer)"]
    M --> L["rate limit<br/>40 ms per device, edge never dropped"]
    L --> O["IBackend::Output(device, haptic)"]
    O --> W["Dial driver writer thread<br/>latest request wins"]
    W --> HID["IOHIDDeviceSetReport<br/>output report 1"]
    C["context change"] --> S["steps / clicks of the new layer"] --> O
```

- The device that moved feels the marks. A companion gets them in its `result`; v1 sends no haptics to companions.
- Surface haptics do not go through `HapticMixer`, which mixes rumble motor levels and whose gate (haptics.md H-5)
  mutes on seek, pause and replay, exactly when the Dial must click. The gate stays for game haptics.
- The **driver alone** writes steps and clicks: on a context change the router asks once through `Output`; the
  driver queues it to its writer thread and applies it again after a reconnect.
- `IOHIDDeviceSetReport` blocks for a BLE round trip: never on the IOHID run-loop thread (`iohidbackend.cpp`).

### 9.1 As built (S3, 2026-10-20)

- `IBackend::Outputs` (`kOutputHaptic`, `kOutputSteps`), `Output(device, HapticOut)`, `Output(device, StepsOut)`; the
  IOHID backend gives a Dial a `SurfaceDialWriter` (`surfacedial.h`): one thread per Dial, a slot per report kind
  (the latest wins), the feature report before a pattern, the device `CFRetain`ed for the thread. `PlayClicks` (the
  test effects) goes through it too, so no caller waits for a BLE round trip any more. The last steps per Dial id are
  kept and written again when it reconnects.
- Bytes: `tick` `01 01 03 50 00`, `double` `01 02 03 3C 00`, `buzz` `01 01 04 50 00`; steps `01 nn nn 00 03|01 05 50
  00` (cutoff 5, retrigger 80: the found values; the feature is not read back first).
- The owner: `GuiActions::Trigger`'s done callback takes `marks[].kind` of a host device's result and posts them to
  the GUI thread, where `HostControllers::PlayMarks` → `HostInputService::PlayMarks` picks the strongest mark the
  device's top layer names a pattern for, applies `min_interval_ms` (default 40; `edge` / `barrier` always play) and
  queues it. The GUI thread hop adds its latency (normally under a few ms) to NFR-2.
- Contexts in the Qt client: entering a debugger pane sets `debug.disasm` / `debug.registers` / `debug.memory` /
  `debug.stack`, entering or showing the TTD panel `ttd` (once per entry: another setter's later context stays until
  the focus moves again); hiding the TTD panel or the debugger returns a context the GUI set to the default. The
  follower: `NC_SURFACE_CONTEXT` and the 250 ms poll (the default changes without a notification) push
  `ContextState::Get` of the bound instance into `InputRouter::SetContext`.
- Planes: host verb `output` (`pattern`, `steps` / `clicks`, `marks`) and the `devices` fields `role`, `role_set`,
  `layers`, `context`, `outputs`, `feel` on CLI, WebAPI (+ OpenAPI), WebSocket (`call`), MCP, Python, Lua
  ([command-interface.md §15](../../emulator/design/control-interfaces/command-interface.md#15-game-controllers)).

## 10. Flows

### 10.1 Dial turn in context `ttd`

```mermaid
sequenceDiagram
    participant D as Surface Dial
    participant B as IOHID backend
    participant R as InputRouter
    participant Q as HostControllers (Qt owner)
    participant W as actions worker
    participant T as TTDControl
    D->>B: input report 01 02 01 00 … (+1 step)
    B->>R: ControlEvent knob +1 (hostTimeNs)
    R->>R: layer @ttd: knob → ttd.seek_by {unit frames, min 1, max 100}, rate
    R->>Q: ActionEvent (library value)
    Q->>W: Submit(ttd.seek_by, core value, origin HostDevice)
    W->>T: Seek {frame: clamp(pos + n), tinframe 0}
    T-->>W: {reached, arrived_at, halt_reason, state}
    W-->>Q: result + marks [second]
    Q->>R: haptic tick for the Dial
    R->>B: Output(haptic) → writer thread → output report 1
```

### 10.2 Stream Deck page change

1. The user opens TTD → Time code; `streamdeck-bridge` sends `{"op":"action","id":"ctx.set","value":"ttd.timecode"}`.
2. `ContextState` stores it and posts `NC_SURFACE_CONTEXT`.
3. The Qt owner calls `SetContext`; the router activates `@ttd.timecode` and asks the Dial for its steps and clicks;
   the hub sends `ctx` to the sessions bound to that instance.
4. The next Dial turn seeks by seconds.

## 11. Threads and lifetime

| Thread | Does |
|:--|:--|
| IOHID run loop (`controllers-io`, existing) | Input reports, router events. Never blocks on a seek or a report write. |
| Dial writer (new, per Dial) | Feature and output reports. |
| Actions worker (new, per instance) | Executors in order, coalescing, results. |
| WebAPI threads (existing) | WebSocket frames, `Submit`. |
| Hub tick (a timer on the WebAPI's main loop, only with subscribers; no thread) | Readouts at ≤ 50 Hz. |
| GUI | GUI actions; context from pane focus; the owner's conversions. |

- Core actions register at core start-up and live until shutdown. The Qt client registers its GUI actions with a
  `RegistrationHandle` and drops it before its objects die; `List()` and `Submit()` never see a dead executor.
- An instance's worker is created with the instance and stopped before it is destroyed: queued jobs get `done` with
  "Not Found" ("the instance is gone"), the running one finishes.
- `done` callbacks into the Qt owner go through a weak handle; after the owner closes they are dropped.

## 12. Determinism

- Actions are host commands, never journaled.
- A seek while recording pauses the recording for browsing (TTD D8), as every navigation does; frame and instruction
  steps follow [debug-and-ttd §6](../2026-10-17-controller-manager/debug-and-ttd.md#6-interaction-with-ttd-recording-and-replay).

## 13. Code placement

| Path | What |
|:--|:--|
| `core/src/emulator/actions/` | `ActionRegistry`, `ActionsWorker`, `ContextState`, the built-in executors (run, ttd, ctx) |
| `core/src/emulator/execution/runcontrol.*` | `RunControl`; `Origin::HostDevice` in `executionarbiter.h` |
| `core/src/debugger/ttd/` | `PublishedCursor` on `ITimeTravelHooks` and both backends; `timelinemarks.*` |
| `core/controllers/src/hostcontrols.*`, `router/` | `Encoder`, speed, context layers, `SetContext`, the library's `ActionValue` |
| `core/controllers/src/hid/drivers/surfacedial.*` | Dial reports and writer thread |
| `core/automation/webapi/src/ws-hub.*`, `surface-hub.*`, `api/actions_api.cpp`, `api-websocket.cpp` | the shared hub and its topics, the `surface` topic of `/api/v1/ws`, `/api/v1/controllers/actions`, the socket's calls |
| `unreal-qt/src/controllers/` | the owner: registry bridge, context push, marks → output, GUI actions, the window as a `TargetState` view (`surfacetargets.*`) |
| `tools/verification/mcp/test_ws_surface.py` | the conformance suite (S4; a virtual companion over `mcplib.WebSocket`; `surfacectl` not built) |
