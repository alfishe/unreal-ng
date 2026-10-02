# S4 input outcome: keyboard and mouse (2026-10-02)

Branch `sprinter-kbd`. The input half of phase S4 ([roadmap-and-plan.md](roadmap-and-plan.md) S4
row): the host keyboard and mouse reach the Sprinter. Design as built:
[tdd-accel-sound-input.md](tdd-accel-sound-input.md) §3.3.

## The symptom and its cause

BIOS 3.04 booted to its prompt in the GUI, but no key had any effect. The Z84C15 SIO channel A, where
the AT keyboard is wired, had no sender: the S1-S2 tests pushed scan codes into the SIO directly,
and nothing turned a host key into one. The shared key event of PLAN #55 E2b (a ZX key and a PC key
per host key, journaled once) already reached `Keyboard`; the Sprinter only lacked a PS/2 sink.

## How a key reaches the CPU

1. The Qt front end (or automation: `DebugKeyboardManager`, behind `type_input` and the WebAPI / MCP
   key tools) posts the ZX key and the physical PC key; both are journaled (TTD input kinds `Key`
   and `PcKey`) and applied on the machine's thread.
2. The ZX key sets the matrix that code `#40` (`IN #FE`) reads: the Spectrum mode.
3. The PC key goes to `SprinterInput` (the decoder's `IPs2KeySink`). Ctrl+Alt+Del and a bare F12
   act there first (CPU reset; turbo switch), then `Ps2KeyboardStream` queues the set 2 bytes.
4. Each byte arrives at SIO A at the end of its 11-bit frame (917 us = 3 210 T at 3.5 MHz). A full
   3-byte FIFO overruns (RR1 bit 5), as on the board: nothing holds the keyboard off.
5. BIOS SETUP and DSS poll SIO A from their frame INT. With ALL_MODE bits 0 and 3 set (SETUP runs
   with `#FF`) every byte also raises the PLD's INT (vector `#FF`, MAME's rule), held until the
   acknowledge.

| Reference | Keyboard INT rule |
|---|---|
| MAME `sprinter.cpp:1706-1718` | ALL_MODE `& #09 == #09`, one INT per 11 clock edges, 32 T pulse |
| PLD `SP2_1K30.TDF:319`, `KBD.TDF` (the `Last` sources) | ALL_MODE bit 0 only; the INT ~0.5 ms after the keyboard clock goes idle, latched until the acknowledge |
| INC `SP2000.inc:550-560` | bit 0: "KBD Int on", bit 3: "keyboard interrupt separate from the accelerator" |
| **Chosen** | MAME's enable (bits 0 and 3, as INC documents bit 3; whether the bitstream BIOS 3.04 loads matches the `Last` sources is **unverified**), the PLD's latch-until-acknowledge, at each byte's arrival |

## What works (tests)

| Behavior | Test |
|---|---|
| Set 2 stream: frame timing, queueing, typematic, 16-byte buffer + `#00`, Pause | `Ps2KeyboardStream_Test` (9) |
| Microsoft packets, split of a 128 move, buttons | `MsSerialMouse_Test` (4) |
| Sink attached, F4 in SIO A after one frame, overrun, in-time polling, keyboard INT on / off and the step hook coming and going, the ZX matrix from the same key, Ctrl+Alt+Del, F12, mouse packet on SIO B | `SprinterInput_Test` (9) |
| BIOS 3.04: F4 skips both IDE waits, ENTER at the prompt reboots, DEL on the boot screen opens SETUP; no overrun | `SprinterInputBoot_Test.Bios304_F4EnterDel` |
| BIOS 3.04: ESC at the prompt prints "Spectrum ROM not installed.  Use spectrum.exe  Press Ctrl+Alt+Del or RESET"; Ctrl+Alt+Del restarts the BIOS | `SprinterInputBoot_Test.Bios304_EscThenCtrlAltDel` |

All keys in the boot tests go through `DebugKeyboardManager` (the automation surface), which derives
the PC keys exactly as it does for ZX-Evo; no automation change was needed.

## Open points

- **DSS typing** (ACC-5 `DIR`) needs the floppy branch (`sprinter-s3a`, not merged when this was
  written); DSS polls SIO A the same way SETUP does, so it is expected to work. Spectrum mode with
  a ROM (DSS `SPECTRUM.EXE`) needs DSS too; the matrix path is unit-tested.
- **GUI shortcuts.** The Qt menu binds F4 to "speed 8x" (`unreal-qt/src/menumanager.cpp:616`), so in
  the GUI F4 may not reach the machine at the IDE wait. On a Mac keyboard DEL (SETUP) is Fn+Delete.
- **Not modeled:** keyboard commands (BIOS function `#EA` bit-bangs through SIO WR5; nothing waits
  for an answer), the mouse's `M` byte on DTR, the PLD's own stream-to-matrix decoder.
- **TTD:** the keyboard and mouse states are plain blobs (`Ps2KeyboardStream::State`,
  `MsSerialMouse::State`) for the S7 serializer; the Sprinter still refuses to record (S7).
- **MAME reference:** not used for this phase. The local MAME build runs with `-kbd ""` because the
  Microsoft Natural keyboard device ROM (`natural.bin`) is missing; a keyboard capture from MAME
  needs that ROM.
