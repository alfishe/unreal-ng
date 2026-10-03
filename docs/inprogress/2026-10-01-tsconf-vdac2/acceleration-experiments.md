# VDAC2: accelerating the FT812 renderer - measured results and the BILINEAR task

**Date:** 2026-10-02. Experiments: [`tools/poc/021-eve-accel/`](../../../tools/poc/021-eve-accel/README.md)
(one README per experiment, the scripts to repeat every number below).

**How to read the numbers.** Host: Apple M1 Ultra (20 CPU cores, 64-core GPU). The machine
was shared with other builds the whole time (1-minute load 37-195), so **wall times
scatter 2-4x**; **CPU times repeat within 2 %** and are the figures to compare.
Real-time factor = chip time / CPU time of the replay: 1.0x means one host core just keeps
up with the FT812; 4x means a quarter of a core. Every accelerated path was checked bit for
bit against the unmodified library (every chip answer byte and every picture hash).

## 1. The numbers

### 1.1 Baseline: one core, unmodified eve-emu (experiment 01)

| Capture (chip time) | NEON, CPU s | real-time factor | plain C++, CPU s | real-time factor |
|---|---|---|---|---|
| rtype-boot (17.8 s) | 3.70 | **4.81x** | 4.31 | 4.13x |
| rtype-play, first 4000 frames (68.0 s) | 19.4 | **3.51x** | 31.7 | 2.15x |
| zuma-flick (14.5 s) | 11.96 | **1.21x** | 11.97 | 1.21x |

Where the time goes (`sample`, top-of-stack samples, 10 s):

| rtype-play | samples | zuma-flick | samples |
|---|---|---|---|
| `DrawBitmap` (span setup, blend kernels) | 517 | **`WrappedTexel` (general bitmap path)** | **4 238** |
| `ShadeSpan` (masking blends) | 413 | `DrawBitmap` | 1 851 |
| palette decode loop (`DecodeSpan`) | 274 | **`Shade` (per-pixel pipeline)** | **1 076** |
| replay harness | 281 | **`Direct` (texel decode)** | **408** |
| `OutputLine` | 97 | `ShadeSpan` | 369 |
| `ExecuteLine` (display list) | 41 | `ExecuteLine` | 179 |

**Zuma: 85 % of the samples are in the general per-pixel path** (`WrappedTexel`, `Shade`,
`Direct`). Its frames draw full-screen `L4` and `RGB565` bitmaps with **BILINEAR** filtering
and scaling; eve-emu's fast path covers only NEAREST. One such frame costs **28 ms** on one
core (1.7 FT812 frame periods). NEON gives 1.6x on R-Type play and nothing on Zuma: Zuma
never reaches the SIMD kernels.

### 1.2 What changes during a frame (experiment 02)

| | rtype-boot (1032 frames) | rtype-play (4000) | zuma-flick (830) |
|---|---|---|---|
| frames drawn in one batch | 95.1 % | 69.6 % | 90.6 % |
| ... if writes outside what the list reads did not split | **100 %** | **80.3 %** | **100 %** |
| lines in batches of 512+ lines | 94.7 % | 80.7 % | 89.2 % |
| lines in batches of 1-7 lines | 2.6 % | 4.8 % | 8.2 % |
| `DLSWAP_LINE` (display list swapped per line) | **0** | **0** | **0** |
| `DLSWAP_FRAME` | 616 | 1 404 | 2 592 |
| handle table changed after a batch's first line | 0 | 0 | 0 |

Splits come from `RAM_G` writes (host 39 116 batches, coprocessor inflate while loading
94 655, of 137 564 on rtype-play), not from the display list.

### 1.3 Lines in parallel on the CPU (experiment 03)

Correctness: exact on rtype-boot, zuma-flick and the whole rtype-play capture (24 193
frames). Lines left serial: 2.78 % / 8.33 % without deferring writes, **0.13 % / 0.14 % /
0.95 %** with it (boot / zuma / play). Handle table divergences: 0.

| Best of two runs | 1 thread | 2 | 4 | 8 | 16 | 8 + deferral |
|---|---|---|---|---|---|---|
| rtype-boot wall (s) | 3.35 | 1.84 | 1.03 | 0.65 | 0.55 | 0.66 |
| rtype-boot CPU (s) | 3.34 | 3.52 | 3.61 | 3.92 | 5.05 | 3.95 |
| rtype-play 4000 wall (s) | >= 18.5 | 10.1 | 15.7 | 14.8 | 16.8 | 5.15 |
| zuma-flick wall (s) | >= 11.3 | 5.7 | **3.0** | 22.1 | 11.1 | 5.7 |

Boot: **5.1x at 8 threads, 6.1x at 16**. Play and Zuma rows are dominated by preemption;
their best runs give lower bounds of 3.6x and 3.7x. CPU time grows 5-20 % with 8 threads.

### 1.4 GPU (Metal) (experiments 04, 04a, 04b, 04c)

Kernel correctness: 125 of 125 test frames bit-exact (C++ and Metal from one integer core);
3 frames not expressible (`REG_CSPREAD` at start-up).

One dispatch per frame, median per frame (load ~180, CPU side preempted):

| | rtype-play | rtype-boot | zuma-flick |
|---|---|---|---|
| **GPU time** | **0.49 ms** | 0.26 ms | 0.12 ms |
| upload (1 MB RAM_G + op list) | 0.089 ms | 0.076 ms | 0.082 ms |
| readback (3 MB) | 0.17 ms | 0.14 ms | 0.20 ms |
| submit-to-done (wall) | 4.1 ms | 1.8 ms | 16 ms |
| eve-emu, one core, same frame | 5.1 ms | 1.3 ms | 0.30 ms |

Heaviest frames: R-Type 216 ops, GPU 1.0 ms vs 10.6 ms on one core; **Zuma BILINEAR frame,
GPU 0.64 ms vs 28 ms**. Fixed costs: commit + wait 0.70 ms wall (7 us on the GPU); serial
dispatches 2.9-3.3 us each; device creation 0.34-1.25 s; precompiled shader library
0.2-21 ms.

Lines per dispatch (frame of 768 lines, 216 ops):

| Mode | GPU | wall |
|---|---|---|
| one dispatch per frame | 0.75-1.0 ms | |
| 768 dispatches, serial | 22.5 ms | 27 ms |
| 768 dispatches, concurrent (no barriers) | **0.76 ms** | 5.4 ms |
| 768 dispatches, **commit + wait per line** | 57.6 ms | **1 756 ms** |
| 48 bands of 16 lines, 256 threads per pixel | 50.4 ms | 85 ms |

Hybrid inside eve-emu (GPU per batch, CPU fallback): 0 mismatches on all captures (whole
play capture included); lines on the GPU 96.1 % (boot), 89.9 % (zuma), 89.8 % (whole play),
96.6 % (play with deferred writes). Estimated emulator CPU time with the GPU (harness
hashing subtracted): boot ~1.5 s (vs 3.7), **zuma ~1.5 s (vs 11.9)**, play 4000 ~10 s
(vs 19.4). Weak spot: the synchronous wait per batch (4.4 ms per batch under this load).

### 1.5 Antialiasing: 256 sub-pixels vs the distance table (experiment 06)

| Model, 13 BT8XX golden cases (3 993 600 pixels) | pixels that differ | worst channel error |
|---|---|---|
| **table (eve-emu today)** | **37** (all in `lines`, still TO VERIFY) | 15 |
| 256 sub-pixels, box | 201 164 | 255 |
| 256 sub-pixels, strict inside test | 193 699 | 208 |

Cost, one dispatch per frame: 2000 lines + 2000 points: table GPU 15.5 ms / CPU 1 647 ms;
256 sub-pixels GPU 20.8 ms / CPU 11 050 ms.

### 1.6 Hardware timing model (experiment 08)

rtype-boot, 779 680 lines: mean 124 commands + 163 fill clocks; worst line 621 of 1344
(46 %); no overflow. Cost of the model per line: closed form 155 ns (0.7 % of a core),
clock-stepped over used clocks 1 513 ns (7.2 %), every clock ~5 100 ns (~24 %). GPU with one
thread per line: 24.6 ms vs one thread per pixel 0.75-1.0 ms.

### 1.7 Cheap profile (experiment 11)

Skipping frames whose inputs did not change, rtype-boot: answers and pictures exact; lines
not drawn 275 824 of 780 176 (**35 %**), 39 % with the read-set test.

## 2. TS-Labs' hypotheses

1. Lines in parallel only, not with a per-line display list swap: true; no capture swaps
   per line (1.2), so it costs nothing in practice.
2. 256 sub-pixels on the GPU, eaten by kernel switching: the sub-pixels are cheap on the
   GPU but miss the reference by ~200 000 pixels (1.5); switching costs only with a wait
   per line (1 756 ms per frame) or a kernel per primitive, not with one dispatch per frame.

## 3. Task: BILINEAR on the fast path (Zuma 1.2x -> 4x+)

### 3.1 Goal

Bitmaps with `BITMAP_SIZE` filter **BILINEAR** go through a span fast path like NEAREST
does today: per-span stepping of the texture coordinates, four texels per pixel, weights in
integers. Output **bit-exact** with today's (which matches the BT8XX goldens `bilinear-*`
and `bitmap-format-*`). Then NEON / SSE2 kernels for the weighting, plain C++ fallback.

Target: Zuma gameplay **at least 4x real time** on one core (R-Type's level); the heaviest
BILINEAR frame **28 ms -> 5 ms or less**; R-Type unchanged or faster.

### 3.2 Workload (track)

- **New capture `zuma-play.evr`, from power-on** (needed: `zuma-flick.evr` starts with a
  chip state record and does not load into the state version 8 of the line-metrics branch;
  it also covers only the loading screen in its broken state).
  1. Config of a `TSL-VDAC2` instance: `[VDAC2] CaptureFile=<scratch>/zuma-play.evr`
     (the capture starts at the chip's power-on).
  2. SD card: the host folder with Wild Commander and the game (`boot.$C`, `WC/`, `ZUMA/`),
     slot `sd.zc`; the BIOS boots WC from it.
  3. In WC open `zuma/zuma_vdac2.spg`; let the loading screen finish, enter the menu, start
     level 1 and play about 60 s (balls moving, shots).
  4. Stop the capture (`vdac2 capture stop`). Expect about 0.5 MB per second.
  Local file, game data: never committed.
- **Regression tracks:** `rtype-boot.evr`, `rtype-play.evr` (first 4000 frames for quick
  runs, the whole capture before landing).
- **Goldens:** eve-emu `testdata/golden/bilinear-*` (6 cases), `bitmap-format-*`.

### 3.3 How to measure

```sh
# eve-emu checkout, Release build, -j = half the cores
tools/poc/021-eve-accel/common/wait-load.sh 600 12      # wait until the 1-min load < 12
./build/eve-replay zuma-play.evr --rom ft81x.rom --no-hash          # timing: read the cpu: line
./build/eve-replay zuma-play.evr --rom ft81x.rom                    # correctness: 0 mismatches
sample <pid> 10 -file profile.txt                                   # where the time goes
```

- Report CPU time and the real-time factor, two runs each, with the load average next to
  each number; before and after, same machine state.
- Heaviest frame: `eve-replay --dump FROM TO DIR` around it; time that frame alone.
- The share of the general path in the profile (`WrappedTexel` + `Shade` + `Direct`):
  85 % today, expected near zero after.

### 3.4 Acceptance

| Check | Required |
|---|---|
| eve-emu tests (goldens included) | all pass, NEON and `EVE_SIMD=OFF` |
| `eve-replay` full check on zuma-play, rtype-boot, whole rtype-play | 0 answer, 0 frame-count, 0 picture mismatches |
| NEON, SSE2 (MinGW build) and plain C++ | identical pictures |
| zuma-play real-time factor, one core | >= 4x (today ~1.2x) |
| rtype-boot / rtype-play | not slower than today (4.8x / 3.5x) |
| warnings | none with clang, gcc, MinGW (`-Werror`) |

### 3.5 Where and how

- eve-emu branch, then `main`, then the vendored copy in unreal-ng (`VENDORED.md` commit).
- Code: the bitmap span path in `eve-bitmap.cpp` / `eve-dl.cpp` (`DecodeSpan` templates per
  format and wrap mode, the axis-aligned matrix fast path). Reuse the existing BILINEAR
  weights and rounding (`eve-bitmap.cpp`, the comment at "BILINEAR weights out of 256") so
  the result stays identical. Mark the scalar weighting loop `SIMD-CANDIDATE(...)` first,
  measure, then add the NEON / SSE2 kernels in `eve-simd.h`.
- Formats in Zuma's frames: `L4`, `RGB565` (plus `ARGB4` on the loading screen); all formats
  of the fast path must keep working.

### 3.6 Status (2026-10-03): done in eve-emu `61a3f19`, vendored

Measured on a new Zuma capture from power-on (Wild Commander, loading screen, menu, play;
recorded without the FT81x ROM image, as the users' builds run), CPU time, two runs each:

| Track | Before (`125876d` / `d7d28e2`) | After (`61a3f19`) |
|:--|:--|:--|
| Zuma, whole capture (8603 FT812 frames) | 305 s, 0.48x real time | **53 s, 2.74x** |
| Zuma loading screen, drawing per frame | 133 ms | **7.5 ms** |
| rtype-boot | 3.32 s | 2.87 - 2.97 s |
| rtype-play, first 4000 frames | 17.5 s | 16.0 s |

What made the difference (eve-replay-profile, the profiling build with counters per bitmap
format, filter, path, matrix and pipeline: `--profile FROM TO`):

1. **77 % of the loading screen was text in a ROM font without the ROM image** (`FT_Text`
   with font 28 / 29, `ts-dos.asm`): an empty layout samples transparent black, and the
   general path blended every transparent pixel. Now such a span only writes the tag under
   the default pipeline. With the ROM image the text is drawn (L1 / L4 fonts).
2. **BILINEAR on the fast path** (the background mask, L4 scaled x1.6): the two texel rows
   decoded once per span, the four taps blended by `Simd::BilinearBlend` (NEON / SSE2 /
   C++), 30 -> 3.4 ns per pixel.
3. **The masking blends in SIMD** (`DST_ALPHA / ZERO`, `ONE_MINUS_DST_ALPHA / ONE`, RGB
   mask) and `ONE / ZERO` under any colour mask as a channel copy: 2.9 -> 1.2 ns per pixel
   for the colour planes.

Bit-exact everywhere (all eve-emu tests with the six `bilinear-*` goldens; rtype-boot and
3000 Zuma frames with every picture and answer compared) on NEON, SSE2 (x86_64) and
`EVE_SIMD=OFF`; MinGW `-Werror` clean. The target of 4x is not reached yet: Zuma's play is
now led by **rotated bitmaps** (the frog, the balls: PALETTED4444 / ARGB4 NEAREST with a
rotation matrix, no fast path, ~50 % of play) and the display list walk (768 commands per
line, ~20 %) - the next task.
