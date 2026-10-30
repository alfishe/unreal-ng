# Control surfaces: phases, later items and open questions

| | |
|---|---|
| **Date** | 2026-10-18 |
| **Status** | S1 landed on master 2026-10-20 (`14916a2d5`); S2 on master (`0faf11b41`); S3 and S4 on master 2026-10-20, the S3 owner test (the Dial felt) open; owner questions decided 2026-10-20 |

## Phases

| Phase | Content | Depends on | Status |
|:--|:--|:--|:--|
| S0 | POC 030: the Dial on macOS, input and haptic reports, felt measurements A2-A7 | | observations done, felt part open |
| S1 | Core `ActionRegistry`, actions worker, gates for every origin, `RunControl` (`Origin::HostDevice`); the working Qt actions moved; `/api/v1/controllers/actions` and the MCP `actions` tool; every plane (CLI `action`, Lua / Python `actions()` / `action()`, the WebSocket call on `/api/v1/ws`) | | done 2026-10-20: `core/src/emulator/actions/` (registry, worker per instance, gates, builtins), `execution/runcontrol.*` (the pause / resume / step routes call it), `Origin::HostDevice` + `OpSource::HostDevice`; Qt keeps the GUI actions (`unreal-qt/src/controllers/guiactions.*`); REST, MCP `actions`, CLI, Lua, Python; `/api/v1/ws` with `call` and `auth` (topics: S4); deviations in [architecture §3.1](architecture.md#31-as-built-s1-2026-10-20); tests `ActionRegistry*`, `McpActions_Test` |
| S2 | `ContextState`, `ctx.set`, `NC_SURFACE_CONTEXT`; the followed instance is `TargetState` (Stream Deck SD4, [targeting.md](../2026-10-19-streamdeck-integration/targeting.md)), not a selected instance the Qt client sets; TTD published cursor (both backends); `ttd.seek_by` with marks; `run.step_by` | S1 | done 2026-10-20: `core/src/emulator/actions/contextstate.*` (Q-5 last setter wins, Q-7 default `ttd` with a history), `NC_SURFACE_CONTEXT` + `SurfaceContextPayload`; `TTDCursor` on `ITimeTravelHooks::PublishedCursor` (seqlock `ttdcursorcell.h`, both backends publish at every frame boundary and in every `GetSessionInfo`); the `position` verb answers its bounds from it (`history`, `first_frame`, `paused`, `fps`) and never takes a session operation; `timelinemarks.*`; builtins `ttd.seek_by`, `run.step_by`, `ctx.set`; the context on every plane (`GET /api/v1/surfaces/context` and its WebSocket call, MCP `actions` `context`, CLI `action context`, Lua / Python `context()`); WebSocket topics stay S4. `TargetState` is not a prerequisite (the context is keyed by instance; the instance is each plane's own rule): it stays SD4. The Qt client as a context setter / follower: S3. Deviations in [architecture §6.1, §7.1](architecture.md#61-as-built-s2-2026-10-20), [actions.md §2-§4](actions.md#2-ttdseek_by); tests `TimelineMarks*`, `TTDCursor*`, `SeekCurve*`, `SurfaceActions*`, `StepBy*`; live `tools/verification/mcp/test_surfaces.py` |
| S3 | `Encoder`, `haptic` and `steps` outputs, the Dial driver in the macOS IOHID backend, `@` layers, `curve` params and per-layer `haptics` in the parser, the Dial profile; owner test: the Dial alone scrubbing with marks | S0, S2 | built 2026-10-20 (uncommitted, worktree `scratch/wt-control-surfaces-s3`); owner test open. `IBackend::Outputs` / `Output(HapticOut)` / `Output(StepsOut)` (`hostbackend.h`), `SurfaceDialWriter` (writer thread per Dial, latest wins, steps first) and `PatternReport` / `FeatureReport` (`surfacedial.*`), the IOHID backend's writer, outputs, steps kept across a reconnect and report time stamps in `hostTimeNs`; the router's context layers (`SetContext`, `Layers`, `Feel`, the output sink), `EncoderRate` (the bridge's rule with a 7.5 ms floor for BLE bunches), `ActionEvent` `turn` / `rate` / `params`; parser: `@` layers in debug profiles, `curve: {unit, min, max, rate_lo, rate_hi}` as params, per-layer `haptics` (`haptics/surfacehaptics.*`); `ProfileStore` resolves surface haptics per scope; `HostInputService::Output` / `PlayMarks` (rate limit, edge / barrier never dropped); Q-8: `"role": "debugger-remote"` in `devices.json`, `DeviceMatch::role`, `InputRouter::SetDefaultRole` (the service calls it on arrival; `SetRole` wins); the Dial profile (`@ttd` clicks per step, `@ttd.timecode` free wheel: both Q-6 modes); host verb `output` and the `devices` fields on every plane; the Qt client sets the context from the debugger's panes and the TTD panel and follows it into the router; marks → the Dial. Deviations in [architecture §5.1](architecture.md#51-as-built-s3-2026-10-20); tests `Surfaces_Test.*` |
| S4 | `SurfaceHub` as the `surface` topic of `/api/v1/ws` (D7; the socket and its calls exist since S1): `join`, actions, `ctx`, readouts, `target`, `revoked`; conformance suite | S2 | done 2026-10-20: `WsHub` (`core/automation/webapi/src/ws-hub.*`), the shared hub: auth by the companion token, `subscribe` / `unsubscribe` of topics (`run.state`, `ttd.position`, `surfaces.context`), one timer armed only with subscribers; `SurfaceHub` (`surface-hub.*`): `join` (`follow` = selected else only instance, or pinned `{instance}`), `action` → registry with origin Companion, `result` by `rid`, `ctx` (setters and the default flip, now posted by `ContextState::HistoryChanged` from both TTD backends), readouts `ttd.position` / `run.state` (full, then changed, ≤ `max_hz`), a minimal `target` on a rebind, `revoked`, `bye`. Boundary with SD4: views, `TargetState`, `pin`, `features` / `activity` / `catalogue`, the larger readout vocabulary; `{"view":…}` is `not_now`. Not built: the byte high-water mark / `slow_consumer`. [protocol.md §10](protocol.md#10-as-built-s4-2026-10-20); plane doc [websocket-interface.md §3](../../emulator/design/control-interfaces/websocket-interface.md#3-topics); tests `WsHub_Test`, live `tools/verification/mcp/test_ws_surface.py` |
| S5 | `streamdeck-bridge`: WebSocket client, page contexts, `ttd.seek_by` / `run.pause_toggle` by id, strip from readouts; ACC-2 | S3, S4 | open |

## Later

| Item | Why later |
|:--|:--|
| The `instances` and `debug.*` topics on `/api/v1/ws` (adapters of `/api/v1/companions/events` and `/api/v1/websocket`), the byte high-water mark and `slow_consumer` | S4 built the hub, the state topics and `surface`; the older sockets keep their contracts meanwhile |
| Raw controls from companion processes (a session backend in the router) | needs a trust rule for the `model` a client claims and a companion identity in `ActionEvent` |
| A core owner of `HostInputService` (the Dial headless) | Q-3 |
| Windows raw HID backend and the Dial on it (Linux hidraw haptics: built with controller manager C9, not exercised on hardware) | Q-4; D6 plans the raw HID backend anyway |
| A DIY HID POC | the generic path exists: a family's `hid.usages` read encoders, counters and split rings (the ShuttlePRO, [devices.md §4](devices.md#4-contour-shuttlepro-v2-and-shuttlexpress), built 2026-10-28; owner test open) |
| MIDI backend | after the generic encoder path |
| Touch strips, keys with displays, text, images, lights | no v1 use case |
| Register / memory nudging, tape scrub, AY solo, profiler toggles | UC-9 |
| More marks: minutes, checkpoints, breakpoint hits | breakpoint hits need replays or a precomputed hit list from the coverage scan |

## Owner questions

All decided by the owner on 2026-10-20: every recommendation accepted.


| # | Question | Recommendation |
|:--|:--|:--|
| Q-1 | The concept: physical devices through backends, processes through one companion protocol (P1, P4, P5)? | yes - **decided 2026-10-20: yes** |
| Q-2 | Move the `ActionRegistry` to `core/src/emulator/actions/` with one worker per instance and a core `RunControl` (P2)? | yes; it also closes C7 and the `/controllers/actions` route - **decided 2026-10-20: yes** (done in S1) |
| Q-3 | Who owns `HostInputService` headless? Today only the Qt client creates it. | v1: nobody (headless = actions and protocol); later a core owner - **decided 2026-10-20: v1 nobody** |
| Q-4 | Windows: a raw HID backend beside the system wheel, or the `RadialController` API (focus only)? | measure in POC 030 on Windows first - **decided 2026-10-20: measure first** |
| Q-5 | Context: last setter wins (P6)? | yes - **decided 2026-10-20: last setter wins** |
| Q-6 | Time code context: free wheel with mark clicks only, or clicks per step too? | free wheel; try both in S3 - **decided 2026-10-20: free wheel, try both in S3** |
| Q-7 | Default context when nothing set one: `play` (the Dial does nothing) or `ttd` when a history exists? | `ttd` when a history exists - **decided 2026-10-20: `ttd` when a history exists** |
| Q-8 | Should a Dial start in role `debugger-remote` with the debug layer on for it, without the Advanced dialog (a family default role: a new `DeviceMatch` field and a caller of `InputRouter::SetRole`)? It bends U6 (non-player roles in Advanced only) and the `Controllers/DebugLayer` default (off). | yes for families that are debug devices only (the Dial); pads unchanged - **decided 2026-10-20: yes for debug-only families like the Dial** |

## Errata found in other docs

| Where | What |
|:--|:--|
| [controller-manager/TODO.md](../2026-10-17-controller-manager/TODO.md) | Rows still say "not committed"; C1a-C13 are on master (`526c1770f` … `b0a539d8c`). |
| [companion-client/requirements.md §6](../2026-10-16-companion-client/requirements.md#6-non-goals) | "Pushing events to a companion" is a non-goal there; follow-up F1 built the events WebSocket. |
| `docs/inprogress/2026-10-16-companion-client/TODO.md` in the main checkout | Untracked and stale (lists the WebSocket as not planned). |
| This design (fixed 2026-10-20, from the Stream Deck TODO's errata) | No own route `/api/v1/surfaces/ws`: the `surface` topic of `/api/v1/ws` (D7); the protocol's `instance` op is `target`; architecture §6's selected instance is `TargetState`; the Dial driver and `ttd.jog_*` / `ttd.scrub_*` landed on master (`720fbb53e`, `f3d35d587`). |
