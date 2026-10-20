# Controller manager: the debugger and time-travel layer

| | |
|---|---|
| **Date** | 2026-10-17 |
| **Status** | Design, for review |
| **Decisions** | D8 (separate layer and profiles, two personas), D9 (layer key, device roles) in [README.md](README.md#decisions-2026-10-17) |
| **Related** | [mapping-and-profiles.md](mapping-and-profiles.md) §2 layers, §7.3 debug profile format; [companion-client](../2026-10-16-companion-client/README.md) |

Two personas, two separate worlds:

| | Player | Advanced user (developer, reverser, speedrunner) |
|:--|:--|:--|
| Dialog mode | Simple (default) | Advanced |
| Sees | host devices, connectors, the picture, game profiles | the same plus the debug layer, debug profiles, roles, backend probes, raw controls |
| Profiles | play profiles only | play profiles and debug profiles (separate files, separate directories) |
| Debug layer | absent; the layer key is an ordinary button unless a debug profile is loaded | on the layer key or a whole device |

## Contents

- [1. Principles](#1-principles)
- [2. Entering the debug layer](#2-entering-the-debug-layer)
- [3. Action catalogue](#3-action-catalogue)
- [4. Default debug profiles](#4-default-debug-profiles)
- [5. Debugger remote and Stream Deck](#5-debugger-remote-and-stream-deck)
- [6. Interaction with TTD recording and replay](#6-interaction-with-ttd-recording-and-replay)
- [7. Feedback](#7-feedback)

## 1. Principles

1. **Host actions, never machine input.** A debug binding runs a command; it never reaches a connector and is never
   journaled. Replay must not repeat a scrub ([architecture.md §8](architecture.md#8-determinism-ttd-and-rzx)).
2. **Works while paused.** Actions are dispatched on arrival, not at the frame sample, so stepping, scrubbing and
   bookmarks work with the machine stopped.
3. **One action registry.** Today there is no shortcut manager: the debugger toolbar has no shortcuts
   (`unreal-qt/src/debugger/debuggerwindow.cpp:89-159`), the TTD widget has none (`unreal-qt/src/widgets/ttdwidget.cpp`),
   and Full Screen is matched by hand (`mainwindow.cpp:1620-1632`). This design adds an **`ActionRegistry`**: every
   action has an id, a label, an enabled state with a reason ("needs a pause", "no TTD history"), and an executor. Pad
   bindings, keyboard shortcuts, menus, toolbars and the Stream Deck call the same ids. Keyboard shortcuts become
   bindings of `kbd:system` in the debug profile, so the shortcut editor and the pad editor are one editor.
4. **Same verbs as automation.** Each action calls the core verb the automation surfaces already use (the MCP
   `control_execution` and `time_travel` tools, the WebAPI behind them), so a pad cannot do anything an agent cannot,
   and it inherits their checks.
5. **Separate profiles.** A debug profile never changes per game ([mapping-and-profiles.md §6.2](mapping-and-profiles.md#62-layering)).
   Switching a game's play profile never touches the debug bindings.
6. **Simple mode hides it.** A player who never opens Advanced never sees the debug layer, and its layer key does
   nothing unless a debug profile is active. The built-in debug profiles are active by default only for the
   `debugger-remote` role and after the user turns on "Debug layer" in Advanced once.

## 2. Entering the debug layer

| Way | Default | Notes |
|:--|:--|:--|
| Hold a layer key | `paddle.p4` (lower left paddle) on the Elite where paddles are visible (not macOS USB, research-host-apis §2.3a) and pads with back buttons (DualSense Edge, Steam Deck L5) | Release returns to play; held play targets were released at entry ([mapping-and-profiles.md §2](mapping-and-profiles.md#2-routing-and-layers)) |
| Chord | `view+menu` on pads without paddles (both down within the 60 ms chord window) | Consumed by the layer: the members' play bindings fire only if the chord does not complete in the window, so ENTER and the OSD keyboard do not fire ([mapping-and-profiles.md §2](mapping-and-profiles.md#2-routing-and-layers) rule 4) |
| Toggle | Advanced option: the layer key toggles instead of holding | For long sessions; the layer LED / overlay shows the state |
| Role `debugger-remote` | a whole device is the debug layer, always | A second pad on the desk, a Stream Deck, a MIDI controller later |
| Elite profile button | optional, Advanced: profile button cycles play / debug | Only if the backend sees it ([below](#elite-onboard-profiles)) |

### Elite onboard profiles

The Elite Series 2 has profile 0 (LED off) and three onboard profiles. When an onboard profile maps a paddle to
another button, the controller reports only the emulated button: SDL zeroes the paddle bits ("respect that the paddles
are being used for other controls", `SDL_hidapi_xboxone.c`), and xpad does the same. A paddle used as the layer key
then never arrives.

- The library reads the profile byte of the input report where its driver sees it (report layouts:
  [research-host-apis.md](research-host-apis.md)). Measured on macOS: over Bluetooth the raw paddle bits and the
  profile arrive on every profile, so P4 works as the layer key on any profile and the mirrored face button is removed
  (§2.3b); over USB neither arrives (§2.3a), and the layer key is the chord.
- Where the raw paddle bits are not visible (macOS USB) and a non-zero profile is active, the dialog shows "Onboard
  profile 2 is active: the paddles are mapped on the controller. Select the profile with the LED off to use them
  here." The layer key falls back to the chord. Where they are visible, the driver removes the mirrored face button
  (D19) and no hint is shown.
- The profile button as a layer switch is offered only when the profile byte is readable; otherwise the hardware owns
  it. A held paddle is the robust default.

## 3. Action catalogue

Ids are stable and shared by every binding source. "Core verb" is what the action calls.

### 3.1 Run control

| Action id | What | Core verb (existing) |
|:--|:--|:--|
| `run.pause_toggle` | pause / continue | `control_execution` pause / resume; debugger Continue / Pause |
| `run.step_in` | one instruction | debugger Step In |
| `run.step_over` | over a CALL / RST / block instruction | Step Over |
| `run.step_out` | to the return | Step Out |
| `run.frame_step` | one frame | Frame step |
| `run.wait_int` | run to the next interrupt | Wait INT / Run to Interrupt |
| `run.to_scanline` | run to a scanline (last used value) | Run to Scanline |
| `run.n_scanlines` | run N scanlines | Run N Scanlines |
| `run.to_cursor` | run to the disassembly cursor | debugger |
| `run.turbo_hold` | max speed while held | speed control |

### 3.2 Time travel

| Action id | What | Core verb (MCP `time_travel`, `mcp-tools.cpp:4520`) |
|:--|:--|:--|
| `ttd.scrub_back` | analog: move back through history at a speed set by the trigger | `seek` at a rate, see the curve below |
| `ttd.scrub_forward` | analog: forward through recorded history, then live | `seek` / `resume` |
| `ttd.step_back_instruction` | one instruction back | `step_back_instruction` |
| `ttd.step_forward_instruction` | one forward in history | `step_forward_instruction` |
| `ttd.step_back_frame` / `ttd.step_forward_frame` | one frame | `step_back_frame` / `step_forward_frame` |
| `ttd.step_back_scanline` | one scanline back | `seek` to the T-state of the previous line (new convenience on top of `seek`) |
| `ttd.reverse_step` | reverse step over | `reverse_step` |
| `ttd.reverse_continue` | back to the previous breakpoint hit | `reverse_continue` |
| `ttd.find_last_write` | back to the last write to the watched address (memory view cursor) | `find_last` |
| `ttd.bookmark_add` | bookmark here | `bookmark_add` |
| `ttd.bookmark_prev` / `ttd.bookmark_next` | jump to the neighbour bookmark | `bookmark_list` + `seek_bookmark` |
| `ttd.resume_live` | leave history, continue live | `resume` |
| `ttd.record_toggle` | start / stop recording | `start` / `stop` |

**Scrub curve.** The trigger's calibrated travel `t` (0..1, [mapping-and-profiles.md §3](mapping-and-profiles.md#3-analog-handling))
maps to a speed in emulated frames per second, with a dead band so a resting finger does nothing:

| Trigger travel | Speed | Use |
|:--|:--|:--|
| 0 - 0.08 | stop | resting finger |
| 0.08 - 0.30 | 1 - 5 frames/s | frame by frame, watching the screen |
| 0.30 - 0.60 | 5 - 50 frames/s | up to real time |
| 0.60 - 0.90 | 50 - 500 frames/s | seconds of history per second |
| 0.90 - 1.00 | 500 - 5000 frames/s | minutes, to the start of history |

Exponential between the points. The right stick X refines inside a frame: full deflection = 1 scanline per tick of 20
ms, so a release lands on an exact line. At the end of history the scrub stops and the pad pulses once ([§7](#7-feedback)).

Built: `curve: scrub` gives the action the source's level each time it changes (`ActionEvent::value`, 0 on the
release); `ScrubSpeed` is the table; a 20 ms timer in the Qt client moves by the frames covered with `seek`, both
triggers held count against each other. It stops on the release, at either end of the history and on a refusal,
with the reason in the status bar. A button bound to a scrub is the full level. Not yet: the fine scrub on the right
stick, the pulse at the end, forward past the end into live (`ttd.resume_live` does that).

`ttd.jog_frames` and `ttd.jog_instructions` take an encoder's signed steps (§4.4): a frame or an instruction per
step, clockwise forward, at most 64 instructions per event. A seek to frame N stands at N's end, which the position
names as the start of N + 1: the move keeps the frame it went to with the place it left, so the next step counts
from N.

### 3.3 Debugger UI

| Action id | What |
|:--|:--|
| `dbg.open` | open / raise the debugger window |
| `dbg.focus_next_pane` / `dbg.focus_prev_pane` | disassembly → registers → memory → stack → breakpoints |
| `dbg.breakpoint_toggle_pc` | toggle an execution breakpoint at PC (or the cursor) |
| `dbg.memory_follow` | memory view follows the register under the cursor (HL, DE, SP, IX) |
| `dbg.cursor_up` / `dbg.cursor_down` / `dbg.page_*` | move in the focused pane (the left stick in the debug layer) |
| `dbg.labels_toggle` | show / hide symbols |

### 3.4 Capture and machine

| Action id | What |
|:--|:--|
| `cap.screenshot` | screenshot |
| `cap.record_toggle` | start / stop video recording (`recording` library) |
| `cap.clip_last` | export the last N seconds of TTD history as a clip (`export_clip`) |
| `mach.reset` | reset (hold 1 s, so it is never an accident) |
| `mach.nmi` | NMI (Magic button) |
| `mach.turbo_toggle` | turbo |
| `osd.keyboard`, `osd.quick_menu` | also allowed in play profiles |

## 4. Default debug profiles

### 4.1 Xbox Elite Series 2

```
             LT: scrub back (analog)                       RT: scrub forward (analog)
             LB: step out                                  RB: step over
                    ┌──────────────── front ────────────────┐
  L-stick: move in  │                                        │  Y: reverse continue
  the focused pane  │   View: debugger pane   Menu: pause    │  X: bookmark add
                    │                                        │  B: resume live
  D-pad ◀ step back │                                        │  A: step in
  D-pad ▶ step in   │                                        │  R-stick X: fine scrub
  D-pad ▲ frame +   │                                        │  (1 scanline per tick)
  D-pad ▼ frame -   └────────────────────────────────────────┘  R-stick click: toggle bp at PC
                    ┌──────────────── back ─────────────────┐
                    │   P3: bookmark prev     P1: bookmark next │
                    │   P4: LAYER KEY (hold)  P2: find last write│
                    └────────────────────────────────────────┘
```

| Control | Action |
|:--|:--|
| `paddle.p4` | layer key (hold) |
| `lt` / `rt` | `ttd.scrub_back` / `ttd.scrub_forward` |
| `rstick.x` | fine scrub |
| `south` (A) | `run.step_in` |
| `rb` / `lb` | `run.step_over` / `run.step_out` |
| `dpad.left` / `dpad.right` | `ttd.step_back_instruction` / `run.step_in`, repeat after 15 frames at 4 frames |
| `dpad.up` / `dpad.down` | `run.frame_step` / `ttd.step_back_frame` |
| `north` (Y) | `ttd.reverse_continue` |
| `west` (X) | `ttd.bookmark_add` |
| `east` (B) | `ttd.resume_live` |
| `paddle.p1` / `paddle.p3` | `ttd.bookmark_next` / `ttd.bookmark_prev` |
| `paddle.p2` | `ttd.find_last_write` |
| `menu` | `run.pause_toggle` |
| `view` | `dbg.focus_next_pane` |
| `lstick` | `dbg.cursor_*` |
| `rstick.click` | `dbg.breakpoint_toggle_pc` |
| `lstick.click` | `dbg.memory_follow` |

Paddle positions follow SDL's numbering: P1 upper right, P2 lower right, P3 upper left, P4 lower left.

### 4.2 Pads without paddles

Same as above with the layer key on the `view+menu` chord; then `view` and `menu` inside the layer mean
`dbg.focus_next_pane` and `run.pause_toggle` only after both are released once. Bookmark prev / next move to `lb` /
`rb` held with `dpad.left` / `dpad.right`; step over / out move to `lb` / `rb` alone.

### 4.3 Keyboard

The keyboard's debug bindings are the shortcut table, editable in the same editor: F5 continue, F10 step over, F11
step in, Shift+F11 step out, Ctrl+F10 frame step, Ctrl+Left / Right step back / forward instruction, Ctrl+B bookmark.
They are active in the debugger window and, with "global shortcuts" on, in the main window.

Built (`data/controllers/profiles/debug/keyboard-default.yaml`, `HostControllers::eventFilter`):

- The window keyboard `kbd:system` is a debugger remote (§5): its debug layer is always up, it drives no connector.
- A combo is one control, the modifiers before the key in the order ctrl, alt, shift, meta: `key.f10`,
  `key.ctrl.f10`, `key.shift.f11` (`KeyControl`). Not '+': that joins a chord. `ctrl` is Qt's Control, Command on
  macOS, as in Qt's own shortcuts; `meta` is the Control key there.
- A debug profile that applies to the keyboard alone is selected without the debug layer: the shortcuts need no
  Advanced switch.
- The debugger window always takes them; the main window only with Controllers > Global shortcuts
  (`Controllers/GlobalShortcuts`, off by default), and a key they take does not reach the machine. A dialog never
  does. In a text field only the function keys are shortcuts. A bound key is taken before a menu shortcut of the
  window (ShortcutOverride).
- The editor shows the keyboard's Debugging tab without the debug layer; press-to-bind takes the combo pressed.

### 4.4 Surface Dial

A control surface (`"player": false`, [surface-dial.md](surface-dial.md)) is a debugger remote by nature: the Qt
client gives it that role, and its debug profile is selected without the debug layer
(`data/controllers/profiles/debug/surface-dial.yaml`):

| Control | Action |
|:--|:--|
| `knob` | `ttd.jog_frames`: a frame per step, clockwise forward |
| `push+knob` (turned while pressed) | `ttd.jog_instructions` |
| `push` alone | `run.pause_toggle`, on the release |

An encoder binding may name buttons held while it turns (`push+knob`): the one with the most held buttons takes the
turn, and a button held for a turn gives its own binding nothing; pressed alone, that binding acts on the release.

## 5. Debugger remote and Stream Deck

- **Role `debugger-remote`**: any host device. Every control maps through the debug profile, no layer key is needed,
  and the device never drives a connector. It follows the instance whose debugger has focus
  ([architecture.md §9](architecture.md#9-several-emulator-instances)).
- **Stream Deck + XL (0FD9:00C6)** is not a HID game pad. It integrates as a **companion client**
  ([companion-client](../2026-10-16-companion-client/README.md)): an Elgato plugin (or a small host helper) pairs once
  and calls the action ids over the WebAPI, `POST /controllers/actions/{id}`, without opening a session, so the GUI
  does not lock. The `debug` and `rewind` companion scopes cover it.
- **Key images show state.** `GET /controllers/actions` returns each action's enabled state and reason; the plugin
  draws a disabled key for "needs a pause", the current TTD position on a dial key, the bookmark count, the layer
  of the pad. The dials of the + XL map to `ttd.scrub_*` and fine scrub.
- Raw hidapi access to the Stream Deck (the Elgato app quit) is out of scope.

## 6. Interaction with TTD recording and replay

| State | Play layer | Debug layer |
|:--|:--|:--|
| Live, not recording | sampled per frame, journaled only if a journal runs | all actions |
| Recording | sampled per frame, journaled; speed held at 1x and fast loaders off (the TTD rule), so `run.turbo_hold` is refused with the reason | all actions; scrubbing back pauses recording at that point, `ttd.resume_live` continues it |
| In history (seeked) | **owned by replay**: live samples are dropped; the dialog and the picture show "input owned by replay" | scrub, step, bookmarks; `ttd.resume_live` returns to live and the manager resynchronizes held inputs at the next frame |
| RZX playback | owned by RZX playback | run control and capture; TTD actions per the RZX + TTD rules |

## 7. Feedback

All optional, all off in Simple mode, never journaled ([architecture.md §10](architecture.md#10-outputs-rumble-leds-triggers)). While the debug layer is active, game haptics are off and only these effects play ([haptics.md §8](haptics.md#8-determinism-ttd-and-replay)).

| Event | Effect | Default (Advanced) |
|:--|:--|:--|
| Breakpoint hit | one short pulse, both motors | on |
| Scrub | trigger rumble on the scrubbing trigger, strength proportional to speed | on where impulse triggers exist (GameController on macOS, GameInput on Windows) |
| End or start of history | two pulses, then the scrub stops | on |
| Layer entered / left | LED colour (DualSense, Switch player LEDs); on the Elite, a 20 ms tick | on |
| Bookmark added | tick | on |
