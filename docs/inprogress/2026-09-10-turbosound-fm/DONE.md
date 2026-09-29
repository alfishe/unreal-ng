# DONE — TurboSound FM (2×YM2203) (2026-09-10, reopened 2026-09-23, closed 2026-09-28)

**Status:** complete. P0–P8 landed; the TTD listening pass was signed off by the user on
2026-09-28 (see Evidence below). P5 (TTD) was the one gap — the device shipped a
P3-era stub (`TTDStateSize()` always 0) despite an earlier `DONE.md` claiming
it was complete. Reopened 2026-09-23 after a user report ("scrubbing back
gives broken sound, internal FM synthesizer state is lost"), confirmed
against `scratch/tsfm-issues.ttd` (0/1169 checkpoints carried a blob), then
fixed in three passes the same day:

1. **v1 (1142 B):** real capture/restore of core chip state (registers,
   ymfm FM engine, timers, SSG generator phase). Fixed the "state is
   completely lost" symptom.
2. **v2 (1158 B):** a follow-up regression test showed the v1 fix still let
   generator PHASE COUNTERS drift after a seek, because `_samplePhase`/
   `_decimationPhase` (render-loop accumulators that gate how many ticks
   reach the tone/noise/envelope generators) were wrongly excluded as "just
   output-stage bookkeeping". **User-confirmed fixed**: noise/tone
   restore-and-replay is correct.
3. **v3 (1190 B):** the user then reported clicks were *still* present at
   the end of a seek+replay session. Real root cause: `TTDLoadState` never
   flushed the output-stage pipeline (decimator FIR history, hold register,
   LQ boxcar, word queues), so it kept whatever audio it held *live* right
   before the seek — mixing stale, uncorrelated content with the freshly-
   restored generator output through the FIR taps produces exactly a click.
   Fix: flush that content to silence on restore (matching what `reset()`
   already does) — but the first attempt at this broke v2's regression test,
   because `FilterDecimator::_phase` (the per-decimator resampling phase) is
   a *third* generator-tick-gating accumulator, missed in v2 because it lives
   inside `FilterDecimator` rather than `SoundChip_TurboSoundFM`. Final fix:
   flush FIR content via `reset()`, then explicitly restore each of the 4 SSG
   decimators' `_phase` on top via new `phase()`/`setPhase()` accessors.

Full writeup, root causes, evidence: [ttd-fm-state-gap.md](ttd-fm-state-gap.md).

## What's landed and verified

- Real TTD capture/restore for TSFM (v3, 1190 B): board latches, the
  `_samplePhase`/`_decimationPhase` render-loop accumulators, 4 SSG
  decimators' resampling phases, per-chip address/fmClockPhase/timers/busy,
  the ymfm engine's own `save_restore()` (494 B), and the SSG half's
  existing AY8910 serializer (57 B) — `static_assert`-pinned at every level.
- `TTDLoadState` actively flushes the output-stage pipeline (decimator FIR
  history, hold register, LQ boxcar, word queues) to silence on every
  restore, avoiding stale pre-seek audio mixing with freshly-restored
  generator output.
- `core/tests/debugger/ttd/ttdtsfm_test.cpp`: unit round-trip tests, manager-
  integration blob-presence tests, a real-chiptune-playback test
  (`testdata/sound/tsfm/tech_support.sna`), a whole-blob bit-identity test
  across sampled I-frame and P-frame checkpoints
  (`SeekTo_RestoresTsfmStateBitIdenticalOnKeyAndDeltaFrames`),
  `SeekTo_NoiseGeneratorStateDeterministicFromDeltaFrame` (caught v1→v2, and
  re-verified it still passes after v3), and
  `SeekTo_FlushesOutputStageToAvoidClickFromStaleHistory` (caught the click;
  verified by temporarily disabling the fix and confirming the test fails
  with the exact expected stale-state symptom, then re-enabling it).
- Full `core-tests` suite: 3289/3289 passed, zero compiler warnings. No
  memset added; the new serialization helpers only `memcpy` plain scalar
  types (same idiom as the existing AY8910 serializer), so no GCC
  `-Wclass-memaccess`-class portability risk.
- `tools/verification/ttd-analyzer`: audited and brought up to date —
  added `decode_peripheral_blob()` (the missing Python counterpart to
  `TTDPeripheralRegistry::DecodeBlob`, needed to actually inspect a device's
  state rather than the raw on-disk wrapper) and completed
  `PERIPHERAL_ID_NAMES` (was missing ids 7/8). The on-disk container format
  itself needed no changes — verified by re-validating all three real
  recordings gathered during this investigation.

## Evidence of completion

- **Listening sign-off (2026-09-28, user):** scrubbing a TSFM recording back and
  forth, the FM data is not corrupted and the FM part stays in sync with the
  TurboSound (SSG) part. This was the one open item (a perceptual check the
  byte-level tests could not give).
- Since the v3 fix the blob grew to the timed-write v5 layout (2008 B) with an
  exact-restore fixture corpus (`6ed6d4c0`): `TTD_Corpus_Test` restores every
  device of every fixture byte for byte (TSFM included) and replays it
  deterministically, now comparing RAM too.

## Follow-ups (outside the TTD gap, tracked separately, kept for continuity)

- ISSUES.md #5 sub-audio coupling (by design), #6 int16 mix headroom →
  wide-mix/limiter design (tracked separately, root `PLAN.md` T4), #7 LQ→HQ
  stale decimator history (minor, same failure class the v3 fix resolved for
  TSFM specifically — worth checking whether the legacy AY/TurboSound device
  has the same gap, since it shares the same `FilterDecimator`/render-loop
  pattern and was never audited for this), #8 SSG pan asymmetry (cosmetic),
  #10 chip-to-chip FM level (not modelled).
- Still deferred from P7: `AYLogRecord.flags` FM classification in the AY log
  analyzer, video-recording FM native taps.
- H1 loudness estimate remains trim-adjustable (`TSFM_FmTrimDb`).
