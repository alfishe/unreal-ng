# ZX DLSS — Optimization Ideas Backlog

**Created:** 2026-09-27
**Status:** collecting — nothing here is scheduled

**Rule (R-26):** implement the naive version first, measure it on the test
corpus and benchmarks, then pick optimizations from this list by measured
benefit. An idea leaves this list only with a benchmark showing the gain and
the quality tests still green.

| # | Area | Idea | Expected gain | Notes |
|---|---|---|---|---|
| O-1 | Triggers | Prefer frame-level forms (PC sampled at INT, HALT address, paging state, page content hash at frame boundary) over per-instruction exec triggers | removes the ≈ 1 % per-instruction prefilter for most packs | TR-13; one-frame precision is enough for GigaScreen |
| O-2 | Triggers | Arm the per-instruction prefilter only while a pack with exec triggers is active | zero cost otherwise | R-24 |
| O-3 | Analysis | Whole-frame early-out: nothing changed in the window → copy through | static frames cost one compare | design-analysis §4 |
| O-4 | Analysis | Segment signature compares as 16-bit SIMD lanes (6144 segments per frame) | large | natural SIMD shape |
| O-5 | Analysis | Motion search only for regions of unexplained segments; bitmap XOR + popcount before color verification | large on mostly static screens | design-analysis §7 |
| O-6 | Analysis | Hybrid pack mode: verify the known period under the known mask instead of detecting | large for recognized software | R-22 |
| O-7 | Mixers | Bake mixer results into LUTs keyed by palette-index multisets | formulas cost nothing per frame | design-mixers §5 |
| O-8 | Mixers | Separable mixers (transfer → weighted mean → inverse) evaluated directly in SIMD/GPU | avoids the 10 MB LUT for frequency weights | design-mixers §5 |
| O-9 | Look-ahead | Shadow skips RGBA rendering when only the meaning plane is consumed | shadow frame cost down | LA-14 |
| O-10 | Look-ahead | Dirty-page snapshot copy instead of full-RAM memcpy (needs a fast-path write hook) | snapshot cost on 1–4 MB machines | LA-4 |
| O-11 | Look-ahead | Resync only on input change; steady state advances one frame | avoids N-frame bursts | LA design §2.4 |
| O-12 | GPU | Present directly from the GPU texture; read back into the output framebuffer lazily, only when a CPU consumer asks | removes per-frame readback | output contract R-25 unchanged |
| O-13 | Plane B / raw | Capture plane B and keep the raw framebuffer copy only while DLSS is on | zero cost when off | R-24, R-25 |
| O-14 | Metadata | Coalesce observations into intervals; content-addressed mask blobs | small memory and files | MD-4 |
| O-15 | Border | 1-D beam-order processing; uniform-border fast path | cheap border handling | design-analysis §8 |
| O-16 | Threads | Horizontal bands per core for classification and composition; regions crossing bands in a second pass | scales with cores | R-8 |
| O-17 | SIMD | AVX2 paths with runtime CPU dispatch for the hottest loops | wider lanes on x86 | only if SSE measurements justify it |
| O-18 | SIMD | Scene stage (mod-tpgwa, spec 7.8): 16-byte compares for the 7-frame constant / period-2 / dyn tests and per-tile counts | the scene features are per-pixel compares over 7 frames | measure first; the stage already runs by tile rows on threads |

---

## SIMD candidates

**Convention.** Every loop or function written in scalar form that is expected
to benefit from SIMD gets a comment tag at the time it is written:

```cpp
// SIMD-CANDIDATE(O-4): 6144 independent 16-bit compares per frame; SSE2/NEON 8 lanes.
```

- Tag format: `SIMD-CANDIDATE(<idea id>): <why it vectorizes, data shape, lane width>`.
- `grep -rn "SIMD-CANDIDATE" core/src` lists all of them; the table below is
  kept in sync when code lands (a review checklist item).
- When a candidate gets its SIMD path, the tag becomes
  `SIMD(SSE2,NEON; scalar fallback): …` and the row moves to "done".
- Target instruction sets: SSE2/SSE4.1 (x86), NEON (ARM64), plain C++
  fallback. AVX2 with runtime dispatch is an idea for later (O-17).

| Candidate | Data shape | Why it vectorizes | Idea | Status |
|---|---|---|---|---|
| Plane B capture next to RGBA write in the ZX renderer | 8 pixels per segment | same loop already has a NEON path (`screenzx.cpp:985`) | O-13 | planned |
| Segment signature history compare (period / recurring-set tests) | 6144 × 16-bit per frame × up to 10 frames | independent lanes, compare + mask | O-4 | planned |
| Whole-frame "nothing changed" check | 6912 bytes screen + border | bulk compare | O-3 | planned |
| Bitmap motion search (XOR + popcount) | 1-bit plane, region × search window | wide XOR, popcount (`vcntq_u8` / SSE4.2 or table) | O-5 | planned |
| Composer: per-pixel weighted average of up to 10 samples | RGBA8 / float per pixel | uniform arithmetic per pixel | — | planned |
| Mixer transfer curves (sRGB ↔ linear) | per channel | LUT gather or polynomial | O-8 | planned |
| `blend` mode (whole-frame) | RGBA8 per pixel | uniform arithmetic | — | planned |
| `adaptive` mode per-pixel tests | RGBA per pixel, 3–6 frames | compares + selects | — | planned |
| Snapshot RAM copy / compare | 128 KB–4 MB | memcpy / memcmp | O-10 | planned |
| Border beam-order shift search | 1-D color-index sequence | sliding compare | O-15 | planned |
