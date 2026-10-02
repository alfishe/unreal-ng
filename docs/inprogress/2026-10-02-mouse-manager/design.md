# Mouse manager: one input path for every emulated mouse

**Created:** 2026-10-02. **Status:** design, not started (see [TODO.md](TODO.md)).

## 1. Why

The host mouse no longer reaches the emulated machine with the default settings.
At the same time, each emulated mouse gets its input its own way, or does not get
it at all. This document replaces the per-device paths with one manager per
emulator. Every source of mouse input (the host window, the five automation
surfaces, TTD replay) goes into the manager. The manager hands the input to every
mouse device the machine has.

### 1.1 What is broken today (code on master c7c6e77f0)

| # | Problem | Where |
|:--|:--|:--|
| 1 | **The GPU screen has no mouse.** Since the GPU window became the default renderer (first master commit with both: 21909a666, 2026-09-14), clicks, motion and the wheel go nowhere. Only the software renderer (`DeviceScreen`) has a mouse manager, so the mouse works only with View → GPU acceleration off. | `unreal-qt/src/widgets/devicescreenglwindow.cpp` (`mousePressEvent` is `Q_UNUSED`; no move, wheel or focus handlers) |
| 2 | **Capture blocks the menus.** `grabMouse()` is called on capture, against the Kempston design (§5.6a: "no grabMouse()"). | `unreal-qt/src/emulator/mousemanager.cpp` `capture()` |
| 3 | **Any click captures, and the click is lost.** There is no visible capture state and no manual toggle: `captureChanged` is connected to nothing, `setMouseCaptured` has no callers. | same file, `handleMousePress` |
| 4 | **Esc is the hard-wired release key and is swallowed.** On PS/2 machines (TS-Conf, ZX-Evo, Sprinter, ATM) the guest never sees the first Esc while the mouse is captured. The configured `lockmouse` key is never read. | same file, `kReleaseKey` |
| 5 | **Host and automation buttons overwrite each other.** The host posts an absolute mask kept in the Qt object. Automation reads the device's buttons, changes one bit and writes the mask back, possibly before queued host events are applied. | `debugmousemanager.cpp` (`GetButtons()` read-modify-write) |
| 6 | **Every device filters every event.** Each `Mouse` instance and each ZX-Poly group subscribes to all `MC_MOUSE_*` topics and drops events for other emulators. | `mouse.cpp` `AcceptEvent`, `zxpolygroup.cpp` `OnHostMouse` |
| 7 | **Devices without their own input.** The Sprinter serial mouse samples the Kempston counters, so `Mouse=NONE` also takes the serial mouse away (capture needs the Kempston device fitted). | `sprinterinput.cpp` |
| 8 | **Devices that are not modeled.** The ZX-Evo / TS-Conf mouse is a PS/2 mouse read by the AVR. The decoders read the Kempston counters directly, and the AVR's own behavior (its resolution setting, wheel nibble) is missing. ATM450 / ATM710 decode no mouse although their configs set `Wheel=KEMPSTON`. The AY mouse is not emulated. | `portdecoder_atm3.cpp`, `portdecoder_tsconf.cpp`, `evoavr.*` |

The "grab only when the program reads the mouse" idea
([polling-detection-and-grab-control.md](../2026-09-12-kempston-mouse/polling-detection-and-grab-control.md))
was never implemented; no code from it exists in history. It did not break
anything. Problem 1 did.

## 2. Terms

| Term | Meaning |
|:--|:--|
| Host mouse | the mouse of the computer the emulator runs on |
| Capture | the emulator window takes the host mouse: the pointer is hidden, its motion goes to the emulated machine |
| Mouse device | an emulated piece of hardware that a program reads mouse data from: Kempston interface, Sprinter serial mouse, ZX-Evo PS/2 mouse, AY mouse |
| Mouse input | one change: motion (dx, dy), the set of pressed buttons, or wheel steps |
| Manager | `MouseManager` in core, one per emulator instance |

## 3. Design

### 3.1 Core: `MouseManager`, one per emulator

```
 host window ─┐
 WebAPI/MCP/  ├─► MouseManager::Submit(input) ──► TTD journal (record)
 CLI/Lua/Py  ─┘          │          ▲
                         │          └── TTD / RZX replay (the only source while replaying)
                         ▼   on the emulator thread, at an instruction boundary
                 MouseManager::Apply(input)
                 ├─► Kempston interface   (counters, buttons, wheel nibble)
                 ├─► Sprinter serial mouse (3-byte packets on SIO B)
                 ├─► ZX-Evo AVR PS/2 mouse (packets → AVR → Kempston registers)
                 └─► AY mouse (later)
```

- **One state per emulator:** the pressed buttons (one mask), and nothing else. Motion
  and wheel are deltas; each device turns them into its own state (counters, packets).
- **Devices register.** A machine's port decoder registers the mouse devices the
  machine has, through `IMouseSink`:

  ```cpp
  class IMouseSink
  {
  public:
      virtual ~IMouseSink() = default;
      virtual void OnMouseMotion(int dx, int dy) = 0;   // dy > 0 = up
      virtual void OnMouseButtons(uint8_t pressed) = 0;  // bit 0 left, 1 right, 2 middle; 1 = pressed
      virtual void OnMouseWheel(int steps) = 0;          // > 0 = away from the user
  };
  ```

  Decoders stop reading another device's counters (Sprinter, ATM3, TS-Conf today).
- **`HasMouseDevice()`** tells the front end whether capturing makes sense at all.
- **Buttons are changed in one place.** Press / release / click from automation and the
  host's mask are all applied on the emulator thread against the manager's mask.
  The read-modify-write happens there, so nothing is lost (problem 5).
- **Routing by emulator.** The front end posts `MC_MOUSE_*` tagged with the shown
  emulator's id; each emulator's manager is the only subscriber (one per
  emulator, not one per device); no device subscribes to the message center
  (problem 6). A ZX-Poly group's manager fans out to its four members at the
  frame boundary, as today.
- **TTD.** The journal records at `Submit` (the existing `TTDInputKind::Mouse*`
  records); replay feeds `Apply` and refuses live input, as `DebugMouseManager`
  does now. Device state (Kempston counters blob id 7, Sprinter input blob 31, the
  new AVR mouse state) stays in each device's blob.
- **`DebugMouseManager`** becomes the automation facade over the manager. Its API
  and the five surfaces keep their names and JSON. Validation (±127, wheel ±7)
  stays.

### 3.2 The devices

| Device | Machines | What changes |
|:--|:--|:--|
| Kempston interface (`Mouse`) | 48K, 128K, +3, Pentagon, Scorpion, Profi, Sprinter (PLD port), ATM3 / TS-Conf *only through the AVR* | becomes an `IMouseSink`; no message center subscription |
| Sprinter serial mouse (`MsSerialMouse`) | Sprinter | a sink that is always fitted (the board has it), so `Mouse=NONE` no longer removes the host mouse. It still samples the board's mouse counters, which the Kempston device keeps for both views of the one mouse (the counters move whether or not the Kempston port is fitted); its TTD blob is unchanged |
| ZX-Evo AVR PS/2 mouse (new, in `EvoAvr`) | ATM3 (ZX-Evo), TS-Conf | the host mouse becomes PS/2 packets (wheel mouse, ID 3, as the AVR's init sequence asks); the AVR firmware logic (`ps2.c`: buttons `(b ^ 7) & 0x0F`, X / Y added to 8-bit counters, wheel steps in the high nibble, keypad resolution keys) updates the Kempston registers the FPGA shows. The decoders read those registers |
| AY mouse | (configs ask for it, `Mouse=AY`) | later; stays "not emulated" until a machine needs it |
| ATM450 / ATM710 | ATM Turbo 2 | check the hardware first (does an ATM2 have a mouse at #FADF?); either decode Kempston or drop `Wheel=KEMPSTON` from the configs |

Source for the AVR behavior:
[zx-evo avr/current/ps2.c](https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/ps2.c)
(packet bytes 1-4, `ps2mouse_init_sequence`, `ps2mouse_set_resolution`).

### 3.3 Front end: one capture controller per main window

- **Both renderers.** A `MouseCaptureController` owned by the main window gets events
  from whichever screen widget is active (software `DeviceScreen` or GPU
  `DeviceScreenGLWindow`, through the wrapper). The per-widget `MouseManager` in
  unreal-qt goes away.
- **When it captures** (agreed 2026-10-02; the polling-based automatic grab stays
  out of scope): a click on the screen captures only if the machine has a mouse
  device (`HasMouseDevice()`) and the mouse gate is open. That click is not passed
  to the machine.
- **Toolbar button: indicator and gate.** One button on the main toolbar, three looks:

  | Look | State | Click on the button |
  |:--|:--|:--|
  | mouse, normal | gate open, not captured (the machine has a mouse device) | closes the gate |
  | mouse, highlighted | captured | releases and closes the gate |
  | mouse, crossed out | gate closed | opens the gate |
  | mouse, grayed out | the machine has no mouse device | none |

  **Gate closed:** the window never captures and passes no host mouse input to the
  machine, whatever is clicked. Automation input (WebAPI, MCP, CLI, Lua,
  Python) and TTD replay still reach the machine: the gate is about the host
  mouse only. The gate is remembered per window across emulator switches. View →
  Mouse gate is the same toggle as a menu item.
- **When it releases:** Ctrl+Esc (the physical Control key on every platform, macOS
  included; `[INPUT] MouseReleaseKey=` names physical keys; a plain Esc reaches the
  machine), focus loss, leaving the application, switching or closing the emulator.
  While captured, an application-wide event filter catches the release key in any
  window, before shortcuts and the machine see it (unreal-qt-tests checks it with
  real Qt key events).
- **No `grabMouse()`.** Motion comes from the platform backend: macOS
  `CGAssociateMouseAndMouseCursorPosition(false)` + an NSEvent monitor (as now);
  elsewhere warp-to-center. The macOS dissociation is undone on every release
  path, also from the application's shutdown.
- **What it posts:** relative motion in physical pixels (`MouseDeltaAccumulator`,
  `[INPUT] MouseScale`), buttons with `[INPUT] SwapMouse`, wheel steps
  (`MouseWheelAccumulator`), all to the shown emulator's manager.

### 3.4 Configuration

Kept: `[INPUT] Mouse=NONE|KEMPSTON|AY`, `Wheel=`, `SwapMouse=`, `MouseScale=`.
`Mouse=` selects the Kempston interface only; machine-built devices (the
Sprinter serial mouse, the ZX-Evo AVR mouse) are part of the machine. New:
`[INPUT] MouseReleaseKey=` (default `Ctrl+Esc`), replacing the unread
`main.lockmouse`.

## 4. Phases

Each phase ends with a green build (zero warnings), green `core-tests` and, where
the user can see it, a live check through the WebAPI and the Qt window.

| Phase | Content | Tests |
|:--|:--|:--|
| M1 | Core `MouseManager` + `IMouseSink`; Kempston and Sprinter serial mouse as sinks; `DebugMouseManager` on top; message center subscriptions removed; button mask in one place | manager unit tests (fan-out, button merge host + automation, replay refusal); existing mouse / TTD / ZX-Poly tests unchanged |
| M2 | Qt `MouseCaptureController` for both renderers; no `grabMouse()`; release key; toolbar indicator / gate button (normal, captured, crossed out, grayed out) and its menu item | manual checks per platform; a Qt-free unit test of the capture and gate state machine |
| M3 | ZX-Evo AVR PS/2 mouse (ATM3, TS-Conf): packets, firmware logic, resolution keys, TTD blob | AVR mouse unit tests from `ps2.c` behavior; a program reading #FADF/#FBDF/#FFDF on both machines |
| M4 | ATM450 / ATM710 decision from the hardware; AY mouse if a machine needs it | per finding |
| M5 | Automation docs: `.recipe/input/mouse.md` (missing today), surfaces' docs, OpenAPI unchanged unless M1 adds fields | link check |

## 5. Open points

- ATM Turbo 2 mouse: hardware sources first (M4).
- Journal size: host motion is journaled per event; batching per frame is a later
  optimization (it changes the recorded timing, so it needs its own TTD check).
