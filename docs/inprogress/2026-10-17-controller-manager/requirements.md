# Controller manager: goals and requirements

| | |
|---|---|
| **Date** | 2026-10-17 |
| **Status** | Design, for review |
| **Decisions** | D1-D12 in [README.md](README.md#decisions-2026-10-17) |

## Contents

- [1. Problem](#1-problem)
- [2. Goals](#2-goals)
- [3. Personas](#3-personas)
- [4. Use cases](#4-use-cases)
- [5. Functional requirements](#5-functional-requirements)
- [6. Non-functional requirements](#6-non-functional-requirements)
- [7. Non-goals](#7-non-goals)
- [8. Acceptance](#8-acceptance)

## 1. Problem

What exists ([architecture.md](architecture.md), [retro-devices.md](retro-devices.md)):

- One emulated joystick, `Joystick` (Kempston, `core/src/emulator/io/joystick/joystick.h`), driven by eight bound PC keys
  (`[INPUT] JoystickKeys=`, default keypad 8/2/4/6/0), and by automation (`DebugJoystickManager`, WebAPI, MCP
  `joystick_input`, CLI, Lua, Python).
- Next MD pads on two connectors, the right one reachable by automation only.
- Mice: Kempston, ZX-Evo AVR, Sprinter serial, through `MouseManager`.
- A complete input journal for TTD and RZX playback.

What is missing:

- Any host game pad or joystick. The Kempston TDD (J9) deferred it because Qt 6 has no gamepad module.
- Sinclair 1 / 2, Cursor, Fuller, Timex, AMX mouse, light guns. The 48K, 128 and +3 have no joystick at all. The Next
  Sinclair and Cursor modes are decoded for reports but press no keys.
- Input settings UI (Edit > Preferences is a stub), profiles, per-game choices, a shortcut manager, debugger and TTD
  hotkeys.
- Unreal Speccy `[INPUT]` keys in every shipped `unreal.ini` (`KJoystick`, `Fire`, `FireRate`, `ZXKeyMap`, ...) are
  silently ignored.

## 2. Goals

| ID | Goal |
|:--|:--|
| G-1 | **Works out of the box.** A pad connected before or after launch plays the loaded game with no dialog opened, on every machine that has (or can have) a joystick interface. |
| G-2 | **Any to any.** Any host input device drives any retro controller on any connector, including several at once and several players. |
| G-3 | **Most capable host path on every OS.** Native APIs first, every control the device has (Elite paddles, DualSense Edge back buttons, gyro), our raw HID fallback, SDL3 later (D2, D6). |
| G-4 | **Recognise the device and show it.** VID/PID identification, a photo in one house style with the mapping drawn on it, live highlight (D5, D12). |
| G-5 | **Recognise the game.** Signature-matched game profiles first, port-read heuristics second, manual profiles third (D7). |
| G-6 | **A developer's instrument.** A separate debug layer and profile set for run control and TTD, invisible to players (D8, D9). |
| G-7 | **One model on every surface.** WebAPI, MCP, CLI, Lua, Python and Qt see the same connectors, devices, verbs and errors (the media manager pattern). |
| G-8 | **Deterministic.** Recordings, TTD and RZX replay identically: machine input is sampled per emulated frame and journaled once. |

## 3. Personas

| Persona | Wants | Sees |
|:--|:--|:--|
| **Player** | Plug in a pad, load a game, play. Maybe change which button is fire. | Simple view: devices, connectors, the picture of the pad with the play mapping, the auto-profile toast. No debug layer, no raw reports, no backend details. |
| **Retro enthusiast** | The right interface per game (Sinclair vs Kempston), two players, autofire, a DB9 stick through an adapter, the Next 6-button pad. | Simple view plus connector options, learn wizard, per-game Keep. |
| **Developer / reverse engineer** | Step, scrub back and forth, bookmark, break, without leaving the pad; a second device as a debugger remote; scripted input. | Advanced view: the Debug layer tab, debug profiles, roles, instance pins, backend probe report, raw HID view, automation. |
| **Agent / script** | Query and change connectors and bindings, drive input, see what a profile decided and why. | WebAPI, MCP, CLI, Lua, Python ([surfaces.md](surfaces.md)). |

## 4. Use cases

| ID | Use case |
|:--|:--|
| UC-1 | A player connects an Xbox pad, starts a Pentagon with a Kempston game: the D-pad and left stick move, A fires, without opening anything. |
| UC-2 | A Sinclair-only game on a 128K: the port-read watch sees half-row `#EFFE` polled, plugs a Sinclair 1 joystick into the virtual Interface 2 connector, shows the toast "Sinclair 1 detected, Keep / Change". Keep stores a user profile for that signature. |
| UC-3 | The same game loaded next week from a TZX re-pack: the signature matches, the profile applies at once. |
| UC-4 | Two players on a +2A: pad 1 on Sinclair 1, pad 2 on Sinclair 2; or Kempston + Sinclair on a Pentagon. |
| UC-5 | A Next game uses an MD 6-button pad: A / B / C / Start on `#1F`, X / Y / Z / Mode through NR `#B2`; the picture marks which buttons a Kempston-mode game cannot see. |
| UC-6 | A QAOP+Space game with no joystick option: the pad drives keys through a keyboard-joystick retro device; the keyboard still types. |
| UC-7 | A graphics editor (Art Studio, Artist II) or NedoOS app with a Kempston mouse: the host mouse or the right stick drives it, with the board's button order. |
| UC-8 | A light-gun game: the host mouse or gyro aims, the trigger fires; the beam test in the core decides hits. |
| UC-9 | A developer holds paddle P4 (the layer key) of an Elite: the right trigger scrubs back in time, the left forward, the D-pad steps; release returns to play. Nothing of it enters the TTD journal. |
| UC-10 | A developer dedicates a second pad (or a Stream Deck as a companion client) as debugger remote while a tester plays with the first. |
| UC-11 | A cheap DB9-to-USB adapter (`0079:0006`) with an unknown layout: the learn wizard asks for Up, Down, Left, Right, Fire, writes a mapping, and the stick works. |
| UC-12 | Two identical pads: each keeps its player, connector and bindings across replug and restart. |
| UC-13 | The video wall runs 16 instances: a pad follows focus; a second pad is pinned to one instance. |
| UC-14 | An agent asks which connector Kempston fire is on, plugs a Fuller joystick, sets an autofire binding, taps fire for 2 frames, and reads back the state. |
| UC-15 | A user adds a photo of their own arcade stick and places the hotspots; the dialog shows it like a built-in device. |
| UC-16 | A headless instance (no Qt) on a Linux box uses the same pad and profiles through the library. |

## 5. Functional requirements

### Host devices

| ID | Requirement |
|:--|:--|
| FR-1 | Enumerate and hot-plug host game pads, joysticks, HID adapters, the keyboard and the mouse on macOS, Windows and Linux. |
| FR-2 | Identify each device by VID / PID / version / serial / path ([architecture.md §5](architecture.md#5-device-identity-and-merging)); keep identity across replug and restart; separate identical devices. |
| FR-3 | Merge endpoints of one physical device from several backends, filter synthetic duplicates (macOS `AppleGCSyntheticDevice`). |
| FR-4 | Expose every control the device has in the canonical vocabulary, including paddles, `share`, `profile`, touchpads and motion where the OS lets us. |
| FR-5 | Report per backend whether it loaded and why not; report missing permissions with the fix. |
| FR-6 | Outputs where available: rumble, trigger rumble, HD haptics, LEDs, player LEDs; battery level. Per-game haptics from audio filters and game events ([haptics.md](haptics.md)). |
| FR-7 | Device database: VID/PID → family, layout, labels, glyphs, image, quirks; user overrides; import of `gamecontrollerdb.txt` lines and RetroArch autoconfig files. |

### Emulated side

| ID | Requirement |
|:--|:--|
| FR-10 | Connectors per machine and per card, with stable ids, aliases and `auto` ([retro-devices.md](retro-devices.md)). |
| FR-11 | Retro devices: Kempston (5-bit and 8-bit, decode variants), Sinclair 1 / 2, Cursor / Protek / AGF, Fuller, Timex TS2068 (two sticks), Next MD 3 / 6-button and user-defined keys, ZX-Evo Sega pad, Kempston mouse (per-board button order, wheel counter), AMX mouse, light guns (Gunstick, Magnum, Defender), keyboard joystick (QAOP / OPQA / custom keys). |
| FR-12 | Sinclair, Cursor and keyboard-joystick devices act as a matrix overlay ANDed with the keyboard, not as key events. |
| FR-13 | Port-read evidence per frame for the heuristic, at no cost when off. |

### Bindings and profiles

| ID | Requirement |
|:--|:--|
| FR-20 | A binding maps one host control, an OR-list or a chord to a retro control, ZX key / chord / macro, mouse axis, action or layer ([mapping-and-profiles.md](mapping-and-profiles.md)). |
| FR-21 | Analog handling: radial dead zone, saturation, hysteresis threshold, 4-way / 8-way, trigger calibration, stick to mouse, gyro to pointer. |
| FR-22 | Autofire per binding in emulated frames (on / off counts), modes classic, toggle, hold, burst. |
| FR-23 | Layers: `play`, `debug`, and profile-defined shift layers; a layer key; held inputs released on switch. |
| FR-24 | Profiles layered built-in ⊂ community ⊂ user and global → machine → device family → device → game; diff-only saves. |
| FR-25 | Profile selection: signature match, then port-read heuristic, then manual (D7). Every automatic choice states its reason and can be kept or changed. |
| FR-26 | Play profiles and debug profiles are separate sets (D8). |
| FR-27 | Host keyboard as a joystick with presets; keys bound to a joystick are not passed to the ZX keyboard unless asked. |
| FR-28 | Legacy keys: `[INPUT] Joystick=` / `JoystickKeys=` unchanged; Unreal Speccy `KJoystick=`, `FJoystick=`, `Fire=`, `FireRate=` read as defaults. |

### Debugger and TTD

| ID | Requirement |
|:--|:--|
| FR-30 | A debug layer entered by holding a layer key, and a `debugger-remote` role for a whole device (D9). |
| FR-31 | Actions: run control, step, frame step, TTD scrub with analog speed, step back, reverse continue, bookmarks, breakpoint toggle, capture ([debug-and-ttd.md](debug-and-ttd.md)). |
| FR-32 | Actions work while the machine is paused and are never journaled. |
| FR-33 | One action registry shared by pad bindings and keyboard shortcuts. |

### Haptics

| ID | Requirement |
|:--|:--|
| FR-35 | One effect model for handle motors, impulse triggers and HD haptics; each device plays what it can and falls back ([haptics.md](haptics.md)). |
| FR-36 | Sources: filters on the emulated audio (AY register stream, beeper edges, low band, onsets) and game-event probes (exec, memory write, port write, values) that never stop the CPU. |
| FR-37 | Per-game haptic profiles selected like play profiles: signature, machine default, manual. |
| FR-38 | Haptics is never journaled and never changes emulation; it is muted on pause, seek, scrub, replay, RZX playback and turbo. |

### UI

| ID | Requirement |
|:--|:--|
| FR-40 | A modeless Qt dialog with three panes and patch cords, Simple and Advanced views (D11, [ui-ux.md](ui-ux.md)). |
| FR-41 | A picture of the selected host device with callouts of the active mapping, live highlight, front / back views; the retro device's picture on the connector. |
| FR-42 | Press-to-bind, test panel, learn wizard, hotspot editor for user photos, identify (rumble / LED). |
| FR-43 | An auto-profile toast in the main window with Keep / Change. |

### Automation

| ID | Requirement |
|:--|:--|
| FR-50 | WebAPI, MCP, CLI, Lua, Python: list host devices, connectors, retro devices; plug / unplug; read and set bindings and profiles; explain the auto decision; inject retro input (existing joystick verbs extended to every connector) ([surfaces.md](surfaces.md)). |
| FR-51 | One reply envelope and error-code table, as `MediaReply`. |

## 6. Non-functional requirements

| ID | Requirement |
|:--|:--|
| NFR-1 | Host latency: endpoint report to the state table < 0.2 ms; the frame sample < 2 µs per instance; no extra latency over the frame-start latch. |
| NFR-2 | Zero cost when unused: no thread wakeups, no per-frame work with no device and no binding. |
| NFR-3 | Determinism: a TTD / RZX replay of a session with pad input is bit-identical to the live run. |
| NFR-4 | No Qt below the dialog; the library builds and runs headless on all three OSes. |
| NFR-5 | Zero warnings on clang, gcc, MSVC and MinGW; the library is optional (`ENABLE_CONTROLLERS`). |
| NFR-6 | No new mandatory third-party dependency; SDL3 only behind `ENABLE_SDL_CONTROLLERS`. |
| NFR-7 | Images and data: every image has a recorded source and licence; no platform-holder logos. |
| NFR-8 | Tests under 50 ms each with fake backends; hardware tests opt-in. |

## 7. Non-goals

- Steam Input action sets: they stay in the Steam Deck client ([input-and-profiles.md §2.2](../2026-10-08-steamdeck-client/input-and-profiles.md)), which consumes this library's router.
- Writing on-device Elite profiles or firmware settings.
- RZX recording (not implemented in the core; separate work).
- Vectorizing controller photos: a separate study (TODO Q-1).
- Network play.

## 8. Acceptance

| ID | Check |
|:--|:--|
| ACC-1 | UC-1 on macOS, Windows and Linux with an Xbox pad, a DualSense and a generic DB9 adapter, no dialog opened. |
| ACC-2 | The Elite 2 shows P1-P4 as `paddle.p1`..`p4` on Windows, Linux and macOS Bluetooth, on every onboard profile, with the mirrored face button removed on macOS Bluetooth; on macOS USB, where the dext truncates the report, the dialog explains that paddles arrive as the buttons the onboard profile assigns. |
| ACC-3 | UC-2 and UC-3 with a Sinclair-only title and its TZX re-pack (needs signatures). |
| ACC-4 | UC-9: scrub 10 s back and forward with the triggers; the TTD journal holds no debug event; replay is identical (NFR-3). |
| ACC-5 | Two identical pads keep their connectors over replug (UC-12). |
| ACC-6 | Every surface lists the same connectors and bindings for a Next and a Pentagon; MCP answers in `content[]` text. |
| ACC-7 | A build with `ENABLE_CONTROLLERS=OFF` is unchanged in behaviour and passes `core-tests`. |
