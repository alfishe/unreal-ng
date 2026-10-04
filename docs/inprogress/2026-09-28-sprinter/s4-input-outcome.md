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

## Mouse baud from CTC ZC/TO0 (2026-10-02, branch `sprinter-ctc-trg`)

SIO B now receives with the clock the board gives it, CTC ZC/TO0 (MAME `sprinter.cpp:2006-2007`): the mouse still
sends at 1 200 baud, and its characters arrive only while ZC/TO0 / the WR4 clock mode is within 5 % of that
(DSS 1.71: CTC 0 `#55` with 45, x16 = 1 215 baud, seen live). Otherwise they are lost and counted
(`z84c15.mouse.framing_errors`). Tests: `SprinterInput_Test.MouseNeedsSioBClockedAt1200Baud`; the other serial-mouse
tests program the clock as DSS 1.71 does.

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

## The board mouse on the shared MouseManager (2026-10-02, branch `sprinter-mouse`)

The mouse manager (`docs/inprogress/2026-10-02-mouse-manager/`) is the emulator's one mouse input path: the host
window, the automation (WebAPI, MCP, CLI, Lua, Python) and TTD replay go in, every mouse device of the machine
gets every input. The Sprinter's board mouse is one `IMouseSink` with two views:

- **`SprinterInput` owns the board mouse counters** (X, Y, buttons; power-on X = 31, Y = 85 as the Kempston
  interface's). `OnMouseMotion` / `OnMouseButtons` / `OnMouseCounters` move them; the wheel is ignored (a
  two-button Microsoft mouse, no wheel nibble in code `#58`). The serial mouse samples them for SIO B packets
  (DSS 1.71), `ReadMouseView` reads them for `#FADF` / `#FBDF` / `#FFDF` (DSS 1.62.9x). Nothing in the
  Sprinter reads the Kempston device (`Mouse`) any more; that interface is another sink of the same manager and
  sees the same input when it is fitted.
- **Always fitted** (`IsMouseFitted`): `MouseManager::HasMouseDevice()` is true for the Sprinter whatever
  `[INPUT] Mouse=` says, so the front end captures the host mouse and the toolbar's mouse button is live.
  With `Mouse=NONE` the automation no longer warns "mouse not present" when another device of the machine reads
  the input (`DebugMouseManager`, `GET /mouse/status`).
- **Buttons:** the manager's active-low mask, D0 left, D1 right, D2 middle. The Kempston view shows all three;
  the serial packet carries left (bit 5) and right (bit 4).
- **TTD:** input is journaled at the manager (`MouseMove` / `MouseButtons` / `MouseCounters`, unchanged).
  Blob 31 (`SprinterInput`) is now version 2, 88 bytes: the v1 layout plus the board counters (bytes 85-87);
  the Sprinter fixture `testdata/machines/sprinter/ttd/boot.ttd` was re-recorded (no other fixture changed).
- **Device state:** `sprinter.z84c15.mouse` reports the counters, the packet in flight and the SIO B FIFO.

What FN does with the mouse (FN 1.10 and 1.15 alike): the pointer moves 1 pixel per count in the 640-pixel mode;
a left click on the inactive panel activates it, a left click on a row puts the cursor bar there; a right click
marks the file under the bar and moves the bar down (directories are not marked); the middle button does nothing.

**Tests:** `SprinterInput_Test.MouseMoveReachesSioB`, `MouseWithoutAKempstonInterface`, `BoardMouseHasItsOwnCounters`,
`MouseButtonsMapping`; `SprinterMouseMachine_Test.EverySourceReachesTheBoardMouseWithMouseNone` (automation, host
buttons composed with automation's, the GUI's `MC_MOUSE_MOVE`); `SprinterFlexNavigator_Test.Dss162_Fn110KeysAndMouse`
and `RealHdd_Fn115KeysAndMouse` (`UNREAL_SPRINTER_HDD`) through `ExerciseMouse`: the pointer follows right and down
moves, D1 / D2 do not activate a panel, D0 does, a left click selects a row, a right click marks a file;
`TTDSprinter_Test.Input_RoundTripsTheKeyboardWireAndTheMousePacket` (counters in the blob);
`TTDSprinterMachine_Test.ExactRestore_MidPs2ByteAndMidMousePacket` (`Mouse=NONE`, moves and left / right buttons
journaled, packets with both buttons logged, exact replays). Mutation checks: swapped D0 / D1 fails the FN and
mapping tests; counters not restored from blob 31 fails the three TTD tests.

**Live GUI check** (BIOS 3.07 beta 1, MAME's `sp_hdd_sys.chd` on `ide0.master`, `Mouse=NONE`, FN 1.15, GPU
renderer): the toolbar's mouse button was "ready"; a click on the screen captured without reaching the machine;
host motion moved FN's pointer; a host click reached FN; with the button switched to "off" host input did not
reach the machine while the automation still did. The software renderer was not switched live (the macOS menu
bar was not reachable by UI scripting in that session); it shares `MouseCaptureController` with the GPU window.

## Keyboard overrun, stuck keys and F12 (2026-10-03, branch `sprinter-ps2-overrun`)

The owner's report: in the GUI keys sometimes stuck (cursor Down kept repeating in DSS / Flex Navigator and in
Spectrum-mode demos), and once the machine jumped to 21 MHz as if F12 (the PLD's turbo key) had been pressed. The
owner's instance showed SIO A with `fifo` = `5A F0 5A`, `fifo_count` 3, `overrun` true: the CPU had not read the
keyboard for a while.

### How the board connects the keyboard

| Question | Answer | Sources |
|---|---|---|
| Where the lines go | KBD_CLKR to /RXCA and /TXCA, KBD_DATR to RXDA of the Z84C15; the same lines to the PLD (KBD_CC / KBD_DD over the XA bus) | schematic `SPRINT_3` (zxgit Sp2000, `PAGE1.pdf`); MAME `sprinter.cpp:1987-1991` |
| Can the host hold the keyboard off | No. The only drivers onto KBD_CLK / KBD_DAT are DD17C / DD17D from latch DD16 Q3 / Q4, and the PLD writes KBD_CX = KBD_DX = GND (the LED-command sender is commented out); /RTSA, /DTRA, /W/RDYA go elsewhere; MAME drives neither line from the host side | `SP2_1K30.TDF:338-352`, `:729`, `:780-781` (both PLD source sets); schematic; MAME |
| What a held-off AT keyboard would do | buffer its bytes and send them when the clock is released (16-byte buffer, `#00` on overflow) - never happens here | IBM AT keyboard reference |
| Byte rate | 11-bit frames at 10-16.7 kHz, 0.66-1.1 ms a byte (model 917 µs) | AT reference; `Ps2KeyboardStream` |
| The SIO on overrun | the 4th character overwrites the newest in the FIFO and carries the flag; RR1 bit 5 when it reaches the top, latched until Error Reset; mode "INT on first character" holds the FIFO there | Zilog SIO manual RR1 D5; Toshiba TMPZ84C015B §3.6; MAME `z80sio.cpp` |
| BIOS / DSS on overrun | BIOS up to 3.05, DSS up to 1.62.93: one key event per frame INT, RR1 never read, so bytes are lost on the board too; BIOS 3.06 / 3.07, DSS 1.71: drain the FIFO, on RR1 bit 5 empty it, Error Reset, forget the shifts (the code is in those binaries) | BIOS-TT `KEY.asm:166-254`, `:771-789`; DSS-TT `KEYINTER.ASM:520-629`, `:1223-1238` |
| F12 / Ctrl+Alt+Del | the PLD decodes the wire, not the SIO: `#07` (not after `#F0`, no Shift / Ctrl / Alt) toggles the turbo switch - each typematic repeat too; `#71` with Ctrl + Alt resets the CPU | `KBD.TDF`; `SP2_1K30.TDF:296`, `:521-523` |

**Consensus:** the keyboard is never held off and bytes the CPU does not read in time are lost in the SIO, on the
board as here. A corrupted SIO stream cannot switch the turbo or reset the board: the PLD sees the wire.

### What was wrong here, what changed

1. **SIO overrun semantics.** The Z84C15 SIO dropped the new byte and set RR1 bit 5 at once. Now it overwrites the
   newest FIFO entry, flags that character, and RR1 bit 5 rises when it reaches the top (latched until Error
   Reset); "INT on first character" holds the FIFO (`z84sio.cpp`). With BIOS 3.06+ / DSS 1.71 this decides
   whether the two good characters before the overrun are used (they are now, as on the chip). The device state
   reports `overrun` (RR1 bit 5) and `overrun_in_fifo` (a written-over character still queued); the TTD layout of
   the chip blob is unchanged (the flags share the old `overrun` byte, bit 0 = the latch).
2. **F12 and Ctrl+Alt+Del came from the host key.** They now come from the PLD's keyboard block decoding each byte
   when its frame ends on the wire (`SprinterInput::OnWireByte`, KBD.TDF's Ctrl / Alt / Shift / E0 / F0 flags): a
   held F12 toggles again with each typematic repeat, as on the board; a phantom `#07` in the SIO (F0 07 losing
   its F0) does nothing to the turbo. The step hook runs while a `#07` / `#71` is on the wire or F12 / Delete /
   keypad . repeats, so the action is on time even when nobody reads the SIO (Spectrum mode). TTD blob 31 is
   version 3 (89 bytes: + the PLD flags); `boot.ttd` re-recorded.
3. **Focus out released only the PS/2 keys.** The ZX matrix keys held when the screen lost focus stayed down,
   and their press counter kept them down even through a later press and release of the same key: a stuck cursor
   key in Spectrum-mode programs. `KeyboardManager::postHeldKeyReleases` now releases the matrix keys too.
4. **The keyboard invents nothing.** It repeats only the last key made while it is held (500 ms, 10.9 / s) and stops
   at the release; checked with the CPU deaf for 30 frames. A key stuck in DSS / FN with the keyboard repeating
   means the host's key-up never reached the machine: focus out now covers both halves; a TTD replay that hands
   input back while the host holds different keys is not reconciled (open).

**The owner's F12 jump** is not explained by the stream: before and after, nothing in the SIO reaches the turbo.
The turbo also comes on through software (the SYS port) and through every CPU reset (DCP `TB_SW.prn = /RESET`, a
Spectrum-mode exit by reset returns to DSS at 21 MHz), and on the board each typematic repeat of a held F12.

**Tests:** `Z84Sio_Test.Overrun_OverwritesTheNewestAndLatchesAtTheTop`, `Overrun_ErrorResetBeforeTheFlaggedCharacter`,
`Overrun_FirstCharacterModeHoldsTheFifo`; `SprinterInput_Test.FullFifoOverruns`, `CtrlAltDelResetsTheCpu`,
`F12TogglesTheTurboSwitch`, `HeldF12RepeatsAndTogglesAgain`, `OverrunWithTheCpuDeafLosesBytesButInventsNothing`
(the reproduction: Enter, Down through its typematic, Shift+F12, F12's break, 30 frames deaf: two old bytes + the
newest `#07` in the FIFO, a phantom F12 for the software, no turbo switch, nothing repeating);
`TTDSprinter_Test.Input_RoundTripsTheKeyboardWireAndTheMousePacket` (PLD flags in blob 31);
`KeyboardManager_Test.FocusOutReleasesThePhysicalAndTheMatrixKeys` (unreal-qt-tests).
