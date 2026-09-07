# Throttler Adoption Analysis — Where to Apply the Universal Throttling Helper Right Now

- **Status**: analysis complete, no code changes proposed yet
- **Date**: 2026-08-27
- **Related**:
    - Design: [Universal Throttling Helper](../../emulator/design/common/universal-throttling-helper.md)
    - Implementation: `core/src/common/throttler.h` (header-only, C++20)
    - Tests: `core/tests/common/throttler_test.cpp` (45 tests, deterministic, 0 ms)

## 1. Purpose

The universal throttling helper is implemented and verified. This document identifies the
codebase sites where adopting it **right now** yields measurable wins, ranks them, and
records the exact throttle configuration, risks, and verification method for each. Sites
that look tempting but must **not** be throttled are listed with reasons, so future work
does not re-litigate them.

## 2. Recap of the Relevant API

```cpp
// Time-driven wrapper (steady clock, injectable for tests)
auto t = MakeTimedThrottler(
    MinInterval(33, IntervalStart::Immediate),   // strategy
    ThrottlePolicy::KeepLast,                    // latch newest rejected call
    [callback](Args... args) { ... });           // what to run on pass

t.Execute(args...);   // returns false when rejected (KeepLast latches args)
t.FlushPending();     // drains the latch — call at lifecycle boundaries
t.Reset();            // forgets state — call between sessions
```

Combinators `AnyOf` / `AllOf` combine count- and time-based arms; `MakeThreadSafe` adds an
opt-in mutex shell. The design doc (§9.1) names the **UI repaint pattern** as the canonical
use case; §9.2 names **log flushing**. Both patterns have dedicated integration tests
(`UiRepaintPattern`, `LogFlushPattern`, `EgressRatePattern`).

## 3. Case Ranking

| # | Site | Thread | Pain today | Effort | Priority |
|---|------|--------|-----------|--------|----------|
| 1 | `Memory::SyncToDisk()` full-range flush, **two** per-frame call sites | emulation (audio-budget-critical) | 2 × synchronous full-RAM `msync`/`FlushViewOfFile` per frame | none — **superseded** | **SUPERSEDED** |
| 2 | `NC_VIDEO_FRAME_REFRESH` → GUI repaint flood | MessageCenter → GUI | queued metacall + full repaint per emulated frame in turbo; 3 hand-rolled suppressions exist | small | **P0** |
| 3 | `AnalyzerManager::dispatchFrameEnd()` no rate gate | emulation | analyzers run at full emulation cadence | medium | P1 |
| 4 | `LogWindow::Out` per-message widget rewrite | any → GUI | `setPlainText` per message = O(total) per line, quadratic under chatty logging | small | P1 |
| 5 | `TileGrid::_isRepaintPending` hand-rolled coalescing (videowall) | MessageCenter → GUI | works, but is the 4th bespoke throttle — consolidation play | trivial | P2 |

## 4. Case #1 — `Memory::SyncToDisk()` (SUPERSEDED — do not implement)

> **2026-08-27 update**: follow-up research ([Shared Memory Coherency Research](../2026-08-27-shared-memory-coherency/coherency-research.md))
> confirmed with vendor documentation (Linux man-pages, Darwin man pages, Microsoft Learn)
> that inter-process coherency of our mappings is guaranteed on all three platforms and
> that no durable backing file exists on any of them (tmpfs / kernel-resident shm /
> paging-file-backed section). The correct amount of per-frame flush work is **zero** —
> eliminate `SyncToDisk()`, do not throttle it. The section below is kept for the record
> of why it was ranked P0 before that finding.

### Evidence

Two **independent per-frame call sites**, both unconditional when shared memory is enabled:

- `core/src/emulator/cpu/core.cpp:601-624` — `Core::CPUFrameCycle()` ends with
  `_memory->SyncToDisk();` (line 623), i.e. once per emulated frame.
- `core/src/emulator/mainloop.cpp:326-339` — `MainLoop::OnFrameEnd()` also calls
  `_context->pMemory->SyncToDisk()` (line 333), guarded only by `IsSharedMemoryEnabled()`.

The callee flushes the **entire mapping** synchronously
(`core/src/emulator/memory/memory.cpp:642-666`): `msync(_memory, _memorySize, MS_SYNC | MS_INVALIDATE)`
on POSIX, `FlushViewOfFile` + `FlushFileBuffers` on Windows. For a 128K machine that is
full 16 MB of RAM range walked twice per frame; in **turbo mode** the frame rate is
uncapped, so the cost scales without bound. This runs on the emulation thread — the same
budget that must feed the audio callback; every millisecond spent in synchronous writeback
is audible as buffer underruns. `unreal-videowall` (N emulators) multiplies the effect by
tile count.

### Proposed configuration

Throttle **inside** `Memory::SyncToDisk()` so both call sites are covered by one gate and
callers stay unchanged:

```cpp
// member: TimedThrottler<MinInterval, ...>
_syncThrottler = MakeTimedThrottler(
    MinInterval(33, IntervalStart::Immediate),  // ≤ ~30 full flushes/sec
    ThrottlePolicy::KeepLast,                    // trailing flush follows last write
    [this]() { DoSyncToDisk(); });
```

- Policy `KeepLast` guarantees the final dirty state is latched and flushed within one
  interval — external viewers (screen-viewer, memory dumpers) lag ≤ 33 ms.
- Add `FlushPending()` hooks at lifecycle boundaries: pause, stop, shutdown, and **before
  any snapshot-save or viewer handshake** that assumes on-disk coherence.
- `Reset()` between sessions (new model load remaps memory).

### Risks and open questions

- Crash-consistency window widens from ~1 frame to ≤ 33 ms of unsynced writes. For a
  debugging tool whose viewers read the **shared mapping** (page-cache-coherent — readers
  see writes without `msync`), the sync exists for durability against host crashes; the
  window is acceptable and bounded.
- Open question (orthogonal alternative, not this change): is full-range `MS_SYNC` needed
  at all, vs. `MS_ASYNC` or dirty-range tracking? Throttling does not block that
  investigation — it composes with either outcome.
  **→ Answered 2026-08-27: none of them is needed. See the coherency research linked
  above.**

### Verification

- Bench: frames/sec in turbo with shared memory enabled, before/after (expect near-linear
  gain in turbo; zero measurable change at 1× speed).
- Test: existing `Memory` tests + a new CUT-style test that a `FlushPending()` call after
  `Execute()` forces exactly one underlying sync.

## 5. Case #2 — `NC_VIDEO_FRAME_REFRESH` → GUI repaint flood (P0)

### Evidence — the problem class keeps being re-solved by hand

- `unreal-qt/src/mainwindow.cpp:1402-1440` — `handleMessageScreenRefresh()` posts a
  queued `deviceScreen->refresh()` **per message** (line 1430). In turbo the emulator
  emits one message per emulated frame — hundreds per second — and each becomes a queued
  metacall + repaint. The `_DEBUG` skip-log at lines 1432-1437 (printing skipped frames)
  is itself evidence the flood outruns the GUI.
- `unreal-qt/src/mainwindow.cpp:141-148` — `DebuggerWindow::screenRefreshRequested`
  funnels into the same ungated invoke.
- `unreal-videowall/src/videowall/TileGrid.cpp:218-243` — the single-sync path implements
  its own coalescing: `_isRepaintPending` flag, "Drop frame if UI is still rendering the
  previous one (prevents event queue flooding)".
- `core/src/emulator/mainloop.cpp:344-363` — TTD replay suppression comment: "a redraw
  storm would dominate seek latency" → hand-rolled `ttdReplayActive` gate (second
  instance in `analyzermanager.cpp:559-572`).

Four bespoke mechanisms for one problem. The design doc §9.1 specifies the replacement.

### Proposed configuration

Design doc §9.1, verbatim pattern:

```cpp
auto repaint = MakeTimedThrottler(
    MinInterval(16, IntervalStart::Immediate),  // cap at ~60 repaints/sec
    ThrottlePolicy::KeepLast,                   // newest frame wins
    [this]() { QMetaObject::invokeMethod(deviceScreen, "refresh", Qt::QueuedConnection); });
```

- Natural home: the MainWindow handler (and the debugger-refresh connect at
  `mainwindow.cpp:143-148`) — the throttle lives on the GUI side where the budget is.
- A 16 ms UI timer calling `FlushPending()` drains the latch even if the emulation pauses
  mid-interval (pause screen must still show the last frame after the final event).
- The TTD replay suppression (`ttdReplayActive`) **remains** — it is a correctness gate
  (replay must not redraw at all), not a rate gate; but with the throttle in place its
  motivation (storm during multi-frame seeks) is also covered for the residual events.

### Risks

- Presentation latency can grow by up to one interval (~16 ms) — below one frame at 60 Hz;
  the existing A/V-latency EMA readout (`pVideoPresentLatencyUs`) will show it if it
  regresses.
- Frame counters in the payload (`_frameCounter`) still update — the `_DEBUG` skip-log
  semantics are preserved because latching keeps the **newest** payload.

### Verification

- Manual: turbo mode + open Log window (or any GUI-heavy state) — skipped-frame debug
  spam should collapse to ~60/s repaints.
- Test: `UiRepaintPattern` in `throttler_test.cpp` is the executable spec (100 events at
  1/ms → 7 repaints + flush = 8).

## 6. Case #3 — `AnalyzerManager::dispatchFrameEnd()` (P1)

### Evidence

`core/src/debugger/analyzers/analyzermanager.cpp:559-572` — `OnFrameEnd` dispatches to all
registered analyzers at full emulation cadence. The only guard is the same TTD replay
suppression as case #2; there is **no rate gate**. Each analyzer's per-frame cost is paid
even when nothing consumes its output (e.g. web clients not polling that analyzer).

### Prerequisite — per-analyzer cost audit

Unlike #1 and #2, the per-frame work is not uniformly expensive: some analyzers are a
handful of branches, others walk frame-sized buffers. Before gating, enumerate registered
analyzers and measure per-frame cost (benchmark harness or simple frame-time delta with
analyzer set toggled). The gate must be **per-analyzer opt-in**, not global: any analyzer
whose contract assumes frame completeness (e.g. builds a timeline keyed by frame number)
must either stay ungated or receive the sample rate as part of its contract.

### Proposed configuration

Sampling gate, drop policy (a latched "last frame" is wrong for most analyzers — a gap is
honest, a stale duplicate is not):

```cpp
auto gate = MakeTimedThrottler(
    MinInterval(20, IntervalStart::FullInterval),  // sample ~50 frames/sec
    ThrottlePolicy::Drop,
    [analyzer](frame) { analyzer->Analyze(frame); });
```

- `FullInterval` (not `Immediate`) so a burst start does not immediately burn a sample.
- Configurable interval per analyzer; default off (opt-in preserves behavior).

### Risks

- Analyzer semantics change: frame-sparse input. Mitigate by making the throttle a
  per-analyzer setting documented alongside the analyzer registration.

### Verification

- Bench: emulation FPS with the analyzer suite enabled, before/after.
- Test: per-analyzer unit tests unaffected (gate off by default); one new test proving the
  gate drops and passes at expected cadence with an injected clock.

## 7. Case #4 — `LogWindow::Out` per-message widget rewrite (P1)

### Evidence

`unreal-qt/src/logviewer/logwindow.cpp:58-100` — every log message:

1. appends to `m_logStream` (fine — full-fidelity sink), then
2. if called off the main thread, posts a **queued metacall per message** (line 75), and
3. on the main thread, rebuilds the visible document with
   `document()->setPlainText(text)` (line 96) — note the surrounding code shows only the
   **last** line survives (`QString text = line + '\n';`), and two status labels are
   re-rendered per message (lines 98-99).

Chatty logging (e.g. `TurnOnLoggingForModule(MODULE_CORE, ...)` enabled in
`mainwindow.cpp:905`) produces thousands of messages per second in turbo → thousands of
queued metacalls and O(document) rewrites each. The redirect from module logger to
LogWindow is currently commented out (`mainwindow.cpp:919-933`, MSVC C4407 member-cast
warning), so the path is dormant — but it is the designed path and will be re-enabled.
Throttling it now means the flood problem is solved the day the redirect comes back.

### Proposed configuration

Batch the **UI update only**; keep the stream sink per-message (full fidelity for Save):

```cpp
auto uiFlush = MakeTimedThrottler(
    AnyOf(MinInterval(250, IntervalStart::FullInterval),
          EveryNCalls(500)),
    ThrottlePolicy::KeepLast,
    [this]() { logViewer->document()->setPlainText(LastLines(...)); /* + counters */ });
```

- This is the design doc §9.2 **log flush pattern**, executable spec:
  `LogFlushPattern` (time arm fires at cadence, count arm caps bursts, trailing flush).
- `KeepLast` latches the newest tail so the final state is never lost; a `FlushPending()`
  on window close / clear / save completes the drain.
- Longer term the widget should use `appendPlainText` on a bounded document instead of
  `setPlainText` — orthogonal; the throttle removes the per-message cost regardless.

### Risks

- Live-tail latency of up to 250 ms in the Log window — acceptable for a viewer with a
  Save-to-file sink that remains per-message.
- `m_logStream` grows unbounded today already; unchanged by this case.

### Verification

- Test: `LogFlushPattern` in `throttler_test.cpp` is the executable spec.
- Manual: enable a chatty module logger with redirect re-enabled; GUI stays responsive.

## 8. Case #5 — `TileGrid::_isRepaintPending` consolidation (P2)

`unreal-videowall/src/videowall/TileGrid.cpp:218-243` already coalesces repaints by hand
(pending-flag + queued reset). It works and is not urgent. Replacing it with
`TimedThrottler(MinInterval(16), KeepLast)` is a small consistency win: one idiom across
MainWindow, TileGrid, and any future surfaces, plus testability (the flag logic currently
has none). Do it opportunistically when case #2 lands, since both touch the same event.

## 9. Considered and Excluded

| Site | Reason not to throttle |
|------|------------------------|
| TTD per-frame checkpoints (`timetravelmanager.cpp:527-556`) | Format contract: every frame must be checkpointed; skipping breaks seek semantics. Cost work belongs to the TTD design (dirty-page Interns), not a rate gate. |
| Porttrace / TTD IO write journal (`portdecoder.cpp:242-294`) | Correctness-critical: every port write must be journaled; any drop silently corrupts replay. |
| `RecordingManager::CaptureFrame` (`mainloop.cpp:311-323`) | Captures every emulated frame by design for correct timing in turbo; a rate gate would corrupt recordings. |
| WebAPI TTD status endpoint (`ttd_api.cpp:123-168`) | Request-driven, returns cached session counters; client-paced by construction. If polling ever becomes hot the fix is response caching, not throttling. |
| `VideowallRecorder` 50 Hz `_frameTimer` | Already time-bounded by construction — the timer **is** the throttle (`EveryNCalls` equivalent). |
| PySide tools error-signal throttling (`tools/verification/...`) | Python side; the existing manual pattern there is intentional per its spec. |
| Port I/O hot path logging | Already handled via port-mute lists (`mainwindow.cpp:913-917`), which is suppression, not rate gating — correct tool for the job. |

## 10. Adoption Order and Next Steps

1. ~~**#1 SyncToDisk**~~ — **superseded by the coherency research**
   ([2026-08-27-shared-memory-coherency](../2026-08-27-shared-memory-coherency/coherency-research.md)):
   delete the flush instead of throttling it. Biggest measurable win on the
   audio-latency-critical thread; lifecycle flush points are moot when there is nothing
   to flush.
2. **#2 frame-refresh throttle** — one idiom replaces the debug skip-log problem class;
   do **#5** (TileGrid) in the same pass. Now the leading candidate.
3. **#4 LogWindow batching** — small, spec'd by §9.2, unblocks re-enabling the logger
   redirect.
4. **#3 analyzer gate** — after the per-analyzer cost audit; opt-in per analyzer.

Each adoption is an independent, small PR-shaped change: add the throttler member, wrap
the call, add flush/reset hooks, extend the existing test file. No change proposed in this
document is implemented yet.