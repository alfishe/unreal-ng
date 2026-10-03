# 04a - A frame per dispatch (TS-Labs' scenario a)

Feeds: **OPTIMAL**.

## Goal

One compute dispatch per frame, one thread per pixel (a 1024 x 768 grid): measure upload,
GPU time, submit-to-done latency and readback per frame against eve-emu on one core, with
the picture checked bit for bit.

## Why it matters

The canonical GPU shape. Its drawbacks per TS-Labs: no raster change inside the frame, a
heavier kernel, no sub-pixels. 02 shows that 70-100 % of frames have no relevant change
inside them; 04c handles the others.

## Method

`metal-render` (code in `04-metal-compute/`): each snapshot frame is flattened (04), its
`RAM_G` (1 MB) and op list are copied into shared buffers, `RenderFrame` is dispatched over
the frame (16 x 16 threadgroups), committed and waited for, 10 times; the median GPU time
(`GPUEndTime - GPUStartTime`) and wall time are reported, and the picture is compared with
eve-emu's.

```
tools/poc/021-eve-accel/04-metal-compute/run.sh metal    # out/metal-frame-<trace>.txt
```

## Results

Apple M1 Ultra (64-core GPU), load average **~180** during this run (the GPU itself was
not busy; the CPU side was preempted constantly, so submit-to-done is inflated):

| Median per frame | rtype-play (32) | rtype-boot (49) | zuma-flick (47) |
|---|---|---|---|
| bit-exact | 31 / 31 | 46 / 46 | 45 / 45 |
| ops | 87 | 29 | 1 (15 in game frames) |
| flatten (CPU) | 0.033 ms | 0.019 ms | 0.006 ms |
| upload (1 MB RAM_G + op list) | 0.089 ms | 0.076 ms | 0.082 ms |
| **GPU time** | **0.49 ms** | 0.26 ms | 0.12 ms |
| submit-to-done (wall) | 4.1 ms | 1.8 ms | 16 ms |
| readback (copy 3 MB) | 0.17 ms | 0.14 ms | 0.20 ms |
| eve-emu, one core (same frame) | 5.1 ms | 1.3 ms | 0.30 ms |

Heaviest frames: an R-Type frame with 216 ops: GPU 1.0 ms vs eve-emu 10.6 ms; a Zuma game
frame (full-screen BILINEAR `L4` + `RGB565`, 15 ops): **GPU 0.64 ms vs eve-emu 28 ms**.

Fixed costs (empty kernel, `--overhead`, same load): commit + wait 0.70 ms wall, 7 us GPU;
serial dispatches in one encoder 2.9-3.3 us each on the GPU. Start-up (09): device
creation 0.34-1.25 s under this load, kernel library 1-90 ms from source (shader cache),
0.2-21 ms from a precompiled `.metallib`.

## Analysis

- GPU time is 10x (R-Type) to 44x (Zuma's BILINEAR frames) below one CPU core; the GPU
  pays most where eve-emu's CPU fast path does not apply.
- The cost that matters is not the GPU but the round trip: a synchronous submit-and-wait
  per frame costs 0.7 ms at best on this loaded machine and several ms when the waiting
  thread is preempted. One frame per 16.9 ms leaves room for that; the round trip should
  overlap with emulation (submit at the frame end, collect the picture when the host
  presents it).
- "Slightly heavier kernel": each pixel walks the op list of its 16-row band and skips ops
  that do not cover it; with 87-232 ops this is still well under 1 ms.

## Conclusions

- Scenario a works, bit-exact, with large margins for R-Type and Zuma frames; it covers
  the 70-100 % of frames with no relevant mid-frame change (02).
- Windows / Linux: the dispatch maps 1:1 (09). On a discrete GPU add the PCIe upload of
  dirty pages and the 3 MB readback (~0.3 ms); measure both on the Ryzen machine.
