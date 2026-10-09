# YM2203 channel decimators: prototype, quality and cost (2026-10-07)

Prototype of the frame-cost backlog items in [TODO.md](TODO.md) ("Profile the card's frame cost"): the
ZX-MultiSound renders eight channel streams per output sample through Reference-quality FIR decimators (six SSG
streams with 97-tap rows, two FM streams with 193-tap rows), and they were about 36 % of an all-sources card frame.
The same `Ym2203Pair` serves the TSFM board, and the same `FilterDecimator` serves every AY machine. Branch
`proto-ym-decimators` (on master `012418ef4`); owner rule 2026-10-07: into the main code base only if the quality
analysis is fully good.

**Landed: a + c0 (owner 2026-10-07); b and c kept as patches** in
[ym-decimator-prototype-patches/](ym-decimator-prototype-patches/) ([section 7](#7-the-prototype-as-patches); the branch
`proto-ym-decimators` is deleted). Branch `ym-decimators-a-c0` carries
variant a and variant c0 rewritten without b (a zero-run counter per SSG channel, the same as the FM one; no held
level, no `ssgLast`); the forced-scalar build switch of the prototype is gone (the tests call the scalar kernel
directly). Verification on that branch: [section 6](#6-landing-a--c0).

## Contents

- [1. Variants](#1-variants)
- [2. Quality](#2-quality)
- [3. Cost](#3-cost)
- [4. Recommendation](#4-recommendation)
- [5. How to repeat](#5-how-to-repeat)
- [6. Landing a + c0](#6-landing-a--c0)
- [7. The prototype as patches](#7-the-prototype-as-patches)

## 1. Variants

Each variant is a commit on the branch, on top of the previous one.

| Id | Commit | What changes | Where |
|:--|:--|:--|:--|
| a | `64c2e29a4` | One pass over the taps for every decimator that shares an output instant and a pair of coefficient rows (`FilterDecimator::getOutputs`), with SIMD dot-product kernels: NEON (aarch64), SSE2 (x86-64), a scalar fallback (`DecimatorDot`, `core/src/common/sound/filters/decimatordot.h`). The history ring is stored newest-first so the kernels load two taps per register in tap order. | MultiSound (8 streams), TSFM board (6), TurboSound / AY device (4) |
| a-scalar | same, built with a temporary switch that forced the scalar kernel (prototype only, not landed) | Variant a with the scalar kernel: the multi-stream pass alone, no SIMD (benchmark binary only) | as a |
| b | `943e21c60` | SSG "levels only" generator tick: `SoundChip_AY8910::updateStateLevels` ticks the generators and computes the three channel levels, without the pan sums and DC blockers of `updateMixer`. Used by `Ym2203Pair::renderChannels` only; the AY machines' `updateState` / `updateMixer` are untouched | MultiSound |
| c0 | `182103b36` | An SSG channel whose whole FIR window (`window()` = 101 samples) of fed levels is 0.0 is not evaluated; its output is +0.0 (the same rule the silent FM part already had). The master decimator consumes its phase through `FilterDecimator::skipOutput` | MultiSound |
| c | `b0fc99d86`, `bf80d53a5` | c0 generalized: any level held for the whole window is written as that level instead of the FIR output | MultiSound |

How the multi-stream pass keeps the bits. Every kernel keeps the original summation order per stream: four
interleaved accumulators per row (accumulator j sums the taps with i % 4 == j), the leftover tap goes to accumulator
0, the result is (acc0 + acc1) + (acc2 + acc3). The SIMD kernels keep accumulators (0, 1) and (2, 3) in two 2-lane
registers, which is the same order lane by lane. The only other source of difference is the multiply-add: GCC and
clang contract the original `acc += s * c` into one fused multiply-add where the target has one (aarch64; x86-64
built with `-mfma`), MSVC never does. `DecimatorDot::kFused` applies that rule in every kernel, scalar included, so
the default builds return the historical bits and the kernels agree with each other under any flags. Checked on this
Mac for arm64 and, under Rosetta, x86-64 with `-mfma`, `-march=x86-64-v3`, `-ffp-contract=off` and the forced scalar
kernel (prototype switch): 0 of 5600 kernel calls differ from the scalar one in every build (`scratch/ymq/dotx86.cpp`). Without the
rule (the first draft) x86-64 `-mfma` and arm64 `-ffp-contract=off` differed in 2347 / 2349 of 5600 calls.

SIMD-CANDIDATE(fir-decimator-dot) is resolved by variant a; the scalar fallback keeps the tag for targets without a
kernel.

## 2. Quality

Method:

- `Ym2203PairQuality_Test` (DISABLED_ tools in `core/tests/emulator/sound/tsfm/ym2203pairquality_test.cpp`):
  `CaptureTraces` records the timed #FFFD / #BFFD writes of real tunes on a TSFM Pentagon: `tech_support.sna`
  (1500 frames, 55 123 writes) and three TFM Music Compiler tunes of `TSFM-EL.TAP` (1000 frames each).
  `RenderTraces` plays every trace and four synthetic worst cases - constant SSG levels stepping every second,
  4-bit sample playback through the SSG volume at 15.6 kHz (fast volume changes), noise on all six channels with the
  period swept, a square tone swept from 109 kHz down plus an FM note - through the per-channel outputs of a
  `Ym2203Pair` (the card path: eight float streams) and through a `SoundChip_TurboSoundFM` (the board path: stereo
  mix, chip and FM buffers). The same traces through the base binary and each variant give the files that
  `scratch/ymq/compare.py` compares byte for byte, and with max abs error, RMS error in dBFS and the error power
  above 20 kHz where they differ.
- The golden digests: `MultiSoundCard_Test.YmRowsMatchTheirGoldenDigests`, `TsfmGolden_Test.*`, and the TTD corpus
  (`TTD_Corpus_Test`, `TimeTravelControllerCorpus_Test`, `TTDSessionFile_Test`, `TTDV1Feeder_Test`), plus every
  `*MultiSound*`, `*Tsfm*`, `*Ym2203*`, `*TurboSound*`, `*Ttd*`, `*Ay*` suite (1676 tests).
- `DecimatorDot_Test`: SIMD == scalar bit for bit (K = 1..4 streams, lengths 1..385); `getOutputs` == one
  `getOutput` per decimator for the card layout plus a standalone pair (30 000 outputs); for variant c, the FIR of a
  constant window against the level itself at every tabulated row and half-way between rows, all four whole-sample
  delays, for 44.1 / 48 / 96 kHz and both qualities.

Results (base = master `012418ef4`):

| Variant | Card rows (8 streams, 4 tunes + 4 synthetic) | TSFM board buffers | Golden digests, TTD corpus | Max abs error | RMS error |
|:--|:--|:--|:--|:--|:--|
| a | identical, all 8 cases | identical | pass | 0 | - |
| b | identical | identical (path untouched) | pass | 0 | - |
| c0 | identical | identical (path untouched) | pass | 0 | - |
| c | identical at the float output | identical (path untouched) | pass | 0 at the float output; in double at most 1.2e-15 (-298 dBFS) | - |

Variant c is the only one that is not bit-identical by construction: the FIR of a constant v is
v x sum(c) with its own rounding, which differs from v by at most 1.2e-15. The card's rows are float, and the cast
removes that difference:

| Check (variant c) | Outputs | Float outputs that differ from the level | Max abs error in double |
|:--|--:|--:|--:|
| All 32 YM2149 levels (the card never sets a user channel volume), every row and half-row, 4 delays, 3 rates x 2 qualities | 393 216 | 0 | 1.2e-15 |
| 2000 random levels in [0, 1] (a user volume would give those), 44.1 kHz Reference | 4 096 000 | 0 | 1.2e-15 |

A difference is possible only for a level within about 1e-15 (relative) of a float rounding midpoint - none of the
32 table levels, and none of 2000 random ones. The spectrum of the difference is therefore empty: THD+N and the
images above 20 kHz are exactly those of the base. For reference, the share of the output power in the 20-22.05 kHz
band (transition band plus aliases, unchanged by every variant): tone sweep SSG -38 dB (chip 1 C, a fixed 36 kHz
tone: -22 dB), noise -31 dB, `tech_support` -27 to -45 dB per channel, FM note -83 dB.

How often the shortcuts apply on real music (share of SSG outputs inside a stretch of one value; output-side
estimate): the three TFM tunes 100 % zero (they do not use the SSG), `tech_support` 37 % zero and 7 % a held
non-zero level, the 4-bit playback case 68 % zero and 24 % held. Variant c0 covers the zero part, c adds the held
levels.

## 3. Cost

Procedure ([performance-guidelines.md](../../guidelines/performance-guidelines.md) section 4): Release builds of the
same tree, one binary per variant kept in `bin/` (`core-benchmarks-base`, `-a`, `-ascalar`, `-b`, `-c0`, `-c`),
run from `bin/` without lowered priority, each round started only at a 1-minute load below 12. Two runs of 18
rounds, interleaved and reversed: run 1 `base a ascalar b c0 c`, reversed, forward again (loads 7.7-11.9 at the
round starts); run 2 the reverse order of run 1 (loads 7.2-11.3). CPU time per frame, minimum per variant
(run 2; run 1 in brackets), change against base:

| Benchmark | base | a | a-scalar | b | c0 | c |
|:--|--:|--:|--:|--:|--:|--:|
| MultiSound idle (0) | 836 us | -14.4 % (-13.3) | -10.5 % (-8.7) | -17.6 % (-13.5) | -26.7 % (-25.8) | -26.4 % (-24.8) |
| MultiSound YM pair (1) | 1084 us | -15.6 % (-16.1) | -13.0 % (-13.0) | -17.7 % (-18.5) | -15.5 % (-16.6) | -19.4 % (-19.5) |
| MultiSound SAA (2) | 864 us | -15.3 % (-16.2) | -11.0 % (-11.6) | -15.8 % (-18.2) | -25.5 % (-25.6) | -27.1 % (-28.1) |
| MultiSound SounDrive (4) | 870 us | -14.1 % (-15.6) | -9.5 % (-9.4) | -16.4 % (-16.8) | -26.3 % (-30.4) | -26.9 % (-23.9) |
| MultiSound MIDI (8) | 1012 us | -13.6 % (-13.7) | -8.4 % (-9.9) | -16.3 % (-13.3) | -24.8 % (-24.4) | -23.1 % (-23.2) |
| MultiSound GS (16) | 858 us | -11.8 % (-12.0) | -5.4 % (-11.0) | -16.7 % (-15.4) | -24.7 % (-25.4) | -25.6 % (-26.5) |
| MultiSound all five (31) | 1455 us | -10.6 % (-12.2) | -10.7 % (-16.0) | -12.7 % (-13.5) | -12.8 % (-12.8) | -12.9 % (-15.5) |
| TurboSound frame, idle | 2419 us | -4.7 % (-4.4) | -4.0 % (-5.1) | -4.3 % (-3.7) | -5.4 % (-6.0) | -4.5 % (-4.2) |
| TurboSound frame, player load | 2070 us | -7.6 % (-4.8) | -3.1 % (+14.2) | +11.3 % (+15.0) | -3.8 % (-5.7) | -7.0 % (-6.3) |
| TurboSound frame, player load, turbo | 1234 us | -0.1 % (-0.0) | -0.2 % (+5.1) | +6.5 % (+5.4) | +1.3 % (-0.3) | -0.5 % (-0.8) |
| Host frame 48K fast | 668 us | +0.7 % (+0.6) | +1.2 % (+0.5) | +1.1 % (+0.7) | +0.0 % (+0.2) | -0.3 % (+1.6) |
| Host frame Pentagon fast | 1566 us | +0.4 % (+0.6) | +0.7 % (+0.5) | +0.7 % (-0.3) | +0.1 % (+0.6) | +0.7 % (+1.4) |
| Port benchmarks (21, geometric mean) | - | -0.4 % (-0.5) | +0.6 % (-0.0) | -0.1 % (-0.5) | +0.9 % (-0.9) | +0.3 % (-1.1) |

What the numbers say:

- The card: a saves 12-16 % of a frame on every source mix, of which the multi-stream pass alone (a-scalar) is
  about two thirds and NEON the rest. b adds 2-4 %. c0 / c take the frames whose SSG is silent (every row but "YM
  pair" and "all five", where the benchmark plays six SSG tones) to -25 %; c adds the held non-zero levels (about
  -4 % more on the "YM pair" row: its square tones have half-periods of 128 to 448 generator ticks, longer than the
  101-sample window, so the flat part of every half-period is a held level). Idle card frame: 836 -> 613 us.
- The TSFM board: -4 to -5 % on the idle frame (variant a, the only one that touches it); the player-load frame is
  bimodal on this machine (every binary, base included, lands at either about 1950-2100 us or 2300-2540 us from
  round to round), so its minimum carries the signal: a -7.6 / -4.8 %, c -7.0 / -6.3 %. The b binary never reached
  the low mode (+11 / +15 %) although its board path is the same source as a's and c's (c contains b's code and is
  at -7 %): a code-layout effect of that one link, not a cost of the change.
- No regression on the classic machines: host frames 48K / Pentagon and the 21 port benchmarks stay within the
  ±2 % round-to-round noise of the procedure in both runs (they run the AY decimators through variant a).

## 4. Recommendation

All four variants pass the quality analysis: a, b and c0 are bit-identical by construction and by every check; c is
not bit-identical in double (1.2e-15 at most) but identical at the card's float output for every level the card can
produce, with an empty error spectrum. Fit for the main code base:

1. **a (multi-stream pass + SIMD)** - yes. The largest single gain (12-16 % of a card frame, 4-8 % of a TSFM frame),
   bit-identical on every path including the AY machines, with a scalar fallback that returns the same bits, and
   the multiply-add rule documented and tested under non-default flags.
2. **c0 (silent SSG channel skip)** - yes. Bit-identical, small, the same rule the FM part already has; it is the
   big win for FM-only music (the three TFM tunes: six of eight FIRs skipped) and the idle card.
3. **b (levels-only SSG tick)** - yes, with a note: 2-4 % on the card, bit-identical, no cost for the AY machines
   (a new function; `updateState` / `updateMixer` unchanged). It duplicates the 15-line level computation of
   `updateMixer`; folding both onto one inline helper is cleaner but changes the AY hot path and needs its own A/B.
4. **c (any held level)** - owner decision. It is identical at the output for every YM2149 level, but its argument
   is a rounding argument (the cast to float hides a 1e-15 difference), not "same arithmetic"; it adds about 4 % on
   the "YM pair" row and covers held non-zero levels (7 % of `tech_support`'s SSG outputs, 24 % in the sample
   playback case). If the main code base must stay bit-identical by construction, land a + b + c0 and keep c here.

Full `core-tests` on the branch head (all variants, c included; `tools/build/test.sh`): 8633 passed, one failure,
`BlockFormats_Test.SparseRawExport`, the known unrelated failure on master.
Full build (`tools/build/build.sh`): 0 compiler warnings. MinGW (`x86_64-w64-mingw32-g++` 16.2, `-Wall -Wextra -Werror -fsyntax-only`): `ym2203pair.cpp`,
`soundchip_ay8910.cpp`, `soundchip_turbosound.cpp`, `soundchip_turbosoundfm.cpp`, `decimatordot_test.cpp`,
`decimatordot.h` (with and without `-mfma`) clean.

## 5. How to repeat

```bash
# traces (once) and renders per binary
YMQ_DIR=scratch/ymq/<variant> ./core-tests --gtest_also_run_disabled_tests \
    --gtest_filter='Ym2203PairQuality_Test.DISABLED_RenderTraces'
python3 scratch/ymq/compare.py scratch/ymq/base/out scratch/ymq/<variant>/out
# constant-window statistics of variant c
./core-tests --gtest_also_run_disabled_tests --gtest_filter='DecimatorDot_Test.DISABLED_ConstantWindowStatistics'
```

The traces need the base binary's `CaptureTraces` once (`$YMQ_DIR/traces`, symlinked into each variant's
directory). The compare and A/B scripts live in the worktree's `scratch/ymq/` (not committed): `compare.py`,
`runab.sh` (interleaved rounds with the load gate), `abtable.py` (minimum per variant, paired differences).

## 6. Landing a + c0

Branch `ym-decimators-a-c0` on master `dd64db70d`: `fa95ce9ad` (variant a) and `b95189168` (variant c0, a
zero-run counter per SSG channel next to the FM one, without b's levels-only tick). Verification on that branch:

- Quality: the same traces through master `dd64db70d` and the branch - card rows and TSFM board buffers of all eight
  cases byte for byte identical; `MultiSoundCard_Test.YmRowsMatchTheirGoldenDigests`, `TsfmGolden_Test.*` and the
  TTD corpus (`TTD_Corpus_Test`, `TimeTravelControllerCorpus_Test`, `TTDSessionFile_Test`, `TTDV1Feeder_Test`) pass
  unchanged; `DecimatorDot_Test` passes; the kernel check under Rosetta (x86-64 default, `-mfma`,
  `-march=x86-64-v3`; arm64 default, `-ffp-contract=off`): 0 of 5600 calls differ.
- Build: full `tools/build/build.sh` and `core-tests` / `core-benchmarks` / `unreal-qt-tests`, 0 compiler warnings.
  MinGW (`x86_64-w64-mingw32-g++` 16.2, `-Wall -Wextra -Werror -fsyntax-only`, with and without `-mfma`):
  `ym2203pair.cpp`, `soundchip_turbosound.cpp`, `soundchip_turbosoundfm.cpp`, `decimatordot_test.cpp`,
  `ym2203pairquality_test.cpp` (both include `decimatordot.h` and `filter_decimator.h`) clean.
- Tests (`tools/build/test.sh`): `unreal-qt-tests` 54 passed, `unreal-asm-tests` 163 passed, `core-tests` 8647
  passed, one failure: `BlockFormats_Test.SparseRawExport`, the known unrelated failure on master.
- Cost: confirming A/B, master `dd64db70d` (base) against the branch (land), 10 interleaved rounds
  `base land base land base land land base land base`, every round started at a 1-minute load below 12 (9.5-11.5;
  the 5-minute load was 13.6-19.6, a busy machine). CPU time per frame, minimum and mean of the paired rounds:

  | Benchmark | base | land | change (paired) |
  |:--|--:|--:|--:|
  | MultiSound idle (0) | 865 us | 645 us | -25.4 % (-25.8 %) |
  | MultiSound YM pair (1) | 1111 us | 929 us | -16.4 % (-16.9 %) |
  | MultiSound SAA (2) | 867 us | 663 us | -23.5 % (-25.3 %) |
  | MultiSound SounDrive (4) | 896 us | 676 us | -24.6 % (-25.4 %) |
  | MultiSound MIDI (8) | 1028 us | 805 us | -21.7 % (-23.0 %) |
  | MultiSound GS (16) | 870 us | 658 us | -24.4 % (-24.7 %) |
  | MultiSound all five (31) | 1416 us | 1263 us | -10.8 % (-12.2 %) |
  | TurboSound frame, idle | 2426 us | 2309 us | -4.8 % (-5.7 %) |
  | TurboSound frame, player load | 2387 us | 2249 us | -5.8 % (-6.6 %) |
  | TurboSound frame, player load, turbo | 1011 us | 1008 us | -0.3 % (-1.3 %) |

  The same picture as the prototype's a + c0 rows (section 3); the classic host frames and port benchmarks were
  measured there (within noise) and the landed code for them is variant a unchanged.

## 7. The prototype as patches

The prototype branch `proto-ym-decimators` was deleted on 2026-10-08 after its five code commits were saved as a
`git format-patch` series in [ym-decimator-prototype-patches/](ym-decimator-prototype-patches/). The series applies
in order on master `012418ef4` (`git am` of all five) and reproduces the branch's tree exactly (checked: the tree after
`git am` equals the prototype's `bf80d53a5`). The variants build on each other, so a later patch needs the earlier
ones; on today's master, where a and c0 landed in their production form, b or c have to be ported, not applied.

| Patch | Variant | Commit | What it changes |
|---|---|---|---|
| [01-variant-a-multi-stream-fir-simd.patch](ym-decimator-prototype-patches/01-variant-a-multi-stream-fir-simd.patch) | a | `64c2e29a4` | one pass over the taps for all decimators of a pair, NEON / SSE2 / scalar `DecimatorDot` kernels (landed, reworked) |
| [02-variant-b-ssg-levels-only.patch](ym-decimator-prototype-patches/02-variant-b-ssg-levels-only.patch) | b | `943e21c60` | `SoundChip_AY8910::updateStateLevels`: the card's SSG tick computes channel levels only (not landed) |
| [03-variant-c0-skip-silent-ssg-fir.patch](ym-decimator-prototype-patches/03-variant-c0-skip-silent-ssg-fir.patch) | c0 | `182103b36` | skip the FIR of an SSG channel whose whole window is 0.0 (landed, rewritten without b) |
| [04-variant-c-constant-level-shortcut.patch](ym-decimator-prototype-patches/04-variant-c-constant-level-shortcut.patch) | c | `b0fc99d86` | the same skip for any held level, plus its tests (not landed) |
| [05-variant-c-plain-order-outputs.patch](ym-decimator-prototype-patches/05-variant-c-plain-order-outputs.patch) | c | `bf80d53a5` | `renderChannels` evaluates the eight outputs in plain order (not landed; landed separately in c0's form) |

Why b and c stay out: section 4 (b duplicates the AY level computation, a merge touches the AY hot path and needs its
own A/B; c is identical at the float output but not the same arithmetic).

