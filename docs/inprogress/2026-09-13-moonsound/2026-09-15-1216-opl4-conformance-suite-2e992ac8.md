# OPL4 Exhaustive Conformance & Regression Suite

> **Status: executed and superseded.** All four tiers landed (2026-09-15): PoC
> sweeps + fuzz in `opl4sweep.cpp`, oracle golden 55 digests, core canaries — and the
> follow-on classic-map adoption closed the §2.1 divergence set (see
> [`2026-09-15-2114-opl4-ymfm-verification-findings.md`](2026-09-15-2114-opl4-ymfm-verification-findings.md) §2.1–§2.3,
> §5). The `#if !defined(OPL4_FM_YMFM)` map-specific guards described below were
> mostly removed by the adoption; the residual guard inventory lives in the findings
> doc §4.

## Goal

Turn today's coverage (5452 unit checks, 17 behavior vectors, 6 differential scenarios, 15 golden digests) into a systematic per-register/per-bit/per-pattern conformance matrix, so every writable YMF278B field is pinned by (a) a spec-derived assertion, (b) a bit-exact golden digest, and (c) fuzz determinism — plus thin core-level canaries. "100% reliability" is delivered as: **every register bit-field exercised + documented known-divergence registry + bit-exact cross-platform fence**, not as a claim of absolute proof.

## Baseline evidence (verified today)

- `cosim-ymfm` 6/6, `cosim-oracle` 15/15, PoC units 5452/0 (in-tree) & 5440/0 (ymfm), core `*MoonSound*` 17/18 (1 fail = §12.8 FDC loader, unrelated to synthesis).
- Gap: register space not exhaustively fenced; digests cover only 15 behaviors.

## Design

### Tier 1 — Exhaustive sweeps: new `tools/poc/015-opl4-synthesis/tests/opl4sweep.cpp`

Added to the existing `opl4tests` executable (`tools/poc/015-opl4-synthesis/CMakeLists.txt` `add_executable` list). Plain CHECK framework from `tests/testfw.h` (no gtest, no 50 ms budget). Reuses `testfw.h` map-aware helpers (`KeyOnFmCh0` pattern with `#if defined(OPL4_FM_YMFM)` register-map branches).

**Assertion philosophy**: every check derives from datasheet/TDD-doc D1-D11 semantics (exact dB ratios, monotonicity, boundaries, symmetric relations) — independent of implementation output. Implementation snapshots are Tier 2's job.

FM families (13): `FmTlLadderSweep` (TL 0-127, adjacent step −0.75 dB exact), `FmMultSweep` (MULT 0-15 zero-crossing ratios 0.5/1-15), `FmKslSweep` (KSL 0-3 × block 0-7 onset thresholds), `FmEnvStageSweep` (AR/DR/SL/RR 0-15 staged grid; rate-15 instant; sustain = SL; release slope), `FmFeedbackSweep` (FB 0-7 growth), `Fm4OpConnections` (3 topologies + connection-select bits), `FmRhythmSweep` (all 5 voices via 0xBD), `FmWaveformSweep` (OPL3 waves 0-7: DC/symmetry/peak), `FmAmVibDepthMatrix` (AM/VIB × depth bits), `FmRoutingMatrix` (CHA/CHB/CHC/CHD 16 combos → L/R matrix), `FmTimerSweep` (T1/T2 + IRQ flags/mask/reset), `FmKonMomentary` (B0 kon edge-only), `FmEnvelopeRatesVsYmfm` (ratios on both backends where map-agnostic).

PCM families (11): `PcmWaveNumberBoundary` (wave <384/≥384 × waveTblHdr 0-7 banking), `PcmWidthDecodeSweep` (8/12/16-bit incl. −FS), `PcmStepSweep` (OCT −8..+7 × FNUM grid, CalcStep = 2^oct·fn/1024 exact), `PcmLoopEdgeMatrix` (E=0 one-shot, S-complement ends, overrun, 1-sample loop), `PcmTlLadderSweep` (TL bits + LD immediate-vs-interp 27/13.5 cadence + 0x7F→0xFF special, per [`opl4pcm.cpp:211-219`](tools/poc/015-opl4-synthesis/src/opl4pcm.cpp#L211-L219)), `PcmEnvRateMatrix` (AR/D1R/D2R/RR 0-15 × rate-scaling octaves, DL 0-15), `PcmDampPrvbMatrix`, `PcmPanSweep` (16 pan values + 0x10 DO1-silence), `PcmLfoMatrix` (LFO freq × depths × per-slot vibrato), `PcmInterpMatrix` (on/off × fractional positions), `MixFieldMatrix` + `MemoryAccessSweep` (0xF8/0xF9 fields; regs 0x03-0x06 auto-increment + LD window).

In-tree-map-specific FM sweeps carry `#if !defined(OPL4_FM_YMFM)` guards (existing vector pattern); PCM families run on both backends.

### Tier 2 — Golden digest expansion: [`cosim/cosim-oracle.cpp`](tools/poc/015-opl4-synthesis/cosim/cosim-oracle.cpp)

One digest per sweep family over a representative sub-sweep: ~40 new cases, `golden/oracle.txt` 15 → ~55. Generated via `golden/generate.py` policy; the **existing 15 digests must remain byte-identical** (only appends allowed) — that is the regression fence during development.

### Tier 3 — Seeded fuzz (§12.6)

In `opl4sweep.cpp`: 64-128 LCG-seeded random FM+PCM register streams with timestamps; assert no crash, no NaN/denormal, replay-determinism; failing seeds become permanent vectors.

### Tier 4 — Core GTest canaries: extend [`core/tests/emulator/sound/moonsound_device_test.cpp`](core/tests/emulator/sound/moonsound_device_test.cpp)

6-8 thin tests (<50 ms each, no turbo — they assert on rendered audio), reusing the existing port helpers (`KeyOnFmChannelThroughPorts`, `KeyOnPcmSlotThroughPorts`, `UploadSquareToneThroughPorts`): FM TL ladder row, FM envelope stage row, waveform symmetry, PCM loop E=0 wrap, PCM pan row, PCM TL 0x7F special, block-mix field row, SRAM access round-trip.

## Implementation order (small chunks, ~150-250 lines per edit)

1. `opl4sweep.cpp` skeleton + CMake wiring + FM TL/MULT/KSL sweeps; run both backends.
2. FM env/feedback/4-op/rhythm/waveform sweeps.
3. FM AM-VIB/routing/timers/kon sweeps.
4. PCM wave/width/step sweeps.
5. PCM loop/TL/env sweeps.
6. PCM DAMP/PRVB/pan/LFO/interp/mix/memory sweeps + runtime budget check (whole suite target < 30 s, §12.7).
7. Fuzz tier.
8. cosim-oracle new cases + digest generation (verify old 15 unchanged, new all match; `cosim-ymfm` stays 6/6).
9. Core canaries + core suite run.
10. Docs: [`2026-09-13-0217-opl4-core-tdd.md`](docs/inprogress/2026-09-13-moonsound/2026-09-13-0217-opl4-core-tdd.md) §12.2 gains implemented-inventory table (family → file → checks); [`2026-09-13-0217-opl4-unreal-ng-integration.md`](docs/inprogress/2026-09-13-moonsound/2026-09-13-0217-opl4-unreal-ng-integration.md) §12.2 unit-status refresh.

## Verification

- PoC suite: both backend builds, 0 failures, zero warnings (`-Wall -Wextra -Wpedantic`).
- `cosim-oracle` ~55/55 (old 15 byte-identical); `cosim-ymfm` 6/6.
- Core: full `core-tests` — canaries green, 17/18 MoonSound unchanged (the §12.8 fail stays known/open).
- No commits without explicit one-time instruction.

## Out of scope (recorded, not done)

VGM music-corpus differential (§12.3), HDL co-sim (§12.4), hardware recordings — per tier selection.