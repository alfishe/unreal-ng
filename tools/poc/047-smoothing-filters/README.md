# 047 — smoothing-filters

Feature: [`docs/inprogress/2026-10-26-frame-presenter`](../../../docs/inprogress/2026-10-26-frame-presenter/README.md),
sections 4.3-4.5 (smooth motion), plan phase 5. Follows [POC 046](../046-motion-interpolation/README.md).

## Goals

The owner watched `interpolate` live on Aleste (60 Hz fixed display) and saw judder. Neither display here runs at
50 Hz (Dell U4320Q: 30 / 60 Hz, U3011: 60 Hz), so a frame rate conversion is unavoidable. The questions:

1. Which of the known 50 -> 60 Hz conversions looks best on ZX content to the owner's eye: the repeat, the blends
   (linear, in linear light, narrowed to the middle of the step), a shutter (box) integration, motion-compensated
   interpolation (per 8x8 block as shipped, one global vector, one vector per 8-row strip), extrapolation without
   lookahead, and the game cadence modes.
2. Is the judder seen live in the filter's pictures (then it is in these videos too) or in their presentation (late
   presents under load: then the videos are smooth and the live window is not)?

## Requirements

- The frames of POC 046 (`scratch/2026-10-26-poc-046/frames/*.rgba`, 352 x 288 RGBA, 200 consecutive frames each,
  captured through the WebAPI).
- The shipped variants (`off`, `blend`, `interpolate`, `cadence`, `carry`) through the CPU reference
  `core/src/emulator/video/smoothmotion.cpp` itself, so the videos show the bits the window shows; the others as
  experiments in `filters.cpp`, integer arithmetic.
- A refresh at 60 Hz shows the picture for the time it reaches the screen; frame k is due at k / 50 s. No presenter
  timing: every picture is exactly on its refresh.
- Output per clip: one 60 fps video per filter (3x nearest) and a labelled 3 x 3 grid of the filters side by side
  (2x nearest), H.264, in `scratch/2026-10-28-poc-047/`.

## Constraints

- Offline, one thread, plain C++20; the cost of a filter is noted, not optimised.
- The 60 fps videos play 1:1 only on a 60 Hz display (both here are).

## Acceptance criteria / expected results

- The owner picks the filters worth a product mode from the grids; the rest are dropped.
- Expected: the blends ghost on the 8 px steps of Aleste's dot scroller; `interpolate` and `pan-strips` move it in
  6.7 px steps; games with a 2-5 frame cadence gain only from `cadence` / `carry`.
- If `interpolate` is smooth here and judders live, the live fault is in the presentation (section 6), not the filter.

## Measurement

[tools/verification/smoothmotion](../../verification/smoothmotion/README.md): position error, jerk, ghost and tear per
moving region against the ideal trajectory, on the synthetic scenes of `testdata/video/smoothmotion` (known motion)
and on any capture. The filters harness (`filters.cpp`) and the generic scenes (`scenes.py`) began here and live
there now, with the product verification; this folder keeps the runners:

| File | What it is |
|---|---|
| `run.sh` | the clips through every filter, the videos and grids (builds `filters.cpp` with `compile-tool.sh`) |
| `suite.py` | the generic suite: `scenes.py` through a filters binary, scored per element |
| `index.py` | an `index.html` over the rendered clips: grids looping, the single filters as links |

Frames: [`tools/smoothmotion/capture.py`](../../smoothmotion/capture.py) (snapshots and `.rzx`).

## Results (2026-10-28)

`jstep / ghost / tear % (pic %)` from `smoothness.py`, 300 frames per clip: Aleste at 48.83 Hz, the synthetic
scenes and five game clips at 50 Hz, all shown at 60 Hz. `floor` is the jerk of the best whole-pixel picture.

| clip | interp-rows | interp-solid | pan-solid | solid-slack40 | floor |
|---|---|---|---|---|---|
| aleste | 0.60/1.16/3.2 (6.7) | 0.61/1.06/2.9 (6.5) | 0.56/0.85/1.8 (6.8) | **0.55/0.67/1.0 (4.5)** | 0.50 |
| dots | 0.47/1.01/2.5 (3.8) | 0.47/0.99/2.6 (3.9) | 0.49/0.83/1.7 (11.9) | **0.47/0.47/0.8 (2.2)** | 0.47 |
| uridiumplus | 1.49/0.52/6.6 (136.7) | 1.49/0.52/6.5 (136.7) | 1.53/0.40/6.2 (136.2) | **1.52/0.17/1.9 (133.6)** | 0.38 |
| flyingshark | 0.42/0.06/0.8 (2.0) | 0.42/0.06/0.8 (2.1) | 0.43/0.06/1.6 (3.8) | **0.42/0.00/0.0 (1.4)** | 0.42 |

`pan-solid` damages static pixels (labels of the synthetic scenes); the others do not.

- `interp-solid`: the shipped field with the row's vector where it fits a block as well (`RowField`), composed by
  `ComposeSolid`: a moving block never takes zero, a pixel no vector explains is drawn whole from the nearer frame.
  Still tears Aleste's diagonal strokes: the blocks there keep a wrong vector that explains them exactly (black
  between the dots, or the dot pitch).
- `solid-slack<N>`: the row's vector also wins when it mismatches up to N more of the block's 64 pixels, except over
  a block that does not change with 8 px around it (a static label keeps zero). Better with N up to 40; 64 (any
  changing block takes the row's vector) measures the same, so 40 keeps the per-block choice for objects moving
  against the row. Whole dots and unbroken strokes on Aleste.
- Left: the cadence scene (2N px every N frames) stays at jstep 1.7 against a floor of 0.43 with every
  interpolation, and the game clips stay well above their floor: the motion there is a cadence, not a per-pair
  vector, and needs the per-object cadence of the game modes.

## Generic suite (2026-10-30)

The filters above were tuned on Aleste and the game clips; the owner rejected that: one algorithm has to carry every
kind of motion, so the target became a synthetic suite with ground truth, and the titles and games stay hold-outs.

`scenes.py` draws 12 scenes of known motion on the Spectrum screen (352 x 288 with the border, 300 frames at 50 Hz);
`suite.py` runs a filters binary over them at 60 Hz and scores every element:

| scene | elements |
|---|---|
| strips | dither -1, -2; stripes4 -4; text -8, -2; tiles16 +3; noise -5; terrain -3 |
| vertical | dither v+1; text v-8; tiles v+4; noise v-3 |
| diagonal | terrain (2,1); dither (-3,-2); a static panel with text |
| overlay | tiles -4 under still sprites, still text and a panel |
| textlines | scrollers -8 and -1, dot rows -8 and -4 between static lines |
| parallax | stars -1, tiles -2, terrain -4; ships (+1,+1), (-5,+2), (+3,0) |
| sprites | five sprites up to (+5,+3) and -8 over a static background |
| cadence | text -8 and terrain -2 every 2nd frame, tiles +4 every 3rd, noise speeding up, stop-start text and sprite |
| dotmatrix | dot-matrix text -8, -4, -6, v-4 |
| thin | 1 px box edges, rules and a dotted line moving by 1-3 px, a bouncing text |
| flux | content changing in place: stretching and colour-cycled text, a blinking sprite, a marching frame |
| animated | content changing shape as it moves: spinning dots -8 and -4 (Aleste's title), water -2, a spinning sprite |

The score of an element is its share of wrong pixels over all refreshes. A moving layer is right in one place for the
whole element: the truth at the refresh's time rounded either way on each axis, or a look-alike velocity when the
tiled bitmap hides the motion, and for an animated layer frame k's shape or k + 1's. A flux element is scored per
8 x 8 cell, and each cell must be one source frame whole. The sum is over the first 10 scenes; thin and animated
came later.

```
python3 tools/poc/047-smoothing-filters/suite.py <filters binary> off,blend,interpolate,mrft8m12s12Lzt4ls6b,layers <work dir>
```

### `layers`

`struct LayerFlow`, built here in `filters.cpp`, shipped as `core/src/emulator/video/smoothlayers.{h,cpp}` (the
harness calls the core's now). Candidate vectors come from the shipped block estimator: the common ones (several
blocks share them) plus those of the 3 x 3 blocks round each block. Every rule below is explained in a
comment where its code lives.

- **Per pixel**: a vector fits a pixel when a(x - s) and b(x - s + v) agree with what the pair shows (s is the
  shift at time w). Its support is the window count (16 px) of the changes it explains. A moving vector is usable
  only with support beyond the changes no vector explains there (`credible`: flux takes no motion). Zero fits only
  a pixel that is the same in k - 1, k and k + 1, or one where nothing changes round it (`still3`, `stopped`).
- **Per 8 x 8 block** (`coherent`): the block takes the vector most of its pixels fit. Look-alikes that fit as many
  pixels tie (`fitTies`). A faster look-alike beats a slower one only with changes of its own (`aliases`).
- **Tied regions** (`spread`, `majority`): 4-connected blocks with the same tie set take what most decided blocks
  round them say. A neighbour votes only where its layer continues into the region: the colours of its edge strip
  are the block's (`colours`, `own`) and its own vector carries its edge content (`vote`).
- **Holes** (`cover`): a still pixel a moving layer passes over takes the layer's content when that layer covers
  this colour round here more than it goes under it, and the sample moved the same way from k - 1 (`steady`).
- **Shape changing on the move** (`first`): in a decided moving block whose vector explains at least half of its
  changes, a pixel the vector does not fit takes the nearer frame shifted by the block's vector. A pixel that
  another vector explains better, a slower layer showing through, or a blink in place keeps its own path.

### Results

Wrong share per scene (mean over the elements), `scratch/suite/run52.txt`:

| scene | off | blend | interpolate | ls6b | **layers** |
|---|---|---|---|---|---|
| strips | 25.90 | 45.27 | 2.10 | 5.52 | **0.92** |
| vertical | 44.85 | 59.09 | 17.20 | 12.49 | **1.65** |
| diagonal | 0.86 | 3.15 | 0.13 | 4.75 | **0.13** |
| overlay | 9.97 | 10.00 | 9.95 | 8.52 | **1.22** |
| textlines | 19.83 | 38.06 | 10.47 | 1.80 | **0.55** |
| parallax | 17.14 | 75.30 | 6.51 | 7.11 | **4.14** |
| sprites | 20.35 | 53.28 | 15.78 | **6.95** | 7.32 |
| cadence | 26.67 | 46.30 | 5.59 | 7.20 | **3.23** |
| dotmatrix | 43.16 | 69.35 | 16.76 | 12.79 | **3.38** |
| flux | 20.37 | 77.72 | 33.95 | 12.18 | **1.96** |
| sum of 10 | 229.10 | 477.52 | 118.44 | 79.31 | **24.50** |
| thin | 8.73 | 61.24 | 5.93 | 5.54 | **1.54** |
| animated | 40.96 | 80.72 | 44.62 | **16.23** | 23.64 |

`ls6b` is `mrft8m12s12Lzt4ls6b`, the best filter tuned on the clips. The hold-outs, `jstep / ghost / tear %`
(`scratch/smoothcap/eval-layers14.txt` and `eval-layers1.txt`):

| clip | ls6b | layers |
|---|---|---|
| aleste | 0.59/0.70/1.1 | 0.72/1.68/5.7 |
| dots | 0.47/0.51/0.9 | 0.49/1.08/3.7 |
| mixed | 0.40/0.07/0.4 | 0.41/0.11/1.7 |
| uridiumplus | 1.51/0.54/8.4 | 1.49/0.61/9.6 |
| scroll, speeds, cadence, zynaps, cobra, hysteria, flyingshark | about equal | about equal |

### Known limits

- **Animated content on the move** (animated 23.6 %, ls6b 16.2 %): spinning dots -8 (27.9 %), the spinning sprite
  (22.0 %), water (38.1 %, ls6b 0.1 %). Aleste's gap to ls6b sits here: its title is spinning dots moving 8 px a frame. A
  fewest-mismatch candidate per block (`loose`, tried) brings the scene to 17 %, but it breaks periodic tiles, and
  the sum went from 24.50 to 24.78.
- **Periodic content ambiguous over two frames**: tiles v+4 with an 8 px period fit +4 and -4 equally. The top block
  row fits only -4 and the bottom one only +4, so the vote of a tied region can split a layer. This needs the
  choice of k - 1 (temporal coherence), not one more rule.
- **Fast and accelerating sprites**: sprites +5,+3 (14.3 %), -8 (10.8 %), 0,-4 (10.7 %); noise speeding up
  (8.9 %); dot-matrix text v-4 (7.9 %); dotted v+2 (10.6 %); stretch text (6.4 %).

Rejected rules, measured on the suite: depth, whole (small objects move whole), reach (any vector in any block),
carry (any unfitted pixel of a moving block), loose. The binary keeps only the accepted `layers`.
