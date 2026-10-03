# 07 - The whole renderer on the GPU: kernel count, switch overhead, latency

Feeds: **OPTIMAL**. An estimate from the measurements of 04, 04a-04c (no code of its own;
the per-op kernel is in `04-metal-compute/eve-ops.metal`, `RenderOp`).

## Goal

Estimate what the full renderer costs on the GPU at 1024 x 768, 59 Hz: how many kernels per
frame, what switching between them costs, and the end-to-end latency.

## Kernel count per frame - two designs measured

| Design | Kernels per frame | R-Type play (median 87 ops) GPU | rtype-boot (29 ops) | Bit-exact |
|---|---|---|---|---|
| one uber-kernel, every pixel runs its band's ops (04a) | **1** | 0.49 ms | 0.26 ms | yes |
| one kernel per op ("per primitive") + clear + resolve | ops + 2 | 1.07 ms | 0.83 ms | yes |

(`metal-render --per-op`, same snapshots, load ~180.) Each extra serial dispatch costs
~3 us of GPU time (empty-kernel measurement) plus the pixel state round trip through device
memory; with 232 ops that is ~0.7 ms more per frame, with a 2048-command list (the chip's
maximum) it would be several milliseconds. The uber-kernel has no switches at all: all
primitive types are cases of one `switch` in the core.

## Latency at 59 Hz (16.9 ms per frame)

| Step | Measured | Notes |
|---|---|---|
| flatten the display list (CPU) | 0.02-0.06 ms | once per batch |
| upload dirty `RAM_G` pages + op list | 0.07-0.09 ms for a full 1 MB on unified memory | discrete GPU: ~0.1 ms per MB over PCIe |
| GPU work | 0.1-1.0 ms (up to 0.64 ms for Zuma's 28 ms CPU frames) | |
| submit + wait | 0.7 ms (empty kernel) to several ms under heavy load | the dominant term |
| readback of the picture | 0.14-0.20 ms (copy of 3 MB) | discrete GPU: ~0.3 ms |
| **end to end** | **~1-2 ms on an idle machine; 2-16 ms measured under load 180** | |

## Conclusions

- One kernel per frame (or per band) is the design; TS-Labs' concern about kernel
  switching applies to the per-primitive design (2x GPU time here, growing with the list)
  and to synchronous per-line dispatch (04b), not to the uber-kernel.
- The whole renderer fits in a few percent of the frame period on this GPU; the latency
  budget is dominated by submission and waiting, which an emulator can overlap with the
  next frame's emulation.
- Windows / Linux: the same structure; on a discrete GPU add ~0.4 ms of PCIe traffic per
  frame. Measure with the 04a/04b/04c scripts after porting (09).
