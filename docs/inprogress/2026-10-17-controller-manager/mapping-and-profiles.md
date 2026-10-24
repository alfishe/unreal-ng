# Controller manager: mapping and profiles

| | |
|---|---|
| **Date** | 2026-10-17 |
| **Status** | Design, for review |
| **Decisions** | D7 (signature → port reads → manual), D8 (separate debug layer and profiles), D9 (layer key, roles) in [README.md](README.md#decisions-2026-10-17) |
| **Generalizes** | [steamdeck-client/input-and-profiles.md](../2026-10-08-steamdeck-client/input-and-profiles.md) §3 routing, §5 heuristic, §6 profile format, §7 defaults |

The Steam Deck design described one router for one device inside one client. This document makes that router part of
the `controllers` library ([architecture.md §3](architecture.md#3-the-controllers-library)), so every client and every
host device uses it. What changes and what stays:

| Steam Deck design | Here |
|:--|:--|
| `ControlEvent` stream, `InputRouter` | Same names, now in `core/controllers/src/router/`. The Deck client links the library instead of keeping its own router. |
| Deck control names (`a`, `l4`, `right_trackpad`) | The canonical vocabulary ([architecture.md §6](architecture.md#6-the-canonical-control-vocabulary)): `south`, `paddle.p3`, `touchpad.x`. The Deck's L4 L5 R4 R5 are `paddle.p1`..`p4`. |
| Targets: key, joystick bit, mouse, app action | Same plus retro controls of any connector device, layers, and debug actions. A target names a connector, not "the joystick". |
| Signatures §4, heuristic §5 | Kept as written; this document fixes the order (D7) and the layering. The signature algorithms stay in the shared `swsignature` library the Deck design proposed. |
| One JSON profile with `controls`, `layers`, `radial` | Split in two: play profiles and debug profiles, separate files and directories (D8). Radial menus stay a play-profile feature of clients that draw them. |
| Defaults §7 for the Deck | The Deck becomes one family in [§8](#8-defaults-out-of-the-box). |

## Contents

- [1. Binding model](#1-binding-model)
- [2. Routing and layers](#2-routing-and-layers)
- [3. Analog handling](#3-analog-handling)
- [4. Keyboard mappings](#4-keyboard-mappings)
- [5. Mouse mappings](#5-mouse-mappings)
- [6. Profile selection](#6-profile-selection)
- [7. Profile format](#7-profile-format)
- [8. Defaults out of the box](#8-defaults-out-of-the-box)
- [9. Two players and more](#9-two-players-and-more)

## 1. Binding model

A binding is **source → target, with options**. A profile is a list of bindings per layer.

### 1.1 Sources

| Form | Example | Meaning |
|:--|:--|:--|
| One control | `south` | Pressed when the button is down, or the axis is past its threshold. |
| OR-list | `["south", "rt.click", "key.space"]` | Any of them (MAME `input_seq` OR, Mesen2's four sets). The same target from a pad and the keyboard at once. |
| Chord | `{"chord": ["view", "menu"]}` | All held together, in any order, within the chord window (60 ms). The members' own bindings are held back until the window closes; if the chord completes, they are not sent ([§2](#2-routing-and-layers) rule 4). |
| Derived digital | `lstick.left`, `lt.click` | An analog control seen as a button through [§3](#3-analog-handling). |
| Analog | `lstick.x`, `rt`, `gyro.y` | For analog targets only: mouse axes, scrub speed, the light gun pointer. |

### 1.2 Targets

| Target | JSON | Reaches the core as |
|:--|:--|:--|
| Retro control | `{"retro": "kempston:fire"}`, `{"retro": "next.left:c"}` | The connector's device state, sampled at frame start ([architecture.md §7](architecture.md#7-the-event-pipeline)). `auto:fire` means "the connector this device drives". |
| ZX key | `{"key": "SPACE"}` | The host key overlay of the matrix, independent of the keys the user holds (ZX key names of `ZXKeysEnum`, incl. `ZXKEY_EXT_*` composites such as `EDIT`, which hold their shift and key); journaled per half-row as `HostKeys` (C2b). |
| ZX chord | `{"keys": ["CAPS", "1"]}` | Held together. |
| Macro | `{"macro": ["J", "SYM+P", "SYM+P", "ENTER"]}` | A sequence; each key held one frame and released one frame, so the ROM scan sees every key (Deck design §3). The router steps it at each frame sample onto the host key overlay, so it is journaled (`HostKeys`) and replays without the router. |
| Mouse axis or button | `{"mouse": "dx", "scale": 6}`, `{"mouse": "left"}` | Mouse deltas through the existing `MouseManager` path. |
| Pointer | `{"pointer": "absolute"}` | Light gun / pen position in screen coordinates. |
| Action | `{"action": "ttd.scrub_back"}` | A host command ([debug-and-ttd.md §3](debug-and-ttd.md#3-action-catalogue)); never journaled. Only some actions are allowed in play profiles (OSD keyboard, pause, quick menu, screenshot). |
| Layer | `{"layer": "keys", "mode": "hold"}` | Switches layers ([§2](#2-routing-and-layers)). |
| None | `{"none": true}` | Explicitly unbound; overrides an inherited binding. |

### 1.3 Options per binding

| Option | Values | Default | Notes |
|:--|:--|:--|:--|
| `turbo` | `off`, `classic`, `toggle`, `hold`, `burst` | `off` | `classic`: pulses while held. `toggle`: one press starts pulsing, the next stops it. `hold`: one press holds the target until the next press (autohold). `burst`: `count` pulses per press (vAmiga bullets). |
| `on`, `off` | frames | 2, 2 | Emulated frames, so the pattern is deterministic and replays identically (12.5 Hz at 50 Hz). The phase restarts on each press. |
| `count` | 1..16 | 3 | For `burst`. |
| `press` | `hold`, `tap` | `hold` | `tap`: a fixed `frames` pulse on the press edge regardless of how long the button is held (menu keys, ENTER). |
| `frames` | 1..50 | 2 | Tap length, like `DebugJoystickManager::Tap`. |
| `invert` | bool | false | For axes, and for "released = pressed" oddities. |
| `repeat` | `{delay, rate}` in frames (1..500 each), or `none` | none | Key repeat for a held ZX key from a pad (menu navigation). Built: a held key or retro control is released for one emulated frame `delay` frames after the press and then every `rate` frames (`rate` 2 or more), so a menu that waits for a new press sees one, and a recording replays it; a held action (a debug layer's step keys) runs again on the host clock, `delay` and then every `rate` frames of 20 ms, also while the machine is paused ([debug-and-ttd.md §3.3](debug-and-ttd.md#33-debugger-ui)). Refused with `turbo`, `press: tap`, a `level` action, an encoder or a mouse target. |
| `label` | string | none | The game's meaning ("Fire", "Hyperspace"), shown in the picture callouts (RetroArch descriptors). |

## 2. Routing and layers

```mermaid
flowchart LR
    CE["control event"] --> ROLE{"device role"}
    ROLE -->|disabled| DROP["dropped"]
    ROLE -->|debugger-remote| DBG["debug layer only"]
    ROLE -->|player| LK{"layer key held?"}
    LK -->|"debug layer key"| DBG
    LK -->|"shift layer key"| SH["shift layer, then play"]
    LK -->|no| PLAY["play layer"]
    PLAY & SH --> B["binding lookup"]
    DBG --> BD["debug binding lookup<br/>(debug profile)"]
    B --> OPT["options: analog, turbo, tap"]
    OPT --> TGT{"target kind"}
    TGT -->|"retro / key / mouse"| ST["state table → frame sample"]
    TGT -->|"action / layer"| ACT["executed now"]
    BD --> ACT
```

DT-5, in words:

1. **Role first.** A `debugger-remote` device never reaches the play layer; a `disabled` device reaches nothing.
2. **Layer stack.** Base layer `play`; a held layer key pushes `debug` or a profile's shift layer. Lookup goes top to
   bottom, so a shift layer only overrides what it names (the Deck design's `keys` layer). `debug` is opaque: a
   control not bound in the debug profile does nothing while the debug layer is up; it does not fall through to play.
3. **Layer keys.** Defaults: hold `paddle.p4` on devices with paddles, the chord `view+menu` on pads
   without ([debug-and-ttd.md §2](debug-and-ttd.md#2-entering-the-debug-layer)). Modes: `hold` (default), `toggle`,
   `oneshot` (the next press only).
4. **A layer key is consumed by the layer.** A control used as a layer key (`paddle.p4` for the debug layer, a
   shift-layer button, the `view+menu` chord) acts only as the layer key: its play binding is suppressed for as long as
   it is the layer key, and it never reaches a connector. For a chord, each member's own binding fires only if the
   chord does not complete within the chord window (60 ms from the first member's press); the held-back press is then
   sent with its original press time, so a member pressed alone still works with one window of delay. The picture
   marks such controls `[layer key]` in their callouts ([ui-ux.md](ui-ux.md)).
5. **Held inputs on a layer switch.** Every target held by the old layer is released at the switch, in the same
   frame sample, and the new layer's bindings see only new press edges. Otherwise a held fire would stay held for
   the game while the user scrubs history. A button held across the switch is ignored until released.
6. **Simple mode** (players) hides the debug layer in the UI; the layer key is still configurable in Advanced. With no
   debug profile loaded, the layer key is an ordinary button.

## 3. Analog handling

| Step | Rule | Default |
|:--|:--|:--|
| Radial dead zone | `r = |(x, y)|`; below `dz` → 0; above `sat` → 1; between, rescaled `(r - dz) / (sat - dz)`, direction kept | `dz` 0.15, `sat` 0.95 (MAME `inputdev.cpp`) |
| Digital threshold with hysteresis | a direction turns on at 0.45 and off at 0.35 of the rescaled value | 0.45 / 0.35 (vAmiga) |
| 8-way | sectors of 45°, centred on the axes | on |
| 4-way | sectors of 90°; on a diagonal the previous direction wins until the angle passes 55°, so a slide along a wall does not flicker | per profile (`ways: 4`) for games that hate diagonals |
| Triggers | the resting value is measured at connect (raw APIs report -1 at rest, Nestopia's bug); `lt.click` at 0.5 of the calibrated travel | 0.5 |
| Hair-trigger stops (Elite) | the trigger's full value is the maximum seen in the session, not 1.0; it adapts as the stop changes | adaptive |
| Stick → mouse | `v = sign(x) · max(0, |x| - dz)^γ · speed`, accumulated in sub-pixels, sent as integer mouse counts per frame | γ 1.8, speed 8 counts/frame at full deflection |
| Stick → mouse, precision | a held modifier (default `rstick.click`) scales speed by 0.25 | |
| Gyro → mouse | angular velocity × scale, only while a chosen control is held (a ratchet), never always-on | hold `lb` |
| Gyro or stick → pointer | absolute: the stick (or gyro integrated with recentring on `rstick.click`) gives a screen position for light guns and pens | |
| Stick → D-pad of a retro device | through the threshold and the ways above | |

All values are per binding, with family defaults from the device database (a DB9 adapter reports digital axes and
needs none of this).

## 4. Keyboard mappings

The window keyboard is a host device (`kbd:system`) with controls `key.<PcKey>`, and `key.<modifiers>.<PcKey>` for
the shortcut combos (`key.ctrl.f10`, [debug-and-ttd.md §4.3](debug-and-ttd.md#43-keyboard)). The Qt client registers
it as a debugger remote: today it carries the debugger's shortcuts only, and the numpad joystick still comes from the
core's `JoystickKeys=` path. Its events come from Qt
(`KeyboardManager::postHostKey`, `unreal-qt/src/emulator/keyboardmanager.cpp:293`) and the core already turns them into
ZX keys, PS/2 bytes and Kempston bits ([docs/features/keyboard.md](../../features/keyboard.md)).

### 4.1 Keyboard as a joystick

Presets, each a play-profile fragment for `kbd:system`:

| Preset | Directions | Fire | Fire 2 | Source |
|:--|:--|:--|:--|:--|
| `numpad` (today's default) | kp8 kp2 kp4 kp6 | kp0 | kp5 | `JoystickKeys=` default (`joystick.h:35`) |
| `cursor-rctrl` | arrows | right Ctrl | right Alt | WinUAE `kb_ck` |
| `wasd-space` | W S A D | Space | left Alt | WinUAE `kb_se` |
| `qaop-space` | Q A O P | Space | M | Classic ZX layout, when the joystick must come from a PC keyboard laid out the ZX way |

### 4.2 Consumed or passed through

Each keyboard binding has `consume: true|false`:

- **consume** (default for joystick presets): the key drives the retro control only; the ZX keyboard does not see it.
  This is vAmiga's `disconnectJoyKeys`. Without it the numpad 8 would also type 8 on machines that map the numpad.
- **pass**: the key drives both (a key that is the same on both sides, a Sinclair joystick fed from 6-0).
- The existing `Joystick::WantsKey` / `OnPcKey` path (`joystick.h:46-83`) is the consume case; the router replaces the
  table, the core keeps the hook for headless use with `JoystickKeys=`.

### 4.3 Layouts

| Mode | Meaning | Where it exists today |
|:--|:--|:--|
| Positional | the physical key position (`PcKey`, from the native scan code) maps to a ZX key; the host's language layout does not matter | `mapQtEventToPcKey`, `PcKey::FromMacVirtualKey` / `FromWindowsScanCode` / `FromLinuxEvdev` |
| Symbolic | the typed character maps to the ZX key or chord that produces it (`"` → SYM+P) | `PcKey::FromCharacter`, `type_input` |
| PS/2 / XT | the key goes to the machine's own keyboard controller as scan codes | `HostKeyboardRoute {Auto, Matrix, Ps2, Both}` (`keyboard.h:184`), `[INPUT] HostKeyboard=` |

Keyboard profiles add on top: **remaps** (`key.f1` → `{"keys": ["CAPS","1"]}` EDIT), **macros** (`key.f5` →
`LOAD ""` + ENTER), and the per-machine function-key tables of `docs/features/keyboard.md`. The route itself stays a
machine setting (`HostKeyboard=`); the controller manager shows it and links to it, it does not duplicate it.

## 5. Mouse mappings

- The host mouse is `mouse:system`; its deltas already go through `MouseManager` to every `IMouseSink`
  (`io/mouse/mousemanager.h`). The controller manager adds only: binding the host mouse to a chosen mouse connector,
  and mouse targets from pads ([§3](#3-analog-handling)).
- **Button order is a board property, not a host option.** The Kempston mouse has D0 = right on the Kondratyev
  schematic, the Next and Karabas-Pro, and D0 = left on the Evo AVR and the Sprinter
  ([research-retro-hardware.md](research-retro-hardware.md)). The connector declares the order; the user's
  `SwapMouse=` stays a preference applied on top.
- **Wheel.** The Kempston wheel is a 4-bit absolute counter in #FADF bits 7:4 (Next, Evo), not a delta. The device
  accumulates host wheel steps into it; a pad can drive it (`{"mouse": "wheel"}` from `dpad.up` / `dpad.down`).
- Existing settings (`[INPUT] Mouse=`, `Wheel=`, `SwapMouse=`, `MouseScale=`, `MouseReleaseKey=`) stay where they are.

## 6. Profile selection

### 6.1 Order (DT-2, D7)

```mermaid
flowchart TB
    L["program loaded / machine reset"] --> S{"signature match?<br/>exact → content → file set → fuzzy"}
    S -->|"yes"| GP["game profile"]
    S -->|"no"| H["neutral profile + port-read watch"]
    H --> E{"evidence decisive<br/>(≤ 30 s of play)?"}
    E -->|"yes"| T["auto profile + toast: Keep / Change"]
    E -->|"no"| N["stay neutral"]
    T -->|Keep| U["user profile keyed by the signature"]
    T -->|Change| ED["dialog, binding editor"]
    GP & U --> M["merge with machine, family, device layers"]
    N --> M
    M --> A["active play profile"]
```

1. **Signature.** As the Deck design §4 defines: exact SHA-256, then the content signature (TAP / TZX payloads), then
   the TR-DOS file set, then the fuzzy set match. The library index and zx-meta-db key on the same signatures
   ([metadata-manager](../2026-09-27-metadata-manager/design.md)). Implemented by `core/swsignature`
   (`unrealng::swsignature`, option `ENABLE_SWSIGNATURE`): when a program is loaded from a file the Qt client computes
   its signatures (`file:sha256:<hex>` and, by media, `tape:sha256:` / `trdset:sha256:` / `mem:sha256:`) and its TR-DOS
   file set into the profile context; a game profile matches by `match.signatures` (exact or content), then
   `match.file_sha256` (also the `file:sha256:` signature: profiles of older versions keep working), then
   `match.file_set` by the catalog rule (every catalogue entry of the profile's disk kept, at most one file with
   other data but not all, at most max(2, n) entries added; a set without `n:` entry tokens falls back to Jaccard
   ≥ 0.8 with no code file missing; `core/swsignature/src/match.h`, header-only, so the controllers library takes no
   core symbols). The host verb
   `match` (every plane: `controllers match`, `GET /api/v1/controllers/match`, MCP `controllers` action `match`,
   Python `controllers_match`) tells which profiles match and how; `context` carries the signatures, `game_matches`
   ([id, how], strongest first) and `haptics_reason`. Measured on the vtrd.in collection by
   [POC 034](../../../tools/poc/034-media-signatures/README.md).
2. **Port-read heuristic.** The core's `PortReadWatch` ([architecture.md §13](architecture.md#13-code-placement))
   counts the guest's reads per frame, smoothed, from the end of loading for up to 30 s of play:

   | Evidence | Decision |
   |:--|:--|
   | a Kempston port (`#1F` with the board's decode) read every frame and the value tested | Kempston on the machine's Kempston connector |
   | `#37` read every frame (Next) | Kempston 2 / MD 2 on the right connector |
   | NR `#B2` read (Next) | MD 6-button pad |
   | half-row `#EFFE` dominant, bits 0-4 tested | Sinclair 1 (6-0) or Cursor (5-8, 0): decided by whether `#F7FE` bit 4 (key 5) is tested too |
   | half-row `#F7FE` dominant | Sinclair 2 (1-5) |
   | `#FBFE` + `#FDFE` + `#DFFE` + `#7FFE` (Q, A, O / P, Space) | QAOP + Space or M |
   | `#7F` read every frame | Fuller |
   | AY R14 read with `#F6` (Timex) | Timex joystick |
   | `#FADF` / `#FBDF` / `#FFDF` | Kempston mouse: the right stick and the host mouse go to the mouse |
   | all half-rows in a loop (text input) | keyboard: hint "this program wants the keyboard" |

   The neutral profile drives Kempston **and** QAOP + Space at once while the watch runs (the Deck design's choice),
   unless the machine has no Kempston connector. The toast offers **Keep** (saves a user profile `user/game-<16 hex of
   the content signature>` with `match.signatures` = the content signature, `match.file_set` = the TR-DOS file set
   and `match.file_sha256` = the file's hash; a medium without a content signature keeps the file hash only) and
   **Change**.
3. **Manual.** Profiles chosen in the dialog or by `POST /controllers/profiles/{profile}/apply`.

### 6.2 Layering

Two axes, merged field by field; only differences are saved (MAME's diff-only cfg):

| Axis | Order (later wins) |
|:--|:--|
| Scope | built-in (compiled-in and `data/controllers/profiles/`) ⊂ community (same directory, reviewed by PR) ⊂ user (`<user config>/unreal-ng/controllers/profiles/`) |
| Specificity | global → machine model → device family → this device (`HostDeviceId`) → game |

Example: the user remaps `west` to Kempston fire 2 for all Xbox-family pads; a game profile maps `west` to SPACE; on
that game `west` is SPACE, elsewhere fire 2.

Debug profiles layer the same way but on their own: global → machine → family → device. They never have a game level
(the debugger does not change per title).

## 7. Profile format

YAML (1.2), parsed by the vendored rapidyaml (`core/src/3rdparty/rapidyaml`): a config needs comments, which standard
JSON does not have (decided 2026-10-18). One schema, version 1, for every file; the WebAPI and MCP keep sending profiles
as JSON, the same tree. Play profiles live under `play/`, debug profiles under `debug/`, game profiles under `games/`;
a profile in the wrong directory is refused with a reason. Where the files are and how a game profile is found among
thousands: [§7.5](#75-files-bundles-and-the-index); shared binding sets: [§7.6](#76-schemes-extends).

### 7.1 Play profile: a Kempston game on an Elite Series 2

```yaml
schema: 1
kind: play
id: user/sabre-wulf
label: Sabre Wulf
match:
  signatures: ["tape:sha256:5e1c..."]
  file_sha256: ["3b2a..."]
  # a disk game: the TR-DOS file set for the catalog rule, two tokens per entry (n: the entry without its data;
  # c: code / BASIC, d: other, with the data; 16 hex each)
  # file_set: [n:51c0e9a2d4b37f18, c:9f2d0c1b7a6e5d4c, n:7d3e0a19c2b4f586, d:1a2b3c4d5e6f7081]
  title: Sabre Wulf
  zxdb_id: 4231
applies_to: { families: [xbox], devices: [] }
connectors:
  kempston: { device: kempston-joystick }
options: { ways: 8, deadzone: 0.15 }
layers:
  play:
    dpad.*:    { retro: "kempston:dpad" }
    lstick:    { retro: "kempston:dpad", ways: 8 }
    south:     { retro: "kempston:fire", label: Sword }
    rt.click:  { retro: "kempston:fire", turbo: classic, on: 2, off: 2, label: Sword (auto) }
    east:      { key: SPACE, label: Start }   # the title screen waits for SPACE
    menu:      { key: ENTER, press: tap }
    view:      { action: osd.keyboard }
    paddle.p1: { retro: "kempston:fire2" }
    paddle.p4: { layer: debug, mode: hold }
```

The `dpad.*` and `lstick` shorthands expand to the four directions. `kempston:dpad` expands to `up down left right`.
A key with a dot or a star is a plain YAML key; a target with a colon is quoted.

### 7.2 Play profile: a Next MD6 game, two pads

```yaml
schema: 1
kind: play
id: community/next-md6-two-players
label: Next, two MD 6-button pads
match: { machines: [next] }
connectors:
  next.left:  { device: md6-pad, mode: md1 }
  next.right: { device: md6-pad, mode: md2 }
players:
  - { device: "auto:1", connector: next.left }
  - { device: "auto:2", connector: next.right }
layers:
  play:
    dpad.*:   { retro: "auto:dpad" }
    lstick:   { retro: "auto:dpad" }
    south:    { retro: "auto:b" }
    east:     { retro: "auto:c" }
    west:     { retro: "auto:a" }
    north:    { retro: "auto:y" }
    lb:       { retro: "auto:x" }
    rb:       { retro: "auto:z" }
    menu:     { retro: "auto:start" }
    view:     { retro: "auto:mode" }
```

`auto:` in a target means "the connector this player drives". The NR `#05` mode the guest writes still decides what the
guest reads (MD on `#1F` / `#37`, or keys in Sinclair / Cursor modes): the profile fills the pad, the machine projects it.

### 7.3 Debug profile

```yaml
schema: 1
kind: debug
id: builtin/debug-elite2
applies_to: { families: [xbox-elite] }
layer_key: { control: paddle.p4, mode: hold }
layers:
  debug:
    lt:         { action: ttd.scrub_back, curve: scrub }
    rt:         { action: ttd.scrub_forward, curve: scrub }
    dpad.left:  { action: ttd.step_back_instruction, repeat: { delay: 15, rate: 4 } }
    dpad.right: { action: run.step_in, repeat: { delay: 15, rate: 4 } }
```

The full default set is in [debug-and-ttd.md §4](debug-and-ttd.md#4-default-debug-profiles).

### 7.4 Validation

Unknown controls, unknown connectors and unknown actions are errors that list the candidates (the media manager's
`unknown-slot` style); a binding to a control the device lacks (paddles on a DualShock) is kept and shown greyed, so one
profile serves several families. A unit test parses every file and every bundle entry of `data/controllers/profiles/`
(the built-ins' test today), so a broken profile fails the build's tests, not a player's session.

### 7.5 Files, bundles and the index

Profiles are data: none is written in C++. A CMake step turns `play/`, `debug/` and `schemes/` into
`generated/builtinprofiles.cpp` (one literal per file, `BuiltinProfileTexts()`); the games stay files.

| What | Where | Loaded |
|:--|:--|:--|
| Defaults: global, machine, family (§8), debug (debug-and-ttd.md §4) | `data/controllers/profiles/{play,debug}/*.yaml` | compiled in at build time (the `devices.json` way: a CMake step makes a source of them), all at start |
| Schemes (§7.6) | `data/controllers/profiles/schemes/*.yaml` | compiled in, all at start (a few dozen) |
| A game, one per file: every new profile, reviewed by its own PR | `data/controllers/profiles/games/<machine>/<slug>.yaml` | through the index, on a match |
| Legacy games in bulk: entries made from a database or a converted collection | `data/controllers/profiles/games/bundles/<name>.yaml`, one YAML document per game (`---` between them) | through the index, on a match |
| User profiles: the dialog's binds, Keep on the auto-decision toast | `<user config>/unreal-ng/controllers/profiles/{play,debug,games}/*.yaml` | all at start (a user has tens, not thousands); they rank above the shipped ones (§6.2) |

The game files and bundles are copied with the runtime data (`cmake/RuntimeData.cmake` already copies
`data/controllers`), not compiled in: a bundle of thousands of entries does not belong in a string literal (MSVC takes
16 380 bytes in one), and an update of the data needs no rebuild of the core.

**The index.** A tool (`tools/controllers/profile-index.py`) reads every game file and bundle and writes
`games/index.yaml` next to them; the loader also rebuilds it in memory when it is missing or older than a file, so a
profile dropped into the directory works without the tool. Per profile, one entry:

```yaml
# games/index.yaml - generated by tools/controllers/profile-index.py: do not edit
version: 1
profiles:
  - { id: builtin/game/vradarks-revenge, label: "Vradark's Revenge", machines: [NEXT], file: next/vradarks-revenge.yaml, offset: 0, length: 1054, keys: ["sha256:32ad0b5f..."] }
```

One line per profile; `file` is relative to `games/`, `offset` / `length` the bytes of its YAML document (a bundle
holds many), `keys` are `sha256:<hex>`, `sig:<signature>`, `zxdb:<id>` in lower case; a profile with
`match.file_set` has `file_set: [...]` too, which the loader indexes by token: a program's set finds its candidates by
a shared token and the catalog rule decides, without parsing a profile. `--check` fails when the
committed index is not what the files say, and the profile test compares the two too.

At start the loader reads the index only and keeps a map from every key to its entries. A key comes from what the
emulator knows about the program: the SHA-256 of a file it loaded, its signatures (`core/swsignature`), its file set,
the ZXDB id the metadata manager found. On a hit it parses that one entry (the `offset` / `length` slice of the file),
validates it and adds it to the store; a second hit is served from the store. The dialog's game list (manual choice,
§6.1 step 3, the program the emulator did not load itself - a NEX started from NextZXOS's browser) is the index's
labels with their machine, plus the user's own game profiles, also without parsing a profile. Duplicate keys over two
profiles are an error of the index tool, not a silent choice.

**The library.** The `controllers` library must not use core symbols (it is linked under the core), so rapidyaml is
its own static library (`core/src/3rdparty/rapidyaml/CMakeLists.txt`, target `rapidyaml`) that the core and
`controllers` both link. The profile parser keeps working on its tree (`JsonValue`): `ParseYaml` (`profiles/yaml.h`)
turns a rapidyaml tree into it (a quoted scalar is a string; a plain one null, a bool, a YAML 1.2 number or a string;
a duplicate key is an error), and the validation, the merge and the WebAPI / MCP JSON stay as they are. A text that
starts with `{` is read as JSON, so the user files of older versions still load (and are replaced by YAML when saved
again). When the index lies (a checkout changed the line ends, so the offsets moved), loading the entry rebuilds the
index in memory once. The dialog writes user files in
YAML; a comment a user put in such a file is not kept when the dialog rewrites it, and the file says so in its first
line.

### 7.6 Schemes: `extends`

Most games of the 1980s use one of a few control schemes: Kempston, Sinclair 1 or 2, Cursor, QAOP + Space, OPQA + M,
the Kempston mouse, a redefinable set. A scheme is a play profile of `kind: scheme` with no `match`; a game names it
and writes only what differs:

```yaml
schema: 1
kind: play
id: builtin/game/jetpac
label: Jetpac
extends: [scheme/kempston, scheme/qaop-space]   # merged in this order, then this profile's own fields
match: { file_sha256: ["..."] }
layers:
  play:
    north: { key: "1", label: One player }
```

The merge is the field-by-field merge of §6.2, so a scheme behaves as a profile one step below the game. A bundle
entry is mostly `id`, `label`, `match` and `extends`: a few hundred bytes. A scheme is not chosen by itself; a scheme
that names another scheme is allowed, a cycle is an error.

## 8. Defaults out of the box

The global default play bindings per family, used when no game or user profile applies. The retro target is the
connector `auto` chose ([retro-devices.md](retro-devices.md)); "fire" means the device's first fire.

| Control | Xbox / Elite | PlayStation | Switch Pro | Generic DB9 adapter | Keyboard |
|:--|:--|:--|:--|:--|:--|
| Directions | D-pad + left stick | D-pad + left stick | D-pad + left stick | stick (axes or hat) | numpad preset |
| Fire | `south` (A) | `south` (Cross) | `south` (B) | button 0 | kp0 |
| Fire 2 (Kempston D5, MD C) | `east` (B) | `east` (Circle) | `east` (A) | button 1 | kp5 |
| Fire 3 (D6, MD A) | `west` (X) | `west` (Square) | `west` (Y) | button 2 | - |
| SPACE | `north` (Y) | `north` (Triangle) | `north` (X) | - | - |
| ENTER | `menu` | `menu` | `menu` (+) | - | - |
| OSD keyboard | `view` | `view` | `view` (-) | - | - |
| Turbo fire | `rt.click` (classic, 2/2) | R2 | ZR | - | - |
| Kempston mouse | right stick | right stick | right stick | - | host mouse |
| Debug layer key | `paddle.p4` (Elite) or `view+menu` held | `view+menu` held (Edge: back button) | `view+menu` held | - | - (keyboard has shortcuts) |

**Next MD 6-button default** (`builtin/play/next-md-pad`, RetroArch's Genesis layout, owner 2026-10-18): the pad's two
rows of three keep their places, A B C on `west` / `south` / `east` and X Y Z on `lb` / `north` / `rb`, left to right;
`menu` → START, `view` → MODE. `south` → B is the Kempston fire (D4), so a game that reads `#1F` as a plain Kempston
fires on `south` as on every other machine. Only R L D U B C A START reach `#1F` / `#37`; X Y Z MODE are readable
through NR `#B2` only, and the dialog says so next to those buttons. jnext's layout (`joystick_dispatcher.cpp:58-90`:
`north` and `menu` → START, `rt.click` → Y) puts START under the thumb for 3-button games at the cost of Y on a
trigger; it is a user profile away, not the default.

**Kempston fire 2 and 3.** Classic interfaces read bits 5-7 as 0; the Evo 8-bit mode, the Next and homebrew use D5
(fire 2 / C) and D6 (A). The default binds them, but a connector whose board grounds D5-D7 ignores them, and the
picture shows those callouts as "not wired on this board".

## 9. Two players and more

- Each player is a device → connector pair. `auto` assigns devices in connection order to the machine's connectors in
  their declared order: Kempston first, then Sinclair 1, then Sinclair 2 on a +2A with a Kempston card; left then
  right on a Next.
- `players` in a profile pins them explicitly; MAME's `mapdevice` idea: a `HostDeviceId` substring pinned to player 1.
- Classic two-player setups: Sinclair 1 + Sinclair 2 (both keyboard overlays, independent), Kempston + Sinclair 1, Next
  left + right. The keyboard is a third device and can be a player too.
- Two identical pads are told apart by `HostDeviceId` ([architecture.md §5](architecture.md#5-device-identity-and-merging)).
  The dialog's **Identify** button rumbles the pad (or flashes its LED) and highlights it in the list.
- With several emulator instances, each device follows its instance assignment
  ([architecture.md §9](architecture.md#9-several-emulator-instances)); two players on two machines is two pins.
