# 06 - "Honest" 256-subpixel antialiasing vs the distance table

Feeds: **OPTIMAL** (which antialiasing model a GPU backend should compute) and **FULL**.

## Goal

TS-Labs' second hypothesis: "on a GPU one could compute 256 sub-pixels honestly for
antialiasing, but the overhead of switching between GPU kernels would eat the gain".
Measure both halves: (1) which model is closer to the reference - 16 x 16 subsample
coverage or eve-emu's distance table - against the BT8XX golden pictures; (2) what each
costs on the GPU and on a CPU core.

## Why it matters

Points, lines and rectangles are antialiased by the chip. eve-emu's model (alpha from a
table indexed by radius and the pixel's distance to the shape's core, in 1/16 pixel, with
the line foot rounded down) reproduces the golden pictures of Bridgetek's BT8XX emulator
exactly. If honest coverage were closer to the chip, a GPU would be the natural place for
it; if it is not, the table stays and the GPU computes the table.

## Method

- Library variant `aa256` (`overlay/eve-raster.cpp`): `EVE_AA_MODEL` selects the model for
  points, lines and rectangles: `table` (eve-emu), `box256` (16 x 16 subsamples per pixel
  at (i + 0.5) / 16 around the pixel center; inside when distance to the shape core <=
  radius; alpha = inside x 255 / 256, rounded), `box256c` (grid at the pixel corner,
  i / 16), `box256le` (strict test, distance < radius). Everything else is eve-emu.
- `golden-run` replays the golden cases' scripts (eve-emu `testdata/golden/<case>`:
  register and display-list writes, then three frames) and compares the frame with the
  stored BT8XX picture: pixels that differ, the largest channel difference, the sum of
  absolute differences. Cases: the antialiasing tables' sources `aa-lines-a..d`,
  `aa-points-a..c`, and `lines`, `points`, `rects`, `probe-line`, `probe-point`,
  `probe-rect` (640 x 480 each).
- `metal-aa` times both models on the GPU (one dispatch per frame; each pixel blends every
  shape whose bounds reach it) and the same loop on one CPU core, on synthetic scenes of
  1024 x 768 with random lines (0.5-4 px wide, up to 200 px long) and points (1-8 px).

```
tools/poc/021-eve-accel/build.sh
tools/poc/021-eve-accel/06-gpu-aa-256/run.sh golden   # out/golden-<model>.txt
tools/poc/021-eve-accel/06-gpu-aa-256/run.sh gpu      # out/metal-aa.txt
```

## Results

### Accuracy against the BT8XX goldens (13 cases, 3 993 600 pixels)

| Model | Pixels that differ | Largest channel error | Sum of absolute errors |
|---|---|---|---|
| **table** (eve-emu) | **37** (all in `lines`, a case still marked TO VERIFY) | 15 | 768 |
| box256 (subsamples at (i + 0.5) / 16) | 201 164 | 255 (rects), 116 (lines) | 20 450 442 |
| box256c (subsamples at i / 16) | 202 089 | 255 | 21 223 416 |
| box256le (strict inside test) | 193 699 | 208 | 14 983 917 |

Per case (box256): `aa-lines-a` 32 245 pixels, `aa-lines-b` 32 862, `aa-lines-c` 32 622,
`aa-lines-d` 16 199, `aa-points-a` 6 229, `aa-points-b` 11 954, `aa-points-c` 14 692,
`lines` 6 317, `points` 441, `rects` 24 885, `probe-*` 749-16 286 (full tables in
`out/golden-*.txt`). The table model matches every case exactly except `lines`.

### Cost

One dispatch per frame (1024 x 768), median of 10; the CPU column is the same per-pixel
loop on one core (not eve-emu's span renderer). Load average ~165 (CPU side only).

| Scene | Model | GPU | CPU, one core | GPU equals CPU |
|---|---|---|---|---|
| 64 lines | table | 0.69 ms | 13.7 ms | yes (integer) |
| 64 lines | 256 subsamples | 1.03 ms | 108 ms | 4 pixels differ (float rounding) |
| 500 lines + 500 points | table | 4.8 ms | 385 ms | yes |
| 500 lines + 500 points | 256 subsamples | 7.4 ms | 2 016 ms | 53 differ |
| 2000 lines + 2000 points | table | 15.5 ms | 1 647 ms | yes |
| 2000 lines + 2000 points | 256 subsamples | 20.8 ms | 11 050 ms | 140 differ |

On the GPU 256 subsamples cost only 1.3-1.5x the table (the table path's 64-bit integer
arithmetic is emulated on this GPU); on a CPU core they cost 7-8x.

## Analysis

- Honest box coverage is far from the reference: about 5 % of all pixels differ, with
  errors up to the full range at rectangle edges. The chip's antialiasing is not a box
  filter over the shape: its ramp reaches 14/16 pixel beyond the radius (the table's
  reach), wider and smoother than a box's 8/16 to 11/16, and it depends only on the
  distance (rotation-symmetric), which a box over square subsamples is not. No choice of
  grid or inside test closes the gap.
- The distance table is exact against the goldens, and it is cheap on both processors: a
  distance, a square root and a lookup per pixel.
- Kernel switching does not enter: both models run in one dispatch per frame (see 04a and
  07 for what per-primitive dispatches would cost).

## Conclusions

- TS-Labs' hypothesis, first half: 256 honest subsamples per pixel are affordable on a GPU,
  but they are the wrong model - further from the chip than the table by more than three orders of
  magnitude in differing pixels. A GPU backend computes the table (bit-exact; done in the
  04 kernel for points, lines and rectangles).
- Second half: the "switching between kernels" overhead is avoided entirely by evaluating
  all primitives of a line or frame in one dispatch (04a, 07).
- Windows / Linux: the table model is integer arithmetic plus 64-bit products for the
  line foot; Vulkan needs `shaderInt64` (AMD and NVIDIA have it), D3D12 SM 6.0 `int64_t`,
  CUDA native.
