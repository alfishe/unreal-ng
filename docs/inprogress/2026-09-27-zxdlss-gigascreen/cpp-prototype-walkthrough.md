# ZX DLSS `mod-tpgw` - C++ prototype and its optimization, step by step

A record of phase 2 of POC 019 (2026-09-29): how the accepted Python algorithm
`mod-tpgw` became a C++ module and a tool, how it was verified frame-exact,
and how it went from ~170 ms to 2.85 ms per frame without changing a single
output pixel. Every number below was measured; the commands to reproduce them
are in section 8.

- Algorithm: [algorithm-mod-tpgw.md](algorithm-mod-tpgw.md)
- Code: [tools/verification/zxdlss](../../../tools/verification/zxdlss/README.md)
- Python reference: [tools/poc/019-zxdlss-gigascreen](../../../tools/poc/019-zxdlss-gigascreen/README.md)

## 1. What was built

| Part | Purpose |
|---|---|
| `zxdlss_algo` | algorithm module: interface `Algorithm` (`delay()`, `process(frame) -> RGB`), self-registering algorithms, no emulator dependency. Since moved into the core (`core/src/emulator/video/zxdlss`, explicit registration) to run live as a temporal effect |
| `mod-tpgw-ref` | the specification implemented literally, scalar, no tricks: the C++ reference |
| `mod-tpgw` | the optimized implementation; must equal `mod-tpgw-ref` bit for bit |
| `raw` | no processing (checks the tools and the video path) |
| `zxdlss-render` | TTD file (replayed through the core) or exported clip -> algorithm -> H.264 video and/or an exact RGB dump |
| `zxdlss-bench` | per-frame and per-stage cost, input decoded up front |
| `scripts/compare_dump.py`, `scripts/parity.sh` | frame-exact comparison, C++ vs Python on all golden scenes |
| core: `TimeTravelManager::VisitComposedFrames` | walks a TTD range and hands each frame's final picture and plane B to a callback; `ExportClip` now uses it too |

## 2. Test setup

- Machine: Apple M1 Ultra (20 cores), macOS, Release build (`-O3 -DNDEBUG`), clang.
- Benchmark scene: `ate-irregular`, clip frames 12100..12300 (201 frames) - the
  heaviest golden scene: the two-page field render runs on almost every frame.
- `zxdlss-bench` decodes the clip first and times only `Algorithm::process`;
  3 runs, best and median reported; stage timers inside the algorithm.
- Frame budget at 50 Hz: 20 ms.
- Caveat: rounds 2 and 3 were measured while the Python parity jobs ran in the
  background (several NumPy processes). Their absolute numbers are inflated and
  noisy; the ordering of the stages is still meaningful. Rounds 4 and 5 ran on a
  quiet machine (at most one background process).

## 3. Correctness first: frame-exact parity

The Python implementation is the reference. A mismatch in a DECISION (which
pixel mixes, with which frames) would show as a large difference; the only
tolerated difference would be a last-bit rounding of a 3+ frame sum. In the
end not even that was needed.

| Check | Frames | Differing pixels | Max difference |
|---|---|---|---|
| `mod-tpgw-ref` vs Python, spiral 12100..12160 (first run) | 61 | 0 | 0 |
| `mod-tpgw-ref` vs Python, all 9 golden scenes | 3 513 | 0 | 0 |
| `mod-tpgw` (every optimization round) vs `mod-tpgw-ref`, spiral | 201 | 0 | 0 |
| `mod-tpgw` (final) vs Python, all 9 golden scenes | 3 513 | 0 | 0 |
| `mod-tpgw` from the TTD file vs from the clip, flicker test | 1 905 | 0 | 0 |

The golden scenes: 8 Across the Edge clips of 201 frames and the flicker test
(1 905 frames), see the spec section 10.1.

Why it could be bit-exact: the linear-light mix uses the same formula, double
precision, the same `pow`, and round-half-to-even (`std::nearbyint` = NumPy's
`round`). Sums of 3..5 frames are order-sensitive; the C++ sums the frame t
term first, then the other frames by ascending ring index - which turned out to
be the order the Python dictionary produced on every golden frame.

## 4. Round 1 - the scalar reference (`mod-tpgw-ref`)

A literal transcription of the spec: every pixel, every shift, `mod` for every
wrapped coordinate, `pow` for every mixed pixel.

61 spiral frames, one run, quiet machine:

| Stage | ms/frame |
|---|---|
| push + keys | 0.44 |
| translation (for the P >= 3 veto) | 82.34 |
| pixel stage (includes the translation above) | 102.11 |
| render stage 1 | 1.92 |
| tile features | 0.02 |
| field stage | 0.40 |
| render field (two-page block motion + mix) | 64.64 |
| **total** | **169.6** |

The Python reference on the same scene: 244-375 ms/frame (NumPy, `run.py`).

Findings: two brute-force searches over 289 shifts dominate - the translation
detector (32x32 tiles, one frame pair per frame) and the two-page block motion
(8x8 blocks). Both did a `mod` per pixel per shift. The period runs re-compared
keys over the whole 23-frame ring for every pixel on every frame.

## 5. Round 2 - algorithmic restructuring (same results)

| Change | Idea |
|---|---|
| rolling per-pixel masks | bit j of `eq[P]` = key(j) == key(j+P), bit i of `mot` = translation(i); a new frame shifts the masks and adds 4 key compares per pixel; a period run test becomes `(eq & mask) == mask` |
| translation once per frame | only the newest frame pair is new; older pairs live in the `mot` mask |
| horizontally pre-shifted rows | 17 copies of the previous frame, one per dx; every shift then compares two contiguous rows - no `mod` in the inner loop |
| SWAR byte compare | differing bytes of two 8-byte words: `t = a ^ b; t |= t>>4; t |= t>>2; t |= t>>1; popcount(t & 0x01..01)` |
| mix tables | pairs 16^2, triples 16^3, the two-page mix 16^3, quadruples 16^4, quintuples on demand - each entry computed once with the reference's summation order |
| frame buffer reuse | the oldest ring frame's buffers are reused for the new frame |

201 spiral frames, 3 runs (background load, see section 2):

| Stage | ms/frame |
|---|---|
| push + keys | 0.39 |
| masks + translation | 7.22 |
| pixel stage | 7.98 |
| render stage 1 | 0.25 |
| tile features | 0.02 |
| field stage | 0.52 |
| render field | 5.72 |
| **total** | **best 17.6, median 22.1** |

The first run of the first measurement showed `push + keys` at 13 ms: a cold
start (first allocations, page faults) that the repeated runs did not have.
Bit-exact against `mod-tpgw-ref`.

## 6. Round 3 - skip static pixels, threads, compiler-vectorized loops

| Change | Idea |
|---|---|
| static-pixel skip | rolling mask `c1`: bit j = key(j) == key(j+1). A pixel equal to its neighbor frame over the whole ring has no non-constant run - skipped outright (most pixels of most frames) |
| constant test from `c1` | a run is constant iff key(a) == ... == key(a+P-1), i.e. bits a..a+P-2 of `c1` - no key reads |
| threads | the 289-shift searches split by shift, the pixel stage by rows; `ZXDLSS_THREADS` (default: hardware threads, at most 8). The block search keeps the first strict minimum per thread chunk and merges the chunks in shift order, so ties resolve exactly as in the reference |
| byte loops for the compiler | `c += a[i] != b[i]` over a tile row instead of the SWAR trick, hoping for automatic NEON |

201 spiral frames, 3 runs (background load):

| Stage | 1 thread | 8 threads |
|---|---|---|
| push + keys | 4.36 | 0.63 |
| masks + translation | 16.98 | 6.36 |
| pixel stage | 4.52 | 1.30 |
| render stage 1 | 0.50 | 0.96 |
| tile features | 0.03 | 0.04 |
| field stage | 0.86 | 0.28 |
| render field | 20.34 | 6.84 |
| **total** | **best 47.6** | **best 13.1** |

Findings:
- the static skip and the `c1` constant test halved the pixel stage even under load;
- the "vectorizable" byte loops were SLOWER than the SWAR trick in one thread
  (translation 7.2 -> 17.0 ms, render field 5.7 -> 20.3 ms): the compiler widened
  the bool-to-int accumulation instead of using byte compares + horizontal adds.
  Lesson: for byte compare + count, write the SIMD explicitly.
Bit-exact against `mod-tpgw-ref`.

## 7. Round 4 - explicit NEON / SSE2 kernels (`simd.h`)

| Kernel | NEON (arm64) | SSE2 (x86-64) | Fallback |
|---|---|---|---|
| differing bytes in a 32-pixel tile row | `vceqq_u8` x2, `vshrq_n_u8(eq, 7)`, `vaddvq_u8` | `_mm_cmpeq_epi8` x2, `_mm_movemask_epi8`, `popcount` | SWAR x4 |
| differing bytes per 8-pixel block, two blocks per 16 bytes | `vceqq_u8`, `vshrq_n_u8`, `vaddv_u8` of each half | `movemask`, `popcount` of each byte of the mask | SWAR |

201 spiral frames, 3 runs:

| Stage | 1 thread | 8 threads |
|---|---|---|
| push + keys | 0.30 | 0.31 |
| masks + translation | 2.85 | 2.15 |
| pixel stage | 1.17 | 0.36 |
| render stage 1 | 0.18 | 0.19 |
| tile features | 0.01 | 0.01 |
| field stage | 0.36 | 0.37 |
| render field | 3.58 | 2.02 |
| **total** | **best 8.44, median 8.46** | **best 5.40, median 5.41** |

Bit-exact against `mod-tpgw-ref`.

## 8. Round 5 - shifted rows with two `memcpy` segments

The pre-shifted copies (17 for the translation, 2 x 9 for the two-page search)
were still built pixel by pixel with a `mod`. A row shifted by s is two
contiguous pieces of the source row: `memcpy(dst + s, src, w - s)` and
`memcpy(dst, src + w - s, s)`.

201 spiral frames, 3 runs:

| Stage | 1 thread | 8 threads |
|---|---|---|
| push + keys | 0.30 | 0.30 |
| masks + translation | 1.53 | 0.82 |
| pixel stage | 1.17 | 0.34 |
| render stage 1 | 0.18 | 0.18 |
| tile features | 0.01 | 0.01 |
| field stage | 0.36 | 0.36 |
| render field | 2.40 | 0.84 |
| **total** | **best 5.91, median 5.95** | **best 2.85, median 2.85** |

Bit-exact against `mod-tpgw-ref`; the final build also bit-exact against Python
on all nine golden scenes (section 3).

## 9. Summary

| Implementation | ms/frame (spiral) | vs Python | share of a 50 Hz frame |
|---|---|---|---|
| Python reference (NumPy) | 244-375 | 1x | 12-19 frames |
| C++ scalar reference (`mod-tpgw-ref`) | 169.6 | ~2x | 8.5 frames |
| round 2: masks, pre-shifted rows, SWAR, mix tables | 17.6 (loaded machine) | ~17x | 88 % |
| round 3: static skip + threads (8) | 13.1 (loaded machine) | ~23x | 66 % |
| round 4: explicit NEON, 1 thread | 8.44 | ~35x | 42 % |
| round 4: explicit NEON, 8 threads | 5.40 | ~55x | 27 % |
| round 5: `memcpy` row shifts, 1 thread | **5.91** | **~50x** | **30 %** |
| round 5: `memcpy` row shifts, 8 threads | **2.85** | **~105x** | **14 %** |

Per stage, from the scalar reference to the final single-thread build:

| Stage | scalar | final, 1 thread | speed-up |
|---|---|---|---|
| translation (+ masks) | 82.3 | 1.53 | 54x |
| pixel stage (without translation) | ~19.8 | 1.17 | 17x |
| render stage 1 | 1.92 | 0.18 | 11x |
| render field | 64.6 | 2.40 | 27x |
| push + keys | 0.44 | 0.30 | 1.5x |

The TTD path: `zxdlss-render --ttd flickering_test.ttd --layout raw-out --video ...`
rendered 1 912 frames in 32.5 s end to end (TTD replay through the emulator core,
the algorithm at 2.95 ms/frame, H.264 encoding) -
`tools/poc/019-zxdlss-gigascreen/out/2026-09-29_1135 - C++ zxdlss-render full renders/flicker-test_raw-out.mp4`.

## 10. Lessons

1. **A literal reference first.** `mod-tpgw-ref` made every optimization round a
   one-command, frame-exact check, without the slow Python in the loop.
2. **Restructure before vectorizing.** Rolling masks and computing each frame
   pair once gave more than SIMD alone could: the pixel stage went from a ring
   re-scan to a mask test.
3. **Write byte compare + count SIMD by hand.** The compiler's automatic
   vectorization of `c += a[i] != b[i]` lost to a scalar SWAR trick; explicit
   NEON / SSE2 won by 3-8x.
4. **Keep tie order under threads.** A parallel argmin must merge in the
   sequential order, or equal costs pick different vectors and the output
   changes.
5. **Measure on a quiet machine.** Background NumPy jobs moved single numbers by
   2-3x; stage proportions stayed useful, absolute numbers did not.

## 11. mod-tpgwa and Pentagon overscan (2026-09-29)

**The scene stage** (spec section 7.8) is a flag of the same class: `mod-tpgwa`
registers `ModTpgw(true)`. It runs after the field stage and, while on, writes
every pixel from the two-page mix table (1/2 t + 1/4 t-1 + 1/4 t+1 is the same
weights and summation order), so no new table is needed. For the parity the
Python pipeline now builds a whole-frame recipe in its own source order.

Optimizations (same results):
- the 7-frame constant / period-2 / dyn tests run on threads by tile rows (each
  thread owns whole rows of tiles, no merge);
- the average render runs on threads by rows;
- the scene decides before the field render: while it is on, the two-page
  render (the block search) is skipped - its output would be overwritten. The
  field stage's state is still updated.

Parity: Python vs C++ bit for bit on the DJ scene (201 frames) and the spiral;
`mod-tpgwa` equals `mod-tpgw` bit for bit on the other nine golden scenes.
Cost of the scene stage over the whole demo: 0.32 ms/frame before the threads
(measured on a loaded machine, load average ~46 - to be re-measured).

**Pentagon overscan** (`--overscan`): the TTD is replayed in M_P384 (384 x 304)
and cropped to 352 x 304 like the emulator's Symmetric Horizontal viewport.
`FrameInput` carries the paper origin ((48, 48) standard, (48, 56) overscan);
a paper tile is one whose center lies on the paper, which keeps the standard
frame's tiles (rows 3..14, columns 3..18) and the output unchanged.

**Sound in the mp4.** The sound pass runs before the picture walk and writes a
temporary WAV next to the video; ffmpeg takes the RGB frames on stdin and the WAV
as a second input and muxes AAC into the same mp4, then the WAV is removed
(`--audio` keeps it). A named pipe would avoid the file but is POSIX-only; kept
as a file (see the tool README, "Sound").

`--stats` prints the stage costs, the share of pixels and frames each detector
mixed, and the runs of the scene stage.

## 12. Next

- Cache the translation of a frame pair across frames (already done for the
  mask; the two-page search is recomputed per frame and could reuse the
  previous frame's t+1 / t-1 pair half the time).
- Move the remaining per-pixel loops (key building, the final two-page gather)
  to SIMD.
- Phase 3: the algorithm inside the emulator as the `dlss` mode of the Temporal
  Effects Manager, with its 6-frame presentation delay.

## 13. Reproduce

```
# build
cmake -S . -B build -DBUILD_ZXDLSS_TOOLS=ON && ninja -C build zxdlss-render zxdlss-bench

# benchmark (single thread / default threads)
ZXDLSS_THREADS=1 build/bin/zxdlss-bench --clip <poc>/data/clip_v2 --from 12100 --to 12300 --repeat 3
build/bin/zxdlss-bench --clip <poc>/data/clip_v2 --from 12100 --to 12300 --repeat 3
build/bin/zxdlss-bench --clip <poc>/data/clip_v2 --from 12100 --to 12160 --repeat 1 --alg mod-tpgw-ref

# optimized vs reference
build/bin/zxdlss-render --clip <poc>/data/clip_v2 --from 12100 --to 12300 --alg mod-tpgw-ref --dump ref
build/bin/zxdlss-render --clip <poc>/data/clip_v2 --from 12100 --to 12300 --alg mod-tpgw --dump opt
python3 tools/verification/zxdlss/scripts/compare_dump.py ref opt --tolerance 0 --max-share 0

# C++ vs Python on the nine golden scenes
tools/verification/zxdlss/scripts/parity.sh <poc code dir> <poc data dir> build/bin <scratch dir>

# TTD file -> video
build/bin/zxdlss-render --ttd <poc>/data/flickering_test.ttd --layout raw-out --video flicker.mp4
```
