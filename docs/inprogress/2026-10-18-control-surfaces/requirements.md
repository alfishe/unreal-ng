# Control surfaces: goals and requirements

| | |
|---|---|
| **Date** | 2026-10-18 |
| **Status** | Design, for review |
| **Proposals** | P1-P10 in [README.md](README.md#proposals-for-the-owner-2026-10-18) |
| **Extends** | [controller manager requirements](../2026-10-17-controller-manager/requirements.md) (G-6, UC-9, UC-10, FR-30..33), [companion client requirements](../2026-10-16-companion-client/requirements.md) (UC-5) |

## Contents

- [1. Problem](#1-problem)
- [2. Goals](#2-goals)
- [3. Use cases](#3-use-cases)
- [4. Functional requirements](#4-functional-requirements)
- [5. Non-functional requirements](#5-non-functional-requirements)
- [6. Non-goals](#6-non-goals)
- [7. Acceptance](#7-acceptance)

## 1. Problem

What exists on master (`a89e4ccbf`):

- `core/controllers` (namespace `controllers`, CMake target `unrealng::controllers`): backends (macOS IOHID matching
  usages 0x04, 0x05, 0x08, `platform/macos/iohidbackend.cpp:207`; Windows XInput; Linux evdev),
  `ControlType::{Button, Axis, Trigger, Delta, Pointer}` (`hostcontrols.h:15`), the `InputRouter` with the `debug`
  layer and the `debugger-remote` role, YAML profiles, rumble haptics.
- `HostInputService` and the router are created by the Qt client only (`unreal-qt/src/controllers/hostcontrollers.cpp`);
  a headless instance has neither.
- `ActionEvent{device, action, pressed}` (`router/inputrouter.h:35`): a press or a release, no value.
- `ActionRegistry` in `unreal-qt/src/controllers/actionregistry.h`; its `run.*` executors call Qt debugger slots
  (`defaultactions.cpp`), its TTD executors call `TTDControl`. Several ids are stubs, `ttd.scrub_*` among them.
- `/api/v1/controllers/actions` is designed and not built ([CM surfaces §6](../2026-10-17-controller-manager/surfaces.md#6-webapi)).
- Erratum 2026-10-20: after this design the Surface Dial driver and the encoder / analog actions `ttd.jog_*`,
  `ttd.scrub_*` landed on master (`720fbb53e`, `f3d35d587`). S1 (2026-10-20) moved the registry into the core
  (`core/src/emulator/actions/`) and built `/api/v1/controllers/actions`.
- Companions: pairing, a bearer token, seven scopes, `GET /companions/me`, a status-only WebSocket
  `/api/v1/companions/events` (first message `{"op":"auth"}` or the header).
- `TTDControl::Seek` on the default backend (`TimeTravelController`) pauses a running recording for browsing (TTD
  D8: kept up to now, continued when execution reaches its end again); a target past the end is a success reply
  with `halt_reason: out_of_range`; a seek halts at external-event markers (replay barriers); it pauses the machine
  and waits up to 1 s for the pause (`ttdcontrol.cpp`, `RefuseWhileRecording`, `PauseAndConfirm`).
- `streamdeck-bridge` (external) drives the Stream Deck + XL over the plain WebAPI (cpp-httplib, no WebSocket): it
  computes the dial acceleration, clamps seeks to the history, polls `/ttd/position` at 50 Hz for its strip readout.

## 2. Goals

| # | Goal |
|:--|:--|
| G-1 | Physical devices through controllers-library backends (HID now); processes through one companion protocol. |
| G-2 | Host actions are a core service: the same ids and "possible now, or why not" for a pad binding, the Dial, the WebAPI, MCP, CLI, Lua, Python and a companion. |
| G-3 | Actions take values: a press, a delta with a speed, a string. |
| G-4 | Several surfaces act as one instrument: one context and one readout per instance. |
| G-5 | Haptic feedback that tells the hand where it is on the timeline. |
| G-6 | Never journaled: a debug action is a host command ([debug-and-ttd §1](../2026-10-17-controller-manager/debug-and-ttd.md#1-principles)). |

## 3. Use cases

| # | Use case | Phase |
|:--|:--|:--|
| UC-1 | Turn the Dial in context `ttd`: the position moves by frames, 1 → 100 per step with speed; a click at every second crossed, two at every 10 s, a buzz when the history ends or a barrier stops the seek. | v1 |
| UC-2 | Press the Dial: run / pause, as the Stream Deck dial press does. | v1 |
| UC-3 | Open TTD → Time code on the Stream Deck: the Dial scrubs by time (0.02 → 2 s per step); back on TTD it scrubs frames. | v1 |
| UC-4 | Focus the disassembly (or open the Debug page on the Stream Deck): the Dial steps in forward and steps back (TTD) backward. | v1 |
| UC-5 | A Stream Deck dial and the Dial scrub the same history; the strip shows the position the Dial reached within one readout tick. | v1 |
| UC-6 | Headless instance in CI: a test opens the surface WebSocket, calls `ttd.seek_by`, checks the result, the marks and the readout. | v1 |
| UC-7 | The Dial sleeps (BLE) and wakes on touch: its steps and clicks are applied again; the context is unchanged. | v1 |
| UC-8 | A DIY HID device (ESP32-S3, RP2040) in the Dial's descriptor pattern works like the Dial with no code of its own. | later |
| UC-9 | Register and memory nudging by dial; tape scrub; AY channel solo. | later |

## 4. Functional requirements

| # | Requirement |
|:--|:--|
| FR-1 | `ControlType::Encoder`: a fractional delta in steps; speed from report times ([controls-and-feedback.md](controls-and-feedback.md)). |
| FR-2 | Outputs `haptic` (pattern) and `steps` (steps per turn, clicks on or off) in `IBackend`. |
| FR-3 | Core `ActionRegistry` with values, availability and reason, scope, one ordered worker per instance ([architecture.md §3](architecture.md#3-the-core-action-registry)). |
| FR-4 | Core `RunControl` for pause, resume, step in / over / out, frame step; the WebAPI routes and the actions call it. |
| FR-5 | `ttd.seek_by` with the curve, the history clamp, the route's reply body plus `marks` ([actions.md §2](actions.md#2-ttdseek_by)). |
| FR-6 | Contexts per instance; `ctx.set`; context layers in debug profiles ([profiles.md](profiles.md)). |
| FR-7 | TTD published cursor, readouts `ttd.position` and `run.state` at up to 50 Hz, changes only. |
| FR-8 | `/api/v1/controllers/actions[/{id}]` (list, run with a value) and the MCP `actions` tool in v1; CLI, Lua and Python rows of the parity table later. |
| FR-9 | Surface protocol v1 as the `surface` topic of `/api/v1/ws` (D7), actions mode, companion auth and error codes ([protocol.md](protocol.md)). |
| FR-10 | Surface Dial driver for the macOS IOHID backend: input, haptic and steps outputs on a writer thread, reconnect. |
| FR-11 | `streamdeck-bridge`: a WebSocket client, `ctx.set` from pages, `ttd.seek_by` and `run.pause_toggle` by id, the strip from readouts. |

## 5. Non-functional requirements

| # | Requirement |
|:--|:--|
| NFR-1 | Dial report → seek submitted: < 2 ms in process. |
| NFR-2 | Seek done → haptic report written: < 5 ms; output report → felt click measured in POC 030 (A7). |
| NFR-3 | Readouts cost nothing without subscribers; with them, one atomic read per tick. |
| NFR-4 | Input and haptic writes never wait for a seek: executors run on the actions worker, haptic reports on the driver's writer thread. |
| NFR-5 | Turning faster than seeks complete coalesces deltas; no backlog after the hand stops (≤ 1 seek in flight + 1 queued per control). |
| NFR-6 | Zero warnings on gcc, clang, mingw, msvc; no new dependency in unreal-ng. |

## 6. Non-goals

- A device wire protocol of our own; physical devices speak HID (MIDI later).
- Rendering Stream Deck faces in the core; images never go through the protocol.
- Network play, remote screens.

## 7. Acceptance

| # | Check |
|:--|:--|
| ACC-1 | UC-1 with the real Dial on a fresh Pentagon instance: a 3-minute recording, 60 s of scrubbing; every second mark felt; a buzz at both ends; no reply with `halt_reason: out_of_range`; the same while recording (the recording pauses for browsing, D8). |
| ACC-2 | UC-3 and UC-5: page switch changes the Dial's unit before the next turn; the strip and the Dial's last result show the same frame. |
| ACC-3 | UC-6: the conformance suite passes against a headless instance. |
| ACC-4 | No surface action in the input journal; a recording made while scrubbing replays identically. |
| ACC-5 | `GET /api/v1/controllers/actions` lists the same ids and reasons as the Qt menu, minus the GUI-only actions when headless. |
