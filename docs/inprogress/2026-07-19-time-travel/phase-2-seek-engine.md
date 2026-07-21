# Phase 2 — TTD Seek Engine

Started: 2026-07-19 (planning)
Reference: `docs/emulator/design/debugger/time-travel-debug/implementation-plan.md` §3.A2
Parent TDD: §8 (Seek and Replay Engine), §5.1 (Input journal + external events), §4.2 (Detached state)

Cross-cutting decisions applied: see [`decisions.md`](decisions.md).
Phase 1 deliverables: see [`phase-1-recorder.md`](phase-1-recorder.md).

## Outcome (target)

Seekable history: any captured checkpoint can be restored to the live
emulator, and arbitrary intra-frame points can be reached by silent
replay from the nearest preceding checkpoint. Resume-from-past truncates
the timeline cleanly. The recorder never produces a desync because the
replay path is observationally silent and the divergence harness gates
the merge.

**Exit criteria** (per implementation-plan §3.A2):
- `TTD_Seek_ArbitraryPoint` green (restore + intra-frame replay)
- `TTD_Session_TruncateOnResume` green
- `TTD_Thinning_EveryPointReachable` green
- `ttd_seek_benchmark` meets targets (1–20 ms dense tier)
- Divergence test green over the corpus (BASIC idle, scroller,
  contention-sensitive multicolor, AccuracyCoinZX, TR-DOS loader)

## Item ordering (each keeps the divergence suite green)

1. **Restore path** ✅ DONE 2026-07-19 (parent TDD §8.1 step 2). Field copies
   for CPU + chipset already exist (`RestoreCpuState` / `RestoreChipsetState`);
   this item wires the higher-level orchestrator: rebuild memory banking
   via `Memory::UpdateZ80Banks` (reads restored `p7FFD` + `CF_TRDOS` flag),
   memcpy RAM pages from the COW page store, dispatch `TTDLoadState` on every
   peripheral, call `Screen::InitFrame()`. No replay yet — destination is a
   frame boundary. Implementation: `RestoreCheckpointForTesting(idx)` public
   entry + `RestoreCheckpoint(cp)` + `RestoreRamPages(pages)` private helpers
   in `timetravelmanager.{h,cpp}`. Tests: 9 cases in `ttd_restore_test.cpp`,
   all green. Full TTD suite: 84/84 green.

2. **Silent-replay mode** ✅ DONE 2026-07-19 (parent TDD §8.2 + Appendix C).
   Adds `_context->ttdReplayActive` plus `TimeTravelManager::EnterReplayMode()` /
   `ExitReplayMode()` / `IsReplayActive()`. Wired suppression sites:
     - BreakpointManager::Handle{PCChange,MemoryRead,MemoryWrite,PortIn,PortOut}
       → early-return BRK_INVALID
     - AnalyzerManager::dispatch{FrameStart,FrameEnd} → early return
     - DebugKeyboardManager::{PressKey,ReleaseKey} → early return (live input
       blocked; journal injection lands in Item 3)
     - RecordingManager::CaptureFrame → early return
     - MainLoop::OnFrameEnd NC_VIDEO_FRAME_REFRESH post → skipped
     - SoundManager mute forced true on EnterReplayMode, restored on exit
       (device ticks handleStep/handleFrameStart keep running — critical for
       AY determinism per TDD §8.2 last paragraph)
   Checkpoint-capture suppression is automatic: replay is driven from Detached
   state, and OnFrameBoundary already early-returns when state != Recording.
   Tests: 12 cases in `ttd_replay_mode_test.cpp`, all green. Full TTD suite:
   96/96 green.

3. **Input journal** ✅ DONE 2026-07-19 (parent TDD §5 row #1 + §5.1).
   New module `ttd_input_journal.{h,cpp}` with
   `TTDInputEvent { TTDTimePoint time; uint8_t key; bool pressed }` and an
   append-only `TTDInputJournal` (Record / Peek / Inject / DropAfter /
   Clear). Capture wired into `DebugKeyboardManager::{PressKey,ReleaseKey}`
   via `TimeTravelManager::RecordInputEvent`, AFTER the Item 2 replay
   guard so injected events are not re-recorded. Injection API
   `InjectDueEvents(Keyboard&, now)` fires every event with
   `time == now` (exact match — each event fires once). Manager exposes
   `InjectDueInputEvents(now)` helper that resolves the Keyboard pointer.
   Journal cleared on `StartRecording` and `InvalidateSession(reason)`;
   preserved by `StopRecording`. Tests: 27 cases in
   `ttd_input_journal_test.cpp` (3 fixtures: pure journal, inject,
   capture+lifecycle), all green. Full TTD suite: 123/123 green.

4. **`SeekTo` / `StepBackInstruction` / `StepBackFrame` / `StepForward*`**
   ✅ DONE 2026-07-20 (parent TDD §8.1 step 3 + §8.1 last paragraph;
   StepBackInstruction deferred). Binary-search the timeline via
   `std::upper_bound`, restore, optional intra-frame silent replay via
   `ReplayWithinFrame`. Step helpers compose SeekTo with frame-counter
   arithmetic. State transitions Idle → Recording → Detached per §4.2.
   Tests: 24 cases in `ttd_seek_test.cpp`. Full TTD suite: 141/141 green.

5. **Resume-from-past truncation** ✅ DONE 2026-07-20 (parent TDD §8.3).
   When the user resumes from a Detached position T, drop everything > T
   from the timeline and journals, release page refs, return to Recording.
   Atomic under pause. Tests: 19 cases in `ttd_resume_test.cpp`. Full
   TTD suite: 183/183 green.

6. **External-event markers as replay barriers** ✅ DONE 2026-07-19
   (parent TDD §5.1 + §717 `halt_reason ∈ {target, external_event, out_of_range}`
   contract). For sources of nondeterminism that aren't journaled in v1
   (tape control, disk writes, debugger edits), record a marker on the
   timeline. Seek refuses to cross a marker silently — returns the marker's
   time and reason via `TTDSeekResult`. Production hook points wired:

   - **Tape control** (`tape.cpp`): `startTape` / `stopTape` / `reset` (only
     when `wasStarted`) each emit a `TapeControl` marker.
   - **WD1793 disk writes** (`wd1793.cpp`): `cmdWriteSector` and
     `cmdWriteTrack` emit `DiskWrite` markers after write-protect / disk-
     presence early-returns, with reason formatting `trk/sec/side`.
   - **Debugger memory edits** (CLI `memory write` dispatcher + WebAPI
     `POST /memory/write`) emit `DebuggerEdit` markers — one marker per
     user action, regardless of byte count.

   Python `WriteByte` / WebAPI `PUT /memory/{addr}` / `memory fill` / direct
   `Memory::DirectWriteToZ80Memory` callers follow the same pattern but are
   not yet wired (lower-traffic paths — add when these surfaces are extended).
   Tests: 41 cases in `ttd_external_events_test.cpp` (3 suites: pure journal,
   capture integration, marker-blocks-seek) + 12 hook integration cases in
   `ttd_external_events_hooks_test.cpp`. Full TTD suite: 224/224 green.

7. **Divergence corpus + capture-benchmark gate** (exit work). Wire the
   five corpus titles into a `TTD_Divergence_*` test family. Add a CI
   gate that fails if `ttd_capture_benchmark` regresses past the budget.

## Detailed notes per item

### Item 1 — Restore path ✅ DONE

**Status:** Implemented + tested 2026-07-19.

**Files touched:**
- `core/src/debugger/ttd/timetravelmanager.{h,cpp}` — new public
  `RestoreCheckpointForTesting(size_t idx)` test entry + private
  `RestoreCheckpoint(const TTDCheckpoint&)` + `RestoreRamPages(const std::vector<TTDPageRef>&)`.
- Reuses existing `RestoreCpuState` / `RestoreChipsetState` from
  `ttd_checkpoint.cpp` (already implemented in Phase 1).
- Calls into: `Z80` (CPU copy target), `EmulatorState` (chipset target),
  `Memory::UpdateZ80Banks` (rebuilds banking from port latches),
  `Screen::InitFrame`, each peripheral's `TTDLoadState`.

**Sequence implemented** (per TDD §8.1 step 2):
1. `RestoreCpuState(checkpoint.cpu, &z80.state)` — fixes registers, MEMPTR,
   Q, IFF, HALT, eipos, haltpos.
2. `RestoreChipsetState(checkpoint.chipset, &emulatorState)` — port latches
   (p7FFD etc.) + counters (t_states, frame_counter).
3. `Memory::UpdateZ80Banks()` — reads the restored `p7FFD` and `CF_TRDOS`
   flag to rebuild the live ROM/RAM banking. (Simpler and more robust than
   walking the PortDecoder: UpdateZ80Banks is the existing canonical
   "re-derive banks from latches" path used elsewhere in the codebase.)
4. `RestoreRamPages(checkpoint.ramPages)` — memcpy 16 KB per model-RAM
   page from the COW page store slot into `memory._ramPages[i]`. Skips
   `kNeverTouched` pages (their live content is the baseline).
5. Dispatch `TTDLoadState(blob.data())` on AY/Tape/Covox/FDC via the
   anonymous-namespace `RestorePeripheral` helper (mirroring
   `CapturePeripheral`). Empty blobs and absent devices are no-ops.
6. `screen->InitFrame()` so the renderer starts fresh from the restored
   beam position; the next rendered frame is the historical frame.

**Design deviation from plan:** the plan said "walk PortDecoder and re-apply
latches". Implementation chose `Memory::UpdateZ80Banks()` instead because
that's the existing canonical path the emulator already uses for the same
purpose (after Reset, after snapshot Load). PortDecoder replay would have
re-done work that UpdateZ80Banks already covers and would have required a
list of "banking-relevant ports" which doesn't exist as a single registry.
The two approaches produce the same live memory map.

**Tests** (`ttd_restore_test.cpp`, 9 cases, all green):
- `Restore_OutOfRange_Fails` — bounds check
- `Restore_NamedByAnonymousBlock` — index 0 is restorable
- `RoundTrip_BaselineHashMatchesAfterRestore` — full state hash via
  `MachineStateHash::CaptureSnapshot + HashSnapshot`
- `RoundTrip_RestoreToLaterCheckpointAlsoMatches` — three checkpoints,
  restore-to-each, hash matches each time
- `Restore_PreservesUndocumentedRegisters` — MEMPTR / Q / eipos / haltpos
- `Restore_PreservesRamPagesContent` — byte-for-byte page 0 check
- `Restore_PreservesPortLatchesAndPaging` — p7FFD flip + paging rebuild
- `Restore_PreservesCounters` — t_states / frame_counter
- `Restore_TwiceInARow_ProducesSameState` — idempotency

**Pitfall discovered:** writes via `Memory::RAMPageAddress()` return a raw
pointer that bypasses the MemoryWriteDebug hook — the dirty tracker never
sees them. Tests must call `tracker->MarkDirty(page)` explicitly when
mutating RAM outside the CPU's write path. This is documented in
`ttd_restore_test.cpp` and `ttd_manager_test.cpp`'s existing dirty-page tests.

### Item 2 — Silent replay mode ✅ DONE

**Status:** Implemented + tested 2026-07-19.

**Files touched:**
- `core/src/emulator/emulatorcontext.h` — added `bool ttdReplayActive = false;`
  with a documentation comment referencing TDD §8.2 + Appendix C.
- `core/src/debugger/ttd/timetravelmanager.{h,cpp}` — added
  `EnterReplayMode()` / `ExitReplayMode()` / `IsReplayActive()` public API.
  EnterReplayMode saves + forces SoundManager mute; ExitReplayMode restores.
  Idempotent + nest-safe (`_inReplayMode` flag prevents overwriting saved
  mute state on a second EnterReplayMode call).
- One suppression site per row of TDD §8.2's table:
  - `BreakpointManager::Handle{PCChange,MemoryRead,MemoryWrite,PortIn,PortOut}`
    → early-return BRK_INVALID when `_context->ttdReplayActive`.
  - `AnalyzerManager::dispatch{FrameStart,FrameEnd}` → early return.
  - `DebugKeyboardManager::{PressKey,ReleaseKey}` → early return (live input
    blocked; journal injection is Item 3).
  - `RecordingManager::CaptureFrame` → early return.
  - `MainLoop::OnFrameEnd` NC_VIDEO_FRAME_REFRESH post → guarded with
    `if (!_context->ttdReplayActive)`.
  - `SoundManager` mute forced true by EnterReplayMode, restored on exit.
    Device ticks (handleStep / handleFrameStart) keep running — critical
    for AY/envelope determinism per TDD §8.2 last paragraph.
  - `TimeTravelManager::OnFrameBoundary` — already early-returns when state
    != Recording; replay is driven from Detached, so checkpoint capture is
    automatically suppressed without an explicit flag check.

**Design notes:**
- Flag is plain bool, not atomic. Replay runs under the existing pause
  discipline: EnterReplayMode → RunTStates → ExitReplayMode all happen on
  the control thread with the emulator paused. No cross-thread visibility
  concern; matches the existing pattern for emulatorState flags.
- Audio muting uses the existing `SoundManager::mute()` facility — it only
  zeroes the host output buffer at the boundary in `handleFrameEnd`, exactly
  matching the TDD's "host-buffer submission muted, device state advances"
  contract.
- MessageCenter notifications other than NC_VIDEO_FRAME_REFRESH (e.g.
  NC_EXECUTION_CPU_STEP posted once per RunTStates call) are intentionally
  NOT suppressed — they're cheap (one post per call, not per t-state) and
  observers filter them already.

**Tests** (`ttd_replay_mode_test.cpp`, 12 cases, all green):
- `EnterReplayMode_SetsFlag` / `ExitReplayMode_ClearsFlag` — flag mechanics
- `EnterReplayMode_Idempotent_NestSafe` — double-enter doesn't overwrite
  saved mute state
- `ExitReplayMode_Idempotent_WhenNotInReplay` — extra exit is a no-op
- `EnterReplayMode_ForcesMute` / `ExitReplayMode_RestoresUnmutedState` /
  `ExitReplayMode_PreservesPreExistingMute` — sound save/restore matrix
- `Breakpoint_HandlePCChange_SkippedDuringReplay` — full before/during/after
- `Breakpoint_HandleMemoryWrite_SkippedDuringReplay`
- `Analyzer_dispatchFrameStart_NoOpDuringReplay` — counting analyzer
- `Keyboard_PressKey_BlockedDuringReplay`
- `OnFrameBoundary_NoNewCheckpoint_InReplayContext` — documents the
  automatic checkpoint-capture suppression

**Deferred:**
- Per-t-state Screen rendering skip (TDD "final segment only"). Reuses the
  ScreenHQ=off batch-render path; not needed for Item 2 correctness, only
  for seek-latency optimization. Will be revisited when Item 4 (SeekTo) is
  wired and `ttd_seek_benchmark` becomes the gate.
- Sequence-based keyboard input (TapKey, QueueSequence). The OnFrame
  processor still runs during replay — Item 3 will address this when the
  input journal injects recorded events instead.

### Item 3 — Input journal ✅ DONE

**Parent TDD refs:** §5 row #1 (determinism audit), §5.1 (External-Event
Markers / Input journal).

**What landed:**

- New module `core/src/debugger/ttd/ttd_input_journal.{h,cpp}`:
  - `TTDInputEvent { TTDTimePoint time; uint8_t key; bool pressed; }`
    (17 B on 64-bit). `key` is `ZXKeysEnum` stored as `uint8_t` to keep
    `keyboard.h` out of the header.
  - `TTDInputJournal` — append-only sorted vector with:
    - `Record(ev)` — capture path
    - `Events() / Size() / IsEmpty()` — read access
    - `PeekNextEventTimeOnOrAfter(from)` — next-pending-event query for
      the seek engine
    - `InjectDueEvents(Keyboard&, now)` — injects every event with
      `time == now` (exact-match, not `<=`; each event fires once at its
      recorded TTDTimePoint)
    - `DropAfter(t)` — Resume-from-past hook for Item 5 (keeps events
      with `time <= t`)
    - `Clear()` — full reset

- Capture wired into `DebugKeyboardManager::{PressKey, ReleaseKey}`.
  Order: replay-guard → record-if-Recording → apply-to-keyboard. The
  record call lives AFTER the Item 2 replay guard so events injected
  during replay are not re-recorded.

- `TimeTravelManager` facade:
  - `RecordInputEvent(uint8_t key, bool pressed)` — derives the current
    `TTDTimePoint` from `EmulatorState` (`frame_counter` +
    `t_states % config.frame`) and forwards to the journal.
  - `InjectDueInputEvents(const TTDTimePoint& now)` — looks up the
    Keyboard pointer from `EmulatorContext` and dispatches; defensive
    no-op when not in replay mode or no keyboard.
  - `GetInputJournal()` — const read-only accessor for tests/seek engine.

- Journal cleared alongside timeline on `StartRecording` (fresh session),
  `InvalidateSession(reason)`. Preserved by `StopRecording` so the seek
  engine can still replay events from the captured history.

**Design decisions:**

- **Exact-match injection, not `<=`.** Each event must fire exactly once,
  at its recorded TTDTimePoint. `<=` would re-inject earlier events every
  step; `==` plus the seek engine's "advance to next event, fire, repeat"
  loop (Item 4) is the correct shape.

- **Plain `std::vector`, not a tree or ring.** A 5-minute session at
  ~10 keys/sec sustained typing is ~3 000 events = ~50 KB. Linear scan
  is fast enough; binary search would add complexity without measurable
  benefit at this scale.

- **`uint8_t` storage for key, not `ZXKeysEnum`.** Avoids pulling
  `keyboard.h` into every translation unit that includes the journal
  header. Cast at the boundary in `.cpp`.

- **Forward-declared `Keyboard` in journal header.** `InjectDueEvents`
  is defined out-of-line in the `.cpp` (where `keyboard.h` is included),
  keeping the header light.

- **Monotonicity not enforced in the journal.** `Record()` is a plain
  append. Host input events arrive in wall-clock order which under the
  emulator's pause/pacing maps to monotonic TTDTimePoint order. A future
  hardening pass could add an assert + log in `RecordInputEvent`.

**Tests:**

- New `core/tests/debugger/ttd/ttd_input_journal_test.cpp` — 27 tests
  across three fixtures:
  - `TTD_InputJournal_Test` (10 tests) — pure journal data structure:
    empty/Record/Clear/Peek (4 cases)/DropAfter (5 cases).
  - `TTD_InputJournal_Inject_Test` (4 tests) — `InjectDueEvents` against
    the real Keyboard: zero-match/count-match/press+release round
    trip/simultaneous-events.
  - `TTD_InputJournal_Capture_Test` (9 tests) — end-to-end capture via
    `DebugKeyboardManager` + lifecycle integration: no-capture-outside-
    Recording, capture-on-press, capture-on-release, no-capture-during-
    replay, manager-helper inject count, manager-helper no-op-when-not-
    in-replay, `StartRecording` clears, `StopRecording` preserves,
    `InvalidateSession` clears.

- Full TTD suite: **123/123 green** (was 96 — added 27 input journal
  tests). All other test suites unchanged.

**Deferred to Item 4 (Seek API):**

- The replay-time *coordination* loop (advance RunTStates to the next
  event's TTDTimePoint, call `InjectDueInputEvents`, repeat until
  target). The journal API needed for that loop
  (`PeekNextEventTimeOnOrAfter`, `InjectDueInputEvents`) is already in
  place.

- Per-instruction injection callback. The seek engine will instead use
  the "advance to next event" pattern above — same precision, no
  per-instruction overhead.

**Parent TDD field-name reconciliation:** the original Item 3 stub
sketched the record as `(TTDTimePoint, keyMatrix, kind)`. Shipped as
`(TTDTimePoint, key, pressed)` because:

- The emulator already models the keyboard as 40 individual keys
  (`ZXKeysEnum`), not as an 8-row bitmask. Per-key events are how
  `Keyboard::PressKey/ReleaseKey` actually mutate state.
- A row-bitmask would force the journal to capture every partial-row
  state, doubling record count for chord presses (CAPS+letter).
- `pressed: bool` is simpler than a `kind: enum` with two values and
  maps directly to the existing Keyboard API.

### Item 4 — Seek API ✅ DONE (StepBackInstruction deferred)

**Parent TDD refs:** §8.1 (SeekTo Algorithm), §8.2 (Silent Replay Mode).

**What landed:**

- Public API on `TimeTravelManager`:
  - `bool SeekTo(const TTDTimePoint& target)` — full algorithm: binary
    search → RestoreCheckpoint → optional intra-frame silent replay →
    Detached transition.
  - `bool StepBackFrame()` / `bool StepForwardFrame()` — frame-counter
    arithmetic composing SeekTo. Preserves the intra-frame t-state.
  - `TTDTimePoint CurrentPosition() const` — reads `frame_counter` +
    `z80.t` for the live position.
  - `TTDTimePoint SessionEndPosition() const` — time of the last captured
    checkpoint.

- Private helper `ReplayWithinFrame(targetFrame, targetTInFrame)`:
  - Wraps the whole loop in `EnterReplayMode` / `ExitReplayMode`.
  - Single-pass linear scan of the input journal for events scheduled
    inside `[0, targetTInFrame]` of the target frame.
  - Drives `Emulator::RunTStates` in chunks: advance to the next event's
    `tInFrame`, inject (and any others at the same TTDTimePoint), repeat.
  - After the last in-interval event, runs the remaining delta to
    `targetTInFrame`.

- Intra-frame position now sourced from `z80.t`, NOT `t_states % frame`.
  Root cause: `emulatorState.t_states` is only updated at frame boundaries
  (MainLoop::OnFrameEnd does `t_states += config.frame`), so its modulo is
  always 0. `z80.t` is the per-frame counter that `AdjustFrameCounters`
  resets at each boundary. This fix also corrected `RecordInputEvent`
  (Phase 2 Item 3) — captured events now have the correct t-in-frame.

- `z80.t = 0` sync after RestoreCheckpoint: the Z80 accumulator is in the
  field-exclusion list (host-side per ttd_checkpoint.h), so RestoreCheckpoint
  leaves it untouched. SeekTo explicitly zeroes it before intra-frame
  replay — checkpoints always sit at frame boundaries where `z80.t == 0`.

- SeekTo preconditions enforced: state must be Recording or Detached, the
  timeline must be non-empty, and the target must be `<= SessionEndPosition`.
  All violations log a warning and return false.

**State machine:**

- `Recording → Detached`: any successful SeekTo (typical case — user pauses,
  scrubs back).
- `Detached → Detached`: re-seeking from a Detached position (forward or
  backward). The original Recording state is NOT preserved — once Detached,
  the user is browsing history. Resuming forward execution is Item 5
  (Resume-from-past truncation).

**Tests:**

- New `core/tests/debugger/ttd/ttd_seek_test.cpp` — 24 tests:
  - Helpers: `CurrentPosition` initial frame, `SessionEndPosition` empty /
    baseline / advancing.
  - SeekTo preconditions: Idle, empty timeline, target beyond session end.
  - SeekTo frame-aligned: at baseline (round-trip from RunFrames(2)), to
    mid-timeline checkpoint, full round-trip determinism (5 frames, each
    SeekTo hash matches the corresponding RestoreCheckpointForTesting
    hash), state transition Recording → Detached, Detached → Detached
    backward, Detached → Detached forward.
  - SeekTo intra-frame: small t-in-frame (z80.t lands near target),
    replay-mode flag cleared on return, full round-trip determinism
    at t=500, no-replay-when-tInFrame==0.
  - StepBackFrame / StepForwardFrame: at-frame-0 fails, at-last-frame
    fails, preserves t-in-frame, Idle fails, Detached composition.

- Round-trip hash reference: `RestoreCheckpointForTesting(N)`, not the
  live hash after `RunFrames(N)`. Reason: the checkpoint is captured at
  the OnFrameBoundary point (mid-OnFrameEnd, BEFORE OnFrameStart runs
  Tape/SoundManager/Screen::InitFrame). The natural live state after
  RunFrames is post-OnFrameStart, so its hash differs by those side
  effects. SeekTo lands at the same point RestoreCheckpointForTesting
  does — they share the RestoreCheckpoint code path.

- Full TTD suite: **147/147 green** (was 123 + 24 seek tests). All other
  test suites unchanged (4 pre-existing failures: TRDOS port,
  BreakpointManager portIn, KeyboardInjection x2).

**Design decisions:**

- **StepBackInstruction deferred.** Per TDD §8.1 last paragraph, this
  requires replaying from the previous checkpoint while remembering the
  last-seen PC boundary (cost: one intra-frame replay, ≤ 20 ms). The
  infrastructure (ReplayWithinFrame) is already in place — what's missing
  is the per-instruction PC probe. Better as a focused follow-up than
  rushed into this item.

- **Single-pass linear scan of the journal** during ReplayWithinFrame,
  not binary search. Typical journals are a few hundred events; the scan
  exits early on the first event past the target. A binary search would
  add complexity without measurable benefit at this scale.

- **EXPECT_NEAR(±32) in step tests** for the post-seek z80.t. RunTStates
  can't stop mid-instruction; the last instruction may overshoot by up
  to 23 t-states (longest Z80 instruction). ±32 covers the worst case
  with margin.

- **No `_currentPosition` member.** Deriving from `EmulatorState` on
  demand keeps the seek engine stateless between calls — `CurrentPosition()`
  is a pure read. The Detached state itself is tracked by `_state`, not
  by a stored position.

- **Forward replay across frame boundaries.** `RunTStates` calls
  `OnFrameEnd`/`OnFrameStart` when `z80.t >= frameLimit`. During replay,
  OnFrameEnd's heavy lifting is suppressed (CaptureFrame, NC_VIDEO_FRAME_REFRESH,
  analyzer dispatch all gated on `ttdReplayActive`), and OnFrameBoundary
  auto-suppresses (state != Recording during Detached). So multi-frame
  forward seeks work transparently.

**Parent TDD API reconciliation:** the TDD sketched five operations
(SeekTo, StepBackInstruction, StepBackFrame, StepForwardInstruction,
StepForwardFrame). Shipped three (SeekTo, StepBackFrame, StepForwardFrame)
plus the two helpers (CurrentPosition, SessionEndPosition). The two
instruction-level ops need the per-instruction PC probe — deferred.

### Item 5 — Resume-from-past truncation ✅ DONE

**Parent TDD refs:** §8.3 (Resume-from-Past / History Truncation).

**What landed:**

- Public API on `TimeTravelManager`:
  - `bool ResumeRecordingFrom(const TTDTimePoint& from)` — atomic resume
    from a historical position. Internally composes:
    1. Precondition checks (state non-Idle, non-empty timeline, target
       within session bounds).
    2. `SeekTo(from)` — ensures the emulator is positioned at `from`
       (idempotent if the caller already seeked). This is what makes the
       API self-contained and matches the TDD's "atomic under pause"
       contract.
    3. `TruncateTimelineAfter(from)` — release page refs for every
       checkpoint with `cp.time > from`, then erase them.
    4. `_inputJournal.DropAfter(from)` — keeps events with `time <= from`
       (i.e. drops strictly-after events; events exactly at `from` are
       preserved).
    5. `_state = Recording`.

- Private helper `TruncateTimelineAfter(from)`:
  - Same `std::upper_bound` comparator shape as `SeekTo`, so the two
    methods agree on "strictly after" semantics.
  - Calls `ReleaseCheckpointRefs(cp)` for each dropped checkpoint before
    erasing — page store slots become eligible for reuse by future
    `Intern` calls (TDD §6.3).
  - No-op if every checkpoint has `time <= from` (common case when `from`
    is exactly at the last captured frame boundary).

- `writeJournal.truncate(after = T)` (line 5 of the original sketch) is
  deferred to Phase 4 (write journal, TDD §9.3) — there's no write
  journal in v1.

**Behavior highlights:**

- After resume, the next `OnFrameBoundary` appends a fresh checkpoint at
  frame `from.frame + 1`, so recording continues normally.
- The dirty tracker is NOT reset — its `_everDirty` set still tracks
  changes from the original session baseline, which is correct (we want
  to keep paying the cheap `AddRef` cost for pages untouched since the
  baseline).
- Released page store slots are reused by subsequent captures: capacity
  does not grow when recording resumes (verified by
  `PageStore_SlotsReusedAfterResume`).
- Dropped checkpoints are no longer seekable: `SeekTo` to a frame in the
  dropped range fails with the standard "target beyond session end"
  warning.

**Design decisions:**

- **SeekTo happens inside Resume, not before it.** The original sketch
  ("On Resume() while Detached at T") assumed the caller had already
  seeked. Making the seek internal means the API is one call, not two,
  which matches the GDB TDD's `c` (continue) semantics: "past the end of
  history transitions to live execution with the truncation rule of
  parent TDD §8.3".
- **Strict-greater-than truncation rule.** Checkpoints and journal
  events exactly at `from` are KEPT. This matters for the edge case
  where the user steps back to the very last captured frame and then
  resumes — no data is lost, the timeline is unchanged.
- **No `_dirtyTracker->ResetSession()` on resume.** Unlike
  `InvalidateSession`, resume preserves the session's baseline capture
  semantics. The dirty tracker's accumulated state is still valid for
  the surviving prefix of the timeline.

**Tests:** 19 cases in `ttd_resume_test.cpp`:
- Preconditions: Idle state, target beyond session end.
- Basic truncation: midpoint drops the right checkpoints; state is
  Recording after; emulator position matches target; mid-frame resume
  keeps the checkpoint at the same frame.
- Boundary cases: resume at last checkpoint (no-op for truncation but
  still transitions state), resume at first checkpoint (drops everything
  except baseline).
- Input journal: events at frames before the resume frame are kept;
  events at the resume frame and later are dropped; events captured at
  EXACTLY the resume TTDTimePoint are kept (via `RecordInputEvent` after
  a frame-aligned `SeekTo`); events captured mid-frame at the resume
  frame are dropped (correct — they're "in the future" from the resume
  perspective).
- Page store: slots are released on truncation; slots are reused by
  captures after resume (capacity does not grow).
- Recording continuation: appends one checkpoint per frame after
  resume; state stays Recording; dropped checkpoints can no longer be
  seek targets.
- Composability: `ResumeRecordingFrom` from Detached state (after
  `StepBackFrame`) seeks + truncates in one call.
- Stop + Resume interaction: after `StopRecording` (state Idle),
  `ResumeRecordingFrom` refuses — history is retained but cannot be
  resumed without re-starting recording.

Full TTD suite: 183/183 green (was 164 + 19 new resume tests).

### Item 6 — External-event markers ✅ DONE

**Module:** `core/src/debugger/ttd/ttd_external_events.{h,cpp}` (new).

**Data structure:**
  - `TTDExternalEventKind ∈ {TapeControl=0, DiskWrite=1, DebuggerEdit=2,
    Other=255}` — stable enum for automation JSON per TDD §10.4.
  - `TTDExternalEvent { TTDTimePoint time; TTDExternalEventKind kind;
    char reason[64] }` — 80 bytes on 64-bit. Inline reason avoids heap
    alloc per event; strncpy with explicit NUL guards truncation.
  - `TTDExternalEventJournal` (append-only `std::vector<TTDExternalEvent>`):
      * `Record(ev)` — caller (TimeTravelManager) enforces monotonicity
      * `FirstMarkerInInterval(from, to)` — strict-inside predicate
        `(from < m.time) && !(to < m.time)`. Linear scan; markers at
        `from` itself are NOT in the interval (their effect is in the
        restored checkpoint). Markers at `to` ARE in (closed upper bound —
        replay would have to cross one to land at `to`).
      * `DropAfter(t)` — strict-greater rule (events at `t` are kept)
      * `Clear()` — used by StartRecording / InvalidateSession
  - `TTDExternalEventKindToString` — stable strings: `tape_control`,
    `disk_write`, `debugger_edit`, `other`, `unknown`.

**TimeTravelManager API additions:**
  - `RecordExternalEvent(kind, reason)` — derives time from
    `(emulatorState.frame_counter, z80->t)`; no-op unless Recording.
  - `GetExternalEvents()` — const& access for UI / tests.
  - `TTDSeekHaltReason ∈ {Target, ExternalEvent, OutOfRange}` (nested
    enum) per TDD §717.
  - `TTDSeekResult { reached, arrivedAt, haltReason, blockingMarker }`
    (nested struct) — written on both success and failure.
  - `SeekTo(target, TTDSeekResult*)` — barrier-aware overload. Defaults
    outResult to OutOfRange; every code path either leaves the default
    or overwrites it.
  - `SeekTo(target)` — inline compatibility wrapper (existing callers
    unaffected; passes nullptr for outResult).

**SeekTo barrier logic (Step 3 of the algorithm):**
  - Only engaged when `target.tInFrame > 0` (intra-frame replay). Frame-
    aligned targets never trigger the check — the chosen checkpoint
    already reflects markers at or before that frame boundary.
  - `FirstMarkerInInterval(cp.time, target)` finds the earliest marker
    strictly inside the replay interval. If non-null:
      * Replay only as far as the marker (`ReplayWithinFrame(cp.frame,
        barrier.time.tInFrame)`).
      * Transition to Detached (same as success path — user can inspect,
        single-step, or resume from the barrier).
      * `outResult = { reached=false, arrivedAt=barrier.time,
        haltReason=ExternalEvent, blockingMarker=*barrier }`.
      * Return false.
  - Otherwise: replay to target, transition to Detached, return true
    with `haltReason=Target`.

**Lifecycle integration:**
  - `StartRecording` (when not already Recording) → `_externalEvents.Clear()`
  - `InvalidateSession` → `_externalEvents.Clear()`
  - `ResumeRecordingFrom(from)` → `_externalEvents.DropAfter(from)`
    (strict-greater rule — markers at `from` are kept)

**Test coverage** (`ttd_external_events_test.cpp`, 3 suites, 41 tests):
  - Pure journal (15 tests): Record, Clear, FirstMarkerInInterval edge
    cases (empty, no-match, mid-interval, at-from excluded, at-to
    included, multiple-match returns first, same-frame-different-tInFrame,
    DropAfter variants), KindToString stability.
  - Capture API (10 tests): no-op when not Recording, capture while
    Recording with correct time, null reason → empty string, long
    reason truncated at 63 chars, StartRecording clears (via Stop+Start
    since Start is idempotent), InvalidateSession clears,
    ResumeRecordingFrom keeps-at-threshold (forced z80.t=0) and drops-
    strictly-future.
  - Marker-blocks-seek (14 tests): no-markers baseline (frame-aligned +
    intra-frame), frame-aligned ignores markers, marker-in-interval
    stops with full blockingMarker, marker at restore point (forced
    z80.t=0) not a barrier, marker at target blocks (closed upper),
    marker past target not a barrier, multiple markers stops at
    earliest, blocked seek → Detached, blocked seek → replay halts near
    marker (RunTStates cycle drift tolerated), idle/out-of-range →
    OutOfRange, null outResult still returns false, StepBackFrame
    unaffected by markers.

**Production hook points wired** (follow-up to faf50d9c):
  - `core/src/emulator/io/tape/tape.cpp` — `startTape()` / `stopTape()` /
    `reset()` (only when `wasStarted`) emit `TapeControl` markers.
  - `core/src/emulator/io/fdc/wd1793.cpp` — `cmdWriteSector` and
    `cmdWriteTrack` emit `DiskWrite` markers after WP / disk-presence early-
    returns. Reason formatted via `std::snprintf` with `trk/sec/side`.
  - `core/automation/cli/src/commands/cli-processor-memory.cpp` — `memory
    write` dispatcher emits a `DebuggerEdit` marker (one per CLI
    invocation, regardless of byte count).
  - `core/automation/webapi/src/api/state_memory_api.cpp` — `writeMemory`
    (`POST /memory/write`) emits a `DebuggerEdit` marker (one per call).

  Hooks are guarded by `IsRecording()` so they are no-op when no TTD session
  is active.

  **Hook integration tests** (12 cases, `ttd_external_events_hooks_test.cpp`):
  Tape start/stop/rewind record markers; no-op when Idle; rewind not
  recorded when tape wasn't started; marker captures live frame_counter;
  tape marker blocks intra-frame SeekTo with correct blockingMarker fields;
  debugger edit (direct RecordExternalEvent) records marker; not recorded
  when Idle; mixed sources all surface; InvalidateSession clears; Resume
  drops strictly-future captured markers.

**Lower-traffic debugger-edit surfaces not yet wired:** Python
  `WriteByte` / WebAPI `PUT /memory/{addr}` / `memory fill` / direct
  `Memory::DirectWriteToZ80Memory` callers. These follow the same pattern;
  add when those surfaces are extended or used in production TTD flows.

### Item 7 — Divergence corpus + benchmark gate

- `core/tests/debugger/ttd/ttd_divergence_*_test.cpp` — five titles.
  Each: run N frames live with per-frame hash capture, then for K random
  frame indices replay from the nearest checkpoint and assert hash matches.
- CI workflow: run `core-benchmarks --benchmark_filter='TTD_Capture_.*'`,
  parse median, fail if > 5% of frame budget at 128 KB.

## What Phase 2 deliberately does NOT ship

- **No reverse watchpoint search.** Phase 4 (with the write journal).
- **No UI.** Phase 3 (TimelineWidget).
- **No GDB reverse execution.** Phase G3 (blocked on Phase 4).
- **No disk-write journaling.** Phase 5+ if TR-DOS write workloads demand
  it; until then disk writes are external-event markers (Item 6).

## Risks specific to Phase 2

- **Paging rebuild correctness.** Re-applying port latches via
  `DecodePortOut` must produce identical memory mapping to the original
  capture. The divergence corpus (Item 7) is the safety net; land Item 1
  with a per-port unit test plus the BASIC-idle divergence title online.
- **Silent-replay audio trap.** Forgetting to keep AY tone/envelope
  counters ticking during replay is the classic replay bug. The
  divergence corpus includes audio-output hash on the destination frame.
- **Input-journal granularity.** Matrix mutations happen between
  instructions; the journal must record `TTDTimePoint` at instruction
  boundary resolution, not frame. Verified by a keyboard-driven game in
  the corpus.
