# 04 - The renderer as a Metal compute kernel: the port and its exactness

Feeds: **OPTIMAL** (the GPU path). The timing scenarios built on this port are 04a (a
frame per dispatch), 04b (a line or band per dispatch) and 04c (eve-emu with the GPU
backend, batches cut at change points); 07 adds the kernel-per-primitive model.

## Goal

Port eve-emu's drawing to a GPU compute kernel with integer arithmetic identical to the
CPU path, and check that it reproduces eve-emu's pictures bit for bit on real frames.

## Why it matters

The brief asked for the hottest primitive class (full-screen bitmaps with R-Type's blends).
Once the display list is flattened (below), the whole bitmap pipeline - every format,
NEAREST and BILINEAR, BORDER and REPEAT, any matrix, alpha test, stencil, every blend
factor, color mask, `CLEAR` with scissor - costs no more to port than one primitive class,
and so do antialiased points, lines and rectangles with the BT8XX table. So the port covers
everything except edge strips, the text formats (`TEXT8X8`, `TEXTVGA`, `BARGRAPH`) and the
output options `REG_ROTATE`, `REG_SWIZZLE`, `REG_CSPREAD`.

## Method

1. **Snapshots.** The threads variant (03) writes, for frames drawn in one batch, what
   drawing read: registers, the handle table at the first line, the active display list,
   `RAM_G`, and the picture eve-emu produced (`--snap DIR FROM TO`, `EVE_POC_SNAP_STEP`).
   128 frames: every 80th of the first 4000 play frames (32), every 20th boot frame (49),
   every 16th Zuma frame (47). ~4 MB each, in `out/` (deleted at the end: game data).
2. **Flattening** (`common/eve-snap.h`). The graphics context is reset per line and the
   handle table is a fixed point after the first line (02), so one walk of the display list
   gives the frame's operations in order: each `CLEAR`, each bitmap vertex, each point, line
   or rectangle, with the full state it runs with and the rectangle of pixels it can touch.
   The walk follows `CALL` / `JUMP` / `RETURN` / `MACRO` / `SAVE_CONTEXT` exactly as
   eve-emu's `ExecuteWord`. The texture coordinates of a bitmap pixel are folded into
   `sx = kx + a (x - x0) + b y` (the `>> 4` of eve-emu's 64-bit arithmetic is exact over
   multiples of 16), so the kernel needs only 32-bit integers for bitmaps. Ops are binned
   into bands of 16 rows.
3. **The per-pixel core** (`common/eve-ops-core.h`): one C-like source - scalars and
   structs by value, no pointers - that compiles as C++ (the CPU reference, `snap-check`),
   as Metal (`eve-ops.metal`), and is meant for CUDA and HLSL unchanged (09). Each pixel
   runs the ops of its band in order, with its RGBA and stencil in registers.
4. **Checks.** `snap-check` (C++ build of the core) and `metal-render` (Metal) compare
   every pixel with eve-emu's picture.

```
tools/poc/021-eve-accel/build.sh
tools/poc/021-eve-accel/04-metal-compute/run.sh snap    # snapshots
tools/poc/021-eve-accel/04-metal-compute/run.sh check   # CPU build of the core
tools/poc/021-eve-accel/04-metal-compute/run.sh metal   # Metal, frame per dispatch and per op
```

## Results

| | rtype-play | rtype-boot | zuma-flick | all |
|---|---|---|---|---|
| snapshot frames | 32 | 49 | 47 | 128 |
| expressible as an op list | 32 | 48 | 45 | 125 |
| not expressible | - | 1 (`REG_CSPREAD`) | 2 (`REG_CSPREAD`) | 3 |
| **bit-exact, C++ core** | 32 | 48 | 45 | **125 / 125** |
| **bit-exact, Metal** | 32 | 48 | 45 | **125 / 125** |
| ops per frame (median) | 87 (up to 232) | 29 | 1 (15 in game frames) | |

The three unsupported frames are start-up frames with `REG_CSPREAD` = 1 (its reset value).
Through eve-emu itself (04c) the same kernel draws 90-97 % of all lines of the three
captures, the whole 410-second play capture included, with every picture hash equal.

Timing: see 04a (frame per dispatch), 04b (lines per dispatch), 04c (end to end), 07.

## Analysis

- Exactness was reached on the first run of the Metal kernel: the core is integer only,
  Multiply's division by 255 is the exact shift form `(t + 1 + (t >> 8)) >> 8`
  (exhaustively equal for every product the blend forms), and the only floating point is
  a first guess for an integer square root that is corrected in integers.
- The flattening is cheap (20-60 us per frame on one core) and runs once per batch.
- The per-pixel form is slow on a CPU (one core, mean per snapshot frame: 10-41 ms
  where eve-emu's span renderer takes 2-6 ms), which is why it is only the GPU form; the CPU keeps
  eve-emu's span renderer.

## Conclusions

- A bit-exact GPU renderer for everything R-Type and Zuma draw is feasible with one kernel
  and one shared integer core; remaining features (edge strips, text formats, rotation,
  swizzle, CSPREAD) are small additions or stay on the CPU fallback.
- Windows / Linux: the same core compiles for Vulkan (GLSL/HLSL to SPIR-V, `shaderInt64`
  for line distances), D3D12 (HLSL SM 6.0) and CUDA; see 09.
