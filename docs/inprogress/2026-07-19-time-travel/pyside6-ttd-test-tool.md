# TTD Scrubber — PySide6 WebAPI Client

## Summary

A standalone PySide6 desktop app that drives the TTD engine via WebAPI. The user connects to a running emulator WebAPI server, the tool waits for at least one active emulator instance, then exposes:

- **TTD on/off** — Start/Stop/Invalidate the recording session.
- **Timeline scrubbing** — slider over the recorded frame range, with Step Back / Step Forward / Seek / Resume buttons.
- **Live state polling** — session info, current position, and external-event markers (replay barriers) refresh automatically.

Target folder: `tools/verification/ttd-scrubber/` — follows the same sub-project layout as `tools/verification/webapi/` and `tools/verification/videowall/` (`src/`, `requirements.txt`, `README.md`).

## API surface used

All 10 TTD endpoints from `core/automation/webapi/src/api/ttd_api.cpp`:

| Method | Endpoint | UI binding |
|---|---|---|
| GET | `/api/v1/emulator` | Instance picker |
| GET | `/api/v1/emulator/{id}/ttd/status` | State badge + session info panel (polled) |
| POST | `/api/v1/emulator/{id}/ttd/start` | "Start recording" button |
| POST | `/api/v1/emulator/{id}/ttd/stop` | "Stop recording" button |
| POST | `/api/v1/emulator/{id}/ttd/invalidate` | "Invalidate" button (with confirm) |
| GET | `/api/v1/emulator/{id}/ttd/position` | Timeline cursor (polled) |
| POST | `/api/v1/emulator/{id}/ttd/seek` | Slider release + "Seek" button |
| POST | `/api/v1/emulator/{id}/ttd/step-back` | "< Frame" button |
| POST | `/api/v1/emulator/{id}/ttd/step-forward` | "Frame >" button |
| POST | `/api/v1/emulator/{id}/ttd/resume` | "Resume from here" button |
| GET | `/api/v1/emulator/{id}/ttd/markers` | Markers list (polled, click-to-seek) |

## File layout

```
tools/verification/ttd-scrubber/
├── README.md                       # Setup, usage, screenshots-described-in-text
├── requirements.txt                # PySide6, requests
├── run.sh                          # convenience launcher (cd src && python main.py)
└── src/
    ├── __init__.py                 # empty package marker
    ├── main.py                     # entry point: QApplication + MainWindow
    ├── ttd_client.py               # TTDApiClient — extends existing UnrealApiClient
    ├── poll_worker.py              # QThread-based HTTP worker (non-blocking UI)
    └── main_window.py              # QMainWindow composing all widgets
```

Plus one **modification to existing file**:

- `tools/verification/webapi/src/api_client.py` — add the 10 TTD methods to `UnrealApiClient` so any tool (including this one) can reuse them. Single source of truth.

## Architecture

### Threading model

Single `QThread` (`PollWorker`) running a `QTimer`-driven loop. All `requests` calls happen on the worker thread; results are passed to the UI thread via Qt signals. UI clicks trigger HTTP calls through the same worker (queued signals). This keeps the UI responsive and avoids QNetworkAccessManager complexity.

```
┌─────────────────┐  signals (state, position, markers, error)
│   PollWorker    │ ──────────────────────────────────────────┐
│  (QThread)      │                                             ▼
│  - QTimer 1Hz   │  slots (start, stop, seek, step, resume)  ┌──────────┐
│  - requests     │ ◀──────────────────────────────────────── │ MainWindow│
└─────────────────┘                                           │ (UI thr) │
                                                              └──────────┘
```

### State machine

```
Disconnected ──Connect──▶ Connecting ──OK──▶ NoInstance
                                              │
                                              │ /emulator non-empty
                                              ▼
                                          InstanceSelected ◀──select──┐
                                              │                       │
                                              │ /ttd/start            │ /ttd/invalidate
                                              ▼                       │
                                          Recording ◀────/ttd/resume──┘
                                              │
                                              │ /ttd/stop or seek
                                              ▼
                                          Detached (browsing)
```

The state badge in the UI reflects `state` from `/ttd/status` (`idle` / `recording` / `detached`).

### Polling cadence

- **No instance yet**: poll `/api/v1/emulator` every 2 s. Auto-pick the first instance when one appears.
- **Instance selected, TTD idle**: poll `/ttd/status` + `/ttd/position` every 1 s.
- **TTD recording**: poll every 500 ms (slider max grows).
- **Detached (after seek/stop)**: poll every 500 ms so cursor follows seeks.

The cadence adapts in `PollWorker._tick()` based on the last known state.

### Widget layout (single `QMainWindow`)

```
┌──────────────────────────────────────────────────────────────────┐
│ Connection bar:  [http://localhost:8090]  [Connect]  ● Connected │
├──────────────────────────────────────────────────────────────────┤
│ Instance: [0: ZX48 ▼]   Model: ZX48    Frame: 12345              │
├──────────────────────────────────────────────────────────────────┤
│ TTD Session                                            [state] │
│   Start frame: 0      End frame: 67890                            │
│   Checkpoints: 678      Page store: 12.3 MB / 64 MB (19%)        │
│   [Start] [Stop] [Invalidate]                                    │
├──────────────────────────────────────────────────────────────────┤
│ Timeline                                                          │
│   ◀███████████████●─────────────────────────▶  Frame 12345/67890│
│   [< Frame]  [Seek]  [Frame >]  [Resume from here]               │
├──────────────────────────────────────────────────────────────────┤
│ External-event markers (replay barriers)                         │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ frame=100 tin=0  tape_control   "Tape play pressed"        │  │
│  │ frame=540 tin=0  disk_write     "Sector write C0H0S1"      │  │
│  │ ...                                                         │  │
│  └────────────────────────────────────────────────────────────┘  │
│  (double-click → seek to marker; seek will stop at the barrier)  │
└──────────────────────────────────────────────────────────────────┘
```

## Implementation order

Each step ends with a runnable state. Write files incrementally per user behavior preference.

1. **Scaffold** — folder, `requirements.txt`, empty `src/__init__.py`, `run.sh`. No code yet.
2. **Extend `api_client.py`** — add 10 TTD methods to `UnrealApiClient`. Reusable, tested by import.
3. **`ttd_client.py`** — thin `TTDApiClient(UnrealApiClient)` subclass if any helper methods are needed (probably just convenience: `seek_frame(id, frame, tin=0)` etc.). May end up empty if `api_client.py` covers it.
4. **`poll_worker.py`** — `PollWorker(QThread)` with state machine, signals for state/position/markers/instances/error, slots for start/stop/invalidate/seek/step_back/step_forward/resume/select_instance.
5. **`main_window.py`** — `MainWindow(QMainWindow)` composing all widget groups; wire signals/slots to `PollWorker`.
6. **`main.py`** — `QApplication` entry, parse `--url` CLI arg, show window.
7. **`README.md`** — setup, usage, architecture overview, troubleshooting.
8. **Manual smoke test** — start emulator with WebAPI on, run script, exercise each button.

## What's NOT in scope

- **Screen preview** — no live framebuffer rendering (separate concern; WebAPI already has `/state/screen` if added later).
- **Snapshot/tape loaders** — out of scope; assume emulator is already loaded by other means.
- **WebSocket streaming** — polling is enough; WebAPI doesn't currently expose a TTD WebSocket.
- **GDB integration** — separate GDB RSP work item.
- **Automated tests** — GUI tool, manual verification only. The underlying WebAPI endpoints are already covered by `ttd_automation_contract_test.cpp`.

## Dependencies

```
PySide6>=6.5
requests>=2.28
```

PySide6 chosen over PyQt6 for LGPL licensing (matches the project's existing licensing posture for tooling).

## Manual verification checklist (end of work)

1. Start emulator with `-dwebapi -w 8090` (or however WebAPI is launched).
2. `python src/main.py --url http://localhost:8090` — should show "Connected, no instance".
3. Start an emulator instance via the existing API or UI — tool auto-selects it within 2 s.
4. Click "Start recording" — state badge flips to "recording", slider range starts growing.
5. Wait a few seconds, click "Stop recording" — state flips to "detached".
6. Drag slider to a past frame, release — seek runs, slider cursor jumps.
7. Click "< Frame" / "Frame >" — single-frame step works.
8. Click "Resume from here" — state returns to "recording" from the seek point.
9. Click "Invalidate" — confirm dialog, state returns to "idle", slider collapses.
10. Double-click a marker in the list — seeks to that frame (and stops there because it's a barrier).
