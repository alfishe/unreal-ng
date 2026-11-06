# 046 — motion-interpolation

Feature: [`docs/inprogress/2026-10-26-frame-presenter`](../../../docs/inprogress/2026-10-26-frame-presenter/README.md),
sections 4.3 (blend) and 4.4 (motion-compensated interpolation), plan phase 5.

## Goals

On a display whose refresh is not the machine's frame rate (60 or 120 Hz against 48.83-50.08 Hz), a refresh falls
between two emulated frames A and B at a fraction `w`. Today the presenter repeats A (judder: a frame for 1 or 2
refreshes at 60 Hz, 2 or 3 at 120 Hz). The questions:

1. Does a block motion search between A and B find the motion of ZX content (scrollers, sprites, whole-screen
   scrolls) well enough to draw the in-between picture at `w`, with the blend as the fallback where it does not?
2. How much better than the blend and the repeat is it, on real content, by an objective measure?
3. What does it cost per frame pair on the presenter's CPU, and does it fit the budget (section 4.4: once per pair,
   the audio delay leaves 30+ ms)?
4. Where does it fail (flicker, colour cycling, gigascreen, new content), and is the failure never worse than the
   blend?

## Requirements

- Content from `testdata/`: a text scroller (Aleste), a game with sprites and scrolling (Darkwing Duck, RZX
  playback), per-frame colour cycling (Scroller by Demarche), demo effects (Across the Edge), mostly static screens
  (Action, 7th Reality, the RZX oracle snapshots).
- Frames as the presenter gets them: the RGBA framebuffer with the border (352 x 288), captured through the WebAPI
  ([`tools/smoothmotion/capture.py`](../../smoothmotion/capture.py): `run_frame` + `capture/framebuffer?format=rgba`). No machine-specific input (the 1-bit bitmap is
  ZX-only; the presenter serves every machine).
- Variants, named the same everywhere:
  - `a-repeat`: the newest frame (today);
  - `b-blend`: `(1 - w) A + w B` (section 4.3);
  - `c-block`: per 8 x 8 block the vector within ±16 px between A and B, the warp by `w`, the blend per pixel where
    the two warped sources disagree.

## Constraints

- Plain C++ on the CPU, one thread, no SIMD (the presenter's adoption would decide GPU or SIMD).
- Ground truth: a refresh's true in-between picture does not exist. The measure is the hold-out triple: interpolate
  frames k and k+2 at `w = 0.5` and compare with frame k+1. It understates the quality for motion that is not
  linear over two frames, and overstates it nowhere.
- Raw frames and outputs in `scratch/2026-10-26-poc-046/`.

## Acceptance criteria / expected results

- `c-block` on the scroller and the game: mean absolute error at most half of `b-blend`'s, and more pixels exactly
  right than both `a-repeat` and `b-blend`.
- On no sequence is `c-block` worse than `b-blend` by more than 5 % (the fallback holds).
- Cost: under 2 ms per frame pair at 352 x 288 (scalar, Apple M-series), so a presenter thread can afford it at
  50 Hz without SIMD.
- Expected failures: the colour-cycled dots of Demarche (no motion, a palette change) and full-screen effects fall
  back to the blend.
- The owner looks at side-by-side 60 Hz animations of the three variants (GIF) before the presenters get the mode.

## Files

| File | What it is |
|---|---|
| `interp.cpp` | the POC's interpolator and its measurements (`c++ -std=c++20 -O3`, standalone) |
| `smoothmotion-eval.cpp` | the same measurements on the production CPU reference (`core/src/emulator/video/smoothmotion.h`); built by `tools/verification/smoothmotion/compile-tool.sh` (the command is at the top of the file) |
| [`measurements-2026-10-26.md`](measurements-2026-10-26.md), [`conclusions.md`](conclusions.md) | the results and the proposal |

Frames: [`tools/smoothmotion/capture.py`](../../smoothmotion/capture.py). Superseded by
[POC 047](../047-smoothing-filters/README.md) (the filters, `layers`).
