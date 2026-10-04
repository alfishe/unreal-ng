# Automation mouse: the machine's own mouse, every model

**Created:** 2026-10-03. **Status:** implemented on branch `mouse-api-routing` (see [DONE.md](DONE.md)).

## 1. Why

The automation mouse (WebAPI `/mouse/*`, MCP `mouse_input`, CLI `mouse`, Lua `mouse_*`, Python
`emu.mouse_*`) was written for the Kempston mouse interface: its texts, its status and its
"mouse not present" rule all talked about the Kempston counters and ports `#FADF` / `#FBDF` /
`#FFDF`. The emulator's `MouseManager` already handed every input to every mouse device of the
machine ([mouse manager design](../2026-10-02-mouse-manager/design.md)), so the input itself reached
the Sprinter's serial mouse and the ZX-Evo PS/2 mouse. What was missing:

| # | Gap | Effect |
|:--|:--|:--|
| 1 | Status reported the Kempston interface only | On the Sprinter an agent saw `present: false` (with `Mouse=NONE`) and counters that no program reads; nothing about the serial line, SIO B, the packets |
| 2 | A machine with no mouse answered "success" with a warning | A script went on as if the click had happened |
| 3 | Moves were limited to ±127 per call, and two calls in a row could wrap the 8-bit counters | To reach FN's drive icon (hundreds of pixels away) an agent sent several moves; without frames in between the counters jumped by more than 127 between two reads, and the program saw a move the other way. The owner could not click the Flex Navigator drive icon |
| 4 | The Sprinter board mouse counted as "in use" whenever fitted | The GUI captured the host mouse on a click even in BIOS SETUP, where nothing reads it (open item of the mouse manager, PLAN #59) |

## 2. Inventory: which mouse each machine has

| Model(s) | Device (`id`) | How it is fitted | What the program reads |
|:--|:--|:--|:--|
| `48K`, `128k`, `PLUS2`, `PLUS2A`, `PLUS3`, `PENTAGON`, `SCORPION`, `PROFSCORP`, `PROFI`, `PROFI3`, `ZXPOLY-*` | Kempston interface (`kempston`) | `[INPUT] Mouse=KEMPSTON` (default in every shipped config) and feature `kempstonmouse`; `Wheel=KEMPSTON` adds the wheel nibble | `#FADF` buttons (+ wheel), `#FBDF` X, `#FFDF` Y |
| `ATM710`, `ATM450` | Kempston interface (`kempston`), an external ZX-bus card | `[INPUT] Mouse=KEMPSTON`, `Wheel=KEMPSTON` in their configs (the boards have no mouse) | the same ports, the wheel nibble |
| `ATM3` (ZX-Evo), `TSL`, `TSL-VDAC2` (TS-Conf) | PS/2 wheel mouse on the AVR (`evo-ps2`) | `[INPUT] Mouse=KEMPSTON` = a mouse plugged into the PS/2 port; `NONE` = none | the AVR's registers at the Kempston addresses (found: X = 0, Y = 1) |
| `SPRINTER` | the board mouse (`sprinter`): a Microsoft serial mouse on the Z84C15's SIO B and the PLD's Kempston view of the same counters (port code `#58`) | always, part of the board; `Mouse=` only fits the optional Kempston interface, which no Sprinter program reads | DSS 1.71: 3-byte packets on SIO B at 1 200 baud (receiver clocked by CTC ZC0); DSS 1.62.9x: `#FADF` / `#FBDF` / `#FFDF` |

Each machine has one mouse device that its programs read. A Kempston interface object exists on every
machine (configuration holder, TTD blob 7), but on the Sprinter, ZX-Evo and TS-Conf the mouse ports
read the board's own mouse (`PortDecoder::HasMachineMouse`); there the Kempston object is **not wired**
and automation does not list it.

## 3. The device abstraction

`IMouseSink` (one per device, registered with the emulator's `MouseManager`) gained:

| Method | Meaning |
|:--|:--|
| `IsMouseWired()` | the machine's ports read this device (false: the Kempston object on a machine with a board mouse) |
| `DescribeMouse()` | a `MouseDeviceStatus`: `id`, `name`, `kind` (`kempston`, `serial-microsoft`, `ps2-avr`), fitted, in use, wheel, buttons, the counters, the three registers at the Kempston addresses, and per kind the serial line or the PS/2 state |
| `HasUnreadMotion()` | motion applied that the program has not read yet (glide pacing, §4) |
| `MotionStepLimit()` | the largest step a program can tell from a step the other way between two reads: 127 for 8-bit counters, `127 >> resolution` for the AVR mouse |

`MouseManager` answers for the machine: `DescribeDevices()` (the wired devices),
`DescribeDefaultDevice()` (the first fitted one: "the machine's mouse"), `DescribeDevice(id)`,
`HasMouseDevice()` (some wired device is fitted), `HasUnreadMotion()`, `MotionStepLimit()`.
Input still goes to every device (one mouse seen several ways, mouse manager §3.1): the TTD journal
records the input at the manager, not per device, so a recording does not depend on which device
an agent looked at. The `device` selection on the surfaces chooses whose status is reported; it is
validated (an id the machine does not have is a 400 with the list of ids it has).

### What status shows per device

| Kind | Fields |
|:--|:--|
| all | `id`, `name`, `kind`, `fitted`, `in_use`, `wheel`, `buttons` (2 or 3), `x`, `y`, `button_mask`, `ports` (`FADF`, `FBDF`, `FFDF`: what the program's `IN` returns now) |
| `serial-microsoft` (Sprinter) | `serial`: `baud` (1 200), `receiver_baud` (CTC ZC0 / the SIO clock mode), `receiver_in_tune` (within 5 %), `receiver_enabled` (SIO WR3 bit 0), `packet_in_flight`, `packet` (3 hex bytes), `packet_bytes_sent`, `pending` (motion not yet in a packet), `packets_sent`, `bytes_received`, `framing_errors`, `receiver_fifo`, `receiver_overrun` |
| `ps2-avr` (ZX-Evo, TS-Conf) | `ps2`: `connected`, `resolution` (0-3), `counts_per_mm`. The AVR applies each PS/2 packet to its registers at once, so there is no queue to show |

Worked example (Sprinter, DSS 1.71 running, automation `move 5 3` and `press left`): one packet
`#6C #05 #3D` (left, 5 right, 3 up), status `packets_sent` 1, `bytes_received` 3, `receiver_baud`
1 215.3 (`in tune`), `ports.FBDF` 36 (the PLD view of the same counters).

## 4. Glide: long moves a program can follow

A program reads 8-bit counters (Kempston, the AVR registers) or packets of at most ±127 (the serial
mouse samples the board counters). Between two reads a counter may move at most 127, or the program
sees a move the other way. `Glide(dx, dy)` (±4096 per axis) splits the move:

1. the first step (at most `MotionStepLimit`, 127) is applied now, as a `move` would be;
2. at each frame end (`DebugMouseManager::OnFrame`, the emulator thread) the next step is applied once
   the program has taken the last one: `HasUnreadMotion()` is false (Kempston / AVR: X and Y read since
   the move; serial: no packet on the wire and no motion left to packetize), or after at most
   `GLIDE_WAIT_FRAMES` (10) frames, or at once when no program reads the mouse;
3. input sent while a glide is in progress (move, press, release, buttons, wheel, click) is queued
   behind it and applied in order, one item per frame (a press and its release never share a frame);
   a click after a glide lands where the glide ended. `release_all` drops the queue and releases.

Every step is ordinary journaled input (`MouseMove` at the frame end it was applied), so a TTD replay
plays the same steps at the same times; during a replay the queue is dropped (the journal owns input).

Worked example (Kempston, a program reading the mouse every frame): `glide 300 0` -> X += 127 now,
+127 at the end of the next frame, +46 at the end of the frame after; a `click left` sent right after
the glide is pressed at the frame end after the last step and released 2 frames later.

**Absolute positions.** There is still no "move the pointer to (x, y)" (automation-interfaces §4.2.7:
the program keeps its own pointer; the emulator does not know where). What works honestly where the
program clamps its pointer at the screen edge: *home* with a glide far into a corner (`glide -1000
1000`: up and left), then glide by the target's coordinates. FN 1.15 clamps and moves 1 pixel per
count in the 640-pixel mode, so the drive icon "D" is reached that way
(`SprinterFlexNavigator_Test.RealHdd_Fn115ClickTheDriveIcon`). A program that scales or does not clamp
needs the closed loop of §4.2.7.

## 5. No mouse: refused, with the reason

When no wired device of the machine is fitted (Kempston machines and ZX-Evo / TS-Conf with
`[INPUT] Mouse=NONE` or the feature `kempstonmouse` off) every input call fails with
`NoMouseFitted`: WebAPI **409** `{"error":"Conflict","reason":"no_mouse","message":"no mouse fitted on
this machine: ..."}`, Python `RuntimeError`, Lua `nil, message`, CLI `Error: ...`, MCP the WebAPI's
error. Status stays 200 with `mouse_fitted: false`, `device: null` and a `warning`. Before, the call
succeeded with the warning "mouse not present: guest reads floating bus". The Sprinter never answers
409: its mouse is part of the board.

## 6. The Sprinter in the GUI: capture only while polled

The GUI path was already host mouse -> `MouseCaptureController` -> `MC_MOUSE_*` -> `MouseManager` ->
`SprinterInput` (mouse manager M1/M2, `s4-input-outcome.md`). What was left: the board mouse counted
as "in use" whenever fitted. Now `SprinterInput::IsMouseInUse()` is true only when a program read the
board mouse within `kPolledWithinFrames` (50) frames:

- an access to SIO B (`#1A` / `#1B`: DSS 1.71's serial driver polls it from its frame interrupt), or
- a read of the PLD's Kempston view (port code `#58`; DSS 1.62.9x).

The status read (`PeekMouseView`) does not count. BIOS SETUP reads neither, so a click there captures
nothing; under DSS 1.71 with FN the click captures and the host mouse drives FN's pointer. The poll
frames are not machine state (not in TTD), as the Kempston interface's are not.

## 7. Tests

| Test | What it proves |
|:--|:--|
| `DebugMouseManagerMachines_Test.InputReachesTheMachinesMouse/*` (19 models) | status names the right device, one device per machine; move / press / wheel / click / release-all / glide + queued click give the golden register values (Kempston: 31/85 -> 36/88, `#FE`; wheel nibble on ATM; AVR found values and its wheel nibble; Sprinter's view) |
| `DebugMouseManagerMachines_Test.MouseNoneRefusesOrKeepsTheBoardMouse/*` (19 models) | `Mouse=NONE`: every input call `NoMouseFitted` with the reason, ports `#FF`; the Sprinter keeps its mouse |
| `DebugMouseManagerMachinesTtd_Test.KempstonGlideReplaysExactly` | 48K: a glide and a queued click recorded; seek to the start and a run forward match every checkpoint (CPU, devices) |
| `TTDSprinterMachine_Test.ExactRestore_GlideAndQueuedClickOnTheSerialMouse` | Sprinter: the program logs packets adding up to the glide exactly, then the click; exact replay from the start and from the middle of the glide |
| `SprinterInput_Test.ApiMoveAndPressBecomeOnePacket`, `GlideReachesTheProgramAsExactPacketsThenTheClick`, `BoardMouseInUseOnlyWhilePolled` | packet bytes from API moves / clicks; no packet over 127, sums exact, the click after the motion; capture only while polled |
| `DebugMouseManager_Test.Absent_RefusedWithReason` | the old "accepted with a warning" case now refuses |
| `SprinterFlexNavigator_Test.RealHdd_Fn115ClickTheDriveIcon` (`UNREAL_SPRINTER_HDD`) | the owner's case: home, glide to FN's drive icon "D", click: the left panel lists drive D; "C" brings the old list back |

## 8. Glossary

| Term | Meaning |
|:--|:--|
| Wired | the machine's mouse ports read this device |
| Fitted | the device is there (config, or part of the board) |
| In use | a program read the device in the last second (50 frames) |
| Glide | a long relative move applied in steps a program can follow |
| Homing | a glide far into a corner so the program clamps its pointer there: a known position to glide from |
