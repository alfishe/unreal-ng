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

1. **Restore path** (parent TDD §8.1 step 2). Field copies for CPU + chipset
   already exist (`RestoreCpuState` / `RestoreChipsetState`); this item
   wires the higher-level orchestrator: re-run paging decode via the port
   decoder, memcpy only-diffed RAM pages, dispatch `TTDLoadState` on every
   peripheral, call `Screen::InitFrame()`, restore `t_states` / `frame_counter`.
   No replay yet — destination is a frame boundary.

2. **Silent-replay mode** (parent TDD §8.2 + Appendix C). Adds
   `_context->ttdReplayActive` and the suppression matrix:
   breakpoints skipped, MessageCenter notifications muted, audio host-buffer
   submission muted (but device ticks still run!), analyzer dispatch
   suppressed, recording subsystem off, live keyboard input blocked
   (journal injection replaces it in Item 3), checkpoint capture off.
   Build the flag, wire one suppression site per subsystem, ship.

3. **Input journal** (parent TDD §5.1). Capture keyboard matrix mutations
   with their TTDTimePoint; inject them at the recorded timestamps during
   replay instead of letting live input through. Without this, any
   keyboard-driven workload diverges the moment replay crosses a key event.

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

### Item 1 — Restore path

**Files touched:**
- `core/src/debugger/ttd/ttd_manager.{h,cpp}` — new private method
  `RestoreCheckpoint(const TTDCheckpoint&)`, plus public `RestoreLatest()`
  test entry point.
- Reuses existing `RestoreCpuState` / `RestoreChipsetState` from
  `ttd_checkpoint.cpp` (already implemented in Phase 1).
- Calls into: `Z80` (CPU copy target), `EmulatorState` (chipset target),
  `Memory::SetRomPage` / `SetRAMPage` via `PortDecoder`, `Screen::InitFrame`,
  each peripheral's `TTDLoadState`.

**Sequence** (per TDD §8.1 step 2):
1. `RestoreCpuState(checkpoint.cpu, &z80.state)` — fixes registers, MEMPTR, IFF, HALT.
2. `RestoreChipsetState(checkpoint.chipset, &emulatorState)` — port latches + counters.
3. Walk `_context->pPortDecoder` and re-apply every latched port via
   `DecodePortOut(port, value, pc)` so the live memory mapping, AY register
   select, banking, and palette all reflect the restored state. This is the
   "re-run paging decode" step.
4. For each `ramPages[i]` where the ref differs from the current live page
   content, `memcpy` 16 KB from the page store into `memory._ramPages[i]`.
   Skip never-touched pages.
5. Dispatch `TTDLoadState(blob.data())` on each peripheral that has a
   non-empty blob.
6. `screen->InitFrame()` so the renderer starts fresh from the restored
   beam position; the next rendered frame is the historical frame.
7. Restore `emulatorState.t_states` and `frame_counter` from the checkpoint.

**Tests** (`ttd_restore_test.cpp`):
- Round-trip: capture → mutate state → restore → assert state matches via
  `MachineStateHash::HashSnapshot()`.
- Per-subsystem restore: CPU-only, chipset-only, RAM-pages-only, each
  peripheral individually.
- Never-touched page: page that was never written in session stays at its
  live content (no spurious restore).
- Port-latch re-application: a checkpoint captured with `p7FFD = 0x10`
  restores the right ROM and RAM bank.

### Item 2 — Silent replay mode

**Files touched:**
- `core/src/emulator/emulatorcontext.h` — add `bool ttdReplayActive = false;`
- One suppression site per row of TDD §8.2's table:
  - `BreakpointManager` — check flag, skip
  - `MessageCenter::Post` — drop notifications when flag set (or post to
    a sink that observers can filter)
  - `SoundManager` — host buffer submission muted; device ticks unchanged
  - `Screen` — batch-render path (reuse existing optimization)
  - `AnalyzerManager::dispatchFrameStart` — early return
  - `RecordingManager` — already feature-gated; add replay gate
  - `DebugKeyboardManager` — block matrix mutation; journal injects instead
  - `TTDManager::OnFrameBoundary` — already early-returns when not Recording;
    replay sets state to Detached so this is automatic

**Tests** (`ttd_replay_mode_test.cpp`):
- Set the flag, run frames, assert no notifications posted, no checkpoints
  captured, breakpoint predicate never invoked, audio device ticks advanced
  but host buffer not submitted.

### Item 3 — Input journal

**Files touched:**
- `core/src/debugger/ttd/ttd_input_journal.{h,cpp}` — new
- Capture hook in `DebugKeyboardManager` (existing key-event observer)
- Injection hook in the silent-replay path

**Record format:**
```cpp
struct TTDInputEvent {
    TTDTimePoint time;     // 12 B
    uint16_t     keyMatrix;// 1 B packed (8 rows × 5 keys would lose info → use 8 B row bitmask)
    uint8_t      kind;     // press / release / matrix-update
};
```

Replay injection: when `_context->ttdReplayActive` and a journal entry's
time == current time, mutate the matrix to the recorded value.

### Item 4 — Seek API

**Public API** (on TTDManager):
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
