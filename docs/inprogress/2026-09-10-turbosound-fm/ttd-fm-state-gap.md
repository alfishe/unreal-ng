# TSFM TTD gap: FM synth state not captured (P5 never actually landed)

**Update 2026-09-23 (later same day):** the core fix described below is now
implemented (`soundchip_turbosoundfm.{h,cpp}`) and tested — see
[Fix landed](#fix-landed-2026-09-23) at the end of this document for what
changed, the verification evidence, and the one known-and-by-design residual
(output-stage state resets on seek, causing a brief audio discontinuity right
at the seek point — see that section for why this is not a bug).

**Found:** 2026-09-23, triaging a user report: "scrubbing back gives broken
sound — only port writes work in restored state, internal FM synthesizer
state is lost."

## Current state

`DONE.md` and `implementation-plan.md`'s P5 evidence row claim TSFM TTD is
complete (1142 B payload, `PeripheralId::TSFM = 4`, core-hash equality). The
code does not back this up. `SoundChip_TurboSoundFM::TTDSaveState/TTDLoadState`
(`core/src/emulator/sound/chips/soundchip_turbosoundfm.h:415-442`) are still
the P3-era stub:

```cpp
/// P5 fills in the §8.2 layout (1142 bytes, PeripheralId::TSFM = 4).
/// Until then the device reports an empty payload - enough for the
/// session-kind guard of P3 to distinguish it from TurboSound.
size_t TTDStateSize() const override { return 0; }
void TTDSaveState(uint8_t* /*dst*/) const override {}
void TTDLoadState(const uint8_t* /*src*/) override {}
ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::TSFM; }
```

`git log` confirms `soundchip_turbosoundfm.{h,cpp}` hasn't been touched since
the unrelated `c7ae65a7`/`473c7b56` FM-feature commits — P5 was designed
(`tsfm-tdd.md` §8, byte-exact layout) but never implemented, and the
`implementation-plan.md` itself correctly has no "Done" line under P5 (only
P6/output-stage does) — the false claim lives only in `DONE.md`'s summary and
in stray evidence-row phrasing, not in the day-to-day plan tracking.

**Root cause of the reported symptom:** every register write still lands in
`SoundChip_AY8910`'s 16-register file (captured today because the SSG half
uses the pre-existing, fully-working AY serializer — see below), so port
writes "stick" across a TTD scrub. But the ymfm FM engine's live synth state —
operator phase/envelope/cache, per-timer counters, busy flag, prescale/CSM
scheduling — is not part of any TTD blob at all. On restore the FM engine is
whatever a fresh `SoundChip_TurboSoundFM` constructed to (silent, timers
stopped), while everything downstream (CPU, SSG, video) has jumped to the
target T-state. That's the "only port writes work, internal state is lost"
report.

Only the AY (`SoundManager`/`SoundChip_TurboSound`) TTD path is real and
working — see `core/src/emulator/sound/chips/soundchip_ay8910.cpp:990-1080`
(fixed 57-byte-per-chip layout, `static_assert`-pinned) and
`soundchip_turbosound.cpp:352-408` (delegates to two `SoundChip_AY8910`
instances). That is the template to follow.

### Confirmed against the reported recording (`scratch/tsfm-issues.ttd`)

Parsed with `tools/verification/ttd-analyzer` (`ttd_format.py`, schema v1).
Session: Pentagon 128K, 1169 checkpoints (frames 0..1168, 24 I-frames), write
journal with 562,029 records (memory **and** port writes — `TTDWriteRecord`,
`ttdwritejournal.h:55-63`, `isIo` bit distinguishes them).

Across **every one of the 1169 checkpoints**, the peripheral-blob map only
ever contains ids `{1 BetaDisk, 2 Tape, 3 Covox, 7 KempstonMouse}`. Ids `0`
(`TurboSound`) and `4` (`TSFM`) are **never present, not even as an empty
entry**:

```
union of peripheral ids across all 1169 checkpoints: [1, 2, 3, 7]
TurboSound(0) ever present: False
TSFM(4) ever present: False
```

This is not a truncated/empty blob — it's a total absence, and it is the
direct, mechanical consequence of the stub: `TTDPeripheralRegistry::CaptureAll`
(`ttdperipheralregistry.cpp:93-94`) does

```cpp
const size_t stateSize = device->TTDStateSize();
if (stateSize == 0)
    continue;   // never writes a blob for this device at all
```

and `SoundChip_TurboSoundFM::TTDStateSize()` always returns `0`. So the
session's TSFM device (this recording was made with `TurboSound = FM`
selected — the device is registered, just always skipped) is invisible to
*every* checkpoint. On restore, `TTDPeripheralRegistry::RestoreAll`
(`ttdperipheralregistry.cpp:114-120`) finds no blob for id 4, increments
`missingBlobs`, and leaves the device holding whatever state it had at the
moment of the seek — i.e. exactly "internal FM synthesizer state is lost".

One correction to the informal bug description: it isn't just the FM engine
that's lost — the SSG half of each `TsfmChip` (a real, working
`SoundChip_AY8910` with a fully-functional 57-byte serializer) is *also*
never captured, because the outer stub never delegates to it. Register
content surviving a scrub is most likely explained by the write journal
(`isIo=1` records exist for port `#FFFD`/`#BFFD` writes in this session) being
consulted by reverse-search/analysis tooling rather than by any actual
peripheral-state restore — worth double-checking once the real
`TTDSaveState`/`TTDLoadState` land, since today literally nothing about
TurboSound/TSFM state is restored on seek.

## What's registered vs. what's captured today

| Device | `TTDPeripheralId()` | Blob size | Captures FM synth state? |
|---|---|---|---|
| `SoundChip_TurboSound` (legacy AY) | `TurboSound` (0) | 1 + 2×57 = 115 B | N/A — no FM engine |
| `SoundChip_TurboSoundFM` (TSFM) | `TSFM` (4) | **0 B (stub)** | **No** |

`ttdtsfm_test.cpp` only exercises the *session-kind guard* (blob-id mismatch
refusal) via a hand-rolled `FakeSerializer`, not the real device — so no test
currently fails to reflect this gap; the gap is silent.

## Proposal

Implement §8.2 of `tsfm-tdd.md` (design already complete, unchanged) as
written:

1. **`SoundChip_TurboSoundFM::TTDStateSize/TTDSaveState/TTDLoadState`**
   (`soundchip_turbosoundfm.cpp`) — per-chip: 1 B address + 4 B
   `fmClockPhase` + 2×4 B timer remaining (-1 = stopped) + 4 B busy remaining
   + 2 B `ymfmSize` + 494 B `ymfm::ym2203::save_restore()` payload + 57 B
   `SoundChip_AY8910::TTDSaveState()` (SSG half) → 1 + 2×(1+4+8+4+2+494+57) =
   **1142 B** total, version-byte prefixed. Construction-time
   `static_assert`/runtime assert that `ymfmSize == 494`.
2. **`TTDHashState`** override (legacy `SoundChip_TurboSound` doesn't have
   one either — TSFM needs it since it's the divergence-detection surface for
   the new device): FNV-1a over the full payload.
3. Per-`TsfmChip` scratch `std::vector<uint8_t>` reserved to 1024 B at
   construction — no heap allocation on the save path (contract, matches
   `NoAllocationInSave` gate in the design's §12.3 test list).
4. `_adoptCpuClock = true` in `TTDLoadState` so `syncTo()` re-adopts the
   framework-restored T-state on the next tick, same convention as every
   other TTD-registered peripheral (no absolute T-state stored in the blob).
5. `fm`/engine members become `mutable` where `ymfm_saved_state::save_restore`
   requires a non-const call even from `TTDSaveState() const`.
6. Output-stage state (decimators, hold value, word queues, DC blockers,
   `_ayPLL`) stays **out** of TTD state by design (matches legacy TurboSound
   and every other device's policy) — it resets after restore and the audio
   pipeline re-settles within one filter length; this is not part of the bug.

No changes needed to `TTDPeripheralRegistry`, `TimeTravelManager`, or the
checkpoint format — TSFM is already registered under `PeripheralId::TSFM`
(`timetravelmanager.cpp:1060`, via `getTurboSound()->TTDPeripheralId()`); only
the device's own three methods are stubs.

## Regression tests added (2026-09-23, intentionally red until the fix lands)

`core/tests/debugger/ttd/ttdtsfm_test.cpp` now carries four tests that fail
today and document/reproduce this exact bug — do not delete or weaken them to
make the suite green, fix the device instead:

- `TTD_TurboSoundFM_Serializer_Test.TTDStateSize_IsNonZero` /
  `RoundTrip_SsgRegistersPreserved` — unit-level, synthetic register pokes on
  a standalone `SoundChip_TurboSoundFM`.
- `TTD_TSFM_ManagerIntegration_Test.CaptureNow_PopulatesTsfmStateBlob` —
  integration-level, mirrors the working AY equivalent
  (`ttdayserializer_test.cpp`'s `CaptureNow_PopulatesAyStateBlob`).
- `TTD_TSFM_ManagerIntegration_Test.CaptureNow_MissesRealDemoPlaybackState` —
  the strongest one: loads `testdata/sound/tsfm/tech_support.sna` (a real
  chiptune snapshot paused at the BASIC entry point with both YM2203s at
  reset, per its `SOURCES.md` entry), runs 50 frames so the player's own init
  and playback drive genuine non-trivial SSG/FM state (asserted as a
  precondition — chip 0's SSG registers are non-zero by frame 50), then
  checks the checkpoint's TSFM blob. This reproduces the reported bug against
  the same class of input as `scratch/tsfm-issues.ttd`, not just a synthetic
  register write.

All four fail with `TTDStateSize()==0` / missing-blob diagnostics pointing
back to this document. Full TTD suite otherwise unaffected: 649 passed / 4
failed (the four above) as of this writing.

## Tests to add

Follow `core/tests/debugger/ttd/ttdayserializer_test.cpp` (the working
template for exactly this shape of problem), in a new
`ttdtsfmserializer_test.cpp` or extending `ttdtsfm_test.cpp`:

- `TTDStateSize_IsStable_1142Bytes`
- `RoundTrip_DefaultState_IsByteIdentical`
- `RoundTrip_RichRegisterState_PreservesRegisters` (SSG half + FM registers)
- `RoundTrip_GeneratorPhase_AdvancedCountersPreserved` — run the FM engine
  (`clockFmOnce`) and SSG `updateState()` many times first, so operator
  phase/envelope/timers/busy move well past reset values, then prove the
  round-tripped buffer is byte-identical. This is the test that would have
  caught the reported bug directly.
- `RoundTrip_PhaseRestored_NotJustRegisterConfig` — restore into a second
  device, step both in lockstep, assert continued byte-identity (proves
  restore is a true continuation of synthesis, not a config copy — this is
  the direct regression test for "scrubbing back gives broken sound").
- `SaveIsPureRead_DoesNotMutateDevice`
- Manager integration test analogous to
  `TTD_AY_ManagerIntegration_Test.CaptureNow_PopulatesAyStateBlob`, asserting
  `peripheralBlobs[PeripheralId::TSFM]` decodes to 1142 B via
  `TTDPeripheralRegistry::DecodeBlob`.
- From the original design's §12.3 list, still relevant: `SeekAnyPoint` (50
  random frame + tInFrame targets on the existing `TsfmPlayerHarness`),
  `ReplayWithSoundOff`, `NoAllocationInSave`, `CheckpointingIsInvisible`.
- Lower layer already covered and green: `ymfm_ttd_patch_test.cpp`
  (`YmfmTtdPatch.*`) proves `ymfm::ym2203::save_restore` itself is
  side-effect-free and exact over 400k/50k-step stress runs — the new tests
  build on top of that, they don't need to re-prove it.

## Gate

Same as the original P5 gate in `implementation-plan.md`: all tests above
green, `SeekAnyPoint` passes at checkpoint cadence 1 and the default cadence,
a TSFM TTD session saves to disk and reloads in a fresh instance with an
identical core hash per frame — plus manual verification that scrubbing back
through audio on a TSFM tune no longer produces broken/silent FM output.

## Fix landed (2026-09-23)

Implemented `SoundChip_TurboSoundFM::TTDStateSize/TTDSaveState/TTDLoadState/
TTDHashState` in `soundchip_turbosoundfm.{h,cpp}` per the §8.2 layout above,
byte-exact (1142 B, `static_assert`-pinned at three levels: per-chip 570 B,
total 1142 B). `TsfmChip` gained a `ttdScratch` vector (reserved 1024 B in the
constructor) so the ymfm `save_restore()` call doesn't allocate on the save
path. `TTDLoadState` sets `_adoptCpuClock = true` at the end, matching
`reset()`'s convention.

### Verification

- Full core-tests suite: **3287 passed / 0 failed / 1 skipped** (the skipped
  test is an opt-in WAV render diagnostic, unrelated). Zero compiler warnings.
- `core/tests/debugger/ttd/ttdtsfm_test.cpp` — all of the regression tests
  added earlier that day (`TTD_TurboSoundFM_Serializer_Test.*`,
  `TTD_TSFM_ManagerIntegration_Test.CaptureNow_Populates*`) now pass; they
  were red against the stub and are green against the real implementation.
- New: `TTD_TSFM_ManagerIntegration_Test.
  SeekTo_RestoresTsfmStateBitIdenticalOnKeyAndDeltaFrames` — records 140
  frames of real chiptune playback (`tech_support.sna`, spanning two
  keyframe boundaries at the 50-frame/"once per second" `kKeyFrameInterval`),
  then for a mix of I-frame and P-frame checkpoints (0, 25, 50, 75, 100, 139)
  seeks there via the real `TimeTravelManager::SeekTo` path and asserts the
  live device's re-serialized state is byte-identical to what was captured
  at record time. **Passes on every sampled checkpoint, keyframe or delta.**
  This directly answers the "verify every keyframe carries a full TSFM
  snapshot, and delta frames restore correctly too" question: peripheral
  blobs are not delta-encoded like RAM pages — `TTDPeripheralRegistry::
  CaptureAll` runs unconditionally on every single checkpoint
  (`TimeTravelManager::CaptureNow`), so every checkpoint, I-frame or P-frame,
  already carries and restores a full, independent 1142 B TSFM payload. No
  special-casing by frame kind was needed once the stub was fixed.
- Confirmed the fix is unrelated to the two `.ttd` recordings gathered for
  this investigation (`scratch/tsfm-issues.ttd`, `scratch/tsfm-issues2.ttd`)
  by re-parsing them with `tools/verification/ttd-analyzer`: both were
  recorded against the pre-fix stub and, as expected, carry **zero**
  TurboSound/TSFM blobs across all of their checkpoints (1169 and 8998
  respectively) — consistent with, not contradicting, the fix. A new
  recording made after this fix is what should be checked next if any
  residual audio issue is still suspected.

### Known, by-design residual: output-stage state resets on seek (superseded — see "v3 fix" below)

**Update:** this section originally speculated the output-stage exclusion was
purely cosmetic/inaudible. It wasn't — it was the direct cause of a real,
reported "occasional clicks on replay" bug. See the "v3 fix" section below
for the actual investigation, root cause, and fix (the exclusion policy
itself turned out to be correct for *content*, but the state needs an active
silence-flush on restore, not just "leave it alone"). Left here for history.

Per §8.2 and the class-level `TTDSerializable` comment in
`soundchip_turbosoundfm.h`, **output-stage** state is deliberately excluded
from the TTD payload: `TsfmOutputState` (sample-and-hold value, HQ
decimators, LQ boxcar accumulator, DC-blocking history), `FmWordQueue words`
(already-computed but not-yet-consumed DAC samples), and the auxiliary
`fmKeyOn[3]` state-report mirror (register 0x28 last-write, used only by
`DeviceState::FmChip` reporting, not audio). This matches the policy already
in place for every other TTD-registered audio device (AY/TurboSound, Covox):
these are host-side rendering/reporting caches, not chip state, and they
re-settle naturally within about one filter length of audio after a restore
(the exact same tradeoff as `ISSUES.md` #7, "LQ→HQ switch replays stale
decimator history").

This is the most likely explanation for a subjective "most of it sounds
right, but something's off right after a scrub" impression: the FM
*synthesis* state (which is what determines the note, pitch, envelope,
timbre from that point forward) is now fully restored and verified
bit-identical; what still resets is a few milliseconds of *already-rendered*
audio buffer/filter history immediately at the seek point. If this residual
click/discontinuity turns out to be audible enough to matter, the fix would
be to warm-restart the decimators from silence (already the behavior) or,
more ambitiously, to also persist output-stage state — a larger, cross-
cutting change affecting every audio device's TTD policy at once, not a
TSFM-specific bug. Worth a fresh `.ttd` recording and a real listening test
before deciding whether it's worth pursuing.

## v2 fix (2026-09-23, later same day): generator-phase drift after seek

**This is the real cause of "noise/tone sometimes takes more than a second
to stabilize" and "occasional clicks on replay after a seek" — not the
output-stage residual above**, which is a much smaller, cosmetic effect by
comparison. Found by a new regression test aimed specifically at ruling out
a "restores fine at keyframes but drifts on delta frames" theory:
`TTD_TSFM_ManagerIntegration_Test.SeekTo_NoiseGeneratorStateDeterministicFromDeltaFrame`
(`ttdtsfm_test.cpp`) forces active, continuously-changing noise state,
records across a keyframe boundary, seeks to a delta frame, runs forward to
a later frame in the new (post-seek) timeline, and compares against what the
original, never-seeked run recorded at that same frame. It failed —
**independent of whether an intermediate keyframe was crossed first** (I
re-ran it with and without a same-timeline detour through the frame-100
keyframe; identical failure both times, which rules out "seek history" as a
factor and confirms it's a pure forward-replay-from-restore bug).

**Root cause.** `SoundChip_TurboSoundFM::_samplePhase` (the mixer-exact
sample-rate PLL accumulator) and `_decimationPhase` (the LQ boxcar phase)
are explicitly documented in `handleFrameStart()`'s own comment as carrying
across frames — "only `reset()` and `setCoreRate()` clear them" — unlike
every other render-loop field (`_lastTStates`, `_renderT`, `_ayBufferIndex`),
which *are* zeroed every frame and therefore need no persistence. But
`_samplePhase` directly gates the `while (_samplePhase >= CPU_CLOCK_RATE)`
loop in `handleStep()` that calls `updateState()` — the call that ticks the
SSG `ToneGenerator`/`NoiseGenerator`/`EnvelopeGenerator` counters. Since
`_samplePhase` was excluded from the v1 (1142 B) TTD payload as if it were
ordinary output-stage bookkeeping, a restored device resumed ticking its
generators from whatever fractional phase `_samplePhase` happened to hold
*live* (i.e. wherever the device was right before the seek), not the true
historical phase at the restored point. That mismatch changes exactly how
many `updateState()` ticks land in each subsequent T-state window, so
generator counters (tone/noise/envelope phase — audible pitch and timbre)
silently drift out of sync with the true history after every seek. Over
enough elapsed time the drift compounds and becomes audible as clicks or
"not quite right" tone/noise — consistent with a "last 2-3 seconds" report
on a session that had been running for a while post-seek, and with a
"takes more than a second" impression (drift needs to accumulate to become
perceptible, not because of anything keyframe-specific).

**Fix.** `_samplePhase` (u64) and `_decimationPhase` (f64) added to the TTD
payload as two new device-level fields (not per-chip), right after the
board byte. Version bumped `1 → 2`; total size `1142 → 1158` bytes.
`TTDLoadState` now asserts on version mismatch (nothing to load-path-branch
on yet, since there's no shipped v1 session to stay compatible with — this
feature was never released). All three `static_assert`s in
`soundchip_turbosoundfm.cpp` updated accordingly.

**Verification.**
`SeekTo_NoiseGeneratorStateDeterministicFromDeltaFrame` passes after the fix
(byte-identical noise state after a delta-frame seek AND after a keyframe
seek AND after resuming forward playback from a delta-frame seek all the way
to a later checkpoint). Full TSFM suite: 45/45 passed. Full TTD suite:
655/655 passed. Zero compiler warnings.

**Cross-checked against `scratch/tsfm-issues3.ttd`** (the recording made
specifically to reproduce "occasional clicks on replay… not present in the
original sound, last 2-3 seconds"): decoded its TSFM blobs with the now-
fixed `tools/verification/ttd-analyzer` (see below) and confirmed every one
of its 8995 checkpoints carries the **1142-byte v1 payload** — i.e. this
recording predates the v2 fix. That was consistent with the click, but
turned out to be the wrong culprit: **the user confirmed after v2 landed
that the noise/tone restore issue was fixed, but the clicks were not** —
see "v3 fix" below for the actual click root cause and fix, found by
following up on exactly that report.

## v3 fix (2026-09-23, later still): output-stage flush + decimator phase

**Root cause of the clicks.** `TTDLoadState` restored the chip's core state
exactly (v1/v2), but never touched `SoundChip_TurboSoundFM`'s **output
stage**: each `TsfmChip::out` (hold register, LQ boxcar sum/count),
`FmWordQueue words`, and — critically — every `FilterDecimator`'s internal
FIR history ring buffer (4 SSG decimators + 2 FM decimators, all of them
slaved for cadence to the SSG-left ones). None of that is "chip state" and
none of it was meant to be restored — but leaving it completely untouched
meant it kept whatever it held **live**, i.e. real audio samples from
wherever the device happened to be playing right before the seek. The very
next output samples after a restore therefore ran the FIR convolution over a
buffer that mixed *stale audio from one point in the tune* with *freshly
restored generator output from a different point in the tune* — precisely
the kind of uncorrelated-history-meets-new-signal transient that's heard as
a click or thump. `reset()` already knew to clear all of this (decimators,
hold, LQ, word queues) — `TTDLoadState` just never called the equivalent.

**First attempt and the regression it caused.** Fixing this by calling the
same `.reset()` methods `reset()` uses (`FilterDecimator::reset()`, which
zeroes both the FIR ring **and** its own fractional resampling `_phase`)
immediately broke `SeekTo_NoiseGeneratorStateDeterministicFromDeltaFrame` —
the exact regression test that caught v1→v2. Investigation: `_phase` is not
"just" output buffering either — `handleStep()`'s inner
`while (!decimatorLeft().hasOutput())` loop (which calls `updateState()` to
tick the SSG generators) runs until the SSG-left decimator's own `_phase`
crosses its threshold, so `_phase` is a **third** generator-tick-gating
accumulator of exactly the same kind as `_samplePhase`/`_decimationPhase`
(v2) — just living one level down, inside `FilterDecimator` rather than
`SoundChip_TurboSoundFM`. It was missed in v2 because v2 only looked at the
TSFM class's own fields. Zeroing it on every restore reintroduces the same
drift-after-seek bug in a different accumulator; leaving it untouched (the
pre-v3 behavior) avoids that regression only by accident, for whichever
specific seek points happen not to expose it — not a real fix either.

**Actual fix.** Two independent concerns, resolved separately:

1. **Content** (the click): flush FIR history + hold + LQ + word queues to
   silence on every restore, via the same calls `reset()` already makes.
   Correct — this content is genuinely not recoverable/desirable to restore
   (design policy, matches every other TTD device), and "settle in from
   silence" over one filter length is inaudible where "snap from stale
   foreign audio" is not.
2. **Cadence** (determinism): added `FilterDecimator::phase()`/`setPhase()`
   accessors, added the 4 SSG decimators' `_phase` values to the TTD payload
   (v3, +32 bytes: `1158 → 1190`), and in `TTDLoadState`, call `reset()`
   first (flushes content) then `setPhase()` (restores cadence) on each of
   the 4 SSG decimators. FM decimators need no phase restore — they run
   permanently in slave mode (`attachMaster()` in `setCoreRate()`), so their
   own `_phase` is never consulted while a master is attached; only their
   FIR history needed flushing, which `reset()` already covers.

**Verification.** New test
`SeekTo_FlushesOutputStageToAvoidClickFromStaleHistory`: records real
playback, confirms the output stage is genuinely "warm" (non-zero hold/LQ
count) before a seek — precondition sanity — then asserts it's silent
(`hold == 0`, `lqSum == 0`, `lqCount == 0`, decimators report no output
ready) immediately after. **Verified the test actually catches the bug**: it
fails with the exact expected stale nonzero `hold` values when the flush
code is disabled, and passes with it enabled — not just a test that happens
to pass. `SeekTo_NoiseGeneratorStateDeterministicFromDeltaFrame` (the v2
regression test) was re-verified green *together with* this new test in the
same build, confirming the phase-restore fix resolves the v3 regression
without reopening v2's. Full TSFM suite: 45/45 passed (was 44/45 with the
v2-only fix's regression). Full suite: 3289/3289 passed, zero warnings.

Like v1/v2, this can't be verified against `scratch/tsfm-issues3.ttd` itself
(it predates all three fixes) — a fresh recording is needed to confirm the
click is gone by ear.

## Python TTD tooling audit (2026-09-23)

Checked whether `tools/verification/ttd-analyzer` needed changes to keep up
with the TSFM work:

- **On-disk container format is unchanged.** `Checkpoint.peripheral_blobs`
  is parsed as opaque per-device byte blobs (`parse_blob`/`parse_checkpoint`
  in `ttd_format.py`) regardless of which device owns a given id or what its
  internal layout is — the v1→v2 TSFM payload change (1142→1158 bytes) needed
  **zero** parser changes. Confirmed by re-validating all three recordings
  (`tsfm-issues.ttd`, `tsfm-issues2.ttd`, `tsfm-issues3.ttd`) with
  `./run.sh validate`: all OK.
- **Gap found and fixed: no blob decoder.** `peripheral_blobs[id]` held the
  raw on-disk *wrapped* bytes (`PeripheralBlobHeader` + optionally
  zstd-compressed payload, mirroring `TTDPeripheralRegistry::EncodeBlob`) —
  there was no Python equivalent of `TTDPeripheralRegistry::DecodeBlob` to
  unwrap it into the actual device state bytes analysis code wants. Added
  `decode_peripheral_blob(expected_id, blob) -> bytes` to `ttd_format.py`,
  byte-for-byte mirroring the C++ decoder (12-byte header parse, id-mismatch
  and size-mismatch refusal, zstd decompress when `compressedSize > 0`).
  Verified against the real files: all 8995 TSFM blobs in
  `tsfm-issues3.ttd` are zstd-compressed (exercises the decompress path, not
  just the trivial raw-store one) and every one decodes to its declared
  size. Added to the module's `_self_test()` too (`python -m ttd_format`).
- **Gap found and fixed: incomplete `PERIPHERAL_ID_NAMES`.** The table only
  went up to id 6 (`ScorpionProfROM`); ids 7 (`KempstonMouse`) and 8
  (`AtmPaging`) printed as bare integers in `info`/reports. Both added.
- No changes needed to `integrity_check.py`, `anomaly_detector.py`,
  `timeline_report.py`, or `framebuffer_renderer.py` — none of them
  interpret peripheral blob *contents*, only sizes/counts, which are
  unaffected by the TSFM payload change.
