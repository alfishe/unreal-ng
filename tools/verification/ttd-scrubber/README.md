# TTD Scrubber

A PySide6 desktop tool for driving the emulator's Time-Travel Debug engine
from outside the process, via the WebAPI. Connects to a running emulator
WebAPI server, waits for an active emulator instance, and lets you:

- Turn TTD recording on/off
- Invalidate the recorded session (drop all history)
- Scrub the timeline by dragging a slider
- Step forward/backward one frame at a time
- Seek to any frame in the recorded range
- Resume recording from the current seek point
- See external-event markers (tape control, disk writes, debugger edits)
  that act as replay barriers

The tool exercises all 10 TTD WebAPI endpoints shipped in Phase 2 of the
TTD engine (`core/automation/webapi/src/api/ttd_api.cpp`).

## Status

Production-ready for manual debugging. Not a test harness — the underlying
WebAPI endpoints are already covered by the C++ contract test
`core/tests/debugger/ttd/ttd_automation_contract_test.cpp` (12 cases).

## Setup

```bash
pip install -r requirements.txt
```

Requires Python 3.9+ and PySide6 6.5+. The dependencies are listed in
`requirements.txt`:

```
PySide6>=6.5
requests>=2.28
```

PySide6 is chosen over PyQt6 for its LGPL licensing.

The tool reuses the shared WebAPI client from
`../webapi/src/api_client.py`. No additional packages are needed — the
launcher adds the path automatically.

## Usage

Start your emulator binary with WebAPI enabled (default port 8090). The
TTD engine must be present in the build (it is by default on all major
platforms; check that `ENABLE_WEBAPI_AUTOMATION=ON` and the time-travel
feature flag is exposed).

Then from this directory:

```bash
./run.sh
# or with a custom URL
./run.sh http://localhost:8090
```

Or invoke directly:

```bash
cd src
python main.py --url http://localhost:8090
```

The window opens showing "disconnected" until an emulator instance is
running. When one appears it is auto-selected and the TTD panel activates.

## UI Walkthrough

The window has five panels stacked top to bottom:

### 1. Connection bar

- **Server** field accepts the WebAPI base URL.
- **Connect** button forces a reconnect.
- Colored badge shows connection state: green = connected, red =
  disconnected, amber = connecting.

### 2. Instance picker

- Dropdown lists every running emulator instance.
- Auto-selects the first active instance as soon as one appears.
- Selecting an instance drops any in-flight action and starts polling
  its TTD state.

### 3. TTD Session

- **State** badge: `idle` (gray), `recording` (green), `detached` (blue).
- **ttd_available** indicator (green when present, red when the build
  lacks the TTD engine).
- Form fields: start frame, end frame, checkpoint count, page store
  usage with percentage, baseline frames captured.
- Buttons: **Start recording**, **Stop**, **Invalidate** (with confirm
  dialog).

### 4. Timeline

- **Slider** spans frame 0 to the current end frame.
- Frame readout below the slider shows `current / end`.
- Buttons: **< Frame** (step back), **Seek** (jump to slider position),
  **Frame >** (step forward), **Resume from here** (resume recording
  from the slider position).
- Dragging the slider and releasing fires a seek automatically.

### 5. Markers

- Lists every external-event marker recorded in this session.
- Each row shows frame, tinframe, kind (`tape_control`, `disk_write`,
  `debugger_edit`, `other`), and the reason string.
- Double-click a marker to seek to that frame. If the marker is a
  replay barrier, the seek will stop at the barrier (the engine reports
  `halt_reason=external_event` in the status bar).

## WebAPI endpoint mapping

Every UI control maps to exactly one TTD endpoint:

| Control | Method | Endpoint |
|---|---|---|
| Instance picker | GET | `/api/v1/emulator` |
| State badge, form | GET | `/api/v1/emulator/{id}/ttd/status` |
| Start recording | POST | `/api/v1/emulator/{id}/ttd/start` |
| Stop | POST | `/api/v1/emulator/{id}/ttd/stop` |
| Invalidate | POST | `/api/v1/emulator/{id}/ttd/invalidate` |
| Slider cursor, frame readout | GET | `/api/v1/emulator/{id}/ttd/position` |
| Slider release, Seek button | POST | `/api/v1/emulator/{id}/ttd/seek` |
| < Frame | POST | `/api/v1/emulator/{id}/ttd/step-back` |
| Frame > | POST | `/api/v1/emulator/{id}/ttd/step-forward` |
| Resume from here | POST | `/api/v1/emulator/{id}/ttd/resume` |
| Markers panel | GET | `/api/v1/emulator/{id}/ttd/markers` |

## Architecture

```
MainWindow (UI thread)
    │  click handlers call worker.request_*  (cross-thread queued slots)
    ▼
PollWorker (QObject moved to a dedicated QThread)
    │  QTimer on the worker thread drives adaptive poll loop
    ▼
TTDApiClient (subclass of UnrealApiClient)
    │  requests
    ▼
  HTTP  /api/v1/emulator/{id}/ttd/*
```

The worker uses the Qt **worker-object pattern**: `PollWorker` is a
`QObject` (not a `QThread` subclass) that is moved to a dedicated
`QThread` via `moveToThread()`. This is the officially recommended Qt
pattern for worker threads. Every `QTimer` it creates and every slot
that runs on it executes on the worker thread — no thread-affinity
ambiguity, no `QTimer` warnings.

All HTTP I/O happens on the worker thread; the UI never blocks. The
worker emits signals back to the UI thread for state updates, position
updates, markers, and per-action results. The polling cadence adapts:

- No instance selected: poll every 2 s.
- TTD idle: poll every 1 s.
- TTD recording or detached: poll every 500 ms so the slider cursor and
  markers stay responsive.

UI clicks queue actions; the worker drains one action per tick so HTTP
calls stay serialized and the result signal for each is clean.

### File layout

```
ttd-scrubber/
├── README.md                       this file
├── requirements.txt                PySide6, requests
├── run.sh                          convenience launcher
└── src/
    ├── __init__.py                 empty package marker
    ├── main.py                     QApplication entry point
    ├── main_window.py              UI composition (5 panels)
    ├── poll_worker.py              QObject HTTP worker (moved to QThread)
    ├── ttd_client.py               TTDApiClient (subclass of UnrealApiClient)
    └── _threading_smoke_test.py    headless threading verifier
```

The shared `UnrealApiClient` (`../webapi/src/api_client.py`) hosts the
10 `ttd_*` HTTP methods. This tool's `TTDApiClient` only adds instance
discovery helpers; it inherits the HTTP surface so any other tool (or
pytest fixture) reuses the same code path.

### Threading smoke test

`src/_threading_smoke_test.py` is a headless verifier for the
worker-object pattern. It runs without a display and without a live
emulator (the HTTP client is stubbed). It checks three things:

1. The QTimer inside `PollWorker` has worker-thread affinity
   (`QTimer.thread() is worker_thread`).
2. The timer keeps firing across many intervals (i.e. `setInterval()`
   does not silently kill the timer).
3. A queued `start` action drains via the `action_result` signal.

Run it from the `src/` directory:

```bash
cd tools/verification/ttd-scrubber/src
python3 _threading_smoke_test.py
```

This test caught the original bug where `_tick` ran on the UI thread
and silently killed the timer via cross-thread `setInterval()`.

## Troubleshooting

**"TTD NOT available" in red on the session panel.**

The emulator binary was built without the TTD engine. Rebuild with the
runtime `timetravel` feature flag and the build-time WebAPI flag on. The
engine is built by default; this indicator is mainly a sanity check.

**Slider doesn't move while recording.**

Confirm the emulator isn't paused. Recording only advances frame count
when the emulator runs. The position poll fires every 500 ms during
recording; the slider cursor tracks the current frame.

**Seek hangs or status bar shows `halt_reason=out_of_range`.**

You tried to seek beyond the recorded range. The slider clamps to the
end frame; if you type a frame manually via the API you can hit this.
Use the slider or step buttons.

**Seek stops at a marker with `halt_reason=external_event`.**

Expected. The marker is a replay barrier (tape control, disk write, or
debugger edit). The engine intentionally halts there so you can inspect
state. To proceed further back, seek to an earlier frame. To proceed
forward past the marker, use **Resume from here** which truncates
history at the current position.

**Worker thread won't stop on close.**

The window's `closeEvent` calls `request_shutdown()` then `quit()` then
`wait(3000)`. If the worker is mid-HTTP-request, it may take up to the
HTTP timeout to return. If you see this consistently, file a bug with
the contents of the status bar at exit time.

**Console shows `QObject::killTimer: Timers cannot be stopped from
another thread` during process exit.**

Benign. This warning appears when Python's garbage collector destroys
the worker `QObject` (and its child `QTimer`) after the worker thread
has already exited. The timer was already inert (its event loop is
gone); the warning is Qt complaining that the cleanup call crossed
threads. To suppress it entirely, ensure the `QThread` is fully torn
down before the `QObject` is GC'd — in practice this only happens at
process exit and is harmless.
