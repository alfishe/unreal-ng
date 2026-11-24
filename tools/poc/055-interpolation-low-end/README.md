# 055 — interpolation-low-end

How cheap can smooth motion's interpolation be made, so that a machine with no GPU to speak of and a fixed 60 Hz
display can use it? Feature: [presenter stabilization](../../../docs/inprogress/2026-11-22-presenter-stabilization/README.md)
(section 1.7), [frame presenter](../../../docs/inprogress/2026-10-26-frame-presenter/README.md) (sections 4.4, 6.5).

Today the picture between two frames is `LayerFlow` (`core/src/emulator/video/smoothlayers.cpp`) over the block field
of `MotionField::Estimate`. On the Steam Deck's CPU a picture of the worst content takes 29.8 ms on one thread and
14.2 ms on four (after the 2026-11-22 pass that halved it), against the ~8 ms a picture has before its refresh. The
window blends instead there (`present.smooth.too_slow`).

## Goals

1. Where one thread's time goes, stage by stage, on worst and on usual content: which stages are tests of pixels
   being equal (candidates for 64 pixels a word), which are counts over windows, which are decisions per block.
2. `b-bitplanes`: a frame as bit planes of its colours' indices (a ZX screen has 16 colours, four planes). How fast
   are the reference's own steps on them, bit for bit the same answers: the field's block compares, the per-vector
   fit / change maps?
3. What stays after that (window counts, block decisions, the pixels' pass), and what a cheaper form of each costs.
4. `c-lite`: if the reference's bits cannot be had in the budget, what does a cheaper interpolator of its own
   (fewer vectors, coarser decisions) score on the smoothness suite against `layers` and `blend`?
5. The answer: on a low-end CPU, does a picture fit a 60 Hz refresh, with which variant, and at what loss.

## Requirements

- No GPU: the CPU path alone, one thread and two (a low-end machine has two cores).
- Workloads: the worst content (the Across the Edge trace of the stabilization work: 31.8 vectors a picture, the
  cap), a game (few vectors, a static border and HUD), a still screen. Traces of the window's own frames
  (`UNREAL_SMOOTH_TRACE`), 352x288.
- x86-64 at its baseline (SSE2, no popcnt assumed) and ARM64; gcc and clang. No intrinsics in a first pass:
  plain 64-bit words.
- A variant that claims the reference's bits proves it: the same sum over every picture of a trace.
- A variant that does not is scored: `tools/verification/smoothmotion` (the truth suite's `sum`, lower is better).

## Constraints

- The low-end machine is a model, none is at hand: one thread of the Steam Deck (Zen 2, 2.8-3.5 GHz) times 3 (a
  Core 2 class core). Budget: a picture in 5 ms there, so **1.7 ms on one Deck thread** (p95), the field of the
  pair included; `Push` (the flicker search, once a frame) in another 1.7 ms.
- The reference (`core/`) is not changed by this POC; its code is not linked into `core/` or the apps.
- Planes need few colours: a frame with more than 256 is the reference's (true-colour modes are not a low-end
  machine's work).
- Timings on the Deck in Game Mode with nothing else running; each number the p50 / p95 of two rounds.

## Acceptance criteria / expected results

| Outcome | Criterion |
|---------|-----------|
| go, exact | a variant with the reference's sum at 1.7 ms p95 a picture on one Deck thread, worst content |
| go, lite | no exact variant in the budget, and `c-lite` within it with a suite `sum` under `blend`'s and no scene worse than `blend` by more than 10 % |
| no-go | neither: the report says what a low-end machine gets (blend) and what the cheapest exact picture costs |

Expected before measuring (to be checked, not trusted):

- The field (about 7 ms of the 29.8) and the per-vector pixel loop (6 ms) are pixel equality tests: on four planes a
  word carries 64 pixels, so 10x or more each.
- The window counts (5.8 ms) and the block decisions with the pixels' pass (6.3 ms) are not: they bound an exact
  variant near 12 ms unless their form changes. So `go, exact` is not expected; the question is how far `c-lite`
  must go.

## Variants

| Name | What |
|------|------|
| `a-reference` | the core as it is (after `9a6bb6834` of 2026-11-22: rows through pointers, the pixel loop in chunks) |
| `b-bitplanes` | the reference's steps on colour-index bit planes, the same bits |
| `c-lite` | a cheaper interpolator, scored: the block field kept from pair to pair ([cpp/litefield.h](cpp/litefield.h)), a pixel's layer picked by masks of whole words ([cpp/lite.cpp](cpp/lite.cpp); `lite19` the form proposed: `lite6` of 2026-11-22 + the 2026-11-23 rules, half the search budget) |

Code: [cpp/](cpp/), built with `tools/verification/smoothmotion/compile-tool.sh` (the core's sources beside it).
[score.sh](score.sh): the suite's 12 scenes for some filters at once. [agree.py](agree.py): how far a filter's
pictures are from the reference's on a game's trace. Raw data: `scratch/2026-11-22-poc-055/`.

## Results

Measurements [2026-11-22](measurements-2026-11-22-cost-and-quality.md) (cost, the first forms) and
[2026-11-23](measurements-2026-11-23-quality-rules.md) (quality rules, the Steam Deck, the cost pass, ARM64),
[analysis](analysis.md), [conclusions](conclusions.md), [proposal](proposal.md). In short: no exact variant fits (the
reference's windows and decisions are 12 ms a thread); `c-lite` (`lite19`) draws a picture in 0.42-0.81 ms on a quiet
Deck thread by trace, 1 / 20 to 1 / 45 of the reference's time, at 1.3 times its error on the suite (58.7 against
43.8; `off` 278.8); the same bits on x86-64 and ARM64.
