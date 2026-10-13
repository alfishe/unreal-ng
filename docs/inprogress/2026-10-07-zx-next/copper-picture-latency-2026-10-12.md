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
| tilemap scroll NR #2F / #30 / #31, control bits NR #6B b0 / b1 / b5 (b3 by reading) | 53 | next cell |
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
  position of the write against the engine's progress through the sprites. See below.

## Sprite fill (open: the emulator draws a line from the live state)

The engine fills the next line's buffer from the end of the line before (sprite qualification at hc ~437), going through the
sprites in index order, so what a sprite of line N + 1 sees is the state at (line N, ~hc 435 + a few clocks per sprite). A write
before that point shows in line N + 1, after it in line N + 2. Measured (injected port #57 / #5B and NextREG writes): NR #4B at hc 20
-> N + 1, at hc >= 100 -> N + 2; attributes mostly N + 2 even at hc 20 for sprites 64-127 (late in the order); pattern bytes N + 2.
`NextSprites::DrawLine` draws from the state at the time the line is rendered, so a program that rewrites sprites in the line
before sees the change one line early, and a copper change of the pattern or the attributes in the middle of a line lands on the
wrong line. Implementing it needs the sprite state snapshot per line at the fill start (or a timestamped write queue); the table
above and `tools/poc/028-next-video-pipeline-alignment/measurements-2026-10-12-register-sweep.md` hold the numbers.

## Not measured

- The tilemap text mode (NR #6B b3): no visible change in the test pattern; by reading `tilemap.vhd` it is latched with the cell.
- Sprite state written by real CPU instructions at the beam (only injected writes: the same RAM, so the same fill rule).
- UART, joystick-port UART and other non-video registers: out of this POC.

## Reproduce

`tools/poc/028-next-video-pipeline-alignment/run.sh <cores/zxnext/src>` (first set, 27 scenarios),
`run-diff.sh <cores/zxnext/src>` (scenarios 28-53, differential), `run-sprite-fill.sh` (fill timing, needs the run-diff build).
GHDL 6.0 (llvm), about 35 s a run, 8 in parallel.
