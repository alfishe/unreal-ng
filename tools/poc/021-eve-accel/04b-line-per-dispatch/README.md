# 04b - A line (or band of lines) per dispatch (TS-Labs' scenario b)

Feeds: **OPTIMAL** (per-line display list emulation on the GPU) and **FULL** (sub-pixels).

## Goal

Measure the per-dispatch and per-synchronization cost when every line, or every band of N
lines, is its own dispatch - the shape that honors any raster change between lines - and
the cost of 256 threads per pixel (sub-pixel antialiasing).

## Method

`metal-render --bands N` on R-Type play snapshot frames (code in `04-metal-compute/`):

- **one buffer, serial**: all band dispatches in one command buffer, default (serial)
  encoder: the GPU puts a barrier between dispatches.
- **one buffer, concurrent** (`--concurrent`): no barriers; bands write different rows.
- **sync per band** (`--sync`): a command buffer per band, committed and waited for before
  the next - what a CPU must do if it changes memory between bands and the GPU has to see
  each state.
- **256 threads per pixel** (`--sub`, `RenderLineSub`): a threadgroup per pixel, one thread
  per 1/16 x 1/16 subsample; bitmap ops by thread 0, primitive coverage counted by all 256
  and reduced (honest coverage, 06).

`04b-line-per-dispatch/run.sh` is the full sweep (N = 1 ... 768); the session was stopped by
the user before the sweep ran, so the figures below are the individual runs made while
developing, on the heaviest play frame (216 ops), load average 150-180.

## Results

| Mode (frame of 768 lines, 216 ops) | GPU time | wall | per band |
|---|---|---|---|
| one dispatch per frame (04a) | ~0.75-1.0 ms | | |
| 768 dispatches, one buffer, serial | 22.5 ms | 27 ms | 29 us GPU |
| 768 dispatches, one buffer, **concurrent** | **0.76 ms** | 5.4 ms | ~0 |
| 768 dispatches, **commit + wait per line** | 57.6 ms | **1 756 ms** | 2.3 ms wall |
| 48 dispatches of 16 lines, 256 threads per pixel | 50.4 ms | 85 ms | 1 ms |
| empty dispatch, commit + wait (04a) | 7 us | 0.70 ms | |
| serial empty dispatches in one encoder | 2.9-3.3 us each | | |

eve-emu draws this frame in 10.6 ms on one core, i.e. ~14 us per line.

## Analysis

- **Dispatches are free, barriers and waits are not.** Without barriers, 768 line
  dispatches cost the same GPU time as one frame dispatch. With the serial encoder each
  line waits for the previous one: 1024 threads cannot fill a 64-core GPU, so a frame takes
  30x longer. With a CPU wait per line the round trip (0.7 ms at best here, 2.3 ms with
  preemption) dwarfs the 14 us of CPU work per line.
- **Break-even against the CPU**: a synchronized band pays the round trip (>= 0.7 ms here;
  typically 50-200 us on an idle machine, to be measured) and saves ~14 us per line on
  R-Type (one core) or ~40 us on Zuma; it pays from ~50 lines (R-Type) / ~20 lines (Zuma)
  at 0.7 ms, from ~5-15 lines at 0.1-0.2 ms. 04c's adaptive rule measures this live.
- **Mid-frame changes do not need a wait per change**: if each band carries its own
  snapshot of what it reads (the dirty pages written since the previous band and its op
  list), all bands of a frame can be recorded into one command buffer without barriers and
  submitted once - the concurrent row. The cost moves to memory: a copy of the changed
  pages per band.
- **256 threads per pixel** cost ~50x a pixel-per-thread dispatch (the reductions and
  barriers per primitive, idle lanes during bitmap ops) and compute a model that is not the
  chip's (06). Not worth it.

## Conclusions

- TS-Labs' second hypothesis ("the overhead of switching between GPU kernels would eat the
  gain") is right for **synchronous** per-line dispatch (2.3 ms per line here) and for
  serial dispatches (29 us per line), wrong for barrier-free dispatches recorded together
  (no measurable cost). The per-line design is viable if each band's inputs are versioned
  and the bands are submitted once per frame.
- Windows / Linux: Vulkan / D3D12 dispatches without barriers in one command list behave
  like the concurrent encoder; a fence wait per band costs a driver round trip (tens to
  hundreds of us). Measure the sweep there (`run.sh`) on a quiet machine.
