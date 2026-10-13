# Measurements 2026-10-12: a register changed at a known clock, the first changed output pixel

## Setup

- RTL: the official Next FPGA repository, `cores/zxnext/src`, core 3.02.03 (`g_machine_id` X"0A", issue 5 board); read only. `patch_rtl.py`
  makes the simulation tree in `scratch/` (the changes are listed in its header and in [analysis.md](analysis.md)).
- Simulator: GHDL 6.0.0 (llvm backend), VHDL-2008, the whole `zxnext` entity (CPU, DMA, copper, video, Layer 2 through the SRAM arbiter
  of `zxnext_top_issue5.vhd`, tilemap, UARTs, ...). One CLK_28 period 36 ns; CLK_14 / CLK_7 / CPU clock as the top level derives them.
  About 17 000 clock cycles a second: 35 s a scenario (three frames).
- Stimulus (`tb_zxnext.vhd`): after reset the CPU is held in WAIT (it would run the boot ROM and write the registers); the registers
  under test are forced by `tbh` hooks that sit at the end of the NextREG process, so a value takes the place the NextREG write
  gives it (visible after the next CLK_28 edge). Per scenario 32 changes, one per 4 lines (change, two lines changed, restore, a clean
  line), the clock offset inside a 7 MHz pixel stepped by one CLK_28 clock each time and the line position fixed (`g_h0`).
- Picture: ULA bitmap rows alternate #F0 / #0F, the attribute of each column gives it its own ink / paper, the palettes map an index to
  the same colour number; Layer 2's SRAM model returns x (bit 7 flipped on odd rows); the tilemap's tile 0 rows alternate. Every
  neighbouring pixel group differs, so a change in the register shows as a changed colour.
- Measure: `o_RGB` at every CLK_14 edge with the line's raw `hc` and the CLK_28 count; the first sample of the changed line that differs
  from the clean line two lines above, after the force; reported as clocks (CLK_28) after the force.

## Calibration

NR #68 bit 7 (ULA enable) and NR #4A (fallback): minimum 19 clocks. The chain `ula_en_0 .. rgb_out_o` / `fallback_rgb_0 .. rgb_out_o` is
22 clocks from the clock the copper sees the WAIT true (`scratch/cosim-copper/LATENCY.md`: write E+4, output E+22), 18 after the write,
so the measure reads 1 clock more than the write-to-output distance (the override takes one edge). The latency from the WAIT used in the
emulator is `minimum - 1 + 4`.

## Results

| scenario | register | min / max (clocks after the force) | from the WAIT (minimum - 1 + 4) | granularity |
|---|---|---|---|---|
| 1 | NR #68 bit 7, the ULA enable | 19 / 20 | 22 | half pixel |
| 2 | NR #4A, the fallback colour | 19 / 20 | 22 | half pixel |
| 3 | NR #26, ULA scroll X (8 px) | 53 / 84 | 56 | 32-clock cell |
| 4 | NR #1A, ULA clip x1 | 19 / 22 | 22 | 4-clock pixel |
| 5 | NR #1A, ULA clip x2 | 19 / 22 | 22 | pixel |
| 6 | port #123B bit 1 / NR #69 bit 7, Layer 2 enable | 23 / 26 | 26 | pixel |
| 7 | NR #16, Layer 2 scroll X | 23 / 26 | 26 | pixel |
| 8 | NR #18, Layer 2 clip x1 | 23 / 30 | 26 | pixel |
| 9 | NR #27, ULA scroll Y | 53 / 84 | 56 | cell |
| 10 | NR #68 bit 2, ULA fine scroll X | 67 / 98 | 70 | cell |
| 11 | NR #18, Layer 2 clip x2 | 23 / 26 | 26 | pixel |
| 12 | NR #17, Layer 2 scroll Y | 23 / 26 | 26 | pixel |
| 13 | NR #70 bits 3:0, Layer 2 palette offset | 19 / 22 | 22 | pixel |
| 14 | NR #6B bit 7, tilemap enable | 19 / 40 | 22 | half pixel |
| 15 | NR #30, tilemap scroll X | 50 / 81 | 53 | cell |
| 16 | NR #1B, tilemap clip x1 | 19 / 26 | 22 | pixel |
| 17 | NR #31, tilemap scroll Y | 50 / 81 | 53 | cell |
| 18 | NR #15 bits 4:2, layer order | 19 / 22 | 22 | pixel |
| 19 | NR #14, global transparency | 19 / 36 | 22 | half pixel |
| 20 | NR #43 bit 1, ULA palette select | 19 / 20 | 22 | half pixel |
| 21 | NR #43 bit 2, Layer 2 palette select | 19 / 20 | 22 | half pixel |
| 22 | NR #6B bit 4, tilemap palette select | 19 / 24 | 22 | half pixel |
| 23 | NR #1A, ULA clip y2 | 19 / 22 | 22 | pixel |
| 24 | NR #43 bit 0, ULANext enable | 17 / 18 | 20 | half pixel |
| 25 | NR #42, ULANext format | 17 / 18 | 20 | half pixel |
| 26 | NR #4C, tilemap transparent index | no visible change in the pattern | - | - |
| 27 | NR #1A, ULA clip y1 | 19 / 22 | 22 | pixel |

(The spread max - min is the granularity: a register read per 8-pixel cell shows at the next cell, 32 clocks; per pixel 4.)
Layer 2 and the ULA leave at the same position: pixel 0 of Layer 2 and of the ULA's paper both at line position 64 (the sweep's
unchanged lines).

## Second set: sprites, LoRes, palette values, tilemap control bits (`run-diff.sh`, scenarios 28-43)

The first set diffed a changed line against the clean line two above; sprites are different on every line, so this set runs each
scenario twice (`g_ref`: the same sequence, the value never changed) and takes the first sample at or after the force that differs
between the runs. The coregen RAMs the first set left out are modelled now (`patch_rtl.py`): `spram_320_9` (sprite line buffers,
asynchronous read), `sdpbram_16k_8` (pattern RAM, one solid colour #21) and five attribute RAMs (128 sprites in a 16 x 8 grid, palette
offset 8). A palette value is written through a hook at the NR #41 / #44 write strobe (`t_pal_we`), so it takes the dpram's write edge
a register would take. Three bank 5 patterns (`POC_PATTERN`): std (the first set's), `lores` (every byte differs: LoRes reads 128 bytes
a row), `dense` (one ULA attribute, tilemap entries alternating tile 0 / 1 and attribute bit 0, a second tile). Same calibration:
from the WAIT = minimum - 1 + 4.

| scenario | register | min / max (clocks after the force) | from the WAIT | granularity |
|---|---|---|---|---|
| 28 | NR #32, LoRes scroll X | 19 / 22 | 22 | pixel |
| 29 | NR #33, LoRes scroll Y | 19 / 22 | 22 | pixel |
| 30 | NR #15 bit 0, sprite enable | 19 / 20 | 22 | half pixel |
| 31-34 | NR #19, sprite clip x1 / x2 / y1 / y2 | 19 / 22 | 22 | pixel |
| 35 | NR #41 / #44 into the ULA palette | 15 / 32 | 18 | half pixel |
| 36 | ... into Layer 2's palette (the SRAM returns one byte) | 15 / 16 | 18 | half pixel |
| 37 | ... into the sprite palette | 13 / 15 | 16 | half pixel |
| 38 | ... into the tilemap palette | 14 / 37 | 17 | half pixel |
| 39 | NR #4C, tilemap transparent index | 17 / 33 | 20 | half pixel |
| 40 | NR #6B bit 0, tilemap over the ULA | 54 / 85 (first run), 71 / 101 (dense) | 57 | cell |
| 41 | NR #6B bit 1, 512 tiles | 50 / 149 | 53 | cell |
| 42 | NR #6B bit 6, 80 columns | 21 / 66 | 24 (an upper bound) | 32 clocks |
| 43 | NR #6B bit 5, no attribute byte | 50 / 85 | 53 | cell |

Reading of the table:
- The sprite clip is applied where the sprite line buffer is read, not where it is filled: the change shows in the same line, 22 clocks
  after the WAIT, like the ULA clip.
- Where a pattern is sparse the minimum rises (it is the distance to the next differing pixel): the minima over every run of a
  register are the values; 40 and 43 are the tilemap scroll's cell 53 / 57 and the 80-column bit is latched every 32 clocks
  (`tm_mode` at `hcount(4:0) = "11111"`) and read by the output side directly, so it is the earliest of them (24; the output shows the
  change from the first latch).
- The four palettes' lookup stages differ by a clock or two: sprites 16 (the line buffer is read first), tilemap 17, ULA and Layer 2 18.

## Third set: injected NextREG and sprite port writes (scenarios 44-53, `run-sprite-fill.sh`)

Hooks (`patch_rtl.py`): `t_nr_*` replace the NextREG write strobe (the real decode), `t_p57_we` / `t_p5b_we` / `t_p303b_we` the sprite
ports with `t_p_dat` on the data bus. Same calibration (from the WAIT = minimum - 1 + 4).

| scenario | register | min / max | from the WAIT | granularity |
|---|---|---|---|---|
| 49 | NR #6A bit 5, Radastan | 20 / 34 | 23 | pixel |
| 50 | NR #6A bit 4, Radastan XOR | 20 / 34 | 23 | pixel |
| 51 | NR #6A bits 3:0, LoRes palette offset | 20 / 23 | 23 | pixel |
| 52 | NR #70 bits 5:4, Layer 2 resolution | 24 / 27 | 27 | pixel |
| 53 | NR #6B bit 3, tilemap text mode (the register forced; a NextREG write of NR #6B showed no change in this bench) | 50 / 81 | 53 | cell |
| 48 | NR #4B = #A1 (the pattern byte + palette offset) | no change | - | - |

NR #4B is compared with the pattern byte before the palette offset is added (47: #21 turns the sprites transparent, 48: #A1 does
nothing); 4-bit patterns compare the nibble. The emulator already does that.

### What the sprite engine consumes: a line or two later, not at a pixel

The engine fills the next line's buffer (`spram_320_9`) from the end of the line before: sprite 5's attribute qualification is at line
hc ~437, and the engine's vertical counter is 33 ahead of the picture's (sprites 64-127 of the test grid, Y = 96..159, show at lines
64-127). Everything the fill reads (the attribute RAMs, the pattern RAM, NR #4B) therefore reaches the picture a line or two after the
write. Writes at position h of line N (scenarios 44 / 45 attributes through port #57, 46 pattern byte through port #5B, 47 NR #4B;
the effect shown on line N + 1 or N + 2; clocks from the force to the first changed pixel):

| h (hc) | NR #4B | attribute | pattern byte |
|---|---|---|---|
| 20 | N + 1 (2027) | N + 1 in 6 of 14 lines, else N + 2 (2117) | N + 2 (3621) |
| 60 | N + 1 for lines >= 98 (from hc 224-238), else N + 2 | N + 2 (3781) | N + 2 (3461) |
| >= 100 | N + 2 (3301 .. 2501) | N + 2 | N + 2 |

So a write is seen by the fill of the next line only when it lands before the engine reached that sprite / byte, and the engine goes
through the sprites in index order: the same write is early for sprite 3 and late for sprite 100. Sprites 0-15 of a row qualify
within a few clocks of each other; pattern bytes are read when the sprite is drawn, after its qualification. A model needs the fill
order, not a latency: the state the sprites of line N + 1 see is the state at (line N, ~hc 435 + a few clocks per sprite).

## Not measured

The sprite attribute / pattern writes by a CPU instruction (the fill rule above is measured with injected writes), the sprite palette
select / NR #43 bit 3 at the fill (the output stage: 22), 
