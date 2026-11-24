# 055 — measurements 2026-11-22: cost and quality

## Setup

- Reference: the core at `9a6bb6834` (`LayerFlow` over `MotionField::Estimate`).
- POC tool: [cpp/lite.cpp](cpp/lite.cpp), built by `tools/verification/smoothmotion/compile-tool.sh` (g++ 13 `-O3` in
  the CI image for Linux, clang `-O2` on the Mac), one thread.
- Traces: the screen window's own frames (`UNREAL_SMOOTH_TRACE`, 352x288) taken on the Steam Deck in Game Mode:
  Across the Edge (worst: the cap of 32 vectors), `action.sna`, Aleste, the synthetic `smooth-scroll.sna`.
- Quality: POC 047's `suite.py` (12 synthetic scenes with their truth, 50 Hz shown at 60 Hz): per element the share
  of wrong pixels over all refreshes, a scene its elements' mean. Lower is better.
- Machines: the Steam Deck (Zen 2, quiet) for the reference's cost; the Linux host .238 (i7-11850H, shared: load
  16-36 during these runs) for the POC variants, as the Deck dropped off the network before they were built. The
  two hosts give the reference the same time on one quiet thread (64 and 62 ms a picture of the worst trace), so the
  loaded host's numbers are read as ratios to the reference measured in the same run.

## 1. The reference, one Deck thread, by trace

| Trace | Vectors a picture | A picture p50 / p95 | Push (a frame) p50 |
|-------|-------------------|---------------------|--------------------|
| `smooth-scroll` | 6.4 | 8.9 / 10.3 ms | 0.24 ms |
| Aleste | 13.9 | 17.6 / 25.4 ms | 0.24 ms |
| `action` | 32.0 | 22.3 / 23.6 ms | 0.22 ms |
| Across the Edge | 31.8 | 29.8 / 42.6 ms | 4.7 ms |

`Compose`'s stages on the worst trace (ms a picture, one thread): the per-vector pass 12.0 (the pixel loop 5.8, the
score window 2.8, the support window 2.9), the usable pass 1.4, block counts 0.8, the block decisions 3.2, the
pixels' pass 3.1, the rest 1.5; the field 7. The simplest trace still pays 3.0 for the pixels' pass and 0.9 for the
decisions with 6 vectors.

## 2. `b-bitplanes`: the reference's field on bit planes

`BitField` ([cpp/bitfield.h](cpp/bitfield.h)) gives the same field as `MotionField::Estimate`, block for block:

| Clip | Blocks that differ | The reference | On planes | Vectors scored a pair (of 289) |
|------|--------------------|---------------|-----------|--------------------------------|
| `strips` (Mac) | 0 of 473,616 | 3.65 ms | 6.44 ms | 117 |
| `vertical` (Mac) | 0 of 473,616 | 5.33 ms | 6.07 ms | 135 |
| `diagonal` (Mac) | 0 of 473,616 | 1.64 ms | 5.25 ms | 114 |
| `overlay` (Mac) | 0 of 473,616 | 3.79 ms | 6.03 ms | 161 |
| Across the Edge (.238) | 0 of 392,832 | 13.82 ms | 11.63 ms | 278 |

Packing a frame into planes: 0.6-2.2 ms as written (a pixel at a time; not looked at yet).

## 3. `c-lite`: cost, one thread

.238, the worst trace, each run with the reference beside it:

| Filter | A refresh | Of the reference |
|--------|-----------|------------------|
| `layers` (the reference) | 56.5-58.4 ms | 1 |
| `lite` (the exact field on planes + the mask composition) | 10.6 ms | 1 / 5.4 |
| `lite2` (the field kept from pair to pair) | 1.28-1.38 ms | 1 / 43 |
| `lite6` (`lite2` + the own-colour fit + the carry) | 1.63-1.67 ms | 1 / 35 |

`lite2`: the field 0.83-0.88 ms a refresh (673 row scores a pair, 18.7 pictures' worth of the 289 a full search is),
the composition 0.46-0.50 ms. By the reference's ratio between the hosts (29.8 ms on a quiet Deck thread against
57 ms here) `lite6` is about 0.85 ms a refresh on a quiet Deck thread; to be measured there.

## 4. Quality: the suite

Mean share of wrong pixels over a scene's elements, %:

| Scene | off | blend | layers | lite (exact field) | lite2 | lite6 |
|-------|-----|-------|--------|--------------------|-------|-------|
| strips | 25.90 | 70.93 | 1.07 | 2.24 | 2.69 | 2.63 |
| vertical | 44.85 | 119.19 | 2.04 | 6.22 | 6.78 | 6.66 |
| diagonal | 0.86 | 27.97 | 0.48 | 3.99 | 4.34 | 4.33 |
| overlay | 9.97 | 16.60 | 1.04 | 12.62 | 12.05 | 0.89 |
| textlines | 19.83 | 53.48 | 0.58 | 0.92 | 1.30 | 1.24 |
| parallax | 17.14 | 75.31 | 4.08 | 4.15 | 5.18 | 5.06 |
| sprites | 20.35 | 53.32 | 7.36 | 8.93 | 9.22 | 8.84 |
| cadence | 26.67 | 46.63 | 3.33 | 3.45 | 6.46 | 6.20 |
| dotmatrix | 43.16 | 71.89 | 4.77 | 8.74 | 9.10 | 9.06 |
| thin | 8.73 | 74.71 | 1.43 | 2.93 | 9.05 | 8.41 |
| flux | 20.37 | 103.39 | 3.02 | 23.01 | 23.09 | 16.09 |
| animated | 40.96 | 83.36 | 14.58 | 21.32 | 22.41 | 21.82 |
| **sum of 12** | **278.79** | **796.78** | **43.78** | **98.52** | **111.67** | **91.23** |

Two forms tried and dropped, on the first four scenes:

| Form | strips | vertical | diagonal | overlay |
|------|--------|----------|----------|---------|
| `lite3`: a pixel the same in k - 1, k, k + 1 stays, before any vector | 12.56 | 26.04 | 4.83 | 4.65 |
| `lite4`: `lite3` + the carry | 12.54 | 25.96 | 4.83 | 4.67 |

Raw outputs: `scratch/2026-11-22-poc-055/` (`suite/`, `s2/` .. `s6/`), not kept in the repository.
