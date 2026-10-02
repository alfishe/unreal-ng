# 03 - Lines in parallel on the CPU (and the fallback path)

Feeds: **OPTIMAL** (the SIMD CPU renderer every GPU path falls back to, made parallel) and
**CHEAP** (the read-set test reused by 11). The overlay here is also the base of the GPU
variant (04c) and the cheap variants (11).

## Goal

Draw the lines of a batch with a thread pool (1/2/4/8/16 threads), bit-exact, and measure
the speedup and the serial fraction. Test the larger batches 02 found possible (deferring
`RAM_G` writes outside the active list's read set).

## Method

Variant `threads` (`overlay/`: `eve-dl.cpp`, `eve-render.h`, `eve-bitmap.cpp`,
`eve-memory.cpp`, `eve-timing.cpp`):

- `LineRun` reads and writes the bitmap handle table through a pointer, so a worker can
  use a private copy; each worker has its own line buffers (color, stencil, tag, texels).
- A batch (`CatchUpLines`): the first line is drawn serially; after it the handle table is
  a fixed point of the list (02). The rest goes to a persistent pool; workers take lines
  one at a time from an atomic counter (round-robin-like, 02 showed contiguous chunks
  balance worse), start each line from a copy of the table and check it is unchanged at
  the end. A line that changes it is reported; lines after it are drawn again serially
  from its table (exact either way; never happened on the captures). Line costs are
  written per line, the overflow count and `REG_TAG` are merged after the batch. Batches
  under 16 lines stay serial.
- `EVE_POC_DEFER=1`: a `RAM_G` write that lies outside the read set of the active list
  (bitmap ranges, palettes, text glyphs, from one run of the list without drawing; cached
  until a swap, a drawing register write or a handle change) skips the catch-up. To keep
  the result identical to the reference, the lines the reference would have drawn before a
  line swap and before a `REG_TAG` read are drawn at that point.

```
tools/poc/021-eve-accel/03-line-parallel-cpu/run.sh check   # picture hashes, 8 threads
tools/poc/021-eve-accel/03-line-parallel-cpu/run.sh scale   # timings
```

## Results

Correctness (every answer byte, frame count and picture hash checked by eve-replay):

| Capture | 8 threads | 8 threads + deferred writes |
|---|---|---|
| rtype-boot | exact | exact |
| zuma-flick | exact (*) | exact (*) |
| rtype-play, **whole capture** (24 193 frames) | - | **exact** |

(*) zuma-flick has one frame that the unmodified vendored library also misses against the
capture; every variant reproduces the vendored library there.

Serial fraction (lines drawn outside the pool):

| | without deferral | with deferral |
|---|---|---|
| rtype-boot | 2.78 % | 0.13 % |
| zuma-flick | 8.33 % | 0.14 % |
| rtype-play (whole) | - | 0.95 % |
| handle table divergences | 0 | 0 |

Timings: the machine was loaded by other agents for the whole session (1-minute load 37-180
during these runs), so wall times scatter by 2-4x between repeats. Best of two runs:

| | 1 thread | 2 | 4 | 8 | 16 | 8 + deferral |
|---|---|---|---|---|---|---|
| rtype-boot wall (s) | 3.35 | 1.84 | 1.03 | 0.65 | 0.55 | 0.66 |
| rtype-boot CPU (s) | 3.34 | 3.52 | 3.61 | 3.92 | 5.05 | 3.95 |
| rtype-play 4000 wall (s) | (>= 18.5 = its CPU time) | 10.1 | 15.7 | 14.8 | 16.8 | 5.15 |
| rtype-play 4000 CPU (s) | 18.5 | 18.5 | 21.0 | 22.7 | 25.6 | 22.3 |
| zuma-flick wall (s), one pass | (>= 11.3) | 5.7 | 3.0 | 22.1 | 11.1 | 5.7 |

Boot (load 37-44 in the first pass, 153-166 in the second; the best runs are from the
second, low-contention moments): 5.1x at 8 threads, 6.1x at 16. The play and Zuma rows are
dominated by preemption; their best runs (play 8 threads + deferral 5.15 s against at least
18.5 s serial: >= 3.6x; Zuma 4 threads 3.0 s against >= 11.3 s: >= 3.7x) are lower bounds
of what the pool does.

The CPU time grows 5-20 % with 8 threads (pool hand-off, per-worker buffers, the serial
first line).

## Analysis

- Exact by construction and confirmed on the whole play capture.
- With deferral the serial part is under 1 %: Amdahl leaves room for >8x; what limits the
  boot figures is the work per batch (a frame of ~1-5 ms split into 768 lines) and the
  machine.
- TS-Labs' first hypothesis: correct that the parallelism is across lines; a line swap
  would end a batch, but no capture uses one, and the real batch boundaries (`RAM_G`
  writes) mostly disappear with the read-set test.

## Conclusions

- A line-parallel CPU renderer is a safe, exact fallback and gives several-fold speedups;
  it is what eve-emu should get first (`EveSetThreads`, default 1), with the read-set
  deferral.
- Windows / Linux: plain C++17 threads and atomics; the SSE2 kernels give the same output.
  Re-measure the scaling there on a quiet machine (`run.sh scale`).
