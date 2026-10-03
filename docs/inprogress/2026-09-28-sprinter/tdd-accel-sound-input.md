# TDD — accelerator, sound, input, Z84C15 devices

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Review round 1 done (2026-09-28): INT-suspend as a config option (Q3, §1.3); accelerator chosen by the configuration module (§1). S0 (2026-10-01): the PLD has the INT-suspend, **default on** (owner decision 2026-10-01, §1.3) |
| **Hardware** | [hardware-reference.md](hardware-reference.md) §7, §8, §13, §4.4 (fixed ports) |
| **Index** | [technical-design.md](technical-design.md) |

## 1. Accelerator

The accelerator belongs to the active PLD configuration module ([tdd-ports-memory.md](tdd-ports-memory.md)
§6.1, hook 4). The Standard module supplies `SprinterAccelerator` as described here; a later module
(for example DooM, with its stretch modes) can replace it and leave the rest of the machine alone.

### 1.1 State

```cpp
struct SprinterAccelState            // POD, part of the Sprinter TTD blob
{
    uint8_t buffer[256];
    uint8_t dir;          // MAME m_acc_dir bit set: bit2 block, bit1 buffer↔RAM, bit3 set-length,
                          // bit4 vertical (PORT_Y++), bit6 double write, bit5/bit0 fill vs copy
    uint8_t fn;           // #BE plain, #A6 AND, #B6 OR, #AE XOR
    uint8_t length;       // RGACC, 0 = 256
    uint8_t count;
    uint8_t prefixSeen;   // previous opcode byte was CB/DD/ED/FD
    uint8_t altMode;      // #C7/#CF addressing (m_alt_acc, m_aagr, m_xcnt, m_xagr)
    uint16_t aagr; uint8_t xcnt, xagr;
};
```

### 1.2 Control: the M1 hook

Port of MAME `accel_control_r` (`sprinter.cpp:917-954`). On each opcode fetch (not after a prefix,
ALL_MODE bit 0 = 1): `LD r,r` with the same register selects the mode (`#40` off, `#49` fill, `#52`
set length, `#5B` vertical fill, `#64` double, `#6D` copy, `#7F` vertical copy; `#76` HALT is
excluded), and `#A6/#AE/#B6` select the logic function (`#BE` = plain).

### 1.3 Data: the read and write paths

When a mode is armed and the CPU makes a **data** access (not M1):

```text
read  (addr): if mode = set-length: length = value read; done
              if block op: for i in 0..length-1: buffer[i] = fn(buffer[i], RAM[addr'])   // addr' = addr+i, or same addr with PORT_Y+i (vertical)
write (addr, v): if fill: for i: RAM[addr'] = v
                 if copy: for i: RAM[addr'] = buffer[i]
                 every store goes through the normal write path (so graphics pages, VRAM shadow and the
                 Covox-Blaster page #FD see it: MAME :1069-1087)
charge the CPU:  length × 6 clocks of 42 MHz  (= 7 MB/s, MAN §6)
```

MAME runs the block from a timer with WAIT asserted (`:956-1052`); the emulator does the block
inside the access and adds the time as extra T-states to the current instruction. Same total
time, deterministic, no mid-block interrupt.

**Accelerator off during INT, back on at RETI** (MAN §6 p. 21; review round 1, Q3; S0 finding
2026-10-01). Implemented in S5 as a **config option, default on** (owner decision 2026-10-01): the standard PLD
configuration has the feature, so on is the hardware behavior; off gives MAME's behavior for
side-by-side comparisons (round 1 had chosen off before the PLD was checked):

- on (default, the hardware): accepting an INT **blocks** the accelerator; the first opcode
  fetch after a `RETI` (`ED 4D`) unblocks it (through the interrupt source's `OnReti()`, §5.1). While
  blocked, no new accelerator operation starts; the **mode register is not saved or cleared**, and
  an `LD r,r` in the handler still changes it. `RETN` does not unblock; an NMI does not block (it has
  no acknowledge cycle). Example: a program armed "fill" (`LD C,C`, `#49`), an INT arrives, the
  handler copies a byte with `LD (HL),A`; that store is a plain store, not a fill, and after `RETI`
  the next store fills again;
- off: MAME's behavior; the accelerator stays active while an interrupt handler runs.

**Default changed to off in S6 (2026-10-02).** Two findings say the board does not block: the `ACC_BLK`
preset term read literally (`ACC_BLK.prn = /RESET & ACC_MODE3`, `ACC_MODE3` clears itself one clock after a
mode select, so the preset holds `ACC_BLK` at "enabled" almost all the time; S5 outcome open point 1), and
real software: the Covox-Blaster WAV player `WAVPLAY.EXE` v2.02 (Ivan Mak, the board's designer, 2025)
refills the ring from its INT handler with accelerator copies into page `#FD`. With the option on, each refill
stores one byte instead of 128 and the player falls silent after 0.45 s; with it off, all 655 360 samples of
`MISSION.WAV` reach the ring (128 per INT), as on MAME ([s6-sound-outcome.md](s6-sound-outcome.md) §3). The
option stays (`[SPRINTER] AccelIntSuspend=1`) for comparisons; the shipped config and the built-in default are 0.

**What the PLD does** (BIOS-TT `0271ac3` `src/altera/acex/k30/ACCELER.TDF`):

| Lines | Logic | In plain words |
|---|---|---|
| `:146-154` | `ED_CMD`, `RETI` latched on each opcode fetch (M1); `RETI` = the byte after `ED` is `4D` | the PLD decodes `RETI` itself |
| `:158-160` | `RETN` (`ED 45`) decoded but unused | `RETN` does not unblock |
| `:164-166` | `ACC_BLK.clk = /M1; ACC_BLK.d = DFF((/IO & ACC_BLK) or (!ACC_BLK & RETI), CLK_Z80); ACC_BLK.prn = /RESET & ACC_MODE3` | an M1 cycle with `/IORQ` low (only the INT acknowledge does that) clears `ACC_BLK` (blocked); a latched `RETI` sets it again at the next M1; reset presets it to "enabled" |
| `:237` | `START_ACC` gated by `!ACC_BLK` | only the start of new operations is blocked |
| `:190`, `:261-270` | the count load and the mode register are not gated | the mode survives the handler |

`/IO` is the raw Z80 `/IORQ` pin (`SP2_ACEX.TDF:33`, `:941`). The same `ACCELER.TDF` is in every
surviving version (Sprinter200x `1039391` `Altera_1K30/Last` and `Sp2000`, gitlab
sprinter-computer/hard incl. branch `quartus2`, Sprinter-BIOS history); the older `SPRINT08.TDF`
(`quartus2:Other/UNUSED`) already has it; the fitter report `Sprinter200x/Altera_1K30/Sp2000/acceler.rpt`
shows `ACC_BLK` synthesized. MAME (`sprinter.cpp:1017`, `accel_go_case`) and ZXMAK2 have no INT or RETI
handling. The manual says the same: "В момент прихода прерывания он отключается и включается обратно
по команде RETI" ("when an interrupt arrives it is switched off, and switched back on by RETI").
ALL_MODE bit 0 is a separate, manual switch.

### 1.4 As implemented (S5, 2026-10-02)

Outcome and evidence: [s5-accelerator-outcome.md](s5-accelerator-outcome.md). Where the build differs
from §1.1-§1.3 (each time the PLD's `ACCELER.TDF` decided; MAME differs where noted):

- **Where the hook sits.** Not the `IMachineM1Hook`: the Z84C15 engine gets a bus agent
  (`IZ84BusAgent`) that sees the opcode fetch with its byte, every operand / data read (it may replace
  the byte the CPU gets), every write (it may replace the byte, before and after), and the INT
  acknowledge. The accelerator is `SprinterAccelerator` (`memory/sprinter/`), supplied by the Standard
  module (hook 4, `SprinterPldConfiguration::Accelerator`). RETI is decoded by the accelerator from the
  opcode stream, as the PLD does, not through `OnReti()`.
- **State.** `SprinterAccelState` keeps `mode` and the derived `dir`, the PRF / ED / RETI latches, the
  block flag, the alternate addressing and two statistics; no `count` (the block runs inside one access).
- **Function.** `FN_ACC` is loaded by every opcode fetch, so `AND / XOR / OR (HL)` act for their own
  read only; any other opcode makes it plain, and `ADD / ADC / SUB A,(HL)` alias AND / XOR / OR (MAME
  keeps the function until the next mode select and knows only `#A6 / #AE / #B6`).
- **Which accesses.** Every non-M1 access starts an operation, operands included (`LD A,0` after
  `LD D,D` sets the length). The length load is not blocked by an INT (`RGACC` is not gated).
- **Order and result.** The CPU's access is the first of `length`; the buffer index counts down from
  `length` to 1; a block read gives the CPU the last byte read; a copy write's first store is the buffer's.
- **Time.** `(length - 1) x 6` clocks of 42 MHz added as wait states (the CPU's own access is the
  first of the `length`), rounded up to whole CPU clocks per operation; "length x 6" in §1.3 counts the
  CPU's access in. Double byte takes no extra time (MAME: 3 clocks).
- **ALL_MODE bit 0 = 0** clears the mode (MAME keeps it and only gates it).
- **Double byte** stores the CPU's byte at `addr ^ 1` (MAME); the PLD's high lane is the IDE latch
  (open, S3b).
- **INT suspend.** As §1.3, plus the `ACC_BLK` equation's corner: an INT taken before the first fetch
  after RETI unblocks. The `ACC_BLK` preset term reads, literally, as "almost never blocked": open point 1
  of the outcome.

## 2. Sound

| Device | Design | Reuse |
|---|---|---|
| AY | codes `#90` (select), `#91` (write), `#52` (read) → `PeripheralPortOut(0xFFFD/0xBFFD)` and the AY read path, like Profi (`portdecoder_profi.cpp` ~`:218-231`); clock 1.75 MHz; ABC stereo | existing AY / TurboSound (single chip); **check the clock option** (1.75 MHz is the Pentagon value) |
| Beeper / tape out | code `#C2` (`#FE` write) bits 3-4 → `Default_Port_FE_Out` (`portdecoder.cpp:1198`) | existing |
| Covox | code `#88` when CBL is off → the existing `Covox` device's write, mono to both channels | existing `Covox` (`covox.h:48`), called directly instead of its self-decoding |
| **Covox-Blaster** | new `CovoxBlaster` (`core/src/emulator/sound/sprinter/covoxblaster.{h,cpp}`): control (code `#89`) bits 7 CBL, 6 stereo, 5 16-bit, 4 INT, 3-0 rate; 256 × 16-bit ring; write index advanced by each code-`#88` write (and by accelerator writes into page `#FD`); play index advanced at 42 MHz / 192 / (div + 1), div table `{13, 9, 0, 0, 0, 0, 0, 0, 27, 19, 13, 9, 6, 4, 3, 1}`; stereo takes two entries per tick; 16-bit mode pairs bytes (low, then high XOR `#80`); INT request every 128 samples when bit 4; `#FE` read bit 7 = bank being played (or play XOR write index when INT is on), bit 5 = beam below line 272 | MAME `sprinter.cpp:785-814`, `:1696-1701`, `:1748-1765`; INC `SP2000.inc:136-220` |

The CBL output is a timed sample stream; it is rendered into the same audio frame as the
beeper/Covox (the existing DAC path), sampled at its own rate by the CBL tick in emulated time.

### 2.1 As implemented (S6, 2026-10-02)

Outcome and evidence: [s6-sound-outcome.md](s6-sound-outcome.md). Where the build differs from the table above,
the PLD (`SP2_1K30.TDF` "COVOX" section, `:1066-1166`) decided:

- **Where it lives.** `CovoxBlaster` (`core/src/emulator/sound/sprinter/covoxblaster.{h,cpp}`) is one device for
  the plain Covox and the Covox-Blaster: the PLD has one output register (`CBL_R`) for both. The port decoder
  owns it (`PortDecoder_Sprinter::GetCovoxBlaster`); SoundManager mixes it through a small interface,
  `IModelAudioSource` (`sound/modelaudiosource.h`), in the **COVOX mixer slot** (row "Covox-Blaster", recording
  source `COVOX`, HUD LED). Other machines hold a null pointer: one test per frame. The Sprinter config fits
  no generic Covox / SoundDrive (`SD=0`, `CovoxFB=0`).
- **Time base.** Base T-states (3.5 MHz) within the frame, `z80->t / current multiplier`: one 218.75 kHz step is
  16 T (42 MHz = 12 x 3.5 MHz), a play tick every `16 x (CBL_TAB + 1)` T, on the grid of the frame start
  (14 steps per 224-T line). The play position is advanced lazily (at every bus access of the device, at the
  INT check when the INT is on, and at the frame end); the audio is a band-limited step stream (blip_buf, as
  the Covox) on the Covox's time axis. Turbo (21 MHz) and the host speed control change neither rate.
- **Rates 2-7** play at 218.75 kHz (`CBL_TAB = 0`: the PLD reloads 0 and counts every step); MAME never ticks.
- **Write address.** INT on: `CBL_WA`, +1 per entry, held at the start of the other half while a request is
  pending (`WA = ~CNT & #80`) and the 16-bit phase restarts there. INT off: `~A15..A8` of the access (so `OTIR`
  with `B = 0` fills 0, 1, ..., 255) and `CBL_WA = 0`. The ring is written in Covox mode too (the PLD's write
  enable is not gated by `CBL_MODE`). MAME always uses `m_cbl_wa++`.
- **Play.** On a tick `CNT += 1` (mono) or 2 (stereo), then the DAC follows `ring[CNT]` (mono) or
  `ring[CNT & #FE]` left / `ring[CNT | 1]` right **continuously**: a write into the entry being played is heard
  at once. No channel swap in 16-bit stereo (MAME swaps; the file's left channel plays left here, right on MAME).
  Turning CBL off clears `CNT` and the DAC keeps its last word until a Covox write; reset puts it at `#8000`.
- **INT.** `CNT` bit 6 falling (#7F -> #80, #FF -> #00) raises the request; the PLD INT acknowledge (vector `#FF`)
  ends it, as for the frame and keyboard INTs (`SprinterIntSource`); INT off drops it. Turning CBL off while
  `CNT` bit 6 is set raises it too (the flip-flop is clocked by the bit, whatever moves it).
- **Page `#FD`.** A store into RAM page `#FD` (bank action `CblPage`) reaches the ring when the CBL INT is on and
  the accelerator mode has `ACC_DIR` bit 1 (the copy modes) - the CPU's own store in such a mode included
  (`CBL_WR`'s page term). MAME takes every accelerator copy, INT or not.
- **`#FE`** (code `#40`) with CBL on: bit 7 = `CNT7 XOR WA7` (the half that needs data), bit 5 = the beam on line
  272 or below (MAME); with CBL off the keyboard byte is unchanged.
- **Level.** A DAC word maps to `(word - #8000) / 2` in the mix (full scale +-16 384, the Covox's mono scale). The
  PLD adds the CBL word to the AY sum in one 16-bit DAC; the emulator keeps the AY and the DAC as two mixer rows.
- **AY.** One chip: `[SOUND] TurboSound=Single` (new kind: the TurboSound pair without the chip switch, so
  `#FE` / `#FF` written to `#FFFD` select no register, as on a lone AY). Its clock is 3.5 MHz / 2 = 1.75 MHz on
  every model already (= 42 MHz / 24); `[AY] FQ=` is not read (the ini says 1750000 now). ABC panning is the
  shared preset (B at half to both sides; the PLD feeds B to both sides at full level, MAME at a quarter).
- **ISA register stub.** What S1 built: code `#1B` keeps A19-A14 (RESET and AEN are not stored: no card), window
  3 on `#D0-#D6` with `#1FFD` bit 4 reads `#FF` and drops writes. No new state, so TTD id 33 stays reserved for
  S6b (the ISA I/O window and the ZX-bus adapter).
- **TTD.** Id 32 `SprinterCovoxBlaster`, v1, 1 + 544 bytes (`CovoxBlasterState`: the ring, control, indices,
  16-bit phase, request, DAC words, next tick, counters). The PLD blob keeps its copy of the control byte
  (layout v1 unchanged).
- **Automation.** `state/sprinter` gains `sound` (`ay`, `beeper`, `covox_blaster`: control decoded, rate and
  divider, play / write index, request, the half that needs data, DAC words, counters); the shared Covox report
  (`/state/audio/covox` and its CLI / MCP / Lua / Python twins) shows the device when it holds the COVOX slot.

### 2.2 The play-tick phase, and the dontBlink freeze at 5:10 (2026-10-03)

**Phase.** The PLD clocks `CBL_CTX` from the video counter (`CBL_CTX[].clk = !CTH1`, `SP2_1K30.TDF:1079`) and
never clears it: it free-runs from the configuration load, and `CBL_CNT` only clears while `CBL_MODE` is 0. The
first INT after a control write therefore lands on the free-running tick grid, not exactly 64 ticks after the
write. The emulator follows the PLD (ticks on the frame-start grid, §2.1). MAME restarts its timer at every control
write (`sprinter.cpp:797-812`), so its INT phase follows the write. In dontBlink the two differ by about 550 CPU
clocks (21 MHz) relative to the CTC tick, half a play tick.

**The freeze.** deMarche's dontBlink (`C:\DEMOS\DNTBLINK`, BIOS 3.07 BETA 1 default, launched from Flex Navigator)
stops drawing about 305-310 s after start, in the "flowers" part (part code loaded at `#0700`). The music then
loops one ring buffer. The cause is a race in the demo's own code, found in a TTD recording of the freeze (frames
15681-15713, reverse steps and `find-last`):

- The effect (`#07D8`) resets an "interrupted SP" log to `#0600` (`#031D` is the pointer). It points SP into its
  data (`LD SP,#8006`), runs `POP`-driven with interrupts on, and leaves through
  `#0D1E LD SP,#3F74 : LD A,IXH : OUT (#E2),A : #0D25 LD HL,(#031D) : EI`. It then repairs 4 bytes at every
  logged SP from the backup page (`(D OR #C0):E`, page IXH). Both interrupt handlers log the interrupted SP
  (`#0310` CBL, `#0125` CTC).
- If an interrupt is taken between `LD SP,#3F74` and `LD HL,(#031D)` (a window of about 35 T), the log gets
  `#3F72`. The repair then copies `#FF72..75` of the backup page over the live stack and destroys the return
  address `#07CC` at `#3F74`. `RET` at `#0D4B` goes to `#FC01`, the CPU runs into the page table at `#0038` and
  halts at `#003D` with the accelerator's `LD A,A` armed. The next CBL interrupt returns there, and its stores
  (vertical copy) overwrite `#0302-#031E` with `#03`. After that the CPU halts with interrupts off, for good.
- In frame 15712 the CBL INT (raised at 262 292 of 430 080 clocks) is taken right after `OUT (#E2),A`.
  `LD SP,#3F74` ran at 262 241.

**Why it is timing luck, not a device fault.** The demo's 4-frame main loop starts on the 48.83 Hz CTC tick. The
end of its first effect call drifts by 100-200 clocks per loop with the data. The CBL INT sits at a fixed phase
(seven per frame, since 21 875 Hz / 64 = 7 x 48.83 Hz). The drift sweeps the call's end across a CBL INT. Whether
an INT then lands inside the 35-T window is chance, decided at clock level. MAME (same CHD, BIOS 3.06) runs past
it: its loop timing matches the emulator's within 0.2% (13 596-13 644 clocks per effect iteration against
13 632-13 650 here, the same CTC and disk-read pattern). At the same crossing its call ends 566 clocks before the
CBL INT, because of the ~550-clock phase difference above. On a real board the tick phase depends on when the
configuration loaded, and the disk reads in the CTC handler take real drive time, so the outcome depends on the
board and the run. In the emulator the same demo with BIOS 3.06 Hotfix 2 (other disk-read timing) plays past the
flowers to the end logo. Nothing the demo does is emulated wrongly here. The race is the demo's (its own SP log
cannot tell a data SP from the real stack). Evidence: TTD file
`scratch/dntblink-freeze/dntblink-307-freeze-f15681-15713.ttd` (not committed), MAME debugger logs (breakpoints
`#0125/#0310/#07D8/#07EB/#0D1E` with `totalcycles`).

## 3. Keyboard

### 3.1 One key event, two outputs

The design shared with ZX-Evo PS/2 (PLAN #55 E2b, [tdd-evo-control-and-avr.md](../2026-09-15-atm-baseconf-highres-ports/tdd-evo-control-and-avr.md) §6.1):
a host key event carries **both** the ZX key (`ZXKeysEnum`, with the extended combinations) and
the physical PC key (`PcKey`), and is journaled once for TTD. For the Sprinter:

| Output | Consumer | Design |
|---|---|---|
| ZX matrix | code `#40` (`#FE` read, DOS off) | the existing `Keyboard::HandlePortIn` (`core/src/emulator/io/keyboard/keyboard.h:334`) with the Sprinter PC→ZX combinations (arrows = CS+5..8, Backspace = CS+0, etc., INC `SP2000.inc:469-513`); bit 6 = tape in; bits 5/7 overridden by the CBL (§2) |
| AT scan codes | SIO channel A receive (§5.1) | `Ps2Set2Encoder` (from E2b) → make/break codes (`#F0` prefix, `#E0` extended) queued in emulated time: one byte per ~0.9 ms (11 bits at ~12 kHz) so the 3-byte FIFO does not overflow |
| Keyboard INT | `SprinterIntSource` | when ALL_MODE bits 0 and 3 are set, one INT per received byte (MAME counts 11 clock edges, `sprinter.cpp:1706-1718`) |
| Ctrl+Alt+Del | reset | PLD `KBD.TDF` `KB_RESET`: a hardware reset (tdd-ports-memory §7) |

Open point (**unverified**): whether the BIOS sends commands to the keyboard (typematic rate from
CMOS `#0F`, LEDs) and waits for `#FA`. MAME does not connect a keyboard input line; if a trace shows
the BIOS waiting, the encoder answers `#FA` to every command byte.

### 3.2 Mouse and joystick

| Device | Design |
|---|---|
| Serial mouse | host mouse → Microsoft 3-byte packets (7N1) into SIO channel B receive, paced at 1 200 baud in emulated time; the DSS mouse driver reads them (`intmouse.asm`) |
| Kempston mouse view | code `#58`: `#FADF` buttons, `#FBDF` X, `#FFDF` Y from the existing Kempston mouse device (`PeripheralId::KempstonMouse`, "every model") |
| Joystick | code `#15` (and the DOS-off `#1F`/`#0F` view) returns Kempston bits from the existing joystick input |

### 3.3 As implemented (S4 input, 2026-10-02)

Outcome and evidence: [s4-input-outcome.md](s4-input-outcome.md).

```text
host key (Qt / automation) -> KeyboardEvent (ZX key) + PcKeyEvent (PC key), journaled
  ZX key -> Keyboard matrix -> code #40 (#FE)                        Spectrum mode
  PC key -> Keyboard::ApplyPcKey -> SprinterInput (the PS/2 sink)
              Ctrl+Alt+Del -> CPU reset (PLD stays configured); F12 -> turbo switch
              -> Ps2KeyboardStream: set 2 bytes, one per 917 us, typematic 500 ms / 10.9 per s
              -> Z84C15 SIO A receive (3-byte FIFO, overrun = RR1 bit 5)
              -> ALL_MODE & #09 == #09: SprinterIntSource keyboard INT (vector #FF)
host mouse -> Mouse (Kempston counters, journaled) -> code #58 view
                                                   -> MsSerialMouse -> SIO B (1 200 baud)
```

- **Who reads the keys.** BIOS 3.04 SETUP and DSS poll SIO A from their frame INT handler
  (SETUP `KEYSCAN`, DSS `keyinter.asm` `RESCAN`): RR0 bit 0, then the data port, set 2 codes with
  `#E0` / `#F0` / `#E1` prefixes, translated in software. No SIO interrupt is enabled (WR1 = 0 in
  both `KINIT`s). SETUP runs with ALL_MODE = `#FF`, so the PLD's keyboard INT is on there and every
  received byte runs the same handler once more.
- **Delivery.** Lazy: the bytes due by now go into the SIO before every access to `#18-#1B`. Only
  while the keyboard INT is on and a byte is on its way does the decoder's step hook run, so the INT
  comes at the byte's arrival; an idle keyboard costs nothing per instruction.
- **Mouse.** DSS 1.62's driver (`intmouse.asm` `READ_M`) reads the Kempston view, not SIO B; the
  serial packets are there for software that reads the raw mouse. A packet starts when software
  polls SIO B and the counters changed since the last packet.
- **Not modeled.** Commands to the keyboard (BIOS function `#EA` bit-bangs them through WR5; nothing
  waits for `#FA`), the mouse's `M` identification byte on DTR, the PLD's own scan-code-to-matrix
  decoder (the matrix comes from the host's ZX key instead).

## 4. Where the `#1F` rewrite and DOS live

The M1 hook (`IMachineM1Hook`, `core/src/emulator/cpu/z80.h:290-295`) of the Sprinter does three
things per opcode fetch: DOS in/out ([tdd-storage.md](tdd-storage.md) §2.2), accelerator control
(§1.2), and arming the operand rewrite (§5.3). One hook, one `switch`, no cost for other models.

## 5. Z84C15 on-chip devices (`core/src/emulator/io/z84c15/`)

> **Moved (2026-10-01):** the CTC, SIO, PIO, system registers, watchdog and the daisy chain now
> live in the Sprinter's CPU library, `core/src/3rdparty/z84c15/` (`Z84Lib::Z84C15`), with the CPU
> core; `core/src/emulator/io/z84c15/` keeps the engine adapter (`Z84C15Engine`). Design:
> [2026-10-01-z84c15-cpu-library](../2026-10-01-z84c15-cpu-library/design.md).

Reusable package; the Sprinter is its first user.

| Class | Ports | v1 scope | Source |
|---|---|---|---|
| `Z84Sio` | `#18` A data, `#19` A control, `#1A` B data, `#1B` B control | asynchronous mode only: WR0-WR7 stored, RR0 (bit 0 Rx available, bit 2 Tx empty), RR1 (overrun), RR2 (vector, channel B); 3-byte receive FIFO per channel; transmit goes to a sink (keyboard commands, mouse power); receive and "special condition" interrupts with the Z80-SIO vector rules | MAN §9.1, §9.4 (the init sequences); Zilog Z84C15 datasheet (not in the corpus: **to add to materials**); MAME `tmpz84c015.cpp:23` |
| `Z84Ctc` | `#10-#13` | 4 channels, timer and counter modes, prescaler 16/256, time constant, ZC/TO outputs as callbacks (channel 0 clocks SIO B in MAME), interrupts with vectors | MAN §9.1 (mouse init writes `#85`, `#45` to channel 0) and §9.5; MAME `sprinter.cpp:1993-1995`, `:2006-2008` |
| `Z84Pio` | `#1C-#1F` | register file (mode, direction, data) for ports A and B; port B inputs read ISA IRQ/DRQ = inactive; the joystick-2 pad lines as in MAME | MAN §9.3; MAME `sprinter.cpp:1338-1344`, `:1998` |
| `Z84SystemRegs` | `#EE/#EF` (wait states, memory-wait boundary, chip-select boundary, misc), `#F0/#F1` (watchdog), `#F4` (interrupt priority) | stored; the watchdog does **not** reset the machine in v1 (logged if enabled) — **unverified** whether the BIOS relies on it | MAME `z84c015.cpp:19-20`, `:109-133`; BIOS-TT `loader.asm` `.START` |

### 5.1 Interrupt delivery

The Z84C15 devices form a daisy chain (priority set by `#F4`). They deliver through the Sprinter
`IInterruptSource` ([tdd-video.md](tdd-video.md) §5), which already returns the data-bus byte on
acknowledge: the PLD sources answer `#FF`, a Z84C15 device answers its vector. RETI must reach the
chain (a RETI hook on the source; the TSConf interface gets an optional `OnReti()`).

Whether the BIOS/DSS enable any Z84C15 interrupt at all is **unverified** (the manual's mouse
example sets the CTC interrupt-enable bit, MAN §9.1); the chain is built so the answer does not
change the design.

### 5.2 Ports that the PLD sees too

MAME forwards writes to the Z84C15 addresses to the PLD decoder as well (`sprinter.cpp:1445-1458`);
the decoder mirrors that (tdd-ports-memory §3.2, open point).

### 5.3 The `#1F` operand rewrite

```text
OnMachineM1(pc, opcode):
    ioOperandPending = (!prefixSeen) and (opcode == #D3 or opcode == #DB)     // OUT (n),A / IN A,(n)
SprinterMemory::MemoryRead(addr) (non-M1):
    v = base read
    if ioOperandPending: ioOperandPending = 0; if bank is RAM and v == #1F: v = #0F
```

MAME `sprinter.cpp:1009-1015`, `:1323`; MAN §9 p. 21. The rewrite applies only to code running
from RAM (vROM is RAM), not from the system ROM.

## 6. Tests

See [test-plan.md](test-plan.md) §2.8-2.10: accelerator per mode (fill, copy, vertical, AND/OR/XOR,
length 0 = 256, timing charge), CBL ring and rate table, `#FE` bits, SIO receive FIFO and RR0, scan
code sequences for a key press, the `#1F` rewrite (RAM vs ROM, `OUT (C),A` untouched), the
INT-suspend option off and on.
