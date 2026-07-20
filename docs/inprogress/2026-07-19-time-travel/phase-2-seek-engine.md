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
   (parent TDD §8.1 step 3 + §8.1 last paragraph). Binary-search the
   timeline, restore, optional intra-frame silent replay. StepBackInstruction
   replays from the previous checkpoint remembering the last instruction
   boundary. State transitions Idle → Recording → Detached per §4.2.

5. **Resume-from-past truncation** (parent TDD §8.3). When the user
   resumes from a Detached position T, drop everything > T from the
   timeline and journals, release page refs, return to Recording. Atomic
   under pause.

6. **External-event markers as replay barriers** (parent TDD §5.1). For
   sources of nondeterminism that aren't journaled in v1 (tape control,
   disk writes), record a marker on the timeline. Seek and reverse search
   surface the marker to the caller instead of silently crossing it.
   Keeps TTD honest: it never pretends to reproduce what it cannot.

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

### Item 4 — Seek API

**Public API** (on TimeTravelManager):
```cpp
// All require emulator paused. All transition state to Detached on success.
bool SeekTo(const TTDTimePoint& target);
bool StepBackInstruction();
bool StepBackFrame();
bool StepForwardInstruction();
bool StepForwardFrame();
```

**Implementation:**
- Binary search `_timeline` for the latest cp with `cp.time <= target`.
- `RestoreCheckpoint(cp)`.
- If `target.tInFrame > 0`: set `ttdReplayActive`, `RunTStates(target.tInFrame)`
  with `skipBreakpoints=true`, clear flag.
- Set `_state = Detached`, `_currentPosition = target`.

### Item 5 — Resume-from-past truncation

```cpp
// On Resume() while Detached at T:
//   1. Drop _timeline entries with time > T (release page refs)
//   2. inputJournal.truncate(after = T)
//   3. writeJournal.truncate(after = T)  // P4 item — placeholder for now
//   4. _state = Recording
//   5. OnFrameBoundary() picks up from T
```

Atomic with respect to UI: caller pauses emulator, calls Resume, we do all
this synchronously, return.

### Item 6 — External-event markers

**Storage:** alongside `_timeline`, a sorted `std::vector<TTDExternalEvent>`.
**Sources:** tape control commands, disk writes (until journaled),
debugger-initiated state edits while running.
**Behavior:** SeekTo refuses to cross a marker silently — returns the
marker's time and reason to the caller. UI shows a barrier on the timeline.

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
