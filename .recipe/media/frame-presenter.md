# Recipe: frame presenter and smooth motion (present policy, display rate, the present report)

Goal: from automation, choose how the unreal-qt window brings frames to the screen (VRR or the refresh grid), switch
smooth motion (`off`, `blend`, `interpolate`, `auto`) and the display rate switch, read what the presenter did
(presents, late ones, timing error, the GPU time per pass) and be told when the presenter's setup changes.

Reference: [frame presenter design](../../docs/inprogress/2026-10-26-frame-presenter/README.md) (sections 4 and
6.4), [command-interface.md `video window`](../../docs/emulator/design/control-interfaces/command-interface.md)
(every field), `GET` / `PUT /api/v1/emulator/{id}/video/window`, the monitor name `video.present`.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) inside host-side pipelines
> or when MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## What there is

- **The window's settings, not the machine's.** Only a GUI unreal-qt has them, for the instance its window shows;
  headless: 409 `no-window`. No control session needed. They are the user's window: put them back after a test.
- **Settings** (the shown instance's `[VIDEO]` values, at run time):

  | Field | Words | What |
  |:--|:--|:--|
  | `smooth_motion` | `off`, `blend`, `interpolate`, `auto` | the picture between two frames on a fixed display: the frame due, a mix of the two, the moving layers drawn at the refresh's time, or interpolate only while the display has neither VRR nor a fitting rate. VRR shows each frame as it is whatever this says |
  | `effects_device` | `gpu`, `cpu` | where smooth motion runs; the same bits either way |
  | `present_policy` | `auto`, `vrr`, `vsync` | native presenter (Metal, DXGI): each frame at its own time, or on the refresh grid |
  | `display_rate` | `keep`, `match` | switch the display to a rate fitting the machine (50 / 100 Hz) while the window shows it |
  | `lip_sync_offset_ms` | -200..200 | moves the frames' target times against the sound |

- **The report** `present` (read with the settings): `presenter` (`metal` / `dxgi` / `gl` / `software`),
  `fixed_display`, `vrr`, `sync` {`mode`, `reason`}, `refresh_hz` {min, max}, `clock`, `presented`, `shown`,
  `dropped`, `late`, `beat` {frames, off}, `lead_ms`, `error_ms` {p50, p99, max}, `sound_offset_ms`, `gpu_wait_ms`,
  `gpu_ms` {`smooth`, `picture`: {p50, max, longest, measured}}, `smooth` {mode in effect, device, game, frames, pictures, renders,
  blended, interpolated, motion_fields, on_device, held_flicker}, `display` {state, reason, hz, original_hz,
  machine_hz}. The counters cover the native presenter's last 600 presents; the GL and software paths have no screen
  times (they stay 0).
- **`gpu_ms`**: the GPU time of smooth motion's steps (`smooth`) and of the picture (`picture`: CRT composition,
  beam model, preset), from GL timestamps read back frames later. `measured` 0: no timer queries (or the CPU path).
  `longest` is since the GL context was made: a GPU reset makes a new one.
- **`video.present`** (a monitor name and a WebSocket topic of the process): the setup only, never the counters:
  `available`, `emulator`, `presenter`, `present_policy`, `fixed_display`, `vrr`, `sync`, `refresh_hz`, `smooth`
  {setting, mode, device, game}, `display`. A message only when one of them changes (a VRR turn, a refused rate
  switch). Poll `video/window` for the counters, at most once a second (the window publishes them once a second).
- **Pitfalls:**
  - An offscreen or covered window gets no refreshes on macOS (the presenter rests while occluded): the counters do
    not move. Measure on a window that is seen.
  - `smooth_motion` on a VRR display changes nothing visible (`present.vrr` true): use `present_policy: "vsync"` to
    look at it on such a display.
  - With View > Video Synchronization on, the window gives a newly shown instance `smooth_motion = auto`,
    `present_policy = auto`, `display_rate = match`; later changes from automation stay.

## MCP (preferred)

```text
capture_media {"action":"window_display"}
capture_media {"action":"window_display_set","display":{"smooth_motion":"interpolate","present_policy":"vsync"}}
capture_media {"action":"window_display_set","display":{"display_rate":"match"}}
capture_media {"action":"window_display_set","display":{"smooth_motion":"off","present_policy":"auto","display_rate":"keep"}}
```

The text has a `present:` line (presenter, VRR or grid, presented / dropped / late, lead, error p50 / p99, the beat,
screen to sound, the clock, the display rate state, smooth motion in effect and its counters). Read `gpu_ms` from
`structuredContent.present.gpu_ms` when the text does not show it.

The setup without the counters: `inspect_state {"aspect":"monitor"}` (the process's `video` entry).

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
EMU_ID=$(curl -s $BASE/emulator | jq -r '.emulators[0].id')

# settings + report
curl -s $BASE/emulator/$EMU_ID/video/window | jq '{smooth_motion, effects_device, present_policy, display_rate, present}'

# interpolate on the refresh grid; the answer is the state after the change
curl -s -X PUT $BASE/emulator/$EMU_ID/video/window -H 'Content-Type: application/json' \
     -d '{"smooth_motion":"interpolate","present_policy":"vsync"}' | jq '.present.smooth'

# what it cost on the GPU, how late the presents were
curl -s $BASE/emulator/$EMU_ID/video/window | jq '.present | {late, dropped, error_ms, gpu_ms}'

# the setup only (also what the topic pushes)
curl -s $BASE/monitor/video.present | jq

# back to the user's settings
curl -s -X PUT $BASE/emulator/$EMU_ID/video/window -H 'Content-Type: application/json' \
     -d '{"smooth_motion":"off","present_policy":"auto","display_rate":"keep"}' > /dev/null
```

An unknown word: 400 `bad-parameter` (the message lists the words); headless: 409 `no-window`.

Be told when the setup changes (WebSocket `GET /api/v1/ws`, the same port):

```text
-> {"op":"subscribe","topics":["video.present"]}
<- {"op":"subscribed","t":"ws","topics":["video.present"],"max_hz":10}
<- {"t":"video.present","op":"state","value":{"available":true,"presenter":"metal","vrr":false,...}}
```

One `state` message at once, then one per change; no `instance` field (the topic is the process's).

## Measuring what smooth motion shows

The pictures, not the counters: `tools/verification/smoothmotion` (synthetic programs with ground truth in
`testdata/video/smoothmotion`, `run_scenes.sh <webapi port>`; the live window's refreshes with `live/live.py` and
`UNREAL_SMOOTH_TRACE`). See its [README](../../tools/verification/smoothmotion/README.md).
