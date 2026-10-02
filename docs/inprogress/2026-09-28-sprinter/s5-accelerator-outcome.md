# S5 outcome: the block accelerator (2026-10-02)

Branch `sprinter-s5`. Phase S5 of [roadmap-and-plan.md](roadmap-and-plan.md): the PLD's block
accelerator, all modes, its time, and the INT suspend as a configuration option (default on). Design:
[tdd-accel-sound-input.md](tdd-accel-sound-input.md) §1; what differs from it: §1.4 there.

## What the accelerator does, in short

A program arms a mode with a same-register `LD r,r` (an instruction that does nothing), and the PLD
repeats the next memory access up to 256 times while the CPU waits. Example: `LD D,D : LD A,0` sets
the length 256 (the operand byte `0` is the access), `LD E,E : LD (HL),E` then writes E into 256 rows
of one column of the graphics screen, `LD B,B` switches it off. Flex Navigator clears its screen this
way, 320 columns, and draws every text glyph with vertical copies (`LD A,A`).

## How it is built

```text
Z84C15 library --M1 fetch / data read / data write / INT ack--> Z84C15Engine
   Z84C15Engine --IZ84BusAgent (only Sprinter has one)--> SprinterAccelerator   (memory/sprinter/)
   SprinterAccelerator --AcceleratorRead / AcceleratorWrite--> SprinterMemory (graphics pages, VRAM shadow)
PortDecoder_Sprinter owns the accelerator; the active PLD module supplies it (hook 4, Standard: the decoder's)
```

| Part | File | What |
|---|---|---|
| Bus agent hook | `core/src/emulator/io/z84c15/z84c15engine.{h,cpp}` | `IZ84BusAgent`: opcode fetch (after the read), data read (result replaceable), data write (value before, the rest after), INT acknowledge (before the pushes). An idle agent costs one flag test per access; the library is unchanged |
| Accelerator | `core/src/emulator/memory/sprinter/sprinteraccelerator.{h,cpp}` | control from the opcode stream, the block loop, the time, the INT block, the `#C7` addressing |
| State | `SprinterAccelState` (POD, 280 bytes, `static_assert` on the size) | the TTD blob of S7: buffer, mode, dir, fn, length, prefix / ED / RETI latches, blocked, alternate addressing, statistics |
| Memory path | `SprinterMemory::AcceleratorReaches / AcceleratorRead / AcceleratorWrite` | the extra accesses: main RAM windows only; the store goes through the plain store, the TTD dirty page and the write intercept (graphics pages with transparency, VRAM shadow, reset page), with no CPU wait |
| Module hook 4 | `SprinterPldConfiguration::Accelerator`, `SprinterPldStandard` | the module's accelerator; `PortDecoder_Sprinter::RefreshAccelerator` makes it the CPU's bus agent when the PLD is configured (null while it loads) |
| Code `#C7` / `#CF` | `PortDecoder_Sprinter::StandardWriteCode` | `OnScaleWrite(port, value)` |
| Option | `[SPRINTER] AccelIntSuspend=1` (`config.sprinter.accel_int_suspend`, default 1) | the INT suspend |

## Behavior (the PLD, `ACCELER.TDF`)

| Item | Implemented |
|---|---|
| Mode select | unprefixed opcode fetch `01 rrr rrr` with equal fields, ALL_MODE bit 0 set: `#40` off, `#49` fill, `#52` length, `#5B` vertical fill, `#64` double, `#6D` copy, `#76` HALT = off, `#7F` vertical copy. ALL_MODE bit 0 = 0 clears the mode (`ACC_MODE.clrn = ACC_ENA`) |
| Function | set by **every** opcode fetch (`FN_ACC.ena = /M1M`): an unprefixed `10 xxx yyy` gives `~xxx`, anything else plain. So `AND (HL)` / `XOR (HL)` / `OR (HL)` act for their own read only, `CP (HL)` is plain, and `ADD` / `ADC` / `SUB A,(HL)` alias AND / XOR / OR |
| Which accesses | every operand or data access (not M1, not refresh): `LD A,n`'s operand sets the length, a `PUSH` in fill mode fills |
| Length | `RGACC`, loaded by any access in mode `#52` (read: the byte read, write: the byte written), not gated by the INT block; 0 = 256 |
| Block | the CPU's access is the first of `length`; the rest go to address + 1 (or the same address with PORT_Y + 1 per access in the vertical modes; PORT_Y ends at start + length); the buffer index is the down-counter `length, length - 1, ... 1` (so a read and a write of the same length pair up) |
| Reads | fill modes: the reads are repeated, the buffer untouched; copy modes: `buffer = fn(buffer, byte)`. The CPU gets the last byte read |
| Writes | fill: the CPU's byte; copy: the buffer (also for the CPU's own store) |
| Double (`LD H,H`) | a write also stores the other byte of the word (`addr ^ 1`, MAME's model; the PLD writes the IDE high-byte latch there, see open points); no extra time |
| Windows | accesses to ROM, fast RAM or the ISA view are skipped (the time is still taken), as MAME |
| `#C7` / `#CF` | `AAGR = A9 A8 D7..D0`, `XCNT = A15..A10`, `XAGR = 0`; from then on (until reset) the buffer index is `XCNT`, and `XCNT:XAGR += AAGR` per copy access |
| Reset | mode off, unblocked, alternate addressing off; buffer and length kept |

**Time.** One accelerator access takes 6 clocks of 42 MHz (7 MB/s, MAN §6). The CPU's own access
is the first; the other `length - 1` are added to the current instruction as wait states:
`ceil((length - 1) x 6 / (12 / ratio))` CPU clocks, ratio 6 at 21 MHz (3 clocks per access), 1 at
3.5 MHz (1/2 per access, rounded up per operation). Example: fill 5 at 3.5 MHz = 2 extra clocks,
at 21 MHz = 12; fill 256 = 128 / 765. The turbo memory waits of the CPU's own access are unchanged.

**INT suspend** (default on). The acknowledge cycle of an INT (an M1 with `/IORQ`) blocks new block
and double operations; the mode stays and may be changed by the handler; RETI is decoded from the
opcode stream (`ED`, then `4D`) and the first opcode fetch after it unblocks (RETI's own pops are still
plain); RETN does not; an NMI has no acknowledge cycle and does not block. An INT taken right after
RETI, before the next fetch, leaves it unblocked (the PLD's `ACC_BLK` equation). Off: MAME's behavior.

## Evidence

| What | Test / file | Result |
|---|---|---|
| Unit tests T-ACC (24): mode select incl. prefixes and HALT, ALL_MODE bit 0, function per fetch, length by operand, fill / length 0 = 256 / 21 MHz time, copy, operand reads accelerated, AND / XOR / OR / CP / ADD alias, function for one instruction, vertical fill and copy on a graphics page (PORT_Y), transparent page, double, ROM window, INT block: pushes plain, option off: pushes filled, RETI / RETN, NMI, length during the block, `#C7` addressing, module hook + reset | `core/tests/emulator/machines/sprinter/sprinteraccelerator_test.cpp` | pass, 3-9 ms each |
| ACCTEST.EXE from a DSS 1.62 floppy built in the test (FAT12 root file added) | `SprinterBoot_Test.Dss162_AccTestCopiesItsPictureWithTheAccelerator` | the 64 x 64 picture equals the file's bytes; screen = `golden/acctest.png` |
| Flex Navigator from `dss_1_62_92.img` | `SprinterBoot_Test.Dss162_FlexNavigatorDrawsWithTheAccelerator` | the screen clear: 320 x 256 one value, 320 vertical fills; the panels with glyphs: screen = `golden/fn-panels.png` |
| MAME, ACCTEST | `testdata/machines/sprinter/reference/mame-acctest-306.png` (MAME `sprinter`, BIOS 3.06, the HDD system disk with `SYSTEM.BAT` running `tests\acctest`) | the picture's 128 x 64 display pixels are **identical** to ours; the rest differs (what each BIOS left in video RAM) |
| MAME, Flex Navigator | `reference/mame-fn115-hdd-306.png` (FN 1.15 from the HDD; ours is FN 1.10 from the floppy) | the F-key bar is the same pixel for pixel up to one color: light gray 170 here, 192 there (FN's own palette of each version / config); fonts, frames and layout match |
| Both programs with `AccelIntSuspend=0` | the two boot tests, run once with the option off | same results (neither draws from an interrupt handler) |
| Recipe | [.recipe/machines/sprinter-accelerator.md](../../../.recipe/machines/sprinter-accelerator.md) | run through the WebAPI (full start, 4 500 frames): 64 of 64 rows match, the screenshot equals the golden |

**Flex Navigator and the floppy.** FN now clears its screen (frame ~689) and, without help, then
stops: it calls the BIOS `RESETD` with the head on track 71, the RESTORE (71 steps x 3 ms) outlasts
the BIOS poll loop at 21 MHz, and the error path crashes (roadmap §8, the open wait-state question; not
the accelerator). The boot test therefore moves the head to track 20 when `RESETD` starts; with that,
FN loads its directory and draws both panels with text as glyphs (no horizontal segments). Without the
workaround the accelerator was left armed in `LD A,A` by the crashed code, so the BIOS loop ran with
256-access copies on every operand read: a symptom of the crash, not a cause. The full FN panels from
an HDD need S3b (IDE).

Screenshots: `golden/fn-panels.png`, `golden/acctest.png` (ours), `reference/mame-*.png` (MAME).

## For the automation branch (`state/sprinter`)

`PortDecoder_Sprinter::GetAccelerator()` (null while the PLD loads) → `State()`:

```json
{"accelerator": {"enabled": true, "mode": 5, "mode_name": "copy", "length": 64, "function": "plain",
  "blocked": false, "int_suspend": true, "alt": false, "xcnt": 0, "aagr": 0,
  "operations": 128, "last_extra_clocks": 189, "buffer_crc32": "...", "buffer_head": "0D 0D 0D 02 ..."}}
```

`enabled` = ALL_MODE bit 0 (`SprinterAccelerator::IsEnabled`), names from `ModeName` / `FunctionName`,
`int_suspend` = `config.sprinter.accel_int_suspend`. No serializer was added (S7).

## Open points

1. **The PLD's `ACC_BLK` preset.** `ACC_BLK.prn = /RESET & ACC_MODE3`, and `ACC_MODE3` clears itself
   one Z80 clock after a mode select (`ACC_MODE[3].clrn = /RESET & !DFF(ACC_MODE3)`). With AHDL's
   active-low `prn` read literally, `ACC_BLK` is held "enabled" except in that one-clock window, so the
   standard bitstream would almost never block, and MAME's behavior (option off) would be the board's.
   The option stays on as decided (2026-10-01); a real-machine check (a handler that stores while a
   fill is armed) or a second reading of the equation would settle it.
2. **Double byte (`LD H,H`).** The PLD writes both byte lanes in one cycle: the low lane gets the CPU's
   byte, the high lane the IDE high-byte latch `HDDR` (`MDOY = HDDR`, `DATA_MUX.TDF`). With the IDE
   latch of S3b the other lane should take that byte (likely the DSS IDE driver's fast sector read).
3. **Waits at 3.5 MHz while a mode is armed.** The PLD's `/MR_WAIT` includes `ACC_ON` (any armed
   mode) besides the turbo; `SprinterWaits` (MAME's rule) applies only in turbo.
4. **The floppy RESTORE timing** (roadmap §8) blocks FN after its screen clear; the boot test works
   around it.
5. **ALL_MODE read-back** (code `#C3` as a read, the 3.07 beta table): already answered, every read of a
   cell code returns the cell, which holds the last ALL_MODE write. Nothing was added.

## Totals

`core-tests` 6 017 passed, 0 failed, 42 skipped (`test-parallel`, 20 shards; the TTD corpus and the
TTD CI gate are among them, unchanged). Build with `-DTESTS=ON -DBENCHMARKS=ON`: no compiler warnings
(only the Homebrew linker notes).
