# Performance: drawing R-Type from a bus capture

Date: 2026-10-02. Branch `perf-replay`. What was measured, how, what changed, and what
comes next.

## 1. The workload

Two captures of the TS-Labs R-Type (VDAC2 build) made in unreal-ng with its bus capture
(`.evr`, every byte on the FT812's bus with the chip's answer and a picture hash per
frame; see the README's `eve-replay`):

| Capture | Content | Chip time | Frames | Bus bytes | Size |
|---|---|---|---|---|---|
| `rtype-boot.evr` | loading to the title screen | 17.8 s | 1 032 | 1.73 M | 8.3 MB |
| `rtype-play.evr` | about 6 minutes of play, levels loaded in between | 409.8 s | 24 193 | 33.1 M | 144 MB |

The game draws 1024 x 768 at 59 Hz (72 MHz system clock): full-screen RGB565 and
PALETTED4444 backgrounds scaled from smaller bitmaps (matrix A = 0.5, E = 0.33...), a
tiled background with REPEAT on both axes, L1 masks accumulated into alpha (ONE / ONE)
with the picture drawn through them (DST_ALPHA / ZERO, ONE_MINUS_DST_ALPHA / ONE), and
paletted sprites.

Captures are local files (game data); they are not in the repository.

## 2. Method

- `eve-replay <capture> --rom ft81x.rom --no-hash` replays a capture and reports the time
  spent in `EveAdvance` (where every line is drawn) and the process's CPU time. The chip's
  real time over the library's time is the **real-time factor**: 1.0 means one host core
  just keeps up with the chip.
- Without `--no-hash` the replay also checks every answer byte, every frame count and
  every drawn picture against the capture: every optimization below kept all of them
  equal, on the whole 6-minute capture.
- Release builds, Apple M1 Ultra, the machine shared with other builds (load 40-120 during
  the runs): the figures are CPU time of one thread, not wall time.

## 3. Results

Time in `EveAdvance` (the drawing), lower is better; real-time factor in brackets:

| Build | Boot (17.8 s of chip time) | Play, first 4000 frames (68.0 s) | Whole play capture (409.8 s) |
|---|---|---|---|
| `main` 5dd1c4c (before) | 23.6 s (0.75x) | 110.9 s (0.61x) | - |
| `perf-replay`, NEON | **3.76 s (4.7x)** | **15.1 s (4.5x)** | **89.7 s (4.6x)** |
| `perf-replay`, `EVE_SIMD=OFF` | 3.84 s (4.6x) | 24.8 s (2.7x) | - |

So one core draws R-Type 6-7 times faster than before, about 4.5 times faster than the
chip: the FT812 in step with an emulated machine now takes about a fifth of a core. The
plain C++ fallback alone is 4.5 times faster than before; the SIMD kernels add the rest on
the play capture, where the default blend dominates.

All three kernel variants replay `rtype-boot.evr` with no difference: NEON (macOS arm64),
SSE2 (MinGW x86-64 build run under Wine) and the C++ fallback. The test suite (162 tests,
the BT8XX golden cases included) passes with clang (NEON), clang with `EVE_SIMD=OFF` and
gcc 16; the MinGW build has no warnings.

## 4. Where the time went, and what changed

Profiles (`sample`) of the replay, in the order the changes were made:

| Step | Hot spots before | Change | Boot CPU |
|---|---|---|---|
| 0 | `Shade` 31 %, `WrappedTexel` 17 %, `Direct` 10 %, `DrawBitmap` 7 % | - | 27.1 s |
| 1 | almost every bitmap pixel on the general path: scaled (B = D = 0 but A, E != 1) and L1 / L4 bitmaps were not on the fast path | fast path for any axis-aligned matrix (scaling, mirroring) and for L1 / L2 / L4 / RGB332 / ARGB2 (decode tables) | 16.7 s |
| 2 | `Shade` per pixel, `Multiply` not inlined (another translation unit) | `ShadeSpan`: the pipeline decided once per span; `Multiply` inline | 13.9 s |
| 3 | the three masking blends R-Type uses, in the general blend | tight loops for ONE / ONE (alpha only), DST_ALPHA / ZERO and ONE_MINUS_DST_ALPHA / ONE (RGB only); the tag written once per span | 9.5 s |
| 4 | the play capture: the tiled background (REPEAT) on the general path; palettes decoded with `Direct` per span | REPEAT on the fast path (the general path's wrap arithmetic); palettes from the decode tables | - |
| 5 | `OutputLine` per pixel | `Simd::RgbaToArgb` (NEON `vld4` / `vst4`, SSE2 shifts) for the usual case: no mirror, no CSPREAD, pins in order | - |
| 6 | the span's texel buffer on the stack (16 KB: a stack probe per bitmap on macOS); the format chosen per texel; the default blend per pixel | the buffer in the chip (`lineTexels`); one decode loop per format and wrap mode; `Simd::BlendSrcAlpha` for the default blend | - |

Boot CPU above includes the harness's picture hashing (about 4 s); the table in §3 is the
library alone.

`Simd::BlendSrcAlpha` computes `min(s * a / 255 + d * (255 - a) / 255, 255)` per channel
with each product rounded as `Multiply` does, `(x + 127) / 255`, as
`(t + 1 + (t >> 8)) >> 8` with `t = x + 127`: equal to the division for every `t` the
blend can produce (0...65 152, checked exhaustively). The same formula gives a = 0 (the
destination stays) and a = 255 (the source replaces it) exactly, so the kernel needs no
branches. With `kMultiplyRoundDiv255` off the old per-pixel blend runs.

Where a replay of the play capture spends its time now (samples of the drawing): `DrawBitmap`
(the span setup and the blend kernels inlined) about 48 %, the per-format decode loops
about 36 %, `ExecuteLine` (display list interpretation) about 10 %, the output conversion
about 3 %.

## 5. Next

### 5.1 More single-thread work (small, safe)

- SIMD for the three masking blends and for `COLOR_RGB` modulation (the kernels have
  scalar loops marked `SIMD-CANDIDATE`).
- Contiguous decode when A = 1 (identity): texels in order, so 16-bit formats decode with
  vector shifts instead of table lookups.
- Points, lines and rectangles still shade pixel by pixel; R-Type uses few, other software
  may use more.

### 5.2 Threads

The lines of one frame can be drawn in parallel, with two conditions:

1. **Bitmap handles.** `BITMAP_*` commands in the display list write the handle table,
   which is chip state that persists from line to line. A display list sets the same
   values on every line, so after its first line the table is a fixed point. A batch can
   draw its first line serially, then the rest in parallel, each worker on a private copy
   of the table; if any copy changed, the batch is drawn again serially. The result stays
   exact either way.
2. **Batches.** Today a line is drawn as soon as the beam reaches it, and a host advances
   the chip at every bus byte, so a call to `EveAdvance` covers only a few lines. Drawing
   lazily makes the batches whole frames: lines that are due are only counted, and drawn
   before anything they read can change (a host or coprocessor write to RAM_G, the active
   display list, a register the drawing reads), at the frame end, and before `REG_TAG` is
   read. Memory does not change between those points, so the lines see what they would
   have seen in step with the beam.

Each worker needs its own line buffers (color, stencil, tag, texels, palette cache) and
writes only its lines' frame buffer rows and line costs; the overflow count and `REG_TAG`
are merged after the batch. API: `EveSetThreads(chip, n)`, 1 by default, so a host that
does not ask keeps today's single-threaded, deterministic behavior. Expected gain: close to
linear in cores for R-Type-like frames (lines are independent and similar in cost).

### 5.3 GPU

The heavy part (sampling, masks, blends over 1024 x 768) maps onto a compute shader, but
the results must stay exact: integer arithmetic with `Multiply`'s rounding, BT8XX's
antialiasing table, the fill and wrap rules. That rules out the fixed-function blending
of a graphics API and needs integer compute shaders (Metal, Vulkan, D3D12), with this CPU
path as the fallback and as the reference the GPU output is compared with. Worth it only
if threads are not enough: at 4.5x real time on one core, they probably are.
