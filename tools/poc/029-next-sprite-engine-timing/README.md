# 029 - next-sprite-engine-timing

Feature: ZX Next sprites ([TODO.md](../../../docs/inprogress/2026-10-07-zx-next/TODO.md), "Sprites: ... sprites_overtime / too-many flag
timing"; `NextSprites::DrawLine`, `core/src/emulator/io/z80n/nextsprites.cpp`).

Status: **concluded 2026-10-12**. Run: `run.sh <cores/zxnext/src>` (GHDL, VHDL-2008, `-fsynopsys`; about a minute).

## Goals

1. How many 28 MHz clocks does the sprite engine (`video/sprites.vhd`) use for a line, and at which total does `sprites_overtime`
   (status bit 1 of port #303B) get set and the tail of the line stop being drawn?
2. What does a sprite cost beyond x = 320, and from which x does it wrap to 0 (the 512 space)?
3. Which writes count for the collision flag (status bit 0): is it before or after the clip window, the 224-line limit, x >= 320?

## Requirements

- The reference is the RTL only (`cores/zxnext/src/video/sprites.vhd`, core 3.02.03).
- The engine alone, with behavioural stand-ins for the three vendor memories (attribute RAM, line buffer, pattern BRAM).

## Constraints

- No change to the core from the POC; the numbers are adopted in `NextSprites::DrawLine` with its own tests
  (`NextSprites_Test.TooManyFlagAndTheCutOffFollowTheLineClocks`, `CollisionIgnoresTheClipWindowTheLowerBorderAndTheEnableBit`,
  `SpritesWrapAroundThe512Space`).

## Acceptance criteria

- A table of the overtime threshold and the cut-off point for both line lengths, and a table of collision cases, from the simulation.

## Method

`tb.vhd` drives `sprites` with a 28 MHz clock, its inverse, a 7 MHz pixel clock, a 9-bit `hcounter_i` that runs from 496 (-16) for
`g_len` pixels a line like `zxula_timing.vhd` (448 on the 48K / Pentagon timing, 456 on the 128K / +3), `vcounter_i` stepping at the
line start. Sprites are programmed through the NextREG mirror interface; the pattern memory returns 1 + the pattern number, the line
is read back through `pixel_en_o` / `rgb_o` (the probe sample is one pixel to the right of the sprite's x). `K` unscaled sprites of
16 pixels stand at one y, a "tuner" sprite (index 126) at x = 320 - m, and a probe sprite (index 127, x = 288).

## Results

T = 1 (S_START) + 128 (one S_QUALIFY per sprite) + the sum of the S_PROCESS clocks of the sprites on the line; a drawn sprite costs
16 << x scale clocks, a sprite whose x is 320..494 costs one, one that starts at 495..511 runs its full width (the wrapped part is
drawn from x = 0), one that starts at 320 - w..319 stops when it reaches 320. The line is 4 * L clocks (1792 for L = 448, 1824 for 456).

| T against 4 * L | status bit 1 | the last sprite |
|---|---|---|
| T <= 4 * L - 1 | 0 | complete |
| T = 4 * L | 1 | complete |
| T = 4 * L + n | 1 | its last n pixels (the clocks after the line period) are missing; sprites after the cut are not drawn |

Collision (bit 0): a non-transparent write onto a line-buffer position already written, with x in 0..319, whatever the clip window, the
224-line limit, the over-border bit and zero-on-top say (those act at the output); two sprites entirely at x >= 320, or one at x >= 320
and one at 312..319, do not collide; overlaps after the wrap (500 and 2: x = 0..3) do. NR #15 bit 0 only gates the output in `zxnext.vhd`
(`sprite_en_1`), the engine and its flags run with the sprites off.
