# ZX Next: copper picture latency, measured on the RTL (POC 028)

Date: 2026-10-12. Source: [POC 028](../../../tools/poc/028-next-video-pipeline-alignment/README.md) (reports, scripts, the
patched-RTL builder). Implementation: `ScreenNext::PictureLatencyOf(reg, previous, value, paletteWriteSelect)`
(`core/src/emulator/video/next/screennext.cpp`), tests `ScreenNextLatency_Test`, `NextLayerCaptureMachine_Test`.

## Method

The official core (`cores/zxnext/src`, 3.02.03) is simulated whole in GHDL 6.0 (VHDL-2008, ~17 000 clocks/s) with the top level's
clocks and SRAM arbiter; the Z80 is held in WAIT; a register (or a palette value, an injected NextREG write, a sprite port write) is
changed at a known 28 MHz clock, 32 clock offsets, and the first output sample that differs from a reference is found: the clean line
two above (first set) or the same run with the value never changed (second and third set). Calibration: the ULA enable and the
fallback colour measure 19 clocks after the force, 22 from the copper's WAIT (`L = minimum - 1 + 4`, one clock for the force's edge,
four from the WAIT to the write). All numbers are 28 MHz clocks from the WAIT; a "cell" is 8 pixels = 32 clocks on a lattice
anchored at the grid's x = 0; a "pixel" is 4 clocks, a "half pixel" 2.

## Results

| register | clocks from the WAIT | the effect starts at |
|---|---|---|
| NR #4A fallback, NR #14 global transparency | 22 | half pixel |
| layer enables (NR #68 b7, NR #15 b7 / b0, NR #6B b7), layer order NR #15 b4:2 (counted 24, measured 22) | 22 | half pixel |
| palette selects NR #43 b3:1, NR #6B b4 | 22 | half pixel |
| ULA clip NR #1A, tilemap clip NR #1B, sprite clip NR #19 | 22 | pixel |
| Layer 2 palette offset NR #70 b3:0 | 22 | pixel |
| LoRes scroll NR #32 / #33 | 22 | pixel |
| LoRes palette offset, Radastan mode / XOR (NR #6A) | 23 | pixel |
| ULANext NR #43 b0, NR #42 | 20 | half pixel |
| tilemap transparent index NR #4C | 20 | half pixel |
| Layer 2 enable NR #69 b7, scroll NR #16 / #17 / #71, clip NR #18, bank NR #12 / #13 | 26 | pixel |
| Layer 2 resolution NR #70 b5:4 | 27 | pixel |
| ULA scroll NR #26 / #27 | 56 | next cell |
| ULA fine scroll NR #68 b2 | 70 | next cell |
| tilemap scroll NR #2F / #30 / #31, control bits NR #6B b0 / b1 / b3 / b5 | 53 | next cell |
| tilemap 80 columns NR #6B b6 | 24 (upper bound) | 32 clocks |
| palette value NR #41 / #44 into the sprite palette | 16 | half pixel |
| ... into the tilemap palette | 17 | half pixel |
| ... into the ULA palette, Layer 2 palette | 18 | half pixel |

When one write changes bits with different latencies the earliest decides.

## Facts that came with it

- Layer 2 and the ULA are aligned: pixel 0 of both leaves at line position 64 (the earlier "5 pixels behind" was a counting error).
- NR #43 bits 3:1 are the active palettes; bits 6:4 are the write select (the emulator had them confused).
- Sprites: NR #4B compares the raw pattern byte before the palette offset (4-bit patterns: the nibble); the sprite clip (NR #19) is
  applied where the line buffer is read, so it shows in the same line.
- Sprites: a fill-stage input (attribute, pattern byte, NR #4B) written in line N shows in line N + 1 or N + 2, depending on the
  position of the write against the engine's progress through the sprites: modelled, see below.

## Sprite fill (implemented 2026-10-12)

The engine fills paper row R's buffer during row R - 1: it starts at about raw hc -21 (sprite qualification at hc 437 of row R - 2) and
goes through the sprites in index order, one 28 MHz clock for each and 17 more for a drawn one (`S_QUALIFY` / `S_PROCESS`), so the fill
of a row ends at raw hc `(last drawn index + 1 + 17 x drawn) / 4 - 21` of row R - 1. What a sprite sees is the state at that moment:
a write before it shows in row R, after it in row R + 1 (measured with injected port #57 / #5B and NextREG writes: NR #4B at hc 20 ->
next row, hc >= 100 -> the row after; the row's later sprites see a write that lands inside the fill).

`ScreenNext` keeps one snapshot of the sprite state per epoch of writes (`NextSprites::Epoch`) and the row's snapshot is taken when the
beam passes the fill end, or before a write to the fill inputs (`SpriteStateWillChange`: CPU NextREG writes through the board's
before-write hook, ports #57 / #5B / #303B, copper MOVEs). A row draws from its snapshot with the enable, the clip and the palette of
the output stage (those are 22 / 16..18 clocks, above). Approximation: one instant per row (the end of the fill), not a time per sprite:
a write inside the ~60 hc window reaches the sprites drawn before it too. Tests `NextSpriteFill_Test`.

## Other items closed with it

- **Mouse DPI** (NR #0A bits 1:0): not in `zxnext.vhd` but in `input/ps2_mouse.v` of the same repository: the PS/2 deltas are scaled
  as the counters take them: 00 x2, 01 x1, 10 half, 11 a quarter (arithmetic shifts). `Mouse::SetDeltaShift`, applied by the Next's
  decoder; the buttons reverse (NR #0A bit 3) is `{third, left, right}` swapped at the same place.
- **ULA+ read wait state**: `ulap_wait_n` is low for `port_ff3b_rd` in mode 0 until `ulap_palette_rd_done` (set one 28 MHz clock after the
  read starts, held while a copper palette write is in the way). At 3.5 / 7 / 14 MHz the CPU samples WAIT after that clock: no wait. At
  28 MHz one T-state at most. Not modelled (the 28 MHz case is not verified on a running CPU).
- **Radastan / LoRes Y wrap**: `lores.vhd` increments y(7:6) from y_pre = 192, so the row runs on to 254 (rows 96-127 read the memory
  behind the 96 rows, 14-bit address wrap) instead of wrapping at 192 as the emulator did; fixed for both modes.
- **Tilemap text mode** (NR #6B bit 3): measured with the register forced: 53 clocks, the cell, like bits 0 / 1 / 5.

## Not measured

- Sprite state written by real CPU instructions at the beam (only injected writes: the same RAM, so the same fill rule).
- UART, joystick-port UART and other non-video registers: out of this POC.

## Reproduce

`tools/poc/028-next-video-pipeline-alignment/run.sh <cores/zxnext/src>` (first set, 27 scenarios),
`run-diff.sh <cores/zxnext/src>` (scenarios 28-53, differential), `run-sprite-fill.sh` (fill timing, needs the run-diff build).
GHDL 6.0 (llvm), about 35 s a run, 8 in parallel.
