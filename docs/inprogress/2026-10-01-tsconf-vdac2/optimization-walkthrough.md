# VDAC2: FT812 drawing optimization walkthrough

**Created:** 2026-10-03. **Status:** rounds 0-7 done; next round open.

How the FT812 emulation (the eve-emu library) went from drawing Zuma Deluxe at half the
speed of the real card to thirteen times faster than it. Each round records what we
started from, what was slow and why, what we changed, and what the numbers mean.
Background on the drawing model: [acceleration-experiments.md](acceleration-experiments.md),
line costs: [line-budget-model.md](line-budget-model.md).

## Terms

| Term | Meaning |
|:--|:--|
| Real-time factor | chip time emulated per second of CPU time on one core. 1.0x = just keeps up with the card; below 1.0x the game slows down. Measured by `eve-replay` on a capture, so the Z80 side is not included |
| Capture (`.evr`) | a recording of everything the Z80 sent to the FT812. Replaying it draws exactly the same frames every time, so before / after comparisons are fair |
| Span | the part of one screen line one bitmap covers. The drawing work is spans x pixels |
| Fast path | the drawing code for the common cases: decode a whole span of texels at once, then blend. Anything it does not cover goes to the general path, which handles every case one pixel at a time and is several times slower |
| ns/px | nanoseconds of CPU per drawn pixel for one kind of span |
| Bit-exact | the optimized code produces the same pictures, byte for byte, as before |

## How we measure

- **Captures.** `rtype-boot.evr` and `rtype-play.evr` (R-Type, with the FT81x ROM image)
  and `zuma-demo.evr`: Zuma from power-on, recorded in unreal-ng through Wild Commander,
  loading screen, menu and gameplay demo, 8603 FT812 frames (~146 s of chip time). It is
  recorded **without the ROM image**, as users' builds run (unreal-ng does not ship it).
  Loading screen ≈ frames 1309-2782, gameplay ≈ 7000-8600.
- **Profiling build.** `eve-replay-profile` (`EVE_PROFILE`, not in the default build) counts
  per kind of span (format, filter, fast / general path, matrix, blend pipeline): spans,
  pixels, time, and why the fast path declined a span. `--profile FROM TO` limits it to a
  frame range. A sampling profiler (`sample`) on the normal build is the second view.
- **Timing.** CPU time of the process, two runs each, before and after taken in turns on
  the same (shared, busy) machine. CPU time hardly depends on the load, wall time does.
- **Bit-exactness gate,** every round:
  - the eve-emu tests (169, incl. golden pictures from the real chip's behavior);
  - `rtype-boot` and Zuma replayed with every picture and every answer compared against the
    capture; `rtype-play` (24193 pictures) for the final state;
  - on three builds: ARM NEON, x86_64 SSE2 (under Rosetta) and plain C++ (`EVE_SIMD=OFF`);
  - MinGW `-Werror` syntax check for the Windows toolchain.

## Round 0: the slowdown that was not drawing (2026-10-02)

**Start.** With VDAC2 enabled R-Type fell from a steady 48.83 FPS to 13-37 FPS and Zuma ran
at 3 FPS on the loading screen and 18 in play. Pentagon and TS-Conf without VDAC2 were
fine. It looked like a drawing regression from the newly added line metrics.

**What was really slow.** Nothing in the drawing. The `.spg` program was loaded while the
TS-BIOS was in the middle of a multi-block SD read (CMD18), and the emulated SD card ignored
the game's reset command (CMD0) during that stream. The game's FAT driver failed
("RTYPECOD.PAC NOT FOUND") and stopped in `DI; JR $`, while the FT812 kept redrawing a heavy
frozen frame from its last display list. Whether the race was won depended on timing, which
is why it came and went (opening a debug window or a capture shifted it).

**Change.** The SD card accepts CMD0 in any state, also in the middle of a stream; the SPG
loader leaves the card as a shell does, initialized and idle (`SdCardSpi::LeaveForProgram`).
Tests for both.

**Result.** R-Type steady 48.83 FPS again. **Meaning:** a frame rate drop on one machine
is first a question of what the program is doing; only then of how fast we draw.

## Round 1: the invisible font (loading screen)

**Start.** Whole Zuma capture: **305 s of CPU, 0.48x** real time. The loading screen cost
**133 ms of drawing per frame** (a frame lasts ~17 ms on the card).

**What was slow and why.** The profile: **77 % of the loading screen** was text in ROM
fonts 28 / 29 (`FT_Text` in Zuma's `ts-dos.asm`). Without the ROM image those fonts have an
empty layout, so every glyph samples transparent black, but the general path still blended
each transparent pixel of each glyph. The user sees nothing, and paid the most for it.

**Change.** A bitmap with an empty layout no longer draws pixels: under the default blend
only the tag buffer is written (what transparent pixels change), other blend settings take
transparent texels through the normal blend (`DrawTransparentSpan`). With the ROM image the
text is drawn as before.

**Result.** Loading screen **133 -> 31 ms** per frame. **Meaning:** the biggest cost was work
with no visible effect; the next step was the real picture.

## Round 2: the scaled background (BILINEAR) and the masking blends

**Start.** Loading screen 31 ms per frame. It is three passes over the whole screen
(`DrawBootDxtBackground` in Zuma's `loader_resident.asm`): an L4 alpha mask scaled 1.6x
with BILINEAR filtering, then two RGB565 passes blended through the destination alpha
(`DST_ALPHA / ZERO`, `ONE_MINUS_DST_ALPHA / ONE`).

**What was slow and why.** BILINEAR had no fast path: per pixel the general path
computed four texel addresses, decoded four texels through a format switch and blended
them, **30 ns/px**. The destination-alpha blends were plain C++ per pixel, 2.9 ns/px.

**Change.**
- BILINEAR on the fast path: the two texel rows a span needs are decoded once into column
  buffers; per pixel only the four neighbors are picked and blended with the same 8.8
  weights and the same rounding as the general path (`DrawBitmapBilinearFast`).
- The blend of the four taps in SIMD (`Simd::BilinearBlend`: NEON, SSE2, C++ fallback);
  division by 255 replaced by an exact shift form valid for the value range.
- The two masking blends in SIMD; `ONE / ZERO` with a color mask as a plain channel copy.

**Result.** BILINEAR **30 -> 3.4 ns/px**, masking blends **2.9 -> 1.2 ns/px**; loading screen
31 -> 19.9 ms (scalar) -> **7.5 ms** per frame (SIMD). Whole capture **305 -> 53 s, 0.48x ->
2.74x**. R-Type boot 3.32 -> 2.9 s, R-Type play 17.5 -> 16.0 s (first 4000 frames).
**Meaning:** the loading screen now draws in under half a card frame; Zuma as a whole is
faster than the card, but gameplay, not the loader, is now the main cost.

## Round 3: the rotated sprites and the palettes (gameplay)

**Start.** Measured again right before the round: **54.8 s, 2.66x** (busy machine). The
gameplay profile (frames 7000-8600):

| Kind of span | Share | Cost |
|:--|--:|--:|
| PALETTED4444, NEAREST, rotated matrix, general path | 31 % | 18.5 ns/px |
| ARGB4, NEAREST, rotated matrix, general path | 18 % | |
| Display list walk, clear, line output | 20 % | |

**What was slow and why.**
- The frog and the balls are bitmaps under a rotation matrix (transform B and D non-zero).
  The fast path only took scaled bitmaps, so every rotated span went to the general path:
  per pixel the full sample-position math, wrap arithmetic and a decode through the format
  switch.
- Spans fully above or below a bitmap with BORDER wrap also left the fast path.
- After the first change a sampling profile showed `LinePalette` at ~12 %: the palette
  cache held one palette and was reset every line, and Zuma's sprites each have their own
  palette. So every line decoded 256 palette entries per sprite again, through the bus.

**Change.**
- `DrawBitmapAffineFast`: NEAREST under any matrix. The sample position steps by A and D
  per pixel exactly as in the general path, wrap / BORDER per axis the same way, and each
  texel is read straight from graphics memory with a decoder chosen once per span.
- BORDER rows outside the bitmap take the transparent span.
- Decoded palettes are kept in the chip, 16 of them, and stay valid until graphics memory
  is written (a write counter bumped by every write, a restore and a reset). No write can
  happen while a line is drawn, so a hit is always current.

**Result.** Rotated spans **18.5 -> 3.6-4.3 ns/px**; scaled paletted spans 1.87 -> 1.41 ns/px
(palette hits). Whole capture **54.8 -> 38.3 s, 2.66x -> 3.8x** (3.94-3.98x on a quieter
machine). R-Type unchanged (6.1x on boot). **Meaning:** every kind of bitmap Zuma draws is on
the fast path now. Drawing is 60 % of gameplay time; the other 40 % is the walk itself.

## Round 4: walking the display list once, not on every line

**Start.** Measured right before the round: **37.3 s, 3.9x**. Gameplay profile: 4.67 ms of
drawing per frame, **40 % of it outside the bitmaps**. Per line the chip walked the whole
display list, 508 - 768 commands: `CELL` 102, `BITMAP_SIZE` 50, `BITMAP_SOURCE` 50,
`BITMAP_HANDLE` 18, `BITMAP_LAYOUT(_H)` 17 + 17, `COLOR_RGB` 17, `BEGIN` 12, the bitmap
transform 11 each, plus the vertices.

**What was slow and why.** Of the 107 bitmap vertices on every line, **96.7 (90 %) did not
touch that line**: a sprite covers a few dozen of the 768 lines. Yet every line decoded all
768 commands and set up every sprite, only to find it missed the line. The real chip does
the same (its time per line is part of the line budget), but an emulator only needs the
result.

**Change.** A line's walk changes the state the same way whatever the line: the words of
the list, `REG_MACRO_0 / 1` and the bitmap handles at the start of the line decide it (the
context is reset every line); only the drawing depends on the line. So the walk of one line
is recorded as its drawing steps (every vertex and `CLEAR`, with the context, the handle and
the primitive state they saw), and each later line with the same inputs replays just those
steps. A bitmap step whose rows miss the line is skipped by the same test the bitmap drawing
starts with. The line's command count (the line cost), its events and the handles it leaves
behind come from the recording.

- Inputs compared before every line: which display list buffer is active, a version counter
  of its contents (a swap, a restore, a reset), both macro registers, all 32 handles (768
  bytes). A miss records the line again, so the first line of a frame (handles still from the
  previous frame) and a macro written in the middle of a frame are handled.
- Tests: a list with every kind of step (bitmaps plain, rotated, paletted, a handle changed
  after it was drawn; points, line strip, edge strip, rectangles from a subroutine and a
  macro, a saved context, a scissored `CLEAR`), the macro and memory changed mid-frame, a new
  list: picture and every line's cost equal with and without the recording. Each check of
  the inputs was removed once to see the tests fail.

**Result.** 99.9 % of the lines are replayed. Gameplay drawing **4.67 -> 3.18 ms** per frame,
the part outside the bitmaps **40 % -> 12.5 %**. Whole capture **37.3 -> 30.8 s, 3.9x ->
4.73x**; rtype-boot 2.92 -> 2.69 s (6.1x -> 6.6x). **Meaning:** the brief's target of 4x
for Zuma on one core is reached; the cost is now the pixels themselves.

## Round 5: the tag buffer only where it is read

**Start.** The loading screen still drew 23 600 fully transparent spans per frame (the text
in ROM fonts without the ROM image, round 1), 21 % of its time; each only wrote the tag
buffer. Every other drawing step writes the tag too.

**What was slow and why.** The tag buffer of a line is read only on the line `REG_TAG_Y`
names (the touch tag register) and by a probe (which has its own buffers); nothing in the
pixel pipeline tests it. On every other line all tag writes were wasted.

**Change.** A line knows whether its tag buffer is read (`LineRun::tagLive`); every tag
write checks it. Tests: `REG_TAG` from a rectangle, from a fully transparent bitmap and from
`CLEAR`'s tag value (they fail when the tag line is not kept).

**Result.** Small: rounds 4 and 5 together 37.8 -> 30.0 s against `5f47ded`, of which round
5 is about 3 %. The transparent spans stayed at 0.13 ns per pixel: their cost is setting up
23 600 spans per frame, not the tag writes. **Meaning:** a correct saving everywhere, but the
loading screen needs the span setup gone (next round).

## Round 6: opaque and transparent pixels take a shortcut

**Start.** **29.7 s, 4.9x.** Gameplay: scaled paletted sprites (PALETTED4444 NEAREST, the
ball rows and the background, 1.1 million pixels per frame) at 1.41 ns per pixel, half of
the drawing. The profile split the fast spans into decoding (583 ms) and the blend tail
(756 ms): the blend cost more than the decoding, although it was already SIMD.

**What was slow and why.** The default blend (`SRC_ALPHA / ONE_MINUS_SRC_ALPHA`) ran the
full formula on every pixel, but sprites are almost all fully opaque (alpha 255) or fully
transparent (alpha 0). For those the formula gives the source or keeps the destination,
exactly.

**Change.** `Simd::BlendSrcAlpha` checks the alphas of each group (8 pixels with NEON, 2
with SSE2, 1 in C++): all 0 skips the group, all 255 stores the source, anything else takes
the formula as before.

**Result.** Scaled paletted sprites **1.41 -> 1.00 ns** per pixel; gameplay drawing **3.18 ->
2.59 ms** per frame. Whole capture **29.7 -> 26.7 s, 4.9x -> 5.46x**. **Meaning:** decoding
(the palette lookups, ~31 % of the time) and the remaining blends (~18 %) are now the main
costs, both per pixel and close to what scalar code can do.

## Round 7: lines that did not change are not drawn again

**Start.** **25.4 s, 5.75x** (measured right before, on a quieter machine than round 6).
Splitting the capture showed the slowest parts were no longer the gameplay: the first 1300
frames (power-on, Wild Commander) ran at 2.8x and the loading screen at 3.0x, against 5.5x on
average. Both draw the same full-screen background in three passes every frame (the BILINEAR
mask and two masked RGB565 passes, Zuma's `DrawBootDxtBackground`): 5.2 and 7.2 ms per frame.

**What was slow and why.** Those frames are the same picture again and again. Counting the
frames whose inputs did not change at all since the previous one (no graphics memory write,
no new display list, no write to a register drawing reads): 79 % of the power-on frames, 29 %
of the loading screen, 18 % of the gameplay, 41 % of R-Type's play. Every one of them was
drawn completely, to produce the pixels already in the frame buffer.

**Change.** A screen line remembers the inputs it was drawn with; while none changed, it is
left in the frame buffer instead of being drawn again. The inputs of a line:

- graphics memory: a counter of changes of its contents (a write of the same value does not
  count);
- the registers drawing reads (the 14 marked "drawing", among them the mode, rotation, pin
  order, macros): a counter of value changes;
- the display list: a swap to a list with the same words does not count (the game rewrites
  and swaps the same list every frame);
- the output: another buffer, size, or drawing switched off and on (the host may clear the
  buffer then);
- the recorded walk of round 4 with the handles it starts from.

A kept line still does what drawing it does besides the pixels: the handles become what the
walk leaves, its cost stays in the line metrics, an overflowing line is counted. The line
`REG_TAG_Y` names is always drawn. Lines that passed while drawing was off, a restored state,
a portrait mode: nothing is kept. Tests: an unchanged frame keeps its lines with the same
picture; a pin order change, drawing off and on with a cleared buffer, memory and macro
changes mid-frame, a new list: picture and line costs equal with keeping on and off. Each
input check was removed once to see the tests fail.

**Result.** Drawing per frame: power-on **5.22 -> 0.31 ms** (94 % of the lines kept), loading
screen **7.2 -> 1.06 ms** (87 %), gameplay 2.59 -> 2.22 ms (18 %). Whole capture **25.4 ->
11.0 s, 5.75x -> 13.3x**; rtype-boot **6.9x -> 12.6x**. **Meaning:** static and slowly changing
screens cost almost nothing now; Zuma's gameplay (balls moving on every line of the track)
is what remains, at 2.2 ms per frame.

## Summary

| Round | Zuma, whole capture (CPU) | Real-time factor | Loading screen per frame | Gameplay drawing per frame |
|:--|--:|--:|--:|--:|
| Start | 305 s | 0.48x | 133 ms | |
| 1. invisible font | | | 31 ms | |
| 2. BILINEAR + masking blends | 53 s | 2.74x | 7.5 ms | |
| 3. rotated sprites + palettes | 38.3 s | 3.8x | 7.5 ms | 4.67 ms |
| 4. display list walked once | 30.8 s | 4.73x | 7.4 ms | 3.18 ms |
| 5. tag buffer where read | ~30 s | ~4.86x | 7.2 ms | |
| 6. opaque / transparent shortcut | 26.7 s | 5.46x | | 2.59 ms |
| 7. unchanged lines kept | 11.0 s | **13.3x** | 1.06 ms | 2.22 ms |

From 305 to 11 seconds: the same capture now needs 28 times less CPU. Picture and line
costs are unchanged in every round (the gate above). Note: the CPU figure includes
`eve-replay`'s own timing of every call (about 5 %); the library alone is a little faster.

## Next round

Zuma's gameplay is now the slowest part (2.2 ms of drawing per frame, 82 % of its lines
change every frame):

- **Decoding** (~31 % there): palette lookups have no SIMD gather on NEON / SSE2; candidates
  are magnified spans (transform A 100 and 160: one texel covers 1.6 - 2.6 pixels).
- **Span setup** (~12 %): the handle's size, stride and layout, the span's first and last
  pixel are the same on every line for a step of the recorded walk.
- **Keeping parts of lines**: a line changes where a ball moves, but the background under
  the rest of the line is drawn again too.
- **R-Type**: profile its loader and demo the same way.
- The rest of the acceleration brief: line threads.
