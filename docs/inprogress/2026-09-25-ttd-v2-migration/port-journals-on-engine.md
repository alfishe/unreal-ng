# Port journals on the engine: one copy, recorded where it is kept

Design step for the last open item of Phase 5 Item 2 ([TODO](TODO.md), "Port event search and find-last on port
writes stay on the live port journals"). Written 2026-10-09 from the code on master `7e3163872`.

**Abbreviations:** TTC.cpp = `core/src/debugger/ttd/timetravelcontroller.cpp`, TTE.cpp = `timetravelengine.cpp`.

## 1. Today

The controller keeps two copies of every IN and OUT the main CPU makes while recording:

| | The controller's journals (`_portReads`, `_portWrites`) | The engine's bus journals (`_busReads`, `_busWrites`) |
|---|---|---|
| Written by | the CPU's IN / OUT hooks (`EmulatorContext::ttdPortReads / ttdPortWrites`, set by `SyncPortJournalHook`) | the controller, copying the new records at each frame boundary (TTC.cpp `FeedShadow`, about line 4214) and at a stop or before a replay (`FlushToEngine`, about line 1791) |
| Read by | port event search **while a recording is paused** (TTC.cpp `SearchPortEvents`: they also hold the current frame) | every replay (`RestoreCheckpointForReplay` → `PlayBus`), port event search otherwise, find-last on port writes (TTC.cpp about line 4838), the session file, the black box segment files |
| Cut by | resume from the past (`TruncateTo`), history limit (`SyncTimelineFront` → `DropBefore`), `DropPortJournal` | the same events, in the engine (`TruncateAfter`, TTE.cpp about line 981; `DropOldestHeldSegment`, about line 896) |
| Cursors | `TTDCheckpoint::portReadCursor / portWriteCursor` | `TTDEngineCheckpoint::busReadCursor / busWriteCursor` |

The TODO's note (2026-10-06) says the controller's journals are also the replay source (`Mode::Play`). The code
says otherwise: `RestoreCheckpointForReplay` stops them and plays the engine's (`PlayBus`), so the controller's
never leave `Record` / `Off` (`OnMachineReset` still checks for `Play`; dead code). Find-last on port writes reads the engine's journal
already. So the controller's copy has one reader left: a port search while the recording is paused.

Two other journals already record straight into the engine: interrupt vectors (`BusVectors`) and sector reads
(`MediaReads`). `SyncMediaReadJournal` points their hooks at the recording engine in `Record` and at the replay
engine in `Play`.

## 2. What the double copy costs

Measured 2026-10-09 (Release, Apple Silicon): a loader-like loop (`in a,(#FE)` / `out (#FE),a`, about 2,100 INs and
2,100 OUTs a frame), 500 recorded frames:

| | Pentagon | TS-Conf |
|---|---|---|
| Recording, whole frame with TTD | 585 µs | 774 µs |
| The feed alone (decode each record, append it, seal and compress the full blocks; both journals) | 57 µs (≈10%) | 58 µs (≈7%) |
| Heap, controller's journals / engine's journals | 1.58 / 0.79 MB | 1.58 / 0.79 MB |

Most of each copy's heap is the raw open block (32,768 records of 24 B) and spare vector capacity; on this
repetitive loop the sealed blocks compress to almost nothing. On less regular traffic (tape loading, network
cards, sound) the sealed blocks grow, and so does the duplicate. An idle machine (a few INs a frame) costs almost
nothing either way.

## 3. Proposal: record into the engine's bus journals

Do what vectors and sector reads do: while recording, the CPU's IN / OUT hooks point at the recording engine's
`_busReads` / `_busWrites` (mode `Record`); during a replay they point at the replay engine's (mode `Play`, as now).
The controller's `_portReads` / `_portWrites` go away.

| Site | Today | After |
|---|---|---|
| `StartRecording` (TTC.cpp about line 320) | clear and start the controller's journals | nothing: `BeginSession` already starts the engine's journals in `Record` (TTE.cpp line 171) |
| `SyncPortJournalHook` | hooks → controller's journals | hooks → `_engine->BusReads/WritesForRecording()` while recording, null otherwise (one function with `SyncMediaReadJournal`) |
| `FeedShadow`, `FlushToEngine` | copy the new records | nothing to copy; `_shadowBusReads / Writes` go |
| `SearchPortEvents` | controller's journals while paused | always the engine's: they hold the current frame too |
| `ResumeRecordingFrom` | `TruncateTo` the controller's journals, then the engine's | the engine's only (`TruncateTimelineAfter` already does it), then `Record` from the cut |
| `SyncTimelineFront` (history limit) | `DropBefore` on the controller's journals | nothing: the engine dropped them with the segment |
| `DropPortJournal` | clears the controller's journals and zeroes the checkpoint cursors | stops the engine's journals (`Off`) and marks them unusable; no cursor rewrite (a replay that finds `Off` reads live devices, as today) |
| Live-state snapshot around a throwaway replay (about lines 5898 and 6006) | saves and puts back both pairs | the engine's pair only (already saved) |
| `TTDCheckpoint::portReadCursor / portWriteCursor` in the controller's timeline | the controller's cursors | unused by the controller; the fields stay in `TTDCheckpoint` for v1 until Phase 6 |
| Status (`portReadCount`, `portWriteCount`, `portJournalBytes`, replay mismatches) and `HeapBreakdown` | the controller's journals (and the engine's added to the heap) | the engine's journals; the heap counts one copy |
| `OnMachineReset` | stops the controller's journals if playing (dead code) | stops the replay engine's, as `ExitReplayMode` does |

v1 (`TimeTravelManager`) keeps its own journals until Phase 6; nothing there changes.

### 3.1 One journal, two modes

The engine's journal switches between `Record` and `Play`. This already happens for vectors and sector reads,
and the transitions exist:

- **Seek while recording (D8):** the recording pauses, the replay plays from a checkpoint's cursor (`PlayBus`).
  Resuming at the paused end needs `Record` again at the end of the journal. Today `StartRecording` appends at
  the end; the controller gets one call that returns the recording engine's bus journals to `Record`
  (`ResumeBusRecording`), made where it re-armed its own journals (resume from the past, resume after a stop).
- **Resume from the past:** the engine truncates at the cut, then `Record` from there.
- **Throwaway replay during browsing:** the snapshot puts each journal back in its mode and cursor (already done
  for the engine's pair).

The one rule: a replay never records. Every replay starts with `PlayBus` (`RestoreCheckpointForReplay`), which
switches the journals to `Play` and points the CPU's hooks at them before the first replayed access, so no
replayed access is appended; a live-state snapshot around a throwaway replay puts the mode back.

### 3.2 Threads

The journal is written on the emulation thread and read by a search from another thread only while the machine is
paused (the check in `SearchPortEvents` stays). That is the rule today for the controller's copy, unchanged.

## 4. Tests

Existing suites that must stay green: `TimeTravelController_Test` (A/B against v1, resume, history limit),
`TimeTravelManager_EngineSeek*`, `TTDControl_Test` (port-events, find-last), the port journal tests
(`timetravelmanager_portjournal_test.cpp` - v1, unchanged), `TTDSprinter*` (the Sprinter's port search), the corpus
and black box tests.

New:

1. **One copy:** after recording the loader loop, the controller holds no port records and the session heap counts
   the bus journals once (fails today: two copies).
2. **Search while paused sees the current frame:** pause mid-frame after an OUT, search for it - found (today it is
   found through the controller's copy; after the step through the engine's).
3. **Seek while recording, then resume at the end:** the records after the resume continue the journal with no gap
   and no duplicate (count = live INs).
4. **Black box ring:** a ring that drops segments keeps the bus journals in step with the checkpoints (the files
   and memory).
5. **A/B:** the D33 matrix and `BM_HostFrame_*`: the feed's time and the duplicate heap gone, nothing else moves.

## 5. Questions for the owner

1. **The v1 gate on the engine.** `_portJournalValid` (TTC.cpp about line 327, `PortJournalUnsupportedReason`) is
   false on configurations v1 could not replay from its journal alone: ZX Next (its DMA), a NeoGS whose ZX-bus
   carries host memory cycles (its ZX-DMA), a machine whose own interrupt source supplies the IM2 vector, or one
   with an engine stepped with the CPU - unless the decoder declares its engines sealed (`TtdEnginesSealed`: the
   Sprinter). That is TS-Conf always (interrupt source and DMA), the Z84C15 machines, and the Scorpion 256 and
   Profi **when their step hook happens to be installed as the recording starts**. The engine records the journals
   on all of these and plays them in every replay, yet `SearchPortEvents` refuses ("the session has no port
   journal"), status says `port_journal_active: false`, and a session file saved there says so too - so the loaded
   session replays against the live devices. Proposal: drop the gate for the engine's controller in the same step.
2. **When:** this step changes only the controller (no file format, no surface). It can go now or with Phase 6
   (deleting v1, not before 2026-10-22). Proposal: now - it is independent of v1 and removes a per-frame cost from
   every recording.

**Owner decisions 2026-10-09:** both proposals - the gate goes for the engine's controller in this step, and the
step is done now.

## 6. Size

About 15 sites in TTC.cpp (table above), two helpers in the controller (`ResumeBusRecording`,
`StopBusRecording`), four tests. No file format or surface change; the status field descriptions follow.

## 7. Results (2026-10-09)

Done as proposed, with the gate dropped for the engine's controller.

- **Speed**, A/B against master `7e3163872`, the loader-like loop (about 2,100 INs and OUTs a frame), best of five
  runs of 500 frames, three alternating rounds (host load average about 29): Pentagon 571-587 → 484-500 µs per
  recorded frame (−15%), TS-Conf 784-790 → 690-699 µs (−12%) - more than the feed alone measured (57 µs).
- **Memory:** one copy of the journals (the controller's 1.6 MB on that loop gone).
- **Found on the way:** the status counters `port_replay_value_mismatches` / `port_replay_divergences` read the
  controller's copy, which never played, so they were always 0; they now read the replay engine's journals.
- **Tests** (`timetravelcontroller_portjournals_test.cpp`): the engine holds the only copy and status counts it; a
  search while paused mid-frame sees the frame being recorded; a TS-Conf session reports its journals and is
  searchable; a recording resumed at its end after a seek continues the journals without a gap or a duplicate.
  Mutant: the IN hook not pointed at the engine - the first and third fail. The whole TTD suite (1509 tests)
  passes.
