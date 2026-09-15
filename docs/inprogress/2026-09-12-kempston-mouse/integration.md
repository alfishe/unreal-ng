# Kempston Mouse — Integration Guide

How Kempston Mouse emulation is wired into the core, the model port decoders, TTD, the Qt
desktop front end and the automation interfaces — **as built** on 2026-09-12. Line
citations are against the working tree on that date; the mouse files were still uncommitted,
so lines can move.

See also:
- [README.md](README.md) — status, decisions, open items
- [design.md](design.md) — design record with "As built" notes
- [hardware-reference.md](hardware-reference.md) — how the real hardware behaves
- [automation-interfaces.md](automation-interfaces.md) — CLI / WebAPI / Python / Lua / MCP

> **Corrections to the earlier version of this page.** It said the Scorpion joystick was
> narrowed to a `0x1F` low-byte match. It was not: the joystick matches **exactly `#FF1F`**,
> as it did before, and wins over the mouse because its decode arm runs first (§4.3). It also
> described capture code living in `DeviceScreen` with `QApplication::setOverrideCursor` and
> `F12`/`Ctrl+Alt` release keys; the code is in `MouseManager`, uses `setCursor`, and releases
> on `Esc` and focus loss only (§6). And it covered Pentagon and Scorpion only; every model
> decoder on this branch now decodes the mouse (§4).

---

## Glossary

| Term | Meaning here |
|---|---|
| **Counter** | X or Y register: 8-bit, wraps (255 + 1 = 0). |
| **Active-low** | Pressed button = bit `0`; `0xFF` = nothing pressed. |
| **MessageCenter** | The emulator's in-process publish/subscribe bus; delivers on its own worker thread. |
| **Funnel** | `DebugMouseManager`, the one class every mouse input passes through. |
| **Replay guard** | Refusing live input while TTD replays recorded history. |
| **Input journal** | TTD's timestamped list of input changes, fed back in during replay. |
| **Decode arm** | One `if / else if` branch in a model decoder's `DecodePortIn`. |
| **Fitted** | The config says the machine has the device (`[INPUT] Mouse=KEMPSTON`) and feature `kempstonmouse` is on. |

---

## 1. Architecture

```
 Desktop (unreal-qt)                     Automation (CLI / WebAPI / Python / Lua / MCP)
 ┌──────────────────────────────┐        ┌───────────────────────────────────────────┐
 │ DeviceScreen                 │        │ validates JSON / args, maps status to     │
 │  forwards Qt mouse/key/focus │        │ HTTP code / CLI text / exception          │
 │ MouseManager                 │        └─────────────────────┬─────────────────────┘
 │  capture, Esc release,       │                              │ direct call, caller's thread
 │  physical-pixel scaling,     │                              │ Move / Click / Wheel / ...
 │  sub-pixel + wheel carry     │                              │
 │  (macOS: native relative)    │                              │
 └──────────────┬───────────────┘                              │
                │ Post MC_MOUSE_MOVE/BUTTON/WHEEL              │
                │ MouseEvent{dx,dy | mask | steps, emulatorId} │
                ▼                                              │
 ┌──────────────────────────────┐                              │
 │ MessageCenter worker thread  │                              │
 └──────────────┬───────────────┘                              │
                ▼                                              │
 ┌──────────────────────────────┐                              │
 │ Mouse::OnMouseMove/Button/   │                              │
 │   Wheel: own emulator id?    │                              │
 └──────────────┬───────────────┘                              │
                │ ApplyHostMove/Buttons/Wheel                  │
                ▼                                              ▼
 ┌─────────────────────────────────────────────────────────────────────────────────┐
 │ DebugMouseManager (funnel)                                                      │
 │  1. replay active? -> refuse (automation: status ReplayActive; host: drop)       │
 │  2. automation only: range checks (±127 move, ±7 wheel, frames 1..65535, 0..255) │
 │  3. TTD recording? -> TimeTravelManager::RecordMouse*  (BEFORE the change)       │
 │  4. apply to Mouse                                                              │
 │  OnFrame (end of every frame): release a timed click through steps 1, 3, 4       │
 └──────────────────────────────────────┬──────────────────────────────────────────┘
                                        ▼
 ┌─────────────────────────────────────────────────────────────────────────────────┐
 │ Mouse (one per emulator, every model)                                           │
 │  atomic X (reset 31), Y (reset 85), buttons (0xFF), wheel (0..15)               │
 │  fitting: present, wheelEnabled  <- [INPUT] Mouse= / Wheel= + feature           │
 └──────────────────────────────────────┬──────────────────────────────────────────┘
                                        │ ReadRegister(0 | 1 | 2)
                                        ▼
 ┌─────────────────────────────────────────────────────────────────────────────────┐
 │ Model port decoders: standard decode A9=1, A5=0, gated (§4)                     │
 └─────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Core device (`Mouse`)

### 2.1 Lifetime and wiring

- `core/src/emulator/io/mouse/mouse.{h,cpp}`.
- Created in `Core::Init` for every model (`core.cpp:143-157`), stored in
  `EmulatorContext::pMouse` (`emulatorcontext.h:85`), deleted in `Core` teardown (`core.cpp:483-488`).
- `Core::Reset` calls only `Mouse::ApplyConfiguration()`. A machine RESET keeps the counters,
  buttons and wheel, as MiSTer does (`mouse.v` resets on `cold_reset`). Power-on values come
  from the constructor.
- Port decoders cache it as `_mouse` (`portdecoder.h:136`, set at `portdecoder.cpp:34`).

### 2.2 State

```cpp
std::atomic<uint8_t> _x{31};          // counter, reset 31
std::atomic<uint8_t> _y{85};          // counter, reset 85 (different from X on purpose)
std::atomic<uint8_t> _buttons{0xFF};  // active-low: D0 left, D1 right, D2 middle
std::atomic<uint8_t> _wheel{0};       // 4-bit counter
std::atomic<bool>    _present{true};      // fitting (config), kept by Reset, not saved by TTD
std::atomic<bool>    _wheelEnabled{false}; // fitting (config), kept by Reset, not saved by TTD
```

Why atomic: desktop input changes the device on the MessageCenter thread, automation on its
own thread, the click release on the emulator thread. `Move` and `SetWheel` are
compare-and-swap loops (`mouse.cpp:103-128`), so two moves that arrive together both land.
Worked example: X = 31, desktop +3 and automation +10 at the same instant → X = 44, never 34 or 41.

### 2.3 Registers (`ReadRegister`, `mouse.cpp:72-101`)

| Register | Address (canonical) | Value |
|---|---|---|
| 0 buttons | `#FADF` (also `#FEDF`, and all other mirrors) | wheel fitted: `(wheel << 4) \| 0x08 \| buttons`; no wheel: `0xF8 \| buttons` |
| 1 X | `#FBDF` | X counter |
| 2 Y | `#FFDF` | Y counter |

All three return `0xFF` from `ReadRegister` when not fitted — but in that case the decoders
do not route the read to the mouse at all (§4.1).

### 2.4 Configuration (`Mouse::ApplyConfiguration`, `mouse.cpp:54-70`)

```
present      = (no Mouse= key parsed  OR  Mouse=KEMPSTON)  AND  feature kempstonmouse on
wheelEnabled = (Wheel=KEMPSTON)
```

Called from the constructor, from `Core::Reset`, and from `FeatureManager` whenever a
feature changes (`featuremanager.cpp:435-439`). Parsing: `config.cpp:312-349`. Shipped
configs: `Mouse=KEMPSTON`, `Wheel=NONE` (why `NONE`: [design §7.2](design.md#72-mousewheel--three-modes-not-a-boolean)).

---

## 3. The funnel (`DebugMouseManager`)

`core/src/debugger/mouse/debugmousemanager.{h,cpp}`, owned by `DebugManager`
(`debugmanager.cpp:28`, accessor `GetMouseManager()` at `:118`). `MainLoop::OnFrameEnd` calls
its `OnFrame()` right after the keyboard manager's (`mainloop.cpp:572-574`).

| Entry | Used by | Range checks | Replay guard | TTD journal |
|---|---|---|---|---|
| `Move`, `Wheel`, `PressButton`, `ReleaseButton`, `SetPressedButtons`, `ReleaseAllButtons`, `SetCounters` | automation | yes | refuse with `ReplayActive` | yes |
| `Click` + `OnFrame` release | automation | frames 1…65535 | release skipped during replay | yes, press **and** release |
| `ApplyHostMove`, `ApplyHostButtons`, `ApplyHostWheel` | desktop via `Mouse::OnMouse*` | no (a fast flick may exceed 127) | silently dropped | yes |
| `GetState` | all front ends | — | — | — |

Results (`MouseInjectResult`) carry a `status` (`Ok`, `NoDevice`, `InvalidArgument`,
`ReplayActive`), a `message`, and on success an optional `warning`:
- mouse not fitted → `mouse not present: guest reads floating bus on the mouse ports`;
- `Wheel` with no wheel fitted → `no wheel fitted ([INPUT] Wheel=NONE): the guest does not see the wheel counter`.
The change is applied in both cases.

Click rules: a new `Click` releases a pending one first; `PressButton`/`ReleaseButton` on the
pending button, `SetPressedButtons` and `ReleaseAllButtons` cancel the pending release.

Bare contexts without a `DebugManager` (some unit tests) apply MessageCenter events directly
to `Mouse`, with no guard and no journal (`mouse.cpp:155-160`).

---

## 4. Port decoding

### 4.1 Shared helpers (`portdecoder.cpp:470-511`)

- `Standard_IsPort_KempstonMouse(port, reg)` — pure address rule:
  A9 = 1 and A5-A0 = `011111` (MiSTer `kemp_sel`); then A8 = 0 → buttons, A8 = 1 & A10 = 0 → X,
  A8 = 1 & A10 = 1 → Y. A15-A11, A7 and A6 are mirrors.
- `Default_IsPort_KempstonMouse(port, reg)` — the rule above **plus**: the mouse is fitted,
  `CF_DOSPORTS` is clear (TR-DOS ports off the bus), and no registered peripheral owns the
  exact address.
- `Default_Port_KempstonMouse_In(port, pc)` — returns `Mouse::ReadRegister(reg)`.

Worked examples (mouse fitted, TR-DOS off): `#FADF` and `#7ADF` → buttons; `#FBDF` and `#FB9F`
→ X; `#FFDF` → Y; `#FBFF` → not the mouse (A5 = 1); `#F9DF` → not the mouse (A9 = 0);
`#03C1` → not the mouse (A4-A0 ≠ `11111`).

### 4.2 Per model

| Decoder | Where the mouse arm sits |
|---|---|
| Spectrum 48K (`portdecoder_spectrum48.cpp:84-91`) | after AY mirrors and `#FE`, before the generic peripheral fallback |
| Spectrum 128K (`portdecoder_spectrum128.cpp:103`) | same pattern |
| Spectrum +3 (`portdecoder_spectrum3.cpp:97`) | same pattern |
| Profi (`portdecoder_profi.cpp:94`) | same pattern |
| Pentagon 128 (`portdecoder_pentagon128.cpp:105-111`), Pentagon 512 inherits | only for addresses the decode table did not claim |
| Scorpion ZS 256 / ProfROM (`portdecoder_scorpion256.cpp:230-237`) | after the joystick arm; silent while TR-DOS is selected; under the monitor latch not on the exact Beta low bytes |

Every arm is an `else if` in its decoder's chain. That matters: see §4.4.

### 4.3 Scorpion: joystick, FDC and mouse

- **Joystick:** `IsPort_KempstonJoystick` matches **exactly `#FF1F`** outside TR-DOS, with
  the DOS trigger disarmed and the monitor latch (`#1FFD` bit 1) clear. With the latch set the read
  goes to the WD1793 status, as MiSTer masks the joystick there. `#FF1F` also satisfies the
  mouse rule, so order decides: the joystick arm (`:217`) runs before the mouse arm (`:230`).
  `IN #FF1F` → joystick stub `0x00`. `IN #FB1F` → mouse X.
- **FDC, TR-DOS selected** (`ScorpionTrDosSelected()`: `CF_TRDOS` or DOS trigger armed): the
  Beta interface decodes A2-A0 = `111` (MiSTer `fdd_sel`), IN and OUT. A7 = 1 → system register
  `#FF`; A7 = 0 → WD1793 register by A6-A5 (`#1F/#3F/#5F/#7F`). That covers every mouse
  address, so `IN #FBDF` returns the system register and the mouse is silent.
- **FDC, monitor latch only** (`#1FFD` bit 1): the exact low bytes `#1F/#3F/#5F/#7F/#FF` stay with
  the FDC. The mouse keeps `#xxDF`/`#xx9F`. It gives up `#xx1F`/`#xx5F`, matching MiSTer's
  `~(scorp & beta_port & scorp_1ffd[1])` mask.
- **ProfROM detection:** page 5 `#08FB` reads `#FADF`, masks `#38`, and turns the mouse off
  unless all three bits are 1. With `Wheel=NONE` the idle byte is `0xFF` and detection passes.

### 4.4 Lesson: splicing into an if/else-if chain

The first Scorpion change inserted the mouse arm as a separate `if` after the chain. Every
earlier arm (`#FE` keyboard, AY readback, `#1FFD`, SMUC, `#7EFD`) still ran, but the trailing
`else` fallback of the new `if` then overwrote its result. Result: a dead keyboard on the plain
Scorpion. Fixed by making it an `else if`; pinned by
`ScorpionPorts_Test.KeyboardRowReadSurvivesLaterDecodeArms` (`scorpionports_test.cpp`).

---

## 5. TTD

- **Checkpoint state:** `Mouse` is a `TTDSerializable`, `PeripheralId::KempstonMouse = 7`
  (`ttdserializable.h:51`), registered as a core device for every model
  (`timetravelmanager.cpp:1052`). 8-byte blob: version, X, Y, buttons, wheel, 3 reserved.
  Fitting is not saved.
- **Input journal:** `TTDInputKind` = `Key`, `MouseMove`, `MouseButtons`, `MouseWheel`,
  `MouseCounters`, `KeyboardReset` (`ttdinputjournal.h:64-72`). Recorded through
  `TimeTravelManager::RecordMouseMove/Buttons/Wheel/Counters` and `RecordKeyboardReset`,
  stamped with frame + t-state, **before** the device changes.
- **Replay:** `InjectDueEvents(keyboard, mouse, now)` applies each event at its time point
  (`ttdinputjournal.cpp:47-105`). Live input from any source is refused while
  `ttdReplayActive` is set.
- **Keyboard, same rules:** `DebugKeyboardManager::ApplyKey` is the single guarded, journalled
  path for press/release/tap/combo/type/sequence; `ReleaseAllKeys` journals `KeyboardReset`;
  sequence state is behind a recursive mutex. Desktop keystrokes (`Keyboard::OnKeyPressed`,
  `OnKeyReleased`) check the replay guard and journal each press/release.
- **Not done:** the input journal is not written to `.ttd` files, and host motion is not
  batched per frame.

Worked example: TTD records; the desktop moves the mouse +3 right at frame 100 t-state 5000.
The journal gets `MouseMove{dx=3, dy=0}` at (100, 5000) and X goes 31 → 34. Seeking back to
frame 99 and replaying restores X = 31 from the checkpoint, then applies +3 exactly at
(100, 5000); any real mouse movement during the replay is ignored.

---

## 6. Qt desktop front end

### 6.1 Classes

- `DeviceScreen` (`unreal-qt/src/widgets/devicescreen.{h,cpp}`) creates a `MouseManager`,
  passing a callback that returns `displaySourceRect().size()` (`devicescreen.cpp:30`), sets
  its target emulator id when an emulator is attached (`:172`), and forwards
  `mousePress/Release/MoveEvent`, `wheelEvent`, key events and `focusOutEvent`
  (`:192-295`). `setMouseCaptured(bool)` / `isMouseCaptured()` are public.
- `MouseManager` (`unreal-qt/src/emulator/mousemanager.{h,cpp}`) owns everything that belongs
  to the host: capture, cursor, scaling, carries, button mask, and posting `MouseEvent`s.
- `MouseCaptureMacOS` (`unreal-qt/src/platform/macos/mousecapture_macos.{h,mm}`) — native
  relative mode on macOS; stubs returning `nullptr` elsewhere.

### 6.2 Capture

1. A click inside the screen calls `capture()`; that click is not sent to the machine.
2. `capture()` focuses the widget, `grabMouse()`, `setCursor(Qt::BlankCursor)`,
   `setMouseTracking(true)`, resets the carries and the button mask.
3. macOS: `MouseCaptureMacOS::Begin` warps the cursor to the widget centre once, detaches it
   from the mouse and reads `NSEvent.deltaX/deltaY` through a local event monitor. No
   Accessibility permission needed.
   Other platforms: warp to the centre with `QCursor::setPos` after every move and ignore the
   move event that the warp itself generates.

### 6.3 Scaling (`MouseManager::applyHostMotion`)

```
dpr          = devicePixelRatioF()
physPerEmuX  = widget.width  * dpr / source.width     // source = displayed source rect, emulated px
physPerEmuY  = widget.height * dpr / source.height    // X and Y computed separately
steps        = MouseDeltaAccumulator::Feed(dx*dpr, dy*dpr, physPerEmuX, physPerEmuY, scale)
post MC_MOUSE_MOVE(steps.dx, -steps.dy)                // screen Y down -> counter Y up
```

Worked example: 352×288 source drawn in a 704×576-point widget on a 2× Retina screen.
`physPerEmuX = 704 × 2 / 352 = 4`. The host moves 3 points right = 6 physical pixels →
1.5 emulated pixels → 1 step now, 0.5 carried; the next 3-point move gives 2.0 → 2 steps.
`scale` is 1.0 (`setScale` exists but nothing sets it from `MouseScale=` yet).

Wheel: `MouseWheelAccumulator` turns Qt angle units into whole notches (120 per notch),
carrying fractions from high-resolution wheels and trackpads.

### 6.4 Release

- `Esc`: releases capture; the press, its auto-repeats and its release are consumed so ZX
  BREAK is not tapped (handled whether the key arrives via the widget or the main window).
- Focus loss, switching the target emulator, or `setMouseCaptured(false)`.
- On release: tracking off, `releaseMouse()`, `unsetCursor()`, native capture ended, and if
  any button was still held, an all-released mask is posted.

---

## 7. Automation

All front ends call `DebugMouseManager` directly for the selected / addressed emulator. See
[command-interface.md §11](../../emulator/design/control-interfaces/command-interface.md#11-mouse-input-injection)
for the command reference and [automation-interfaces.md](automation-interfaces.md) for the
decisions.

| Front end | Source |
|---|---|
| CLI `mouse …` | `core/automation/cli/src/commands/cli-processor-mouse.cpp`, `cli-mouse-format.h` |
| WebAPI `/api/v1/emulator/{id}/mouse/*` | `core/automation/webapi/src/api/mouse_api.cpp` |
| Python `emu.mouse_*` | `core/automation/python/src/emulator/python_emulator.h` |
| Lua `mouse_*` | `core/automation/lua/src/emulator/lua_emulator.h` |
| MCP `mouse_input` | `core/automation/mcp/src/mcp-tools.cpp` (forwards to WebAPI) |
| OpenAPI | `core/automation/webapi/src/openapi/openapi_mouse.inc` |

---

## 8. Tests

| Test file | What it pins |
|---|---|
| `core/tests/emulator/io/mouse/mouse_test.cpp` | reset values, register layout, wrap, active-low buttons, wheel nibble, wheel hidden when not fitted, absence, MessageCenter events |
| `core/tests/emulator/io/mouse/mouse_messagecenter_test.cpp` | id routing on real instances (Pentagon / Scorpion / ProfROM), wrong kind and foreign id ignored, two instances isolated |
| `core/tests/emulator/io/mouse/mouse_delta_accumulator_test.cpp` | 1:1 identity, sub-pixel carry, DPR cancels, independent axes, scale, degenerate geometry, wheel notches |
| `core/tests/emulator/ports/models/portdecoder_pentagon_scorpion_mouse_test.cpp` | standard decode and mirrors, TR-DOS gating, Scorpion `#FF1F` joystick vs mouse |
| `core/tests/emulator/ports/models/scorpion_kempston_mouse_test.cpp` | ProfROM detection mask, cold boot keeps mouse enabled, service-monitor pointer follows motion |
| `core/tests/emulator/ports/models/scorpionports_test.cpp` | keyboard read survives later decode arms (else-if regression), mouse stub values replaced by real registers |
| `core/tests/debugger/mouse/debugmousemanager_test.cpp` | funnel validation and warnings, click timing (incl. under `RunNFrames`), replay guard, journalling of automation and host input, journal injection, concurrent moves, TTD blob and seek restore, machine reset, feature flag, keyboard timed-op / release-all / host-keystroke journalling |
| `core/tests/automation/cli-mouse-format-test.cpp` | CLI argument parsing and output formatting |
| `core/tests/automation/mcp-tools-test.cpp`, `mcp-dispatcher-test.cpp` | `mouse_input` routing, click pre-move sequencing, tool count |
| `tools/verification/webapi/src/test_api_mouse.py`, `test_api_interpreter.py` | live WebAPI contract (status shape, 400/404/409 cases, CORS), Python/Lua bindings through the interpreter endpoint, keyboard unknown-key 400 |

Not found: a single record → seek → replay → compare-hash test driven by live mouse motion.
Pass/fail of the suites above was not re-verified for this documentation update.
