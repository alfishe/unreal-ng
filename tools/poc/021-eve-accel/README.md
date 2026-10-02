# eve-accel - accelerating the FT812 renderer (eve-emu): proof-of-concept experiments

Date: 2026-10-02. Host: Apple M1 Ultra (20 cores, 64-core GPU), macOS. The library is the
vendored eve-emu (`core/src/3rdparty/eve-emu`, commit d7d28e2), never modified: every
experiment builds it with some files replaced by its own copies (see `CMakeLists.txt`).
Workloads: bus captures of R-Type (boot, 6 minutes of play) and Zuma (game data, not in the
repository). Gate everywhere: the accelerated path must reproduce eve-emu bit for bit
(eve-replay's answer, frame-count and picture-hash checks, or a pixel-by-pixel compare).

Measurement conditions: the machine was shared with other agents' builds for the whole
session (1-minute load 37-195, never below the 12 the method asks for). CPU-time figures
repeat within a few percent; wall times scatter 2-4x. The final timing pass was stopped on
the user's request, so several figures come from single development runs; each README says
which. Every folder has the scripts to re-run its measurements on a quiet machine.

```
tools/poc/021-eve-accel/build.sh                 # all variants, Release, -j = half the cores
tools/poc/021-eve-accel/common/replay.sh <variant> boot|play|zuma [eve-replay options]
tools/poc/021-eve-accel/run-all-timings.sh       # every timing, in sequence
```

## The three emulation profiles

| | FULL - maximum accuracy | OPTIMAL - the one to build | CHEAP - almost a slideshow |
|---|---|---|---|
| emulates | the documented line pipeline: per-line budget, the overflow cut point (08) | eve-emu's renderer bit-exact, on a native GPU backend per batch of lines where it pays, else the SIMD CPU renderer in parallel; coprocessor commands native, charged measured durations (10) | eve-emu with exact skipping of unchanged frames, plus optional draw-1-in-N / half vertical resolution (11) |
| expected cost | closed-form budget: < 1 % of a core on top of OPTIMAL; a literal clock-stepped model 7-24 % of a core in bookkeeping alone | R-Type: GPU 0.3-1 ms per frame (4-10x below one core); Zuma's BILINEAR frames 0.64 ms vs 28 ms; CPU fallback 3.6-6x faster with 8 threads | skip-unchanged removes 35-39 % of boot's lines for free; draw-every-4: est. ~10 % of a core for R-Type |
| accuracy loss | none (what an overflowing line shows must be measured on a card) | none (bit-exact on every capture, whole 410 s play capture included); coprocessor durations are approximate until measured | none for skip-unchanged; stale or line-doubled pictures for the others; chip timing answers stay exact |
| portability | plain C++ | Metal (here); Vulkan (native driver) / D3D12 / CUDA from the same integer core (09); CPU fallback everywhere (NEON / SSE2 / C++) | plain C++ |
| backed by | 08, 06 | 01, 02, 03, 04, 04a, 04b, 04c, 06, 07, 09, 10 | 02, 11 |

Firmware-level execution: studied separately, not in this repository.

## Index

| # | Experiment | Question | Verdict | One-line result |
|---|---|---|---|---|
| 01 | [baseline](01-baseline/README.md) | How fast is eve-emu on one core, where does the time go? | reference | R-Type 3.5-4.8x real time; **Zuma 1.2x** (BILINEAR on the general per-pixel path, 85 % of samples) |
| 02 | [dl-swap-census](02-dl-swap-census/README.md) | How often does something a line reads change mid-frame? | batches are large | 70-95 % of frames are one batch, 80-100 % with the read-set test; **no `DLSWAP_LINE` in any capture**; splits come from `RAM_G` writes, mostly outside what is drawn |
| 03 | [line-parallel-cpu](03-line-parallel-cpu/README.md) | Lines in parallel with a thread pool, exact? | **yes, do it** | bit-exact on all captures (whole play capture); serial part 0.13-0.95 % with deferral; 5-6x at 8-16 threads on boot (noisy machine) |
| 04 | [metal-compute](04-metal-compute/README.md) | Can a GPU kernel reproduce eve-emu exactly? | **yes** | all bitmap formats/filters/blends/stencil + AA points/lines/rects; 125/125 frames bit-exact (C++ and Metal from one integer core) |
| 04a | [frame-per-dispatch](04a-frame-per-dispatch/README.md) | One dispatch per frame: cost? | **viable** | GPU 0.12-0.49 ms median per frame vs 0.3-5.1 ms on one core; submit + wait 0.7 ms+ dominates |
| 04b | [line-per-dispatch](04b-line-per-dispatch/README.md) | One dispatch per line / band: overhead? | viable only without waits | 768 barrier-free line dispatches = one frame (0.76 ms); serial 29 us per line; commit + wait 2.3 ms per line; 256 threads per pixel ~50x |
| 04c | [hybrid-change-points](04c-hybrid-change-points/README.md) | Frames on the GPU, split at change points, CPU fallback, inside eve-emu? | **yes, the design** | bit-exact over the whole 410 s play capture, 90-97 % of lines on the GPU; per-batch cost rule; the synchronous wait is the weak spot |
| 05 | [opengl](05-opengl/README.md) | GL 4.1 fragment shader? | works, not recommended | bit-exact on the frames tried; deprecated, a layer over Metal on macOS |
| 06 | [gpu-aa-256](06-gpu-aa-256/README.md) | 256-subpixel coverage vs the distance table? | **keep the table** | table: 37 of 4 M golden pixels differ (one TO VERIFY case); box 256: ~200 000; on the GPU both are cheap |
| 07 | [whole-frame-gpu](07-whole-frame-gpu/README.md) | Kernel count, switch overhead, latency? | one kernel | per-op kernels 2x GPU time and growing with the list; end to end ~1-2 ms per frame on an idle machine |
| 08 | [hardware-timing-model](08-hardware-timing-model/README.md) | Cost of a hardware-level line pipeline? | closed form | budget + overflow cut in closed form < 1 % of a core; clock stepping 7-24 %; a thread per line on the GPU 25-30x slower than a thread per pixel |
| 09 | [native-backends](09-native-backends/README.md) | One code base on Metal / Vulkan / D3D12 / CUDA + CPU fallback? | design | shared C-like integer core; thin backend (~250 lines); fallback at start-up, per batch by capability and by measured cost |
| 10 | [coprocessor-timing](10-coprocessor-timing/README.md) | Native coprocessor commands, real durations? | numbers missing | eve-emu has the cost machinery, all costs 0; measurement plan for a VDAC2 board |
| 11 | [cheap-profile](11-cheap-profile/README.md) | Cheapest picture that changes? | skip-unchanged | exact skipping of 35-39 % of lines; inexact options keep chip answers exact |

## TS-Labs' two hypotheses

1. *"Multithreading is possible only for several screen lines in parallel, and not when the
   display list is swapped per line."* **True as stated, harmless in practice.** Parallelism
   is across lines (03); a line swap ends a batch. No capture swaps per line (02); the real
   batch boundaries are `RAM_G` writes, and most of them can be skipped because they do not
   touch anything the active list reads (02, 03).
2. *"On a GPU one could compute 256 sub-pixels honestly, but kernel switching would eat the
   gain."* **Half true.** 256 subsamples are affordable on the GPU (06, 1.3-1.5x the table)
   but they are the wrong model: box coverage misses the BT8XX goldens by ~5 % of all
   pixels, the distance table matches them. Kernel switching costs ~3 us per serial
   dispatch and a CPU wait costs 0.7-2.3 ms (04b, 07), but one kernel per frame or per band
   - or barrier-free band dispatches recorded together - avoids switching entirely.

## Recommendation

1. **In eve-emu, first (CPU, exact, portable):** the line-parallel renderer of 03
   (`EveSetThreads`, default 1), the read-set deferral of `RAM_G` writes (02/03), the exact
   skip of unchanged frames (11), and a BILINEAR fast path with SIMD (01: Zuma's frames are
   28 ms on one core today).
2. **Then the GPU backend (OPTIMAL):** the per-batch hook of 04c with the shared integer core
   (`common/eve-ops-core.h`), Metal first; collect rows asynchronously instead of waiting
   per batch; ship precompiled shader libraries; keep the CPU renderer as the reference and
   the fallback, chosen at start-up and per batch by capability and measured cost (09).
   Add edge strips, text formats and `REG_CSPREAD` / `ROTATE` / `SWIZZLE` to the kernel or
   leave them on the CPU.
3. **Coprocessor:** keep native commands; measure command durations on a VDAC2 board and
   fill eve-emu's cost table (10). Measure the overflowing-line look and the budget
   overhead on the card (08).
4. **On Windows (AMD Ryzen, Windows 11) and NVIDIA / Linux:** port the backend to Vulkan
   (native driver) first, D3D12 or CUDA if needed (09); re-run 04 (snapshots), 04a, 04b and
   04c `check` / `time`, and 03 `scale` on a quiet machine. Watch the PCIe upload of dirty
   pages and the 3 MB readback on a discrete GPU.
