# 09 - Native GPU backends behind one thin interface, with the SIMD CPU renderer as fallback (design)

Feeds: **OPTIMAL** (point a: the GPU path on every platform the emulator ships on).

Decision taken by the user (not measured here): no cross-platform GPU layers (WebGPU, OpenCL,
translation layers such as MoltenVK). Each platform gets its native API; where there is
none, it fails, or it does not pay off, eve-emu falls back to its SIMD CPU renderer
(NEON / SSE2 kernels, `eve-simd.h`, plus the line-parallel work of 03) with identical,
bit-exact output.

## Goal

Describe how the Metal implementation of this POC maps onto the native APIs of Windows and
Linux (Vulkan through the vendor's driver, Direct3D 12, CUDA), how the fallback decision is
made and what it costs, and what to measure on TS-Labs' AMD Ryzen / Windows 11 machine and
on NVIDIA.

## Why it matters

The renderer has to run on the Mac, on AMD and NVIDIA under Windows and Linux. A GPU path
only helps if it is exact (the CPU renderer is the reference) and if the per-platform part
stays small enough to maintain.

## What the POC already separates

| Layer | File | Per platform? |
|---|---|---|
| Flattening the display list into an op list (once per batch, on the CPU) | `common/eve-snap.h` `Flatten` | no |
| The per-pixel evaluation, integer only | `common/eve-ops-core.h` | **no**: one C-like source compiled as C++ (the CPU reference), Metal, CUDA, HLSL |
| The kernel entry and buffer bindings | `04-metal-compute/eve-ops.metal` (60 lines around the core) | yes, thin |
| Device, buffers, dispatch, readback | `04-metal-compute/eve-gpu-metal.mm` (~250 lines) | yes |
| The hook into eve-emu (batch of lines -> GPU or CPU) | `03-line-parallel-cpu/overlay/eve-dl.cpp` (`GpuHooks`, `CatchUpLines`) | no |

The core is written in a subset every shading language accepts: scalars, structs passed and
returned by value, `if` / `switch` / `for`, C casts; data reaches it through macros
(`EVE_BYTE`, `EVE_OP`, `EVE_LIST`, `EVE_AA`) and, in Metal, a context struct of buffer
pointers (`EVE_CTX` / `EVE_PASS`). The C++ build of the same file reproduces eve-emu's
pictures bit for bit (04: 125 of 125 expressible snapshot frames), and so does the Metal
build (04a; and the whole 410-second R-Type capture through the eve-emu backend, 04c).

## Mapping onto the native APIs

| Concept | Metal (this POC) | Vulkan (native driver) | Direct3D 12 | CUDA |
|---|---|---|---|---|
| kernel language | MSL; core included as is | GLSL 4.50 or HLSL -> SPIR-V; core included with `#define`s (no `typedef struct`: use `struct EveOp` - already so); 64-bit ints via `GL_ARB_gpu_shader_int64` / `shaderInt64` | HLSL SM 6.0+ (`int64_t`), core included with `#define`s; buffers as `StructuredBuffer` / `ByteAddressBuffer` | CUDA C++; core included with `EVE_FN = __device__ static inline` |
| memory image (RAM_G + ROM, 3 MB) | shared `MTLBuffer` (unified memory: a `memcpy` of dirty 4 KB pages) | storage buffer; on a discrete GPU a device-local buffer plus a host-visible staging buffer and `vkCmdCopyBuffer` of the dirty pages | default-heap buffer + upload heap, `CopyBufferRegion` of dirty pages | `cudaMemcpyAsync` of dirty pages into device memory (pinned host memory) |
| op list, band lists | small shared buffers per batch | storage buffers (or push constants for the parameters) | root constants + buffers | kernel arguments + `cudaMemcpyAsync` |
| one frame / one band | `dispatchThreads(width, rows)`, 16x16 groups | `vkCmdDispatch(ceil(w/16), ceil(rows/16), 1)` | `Dispatch(...)` | `<<<grid, 16x16>>>` |
| bands with no data dependency | concurrent encoder: no barrier between dispatches (04b) | dispatches without a pipeline barrier between them | dispatches without a UAV barrier | kernels on one stream are ordered; independent bands can share one kernel launch or use several streams |
| bands that must see new memory | wait for the GPU, copy, dispatch again (04b `--sync`) | fence wait per batch (`vkQueueSubmit` + `vkWaitForFences`) | fence + `WaitForSingleObject` | `cudaStreamSynchronize` |
| picture back to the host | shared buffer (no copy) | device -> host-visible copy, then map | readback heap | `cudaMemcpy` device -> host |
| GPU time | `GPUStartTime` / `GPUEndTime` | timestamp queries | timestamp queries | `cudaEvent` |

On a discrete GPU two costs exist that this Mac does not have: the upload of dirty `RAM_G`
pages over PCIe (a few KB to 1 MB per batch; at ~10 GB/s 1 MB is 0.1 ms) and the readback
of the picture rows (1024 x 768 x 4 = 3 MB per frame, ~0.3 ms). Both can overlap with the
next frame's CPU work.

## The fallback decision

Implemented in the 03/04c overlay (`CatchUpLines`):

1. **At start-up**: the backend registers only if the device exists and the kernel
   compiles (`eve-gpu-metal.mm`: `MTLCreateSystemDefaultDevice`, `newLibraryWithSource`);
   otherwise every batch stays on the CPU. Cost measured here: run-time compilation of the
   kernel source took 0.48-0.76 s under load (04a); a shipped build would load a
   precompiled library (`.metallib`, SPIR-V, DXIL, cubin/PTX) instead.
2. **Per batch, by capability**: the flattener refuses what the kernel does not express
   (edge strips, text formats, `REG_CSPREAD`, `REG_ROTATE`, `REG_SWIZZLE`, bitmaps reading
   outside `RAM_G` / ROM); such a batch is drawn on the CPU. On the captures: 1-4 % of the
   lines (04c).
3. **Per batch, by cost** (`EVE_POC_GPU_MIN=auto`): the GPU's time per batch is fitted as
   `a + b x lines` (least squares over recent GPU batches), the CPU's time per line is a
   running average of CPU batches; a batch goes to the GPU when `a + b x lines` is the
   smaller, and one batch in 64 takes the other path to keep both estimates current. The
   decision costs a few multiplications per batch. The break-even `a / (cpu - b)` it
   reports is the GPU's fixed cost per batch over the per-line saving (04c).
4. **The output is identical on both paths** (the gate of every experiment), so switching
   per batch is invisible.

## What to measure on Windows (AMD Ryzen, Windows 11) and on NVIDIA / Linux

Port `eve-gpu-metal.mm` to the API (Vulkan first: one code path for AMD, NVIDIA and Linux;
D3D12 if Vulkan drivers misbehave on the target; CUDA only as an NVIDIA-specific
experiment), then run the same checks:

| Measurement | Command (after porting) | Gate / what to look at |
|---|---|---|
| bit-exactness of the kernel on snapshots | `metal-render`-equivalent on `04-metal-compute/out/snap-*` | every expressible frame equal to eve-emu |
| bit-exactness through eve-emu | `eve-replay-gpu` on rtype-play (whole), rtype-boot, zuma-flick | 0 answer / frame / picture mismatches (zuma: the one frame the vendored library itself misses) |
| GPU time per frame, frame per dispatch | 04a | compare with eve-emu one core and 8 threads (03) |
| per-dispatch and per-sync cost | 04b sweep (bands of 1...768, one submission vs fence per band) | the fixed cost `a` of a batch; break-even lines |
| end to end | 04c `time` | CPU % per chip second and the auto break-even |
| integrated vs discrete GPU (Ryzen APU vs a dGPU) | all of the above | PCIe upload / readback per frame |
