# Phase S2 — Seek Engine Hardening + Exhaustive Stability Coverage

Started: 2026-07-29
Completed: 2026-07-29
Reference: Side-phase extending Phase 2 (seek engine) + Phase S1
(serialization). This phase fixes three real bugs discovered by exhaustive
testing and adds the canonical "no jitter over a long demo" coverage.

Parent TDD: §8 (Seek and Replay Engine), §10.4 (serialization).

## Outcome

The seek engine is now provably stable across every reachable target — no
screen corruption, no memory corruption, no drift across runs. The user's
original symptom ("rare screen corruption on some backward seek operations
with recover on next reposition") is fixed and pinned by 18 new tests
spanning short + long-duration scenarios.

**248 TTD tests green** (was 230 at Phase S1 close). The 18 new tests live
in `ttd_seek_exhaustive_test.cpp`:

- 10 short-duration tests in `TTD_Seek_Exhaustive_Test` /
  `TTD_Seek_Exhaustive_RoundTrip_Test` (60-frame recording, ~1.5s)
- 8 long-duration tests in `TTD_Seek_LongDuration_Test` (1500-frame / 30-sec
  recording, ~46s)

## Why this is a side-phase (Phase S2)

Linear phases 1, 2, S1 each shipped with green tests, but the test surface
was dominated by single-checkpoint or short-sequence scenarios. The user's
"rare corruption" report made clear that the existing tests missed three
real bugs that only surface under exhaustive permutation over a long
recording. Phase S2 closes that gap:

1. Adds the missing coverage (exhaustive matrix + 30-second long-duration)
2. Fixes the three bugs that coverage exposed
3. Pins them forever

Side-phase numbering keeps the linear phase progression intact (Phase 3
remains "next linear phase").

## Bugs fixed

### S2.1 — Backward-seek screen corruption (refcount leak in delta chain)

**Symptom**: rare screen corruption after a backward seek, recovering on
the next reposition. This is the user's original report.

**Root cause**: three compounding bugs in the COW page store's refcount
bookkeeping:

1. `TTDCodecPageStore::InternXor` did not AddRef `prevSlot` when emitting
   an `XorPrev` slot. The delta chain held an implicit reference that the
   refcount didn't track.

2. `TimeTravelManager::UpdateRamPages` made spurious `Release(prevSlot)`
   calls on both the P-frame and I-frame dirty paths, dropping refs still
   held by the previous checkpoint's `prevRamPages`.

3. `TTDCodecPageStore::Release` only freed the immediate slot, not the
   delta chain it anchored. A single Release could leak the entire chain.

Any one of these was survivable; all three together produced a use-after-
free when a slot's refcount hit 0 while still referenced by an earlier
checkpoint's delta chain. Restoring that earlier checkpoint then read
stale or zero-filled memory, producing the reported screen corruption.

**Fix**:

- `ttd_codec_page_store.cpp` `InternXor`: when emitting `XorPrev`, do
  `_slots[prevSlot].refcount++` before returning the new slot index.
- `ttd_codec_page_store.cpp` `Release`: iteratively unwind the delta
  chain — when a slot's refcount hits 0 and it has `encoding == XorPrev`,
  recursively Release its `prevSlot`.
- `timetravelmanager.cpp` `UpdateRamPages`: remove the spurious
  `Release(prevSlot)` calls on both dirty paths. The previous checkpoint's
  refs are managed by `InvalidateAfter` when the timeline is truncated,
  not by per-frame updates.

**Pinned by**: `TTD_Seek_Exhaustive_Test.BackwardSweep_InterleavedMatchesEveryCheckpoint`
(the exact user-reported scenario) +
`TTD_Seek_LongDuration_Test.EveryCheckpoint_AllApproaches_ProduceSameHash`
(1501 checkpoints × 5 approach angles, ~7500 seek operations).

### S2.2 — .ttd round-trip corruption (only first 4 KB sub-page of each 16 KB page survived)

**Symptom**: after `SerializeSession` → `DeserializeSession`, every
`XorPrev` slot's reconstructed page content was wrong. The first 4 KB
sub-page of each 16 KB page was correct; the remaining 12 KB was zero-
filled or systematically corrupted.

**Root cause**: contract mismatch between writer and reader.

- Writer (`SerializeSession`): emits `Compress(GetPage(idx))` — i.e. the
  zstd-compressed **full reconstructed 4 KB page** (not a delta).
- Reader (`DeserializeSession`): assumed the payload was a delta and
  XORed the decompressed buffer with `prev_page` before calling
  `InternXor`. `InternXor` then computed its own delta against `prev_page`.

The double-XOR produced this chain on `GetPage(slot)`:

```
stored delta = (full XOR prev) XOR prev = full
GetPage(slot) = decompress(prev) XOR decompress(this) = prev XOR full ≠ full
```

So every `XorPrev` slot decoded to `prev XOR full` instead of `full`.

**Fix**:

- `timetravelmanager.cpp` deserializer `XorPrev` branch: drop the spurious
  `XorBuffers(xorBuf, pageBuf, pageBuf, ...)` call. Pass the decompressed
  full page directly to `InternXor(prevSlot, pageBuf)`. `InternXor`
  internally decides between `XorPrev` and `Full` encoding for the in-
  memory representation.

**Pinned by**: `TTD_Seek_Exhaustive_RoundTrip_Test.SerializeDeserialize_PreservesAllReferences`,
`TTD_Seek_Exhaustive_RoundTrip_Test.DiskFile_WriteAndRead_PreservesAllReferences`,
`TTD_Seek_LongDuration_Test.Serialize30SecondSession_AllCheckpointsRoundTripIdentically`
(1501 checkpoints byte-identical after round-trip).

### S2.3 — SeekTo({lastFrame, T>0}) wrongly rejected

**Symptom**: intra-frame seek to any t-state within the last recorded
frame returned failure. The 30-second test matrix caught this on the
session-end frame.

**Root cause**: the bounds check in `SeekToInternal` compared
`sessionEnd < target` using `TTDTimePoint::operator<`, which is a
lexicographic (frame, tInFrame) comparison. Checkpoints sit at frame
boundaries, so `sessionEnd.tInFrame` is always 0. For any target with
`target.frame == sessionEnd.frame && target.tInFrame > 0`, the check
`sessionEnd < target` was true, causing wrongful rejection.

**Fix**: relax the check to `target.frame > sessionEnd.frame`. Intra-
frame replay at the session-end frame is valid — `ReplayWithinFrame`
handles it by running T t-states forward from the last checkpoint.

**Pinned by**: `TTD_Seek_LongDuration_Test.IntraFrameTState_NoDriftAcrossThreeRuns`
explicitly includes `frameIdx == N-1` (the session-end frame) in its
frame matrix.

## Coverage added

### `ttd_seek_exhaustive_test.cpp` — 18 new tests, 3 fixtures

File: `core/tests/debugger/ttd/ttd_seek_exhaustive_test.cpp`

#### `TTD_Seek_Exhaustive_Test` — short-duration (60 frames, ~1.5s)

Records a 60-frame session (1 baseline I-frame + 1 mid I-frame at frame 50
+ intervening P-frames), then exercises:

| Test | Scenario |
|---|---|
| `Setup_RecordsSessionSpanningTwoKeyFrames` | Timeline shape: 2+ I-frames, anchor invariant |
| `FrameAligned_SeekEveryCheckpoint_FromMidpoint` | Every cp reachable from a midpoint starting position |
| `FrameAligned_SeekEveryCheckpoint_FromEnd` | Every cp reachable from session-end (backward sweeps) |
| `FrameAligned_SeekEveryCheckpoint_FromStart` | Every cp reachable from session-start (forward sweeps) |
| `BackwardSweep_InterleavedMatchesEveryCheckpoint` | The exact user-reported pattern: `N-1→0→N-2→1→...` |
| `RandomOrder_SeekEveryCheckpoint_MatchesReference` | Random-seed sweep (std::mt19937), every cp matches reference |
| `RepeatSeek_ToSameCheckpoint_IsStableAcrossIterations` | 10 iterations to same target with decoy between (refcount drift) |
| `IntraFrame_TStateSeek_IsDeterministic` | 7 frames × 8 t-states × 2 runs — overshoot OK, drift NOT OK |

#### `TTD_Seek_Exhaustive_RoundTrip_Test` — short-duration with .ttd round-trip

Inherits from `TTD_Seek_Exhaustive_Test`. Records 60 frames, serializes to
`.ttd`, deserializes back, re-runs the entire reference matrix.

| Test | Scenario |
|---|---|
| `SerializeDeserialize_PreservesAllReferences` | In-memory stringstream round-trip |
| `DiskFile_WriteAndRead_PreservesAllReferences` | Real file on disk (`/tmp/ttd_seek_exhaustive_session.ttd`) |

#### `TTD_Seek_LongDuration_Test` — long-duration (1500 frames, ~46s)

Inherits from `TTD_Seek_Exhaustive_Test`. Records 30 seconds @ 50 Hz =
1500 frames (31 I-frames + 1470 P-frames, delta chains up to depth 49).
The canonical "nothing flaked over a long demo" coverage.

| Test | Scenario | Runtime |
|---|---|---|
| `Record30Seconds_TimelineHasExpectedShape` | 31 I-frames + anchor invariant over 1501 cps | ~5s |
| `EveryCheckpoint_AllApproaches_ProduceSameHash` | 5 approaches (self/prev/next/far-earlier/far-later) × 1501 cps | ~7s |
| `FiveFullSweeps_NoDriftAcrossRuns` | Forward + backward + 3 random shuffles, every cp identical across all 5 sweeps | ~10s |
| `PingPong_Pattern_NoCorruptionAfterRapidTransitions` | Every 7th cp × offsets {1,5,25,49} × 3 iterations | ~7.5s |
| `DeltaChain_AllDepths_1through49_RestoreIdentically` | Every I-frame interval × depths {1,5,25,49}, 2 runs each | ~2.8s |
| `IntraFrameTState_NoDriftAcrossThreeRuns` | 13 frame indices × 6 t-state offsets × 3 runs, zero drift | ~1.6s |
| `Serialize30SecondSession_AllCheckpointsRoundTripIdentically` | All 1501 cps byte-identical after .ttd round-trip | ~4s |
| `PostRoundTrip_FiveSweeps_NoDrift` | 5 sweeps on deserialized timeline, zero drift | ~10s |

Total: ~30,000 seek operations across the long-duration suite, zero drift.

### Reference hashing strategy

The reference hash for checkpoint `i` is computed via
`RestoreCheckpointForTesting(i)`, not via `SeekTo({frame_i, 0})`. Both
code paths invoke the same `RestoreCheckpoint` private helper, but
`RestoreCheckpointForTesting` is guaranteed to land exactly on the
captured state with no replay side effects.

Two hashes per checkpoint:

- `HashNow()` — full machine state: CPU registers, hidden flags, chipset
  state (p7FFD, pFE, AY registers, etc.), and a digest of every model-RAM
  byte (`config.ramsize * 1024`). Catches any memory corruption.
- `HashScreen()` — first 6912 bytes of RAM page 0 (the standard Spectrum
  VRAM: 256×192 pixels / 8 + 32×24 attribute bytes). Catches the exact
  user-visible artifact of memory corruption in page 0.

A divergence where `HashNow` differs but `HashScreen` is stable means
memory outside VRAM was corrupted; a divergence in both means VRAM
itself was clobbered (the user-reported symptom).

## Intra-frame seek contract (refined)

Pre-Phase S2 the contract was unspecified and the test asserted exact
t-state landings. RunTStates stops at the next instruction boundary, so
the test was flaky.

**Refined contract** (per user 2026-07-29): "t-state overshoot is ok, but
there must be no drift between runs. Always same position right or after
specified t-state after current instruction handling ends".

Test enforces:

1. `actual.tInFrame >= T` — never undershoot (RunTStates stops at or after
   the target t-state).
2. `actual.frame == target.frame` — intra-frame seek must not advance the
   frame counter.
3. `pos_run1 == pos_run2 == pos_run3` — zero drift across multiple runs to
   the same `{frame, T}` target.
4. `hash_run1 == hash_run2 == hash_run3` — full machine state identical
   across runs.

## Items NOT shipped in Phase S2 (non-goals)

- **Exact t-state landing** — would require either single-t-state stepping
  (not the current RunTStates granularity) or rollback-to-target. Out of
  scope; the user explicitly accepted instruction-boundary overshoot.
- **Multi-threaded seek stress** — the seek engine is single-threaded by
  contract (the emulator lock must be held). Concurrent WebAPI writes
  during recording are covered by the existing
  `TTDExternalEventJournal::mutex` and tested in `ttd_input_journal_test.cpp`.
- **GDB reverse-debugging integration** — Phase G1/G2 territory.

## Files modified

| File | Change |
|---|---|
| `core/src/debugger/ttd/ttd_codec_page_store.cpp` | `InternXor`: AddRef prevSlot on XorPrev. `Release`: iterative delta-chain unwind. |
| `core/src/debugger/ttd/timetravelmanager.cpp` | `UpdateRamPages`: remove spurious Release calls. Deserializer `XorPrev` branch: drop double-XOR. `SeekToInternal`: relax session-end check to `target.frame > sessionEnd.frame`. |
| `core/tests/debugger/ttd/ttd_status_endpoint_test.cpp` | Update stale v1 assertions: `pageStoreBytes` / `pageStoreUsedBytes` now report slot-vector capacity + compressed payload bytes. Relax `sessionHeapBytes` assertion (EstimateSessionHeapBytes excludes per-slot payload heap allocation). |
| `core/tests/debugger/ttd/ttd_seek_exhaustive_test.cpp` | NEW: 18 tests across 3 fixtures covering every seek permutation over short and long durations. |

## Exit criteria — all met

- [x] All 230 pre-existing TTD tests still green
- [x] All 18 new exhaustive/long-duration tests green
- [x] The user-reported "rare backward-seek screen corruption" pinned by
      `BackwardSweep_InterleavedMatchesEveryCheckpoint` and
      `EveryCheckpoint_AllApproaches_ProduceSameHash`
- [x] .ttd round-trip byte-identical over 1501 checkpoints
- [x] Zero drift across 5 sweeps + 3 random orderings over 1501 checkpoints
- [x] Full TTD suite runtime under 60 seconds
