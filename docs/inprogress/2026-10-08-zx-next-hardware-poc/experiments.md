# The experiments: method, screen, expected values

All numbers on the screen are **hexadecimal**, printed with the program's own 4-pixel-wide digits (0-9, A-F). Predictions
are what the FPGA VHDL (core 3.x, [research-fpga-vhdl.md](../2026-10-07-zx-next/research-fpga-vhdl.md)) says; they are
what we want to confirm or refute. "Core" means the version printed on the second row of every screen (NextREG `#01`,
`#0E`, `#0F`, and `#03` = machine type / timing).

Every screen starts with:

| Row | Content |
|:--|:--|
| 0 | the program id: `01` h1timing, `02` h2dma, `03` h3sprites, `04` h4z80n |
| 1 | four bytes: NR `#01` (core id), NR `#0E` (core sub-version), NR `#0F` (board id), NR `#03` (machine type and timing as set) |
| 23 | `D0DE` when the program has finished |

## H1: instruction throughput, memory waits, contention (`h1timing`)

**Method.** Six "bodies" of 64 identical instructions (`NOP`; `LD A,(HL)` from four kinds of memory; `LD (HL),A`) are run
in a loop; each loop pass also reads the raster line (NextREG `#1E` / `#1F`). The program counts passes over **eight**
video frames (raster wrap = frame boundary) at CPU speed 0 / 1 / 2 / 3 (NextREG `#07`). Contention is left as the machine
has it; the last column repeats speed 0 with contention disabled (NextREG `#08` bit 6).

**Screen.** Row 2: lines per frame (`0138` = 312, `0137` = 311, `0140` = 320). Rows 4-9, one body per row:

| Row | Body |
|:--|:--|
| 4 | `NOP` x 64 (baseline) |
| 5 | `LD A,(HL)` from `#4000` (bank 5, the screen bank: the contended one) |
| 6 | `LD A,(HL)` from `#8800` (bank 2, plain RAM) |
| 7 | `LD A,(HL)` from `#C000` showing 8K pages 2-3 (bank 1: odd bank, contended in 128K timing) |
| 8 | `LD A,(HL)` from `#C000` showing 8K pages `#20-#21` (extra RAM, never contended) |
| 9 | `LD (HL),A` to `#C000` showing 8K pages `#20-#21` |

Columns 2, 7, 12, 17 = speed 0 (3.5 MHz), 1 (7), 2 (14), 3 (28); column 22 = speed 0 with contention off. Each cell is
the number of passes in 8 frames.

**Reading it.** Let `n` be a cell, `F` the CPU clocks in 8 frames at that speed
(`F = 8 * lines * T_line * 2^speed`, with `T_line` = 228 for 128K / +3 timing and 224 for 48K / Pentagon timing; take
`lines` from row 2), and `n0` the row-4 cell of the same column. The extra clocks of a body over a `NOP` body is

    delta = (F / n  -  F / n0) / 64          clocks per instruction

**VHDL prediction** for `delta` (CPU clocks, counted at the CPU's own speed):

| Body | 3.5 / 7 / 14 MHz | 28 MHz | Why |
|:--|:--|:--|:--|
| `LD A,(HL)` plain RAM (rows 6, 8) | 3 | 4 | 7 T instead of 4 T; at 28 MHz every SRAM read has one more wait state (`sram_wait_n`), the opcode fetch of the `NOP` too, so only the data read adds one |
| `LD (HL),A` (row 9) | 3 | 3 | writes have no extra wait |
| bank 5 (row 5) | 3 plus contention (about 1 on average over a frame, see below) | 3 or 4 (bank 5 is a block RAM read by the CPU on its own schedule: `cpu_bank5_sched`) | contention only at 3.5 MHz |
| bank 1 (row 7) | 3 in 48K timing; 3 plus contention in 128K timing | as row 6 | odd banks contended only in 128K timing |
| column 22 | 3 for rows 5 and 7 | | contention disabled |

Contention averages over the frame because it only applies on the 192 paper lines and the first 128 pixel clocks of a
line (about a third of the frame); a contended read inside the window costs 0-6 extra clocks following the
`6,5,4,3,2,1,0,0` pattern. So expect `delta` for row 5 at speed 0 between about 3.7 and 5.

**What we learn.** The SRAM wait rule at 28 MHz (is the data read really `+1`? does the opcode fetch also wait? does a write
wait?), whether contention is absent at 7 / 14 / 28 MHz, the frame length in the current timing, and the true
`NOP` rate (the ratio of the row-4 cells between speeds shows the overhead of the loop, which is the same at every speed).

## H2: DMA transfer time (`h2dma`)

**Method.** The zxnDMA (port `#6B`) copies 4096 bytes from `#A000` to `#C000` (extra RAM) after a raster wrap; the raster
line is read when the transfer has finished. A second run copies 1 byte to measure the cost of loading the DMA program;
the difference is shown. Port timing bytes 0 / 1 / 2 give 4 / 3 / 2 cycles per access; the fourth column is burst mode.

**Screen.** Row 2: lines per frame. Rows 4-7 = speed 0..3; columns 2 / 7 / 12 / 17 = continuous with 4 / 3 / 2 cycles, burst
with 4 cycles. Each cell = raster lines elapsed (hex).

**VHDL prediction** (each byte = read cycles + write cycles, counted in CPU clocks; one raster line = 228 clocks at 3.5
MHz in 128K timing, 224 in 48K, times `2^speed`): about

| Speed | 4 cycles | 3 cycles | 2 cycles | burst, 4 |
|:--|:--|:--|:--|:--|
| 0 (3.5 MHz) | `90` (144) | `6C` (108) | `48` (72) | `90` |
| 1 (7 MHz) | `48` | `36` | `24` | `48` |
| 2 (14 MHz) | `24` | `1B` | `12` | `24` |
| 3 (28 MHz) | `12` | `0D` | `09` | `12` |

(lines, hex; 4096 x 8 / 228 = 143.7 etc.) The cells are only exact to about one line; a mismatch beyond 5 percent matters.
Possible real-board differences: one extra clock per byte for state changes, a different cost in burst mode, the DMA
clock not following the CPU speed. **Core 3.02.02 fixed the DMA at 3.5 and 7 MHz** (the read byte had been one read
cycle behind), so older cores may differ at those two speeds: always file results with the core version.

## H3: sprites per line (`h3sprites`)

**Method.** 128 sprites, all with the same 16 x 16 pattern, are switched on one by one. After each step the program waits
two frame boundaries and reads port `#303B`: bit 1 = "more sprites than the hardware could draw on a line", bit 0 =
collision (it will always be set, the sprites overlap). The program prints the first count at which bit 1 appeared
(`FF` = never up to 128). Six variants (sprites 0..N-1 on):

| Row | Variant | The question |
|:--|:--|:--|
| 4 | 8-bit patterns, one size, all on the same line | the basic limit |
| 5 | 4-bit patterns | does the pattern depth matter? |
| 6 | 8-bit, X scale 2 | is the limit pixel-bound (a wider sprite takes longer)? |
| 7 | 8-bit, X scale 8 | same, stronger |
| 8 | 8-bit, Y scale 8 | taller sprites: same pixels per line as row 4 |
| 9 | 8-bit, 12 different rows (about 10 sprites per line) | a fixed per-sprite cost (the hardware looks at every sprite on every line) |

**Screen.** Column 2 = first N with bit 1 (hex byte), column 6 = collision seen (`01`).

**Prediction.** The VHDL has no sprite counter: the flag is set when the line engine is still busy at the next line start, so
the threshold is a **time budget**. Rows 6 and 7 should show a clearly smaller N than row 4; row 8 about the same as row 4;
row 9 `FF` (or a much larger N than row 4) if only sprites on the current line cost time. Changelog 3.02.02 says the
hardware "will always use as much time as possible" and that the limit is **at least 100** sprites per line (before that core
drawing stopped around 70): so on core 3.02.02 or later row 4 should read `65` (101) or more, `FF` if all 128 fit. ZEsarUX uses
a count of 100. A smaller value means the core is older or the budget has to be derived from the VHDL cycle model (POC P6 of
the design).

## H4: `LDIRSCALE` and friends (`h4z80n`)

**Method.** Same setup for three instructions: source `#A000` = bytes `00..0F`, destination `#A100` zeroed, `A` = `05`,
`BC` = 16, `HL` = `#A000`, `DE` = `#A100`, alternate `BC'` = `DE'` = `#0100`; one instruction, then the registers and the
16 destination bytes are shown.

**Screen.** Three groups: rows 4-7 `LDIRSCALE` (`ED B6`), rows 9-12 `LDIRX` (`ED B4`), rows 14-17 `LDPIRX` (`ED B7`). In a
group: first row = `HL`, `DE`, `BC` (4 digits each, columns 0, 5, 10); second row = `A` and `F` (`AAFF`); the next two rows =
the 16 destination bytes as 8 words (4 per row).

**Prediction (VHDL).** `LDIRSCALE` is decoded but its scaling is commented out, so it behaves like `LDIRX`:

| | HL | DE | BC | A | destination |
|:--|:--|:--|:--|:--|:--|
| `LDIRX` and (predicted) `LDIRSCALE` | `A010` | `A110` | `0000` | `05` | `00 01 02 03 04 00 06 07 08 ... 0F` (the byte equal to `A` is not copied: the 6th stays 0) |
| `LDPIRX` | `A000` (unchanged) | `A110` | `0000` | `05` | `00 01 02 03 04 00 06 07 00 01 02 03 04 00 06 07` (the source address is `HL` with its low 3 bits replaced by those of `DE`) |

A real `LDIRSCALE` that scales (`HL += BC'`, `DE += DE'` per step) would show a different `HL` / `DE` and a sparse
destination. `F` after each instruction is shown for the Z80N flag rules (S, Z, C kept; H = N = 0; P/V = BC != 0).
