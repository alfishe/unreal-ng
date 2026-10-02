# 01 - Baseline: the vendored eve-emu on one core

Feeds: all three profiles (the reference every other figure is compared with).

## Goal

Build eve-replay against the vendored eve-emu (`core/src/3rdparty/eve-emu`, commit d7d28e2,
unchanged), measure its real-time factor on the three captures with the NEON kernels and
with the plain C++ fallback, profile where the time goes, and see how uniform the lines
are (parallel-friendliness; the line figures are in 02).

## Why it matters

The real-time factor (chip seconds per second of host CPU) says how much of a core the
FT812 costs when it runs in step with an emulated machine, and which part of the renderer
an accelerator has to take over.

## Method

- `tools/poc/021-eve-accel/CMakeLists.txt` builds library variants: the vendored `src/` copied
  into the build tree with an experiment's files replacing some of them ("overlays"). The
  `base` variant has no overlay; `scalar` is the same with `EVE_NO_SIMD`. Release
  (`-O3`), Apple clang 17, arm64. eve-replay is the eve-emu tool (branch aa-threads, with
  `--dump`) plus POC options (`common/eve-replay.cpp`).
- `--no-hash` (the picture hash is the harness's cost); CPU time of the process is the
  figure to compare on this shared machine (wall time is reported too).

```
tools/poc/021-eve-accel/build.sh
tools/poc/021-eve-accel/01-baseline/run.sh              # out/timing.txt (twice, both variants)
tools/poc/021-eve-accel/01-baseline/run.sh profile play # out/profile-play.txt (sample, 10 s)
tools/poc/021-eve-accel/01-baseline/run.sh profile zuma
```

## Results

Real-time factor = chip time / CPU time of the replay (higher is better). The load stayed
above 12 for the whole session (other agents' builds); each run waited 10 minutes first.

| Capture (chip time) | NEON, run 1 | NEON, run 2 | C++ fallback, run 1 | run 2 | load (1 min) |
|---|---|---|---|---|---|
| rtype-boot (17.8 s) | 3.70 s CPU = **4.81x** | 3.75 s = 4.76x | 4.31 s = 4.13x | 4.30 s = 4.15x | 95-125 / 49-70 |
| rtype-play, 4000 frames (68.0 s) | 19.4 s = **3.51x** | 19.7 s = 3.45x | 31.7 s = 2.15x | 31.8 s = 2.14x | 95-125 / 49-70 |
| zuma-flick (14.5 s) | 11.96 s = **1.21x** | 11.90 s = 1.22x | 11.97 s = 1.21x | 12.14 s = 1.19x | 95-125 / 49-70 |

Wall time was 1.3-2x the CPU time (preemption). The CPU figures repeat within 2 %.

Where the time goes (`sample`, samples at the top of the stack):

| rtype-play | samples | zuma-flick | samples |
|---|---|---|---|
| `DrawBitmap` (span setup, blend kernels inlined) | 517 | `WrappedTexel` (general bitmap path) | 4 238 |
| `ShadeSpan` (masking blends) | 413 | `DrawBitmap` | 1 851 |
| palette decode loop (`DecodeSpan`) | 274 | `Shade` (per-pixel pipeline) | 1 076 |
| replay harness (`main`, buffer fill) | 281 | `Direct` (texel decode) | 408 |
| `OutputLine` | 97 | `ShadeSpan` | 369 |
| `ExecuteLine` (display list) | 41 | `ExecuteLine` | 179 |

(The play profile also has 1 189 samples reading the 144 MB capture file at start-up,
left out above.)

## Analysis

- R-Type runs 3.5-4.8x faster than the chip on one core, as eve-emu's performance notes
  say (§3 there: 4.5-4.7x for the drawing alone). The NEON kernels give 1.6x on the play
  capture (default blend and output conversion), nothing on boot and Zuma.
- **Zuma is the heavy case: 1.2x real time.** Its game frames draw full-screen `L4` and
  `RGB565` bitmaps with **BILINEAR** filtering and scaling; eve-emu's fast path covers only
  NEAREST, so every pixel goes through the general per-pixel path (`WrappedTexel`, `Shade`,
  85 % of the samples). Such a frame costs 28 ms on one core (04 snapshot), i.e. 1.7 frame
  periods - the program would not keep up on a slower host core.
- Lines are cheap and many (02): R-Type 2-16 us per line, Zuma's game lines ~40 us.

## Conclusions

- Single-thread headroom exists on the CPU side before any parallelism: a BILINEAR fast
  path (SIMD-CANDIDATE: the four-texel weighted sum, eve-bitmap.cpp `Blend4`) would lift
  Zuma the most; R-Type is already well above real time.
- Windows / Linux: the SSE2 kernels exist with the same results; x86 cores of the same
  generation should give similar factors. To measure there: `01-baseline/run.sh` built
  with MinGW or MSVC.
