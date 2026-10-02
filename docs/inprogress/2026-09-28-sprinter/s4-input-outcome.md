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

## Flex Navigator follow-up (2026-10-02, branch `sprinter-fn-input`)

The owner's report: in the GUI, Tab did not switch FN's panels and the mouse did nothing.

- **Keys: the core was right.** FN reads keys through DSS (KEYSCAN reads the set 2 bytes from SIO A; Tab = `#0D`,
  DSS position code `#0F`); through the automation Tab, the arrows and the rest worked on FN 1.10 (DSS 1.62 floppy)
  and FN 1.15 (DSS 1.71 HDD). **The Qt layer ate Tab:** on the software renderer `QWidget::event` offers Tab to
  `focusNextPrevChild` before `keyPressEvent`, so the focus jumped to the main window (whose filter forwards keys)
  and back: every other Tab was lost. `DeviceScreen` / `DeviceScreenGL` now refuse focus traversal, and the GPU
  window container passes a Tab it receives to the GL window. Verified live with native key events (System Events).
- **Mouse protocol.** DSS 1.62.9x (`intmouse.asm`, the community build) reads the PLD's Kempston view (`#FADF`
  buttons, `#FBDF` X, `#FFDF` Y, code `#58`); DSS 1.60 and 1.71 (`SYSTEM.DOS` 1.71: WR4 `#44`, WR3 `#41`, CTC0 /45
  for the SIO B clock) read Microsoft serial packets on SIO B, synced on bit 6, with no `M` identification. FN only
  calls the DSS driver. **Fix:** code `#58` read the optional Kempston interface (`#FF` with `[INPUT] Mouse=NONE`);
  it is the PLD's view of the board's own mouse (MAME reads it unconditionally), so it now reads the counters
  through `SprinterInput::ReadMouseView`, the same source as the serial packets.
- **The GUI mouse** does not work on the GPU renderer for any machine (the GL window has no mouse capture); that is
  the shared mouse manager work (`docs/inprogress/2026-10-02-mouse-manager/`). For the Sprinter it must deliver
  relative moves and the button mask (D0 left, D1 right, D2 middle) to one per-machine set of counters that both
  views read, capture on the GL window, and not refuse capture when no Kempston interface is configured.
- **Tests:** `SprinterInput_Test.MouseWithoutAKempstonInterface`, `SprinterFlexNavigator_Test.Dss162_Fn110KeysAndMouse`
  (Tab, Down, a mouse click on the right panel, `Mouse=NONE`), `RealHdd_Fn115KeysAndMouse` (`UNREAL_SPRINTER_HDD`).
- **Open:** after a panel switch FN 1.10 reads the floppy with interrupts off for ~30 frames; keys sent then overrun
  the SIO FIFO (whether the real machine reads that fast is the S5 floppy-timing question).
