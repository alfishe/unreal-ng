# VDAC2: FT812 drawing optimization walkthrough

**Created:** 2026-10-03. **Status:** rounds 0-3 done (eve-emu `5f47ded`, vendored); next round open.

How the FT812 emulation (the eve-emu library) went from drawing Zuma Deluxe at half the
speed of the real card to almost four times faster than it. Each round records what we
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

## Summary

| Round | Zuma, whole capture (CPU) | Real-time factor | Loading screen per frame |
|:--|--:|--:|--:|
| Start | 305 s | 0.48x | 133 ms |
| 1. invisible font | | | 31 ms |
| 2. BILINEAR + masking blends | 53 s | 2.74x | 7.5 ms |
| 3. rotated sprites + palettes | 38.3 s | 3.8x | 7.5 ms |

## Next round: the display list walk

Gameplay profile after round 3: 4.67 ms of drawing per frame; **40 % outside the bitmaps**:
walking the display list (768 commands per line, every line), clearing and line output.
Commands per drawn line by opcode: `CELL` 102, `BITMAP_SIZE` 50, `BITMAP_SOURCE` 50,
`BITMAP_HANDLE` 18, `BITMAP_LAYOUT(_H)` 17 + 17, `COLOR_RGB` 17, `BITMAP_SIZE_H` 13,
`BEGIN` 12, `BITMAP_TRANSFORM_C..F` 11 each, plus the vertices. Most of these set state that
is the same on every line; the candidates are a cheaper command dispatch and skipping
primitives whose vertical extent misses the line. The target in the acceleration brief is
4x for Zuma on one core.
