# 046 — measurements 2026-10-26

Setup: frames captured with `capture.py` from unreal-qt (offscreen, Pentagon, own ports): 200 consecutive frames per
title after a warmup, the presented RGBA framebuffer 352 x 288. Interpolator `interp.cpp` built with
`c++ -std=c++20 -O3`, Mac Studio (M-series), run while the machine had a load of 40-60 (other sessions' builds), so
the tail of the times is preemption. Raw frames, `eval.txt` and the 60 Hz videos: `scratch/2026-10-26-poc-038/`.

Quality: hold-out triples (frames k and k+2 at `w = 0.5` against frame k+1), only triples with motion on both steps,
only pixels that change across the triple. MAE = mean absolute error per channel (0-255); exact = pixels exactly
right. `c-block` searches ±16 px here (the two-frame motion). Cost: consecutive pairs that differ, ±8 px, at most 96
full searches per pair (the real use).

| Title | Triples | `a-repeat` MAE / exact | `b-blend` MAE / exact | `c-block` MAE / exact | `c` worse than `b` (triples) | `c` ms p50 / p95 |
|---|---|---|---|---|---|---|
| Scroller by Demarche (dots, colour cycling) | 198 | 97.5 / 31.7 % | 97.8 / 0.0 % | 51.7 / 63.4 % | 0 | 1.57 / 5.86 |
| 7th Reality (moving picture) | 29 | 64.5 / 35.0 % | 66.2 / 0.0 % | 10.3 / 90.2 % | 0 | 1.33 / 52.95 |
| Action (demo) | 198 | 165.3 / 35.2 % | 165.6 / 0.0 % | 105.6 / 53.5 % | 0 | 1.47 / 5.84 |
| Aleste (text scroller) | 198 | 135.1 / 39.5 % | 135.0 / 0.0 % | 93.8 / 52.4 % | 0 | 1.67 / 5.18 |
| Darkwing Duck (RZX, scrolls every 5th frame) | 34 | 120.5 / 2.8 % | 61.9 / 0.0 % | 64.0 / 45.8 % | 7 | 2.29 / 4.55 |
| Across the Edge (full-screen effects) | 196 | 87.6 / 10.5 % | 86.1 / 0.0 % | 79.9 / 13.7 % | 2 | 1.36 / 6.15 |
| Garfield (snapshot, idle) | 12 | 166.8 / 34.5 % | 125.6 / 0.0 % | 126.1 / 42.0 % | 0 | 1.31 / 5.00 |
| Thundercats (snapshot, idle) | 8 | 75.2 / 59.8 % | 93.5 / 0.0 % | 102.0 / 44.3 % | 3 | 1.68 / 4.40 |

Observations:

- Most games draw every 2nd-5th frame (Dizzy, Green Beret, Thundercats: 25-50 distinct pictures of 200; Darkwing
  Duck scrolls the screen once every 5 frames and touches one status row in between). Between repeated pictures
  there is nothing to interpolate; their cadence is the game's own.
- Raster-raced redraws are in the source frames (Aleste's "PRESS KEY" line is torn in some frames: "PRF55 KFY");
  the interpolation reproduces them, it does not add them.
- Version 1 took the first agreeing vector per pixel and drew a coincidental match (a static letter under the
  neighbouring block's vector). The 3x3 agreement choice fixed it at +0.3 ms.
- Without the search budget and with ±16 px on consecutive pairs the cost was 3-8 ms p50 with 100+ ms peaks on
  full-screen effects.

## Stage 2: the game's own pictures (cadence)

Owner (2026-10-26): no added latency; measure the game's picture period and smooth by it. `interp cadence-eval`:
a burst of changes on consecutive frames is one picture (Darkwing Duck redraws the play area, then the status row a
frame later); the period is the median distance between pictures. Variants on pictures, not frames:

- interpolation (`c-block` between pictures i-1 and i+1 at the middle, against picture i): what `e-cadence` shows
  when the next picture is already in the lookahead;
- `d-carry`: the motion of pictures i-1 -> i carried one picture on (w = 2), against picture i+1: the error left is
  the jump seen when the real picture arrives; `repeat` is today's (picture i until i+1). Where nothing agrees the
  output is picture i; extrapolating, a moving vector draws only pixels whose source changed (the static border was
  pulled into the scrolling window before).

| Title | Period | Pictures | interp `b-blend` | interp `c-block` | `repeat` | `d-carry` | carry ms p50 |
|---|---|---|---|---|---|---|---|
| Darkwing Duck (RZX gameplay) | 5 | 37 | 78.3 / 0 % | 22.5 / 80.9 % | 78.5 / 36.5 % | 19.5 / 84.9 % | 3.11 |
| Thundercats | 4 | 17 | 113.9 / 0 % | 69.6 / 57.0 % | 109.0 / 41.8 % | 80.1 / 57.3 % | 1.29 |
| Garfield | 3 | 53 | 159.9 / 0 % | 109.7 / 48.8 % | 158.0 / 37.1 % | 118.1 / 52.8 % | 1.34 |
| Green Beret | 3 | 48 | 100.4 / 0 % | 90.4 / 20.3 % | 102.3 / 39.1 % | 101.5 / 39.5 % | 1.56 |
| Dizzy X | 4 | 49 | 193.4 / 0 % | 193.4 / 0 % | 193.4 / 0 % | 157.3 / 15.7 % | 1.21 |

(MAE / exact over the pixels changing across the three pictures.) Dizzy and Green Beret cycle animation frames in
place (no translation): interpolation has nothing to find. `interp cadence-render`: 60 Hz videos, four panels
`a-repeat | c-block consecutive frames | e-cadence (lookahead 2 frames) | f-carry`:
`scratch/2026-10-26-poc-038/*-cadence-60hz.mp4`.
