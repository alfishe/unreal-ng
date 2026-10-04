# Crash: the TTD toolbar tooltip walked a freed timeline (2026-10-03)

## Symptom

unreal-qt crashed with `EXC_BAD_ACCESS` (`KERN_INVALID_ADDRESS`, a wild pointer) on the UI thread
shortly after an agent stopped a TTD recording:

```
std::vector<uint8_t>::capacity()                  vector.h:388
ttd::TimeTravelManager::GetHeapBreakdown()        timetravelmanager.cpp:814
ttd::TimeTravelManager::EstimateSessionHeapBytes() timetravelmanager.cpp:788
ttd::TimeTravelManager::GetSessionInfo()          timetravelmanager.cpp:646
ToolBarManager::updateActiveTooltips()            toolbarmanager.cpp:496
ToolBarManager::onBreathingTick()                 toolbarmanager.cpp:479   (QTimer, every 33 ms)
```

Line 814 is the per-checkpoint loop: `cp.peripheralBlobs[i].second.capacity()`. At the moment of
the crash the emulator thread sat parked in `Emulator::WaitWhilePaused`; the WebAPI, CLI and Lua
threads were idle, so the thread that changed the session had already finished its call.

## Root cause

`TimeTravelManager::GetSessionInfo()` reads the live session: it walks `_timeline` (every
checkpoint's device blobs and page references), the page store's slots, the port and coverage
journals' block lists, and copies several `std::string`s. Those structures belong to the thread
that drives the session:

- the machine's thread appends a checkpoint at every frame boundary while recording
  (`_timeline.push_back`, reallocation moves every checkpoint), evicts from the front under a
  history limit, and applies a requested invalidation (`_timeline.clear()`);
- a control thread (WebAPI, CLI, Lua, Python, the Qt TTD widget) stops, invalidates, loads,
  seeks and resumes with the machine paused.

The TTD design ([time-travel-debugging-tdd.md](../../emulator/design/debugger/time-travel-debug/time-travel-debugging-tdd.md)
section 7.2) says UI reads go through "a small mutex-protected summary struct updated once per
frame, not the raw timeline vector". That struct never existed: three Qt timers read the live
session from the UI thread:

| Reader (UI thread) | Cadence | Read |
|---|---|---|
| `ToolBarManager::updateActiveTooltips` | 200 ms while TTD records | `GetSessionInfo()` |
| `TtdWidget::updateTelemetry` | 100 ms while visible or recording | `GetSessionInfo()` |
| `StatusBarManager::updateTtd` | status poll, while Detached | `GetSessionInfo()` |
| `ToolBarManager::isTtdRecording`, `MenuManager` | 1 s / on demand | `IsRecording()` (plain enum) |

So the tooltip walked the timeline while another thread freed it (a stop/invalidate from
automation, or a capture reallocating it). All of these also took the raw `GetContext()` without
a lease, so an instance removed by automation could be freed under them as well.

Two control paths changed the session beside a running machine too:

- `StopRecording` flushed the coverage index, set the state to Idle and stopped the port
  journals **before** pausing the machine;
- `SetHistoryLimit` (the TTD widget's history combo, on the UI thread while recording) evicted
  checkpoints at once, beside the machine's capture.

## Fix

Two rules, both from the TTD design (section 7.2): observers read a published summary, and every
other access to the session runs as a control operation with the machine parked.

- **Published snapshot** (`TimeTravelManager::GetPublishedSessionInfo`): a `TTDSessionInfo` copy
  under its own small mutex; any thread, any time, never touches the live session. Only the thread
  that drives the session writes it: every session operation when it ends, every owner
  `GetSessionInfo()` call, the machine's thread as it parks (`OnMachineParking`, from
  `MainLoop::Run` and `Emulator::WaitWhilePaused`: a paused recording reads exact), and frame
  boundaries while a session is active, once an observer has asked and `kPublishIntervalMs`
  (100 ms) has passed. A session nobody watches costs one atomic load per frame. The ROM signature
  of a live session is taken with its baseline instead of hashing the ROM on every call.
- **Session operations** (`TimeTravelManager::SessionOperation`, held for the whole run of every
  public method that reads or changes the session: start, stop, invalidate, load, save, seek,
  steps, reverse steps / continue, resume, history limit, write journal switch, bookmarks,
  external events, port search, coverage queries, find-last, clip export, machine reset). On the
  machine's thread it only publishes after a change. On any other thread it takes the control lock
  (one control operation at a time), parks a running machine while a session is active (Recording
  or Detached; an Idle session is not touched by a running machine) and resumes it afterwards if it
  parked it, and the outermost change publishes.
- **Status reads from automation** (`ReadSessionInfo`): live when the caller is the machine's
  thread, or when nothing drives the machine, the control lock is free and the session is not
  recording; otherwise the published snapshot. It never blocks and never pauses the machine, so a
  polling client does not slow the machine down. WebAPI, CLI, Lua, Python, GDB and the Qt load path
  use it; the external-event listings iterate `SnapshotEvents()` (a copy under the journal's mutex)
  instead of the live vector.
- **`GetSessionInfo()` / `GetHeapBreakdown()`** keep their contract, now written down: the
  session-driving thread only.
- **Atomic state:** `_state` and the history limits are atomics, so `IsRecording()` / `GetState()`
  are safe from any thread.
- **Stop parks first:** `StopRecording` pauses the machine before it touches the session (as
  `StartRecording` always did) and publishes while it is still parked.
- **Qt:** the toolbar tooltip reads through `TtdSessionObserver`
  (`unreal-qt/src/emulator/ttdsessionobserver.h`: a context lease plus the published snapshot);
  the TTD widget's telemetry and jump buttons and the status bar's TTD label read the published
  snapshot under a lease; the TTD widget's Clear parks a running Detached machine before it frees
  the history.

## Tests

- `core/tests/debugger/ttd/timetravelmanager_publishedinfo_test.cpp`: when the snapshot is
  published (operations, owner reads, frame boundaries only on request), the history limit in
  synchronous mode, and a reader thread polling the snapshot and the state while the owner
  records, evicts, stops and invalidates in a loop; `AutomationReadsWhileTheLoopRecords`: the
  automation read path (`ReadSessionInfo` as `GET ttd/status` and the other status reads use it,
  plus bookmarks, markers, session end and a coverage summary) on its own thread while the machine
  records on its loop thread and the test thread starts, evicts, bookmarks, stops and clears.
- `unreal-qt/tests/qt/ttdsessionobserver_test.cpp` (unreal-qt-tests, part of `test-parallel`):
  the UI thread hammers the toolbar's read and tooltip while an automation thread drives ten
  record / evict / stop / invalidate cycles; then automation removes the instance while the UI
  keeps polling.

There is no TSan configuration in the project, so the stress tests run real threads. Mutation
check: with `TtdSessionObserver::Read` switched back to the live `GetSessionInfo()`, the
`PollWhileAutomationRecordsStopsAndInvalidates` run crashed with SIGSEGV in 2 of 10 runs (10
cycles and 25 cycles alike); with the published snapshot it passes every run. With the
automation thread in `AutomationReadsWhileTheLoopRecords` reading `GetSessionInfo()` (what every
surface did before) the test crashed with SIGSEGV in 5 of 5 runs; with `ReadSessionInfo()` it
passes. The contract tests in the core file are deterministic.

A query that parks the machine costs a pause handshake; a client spinning on one starves the
machine (the test paces them to one per emulated frame). Status polls use `ReadSessionInfo`, which
never parks.

## Limits

- A control thread that steps a paused machine itself (`RunNFrames` / `RunTStates` from
  automation, "direct stepping") is not parked by another thread's operation; two control planes
  driving one instance at once is what the run-control claim (TDD section 7.2) arbitrates.
- An outside `Resume()` (the UI's resume button, `POST /resume`) during another thread's operation
  still lets the machine run; every operation pauses with the same `Pause` / `Resume` pair as
  before.

## Other crash in the same test run (not this class)

The first full test run had one shard die with SIGSEGV (`core-tests` shard 6 of 20, pid 85329,
`~/Library/Logs/DiagnosticReports/core-tests-2026-10-03-192202.ips`). It is not a crash at exit:
it died inside `VideoToolboxEncoder_Test.TimestampThatDoesNotGrow_DroppedRecordingGoesOn`
(`core/tests/emulator/recording/videotoolbox_encoder_test.cpp:82`, the frames after the two
dropped ones), main thread:

```
objc_msgSend                                     libobjc
CVPixelBuffer::setDefaultAttachments             CoreVideo
CVPixelBufferPool::createPixelBuffer             CoreVideo
CVPixelBufferPoolCreatePixelBuffer               CoreVideo
VideoToolboxEncoder::OnVideoFrame                core-tests
```

`EXC_BAD_ACCESS` at `0x605126858750`: the encoder asks a pixel buffer pool that is already gone
(likely after the writer failed on the non-growing timestamps under load). Video recording, not
TTD; the shard passed three times on its own and the rerun of the whole suite was green. Left for
the recording owner.
