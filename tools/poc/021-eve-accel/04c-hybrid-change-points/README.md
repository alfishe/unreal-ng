# 04c - Hybrid: whole frames on the GPU, split at change points, CPU fallback

Feeds: **OPTIMAL** (the design to build) - TS-Labs' hybrid: a whole-frame dispatch when
nothing a line reads changes during the frame, bands split at every change point.

## Goal

Put the GPU kernel inside eve-emu and let eve-emu's lazy catch-up decide the bands: a batch
of lines ends exactly where something the lines read changes (a `RAM_G` write - with
`EVE_POC_DEFER=1` only one the active list reads -, a drawing register, a line swap, the
frame end). Check the result over whole captures with eve-replay's picture hashes, and
measure the cost and the fallback decision.

## Method

Variant `gpu` = the 03 overlay + `04-metal-compute/eve-gpu-metal.mm` (the Metal backend,
~250 lines) registered through `GpuHooks`:

- per batch: the first line on the CPU (handle table fixed point), the rest in one
  dispatch of `RenderFrame` over its rows; dirty 4 KB pages of `RAM_G` copied before the
  dispatch; rows copied back into the host's frame buffer; the line at `REG_TAG_Y` is also
  drawn on the CPU for `REG_TAG`;
- batches the op list cannot express (edge strips, text formats, `REG_CSPREAD`, ...) and
  batches below `EVE_POC_GPU_MIN` lines (64) go to the CPU (03's renderer, threads as set);
- `EVE_POC_GPU_MIN=auto`: per-batch choice by measured cost (09).

```
tools/poc/021-eve-accel/04c-hybrid-change-points/run.sh check   # picture hashes
tools/poc/021-eve-accel/04c-hybrid-change-points/run.sh time    # timings (not run: stopped)
```

## Results

Correctness (eve-replay: every answer byte, frame count, picture hash):

| Capture | Mismatches | Lines on the GPU | GPU batches | CPU fallbacks (reason) |
|---|---|---|---|---|
| rtype-boot | 0 | 96.1 % | 1 016 | 25 (`REG_CSPREAD` at start-up) |
| zuma-flick | 0 (*) | 89.9 % | 737 | 41 (`REG_CSPREAD`) |
| **rtype-play, whole (409.8 s, 24 193 frames)** | **0** | 89.8 % | 27 986 | 1 093 (bitmaps reaching past `RAM_G`) |
| rtype-play, 4000 frames, deferred writes | 0 | 96.6 % | 4 530 | 119 |

(*) the one frame the vendored library itself misses.

Cost - only the checked runs exist (hashing on, load 84-126; the timing pass was stopped
on the user's request). CPU time of the replay, with the harness's hashing (~4.3 ms per
frame, measured as the difference between hashed and unhashed base runs) subtracted to
estimate the emulator's own share:

| | base, one core (01, no hash) | gpu variant, with hash | gpu, hash subtracted (estimate) |
|---|---|---|---|
| rtype-boot | 3.7 s CPU | 5.9 s | ~1.5 s |
| zuma-flick | 11.9 s | 5.1 s | ~1.5 s |
| rtype-play 4000 (deferred writes) | 19.4 s | 26.8 s | ~10 s |

Time spent in the GPU path (flatten, upload, dispatch, wait, copy back): 4.4 ms per batch
on average on the whole play capture under this load - almost all of it waiting for the
command buffer while the waiting thread competes with ~100 runnable processes. The
adaptive rule, run on the boot capture at load ~170, measured the GPU batch at
6.1 ms + 1.6 us per line against 2.0 us per line on the CPU and therefore sent nearly
everything to the CPU (its break-even, `a / (cpu - b)`, came out at ~15 000 lines): on
this machine in this state the CPU path was the right choice, which is what the rule is
for.

## Analysis

- **The hybrid is exact over a whole 6-minute capture**: eve-emu's existing change points
  are the right band boundaries, and the kernel reproduces every line it draws.
- The CPU work the GPU removes is large for Zuma (BILINEAR frames: ~8x less CPU) and about
  half for R-Type; what remains is the emulator itself, the first line of each batch and
  the flattening.
- The weak spot is the synchronous wait per batch. On an idle machine a Metal round trip is
  ~0.1-0.2 ms; here it was 4-6 ms. Two remedies, both compatible with exactness:
  (1) draw the frame's GPU batches asynchronously and collect the rows when the host needs
  the picture (frame end) - the chip's answers do not depend on the picture except
  `REG_TAG`, which the CPU line already provides; (2) record every band of a frame into one
  command buffer with its own snapshot of changed pages (04b, concurrent).

## Conclusions

- Recommended structure for eve-emu: lazy batches (exists) + read-set deferral (03) +
  GPU backend per batch where it pays + bit-exact CPU fallback, decided per batch by
  measured cost. Mid-frame display list rewriting (TS-Labs' planned TS-Conf demo) only
  shortens the batches; the result stays exact, and the cost rule moves such frames to the
  CPU when the bands get too short.
- Windows / Linux: port `eve-gpu-metal.mm` (09); re-run `run.sh check` and `run.sh time`.
