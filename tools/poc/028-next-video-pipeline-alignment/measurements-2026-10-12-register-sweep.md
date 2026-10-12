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

## Not measured

Sprites (their line buffers and pattern RAM are coregen cores the model does not replace: `spram_320_9`, `sdpbram_16k_8`), the LoRes
scroll, the tilemap control bits and its transparent index (no visible change), the Layer 2 resolution, the palette value writes (they
do not pass a register: the 16 clocks of the emulator are the derived lookup stage), NR #15 bit 0 (sprite enable) and bit 7 (LoRes enable)
on the assumption that they travel the chain of the other enables (`sprite_en_0 .. _1`, `lores_en_0 .. _1`).
