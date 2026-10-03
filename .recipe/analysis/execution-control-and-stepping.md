# Recipe: execution control and stepping

Pause, resume, reset and NMI the machine, advance it by an exact amount (instructions,
frames, T-states, scanlines), run it to a raster position, an interrupt or an address, read
the beam, and manage breakpoints by id (enable, disable, delete, status). Main Z80 on every
machine. Breakpoint semantics (what stops where, events pushed over the WebSocket) are in
[breakpoints-and-events.md](breakpoints-and-events.md); this recipe does not repeat them.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi)
> only inside host-side pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## Which tool for which task

| Task | Use | Why |
|:--|:--|:--|
| Let the machine run, stop it on demand | `resume` / `pause` | free-running; the stop point is not reproducible |
| Deterministic "N frames later" | `run_frames` | exactly N frames of emulated time, always leaves the machine paused |
| Reproducible 1-frame step with a fixed raster phase | `run_frame` | keeps the intra-frame t-state position between calls |
| One instruction, a few, or until something | `step`, `step_n` (`steps`) | stops early on a breakpoint |
| Over a CALL / out of a routine | `step_over`, `step_out` | |
| Exact T-state budget | `run_tstates` (1 .. 10 000 000) | |
| Reach a raster position | `run_to_scanline`, `run_scanlines`, `run_to_pixel` | then read `video/beam` |
| Be at the interrupt handler's entry | `run_to_interrupt` | stops right after the INT is accepted; gives up after 2 frames |
| Wait for PC to reach an address | `skip_until` | breakpoints ignored; bounded by `max_tstates`; check `hit` |
| Wait for a memory / port / address event | a breakpoint, then `resume` | see [breakpoints-and-events.md](breakpoints-and-events.md) |
| Hard / soft reset, NMI | `reset`, `nmi` | |

Worked example: stop at the first paper pixel of the next frame and read the beam:
`run_to_pixel`, then `GET .../video/beam`: `in_paper` is true, `line` is the first paper
line, `dot_in_line` the first paper column in T-states. (Field names from the handler;
the values depend on the model.)

## Timing and pause races (read this once)

- Every `run_*`, `step*`, `skip_until` call **pauses a running machine first** and leaves it
  **paused** (the emulator source comments: "step commands always leave emulator
  paused"). They are synchronous: the HTTP reply arrives when the run is done, with
  `pc`, `sp` and `state`.
- `run_frame`, `run_frames` wait for the emulation thread to park (250 ms / 500 ms)
  before they step the Z80 themselves, so a call right after `pause` is safe. For `step`,
  `steps`, `stepover`, `stepout` the handler calls the run directly (no explicit wait in the
  handler; whether it waits is unconfirmed): pause first and check `state` is `paused`
  before the first step.
- `resume` after those calls continues from exactly where the run stopped.
- `run_frames` counts `frames * frame length` T-states from the current position: the raster
  phase is kept, so `run_frames 1` from the middle of a frame stops in the middle of the next
  one. Use `run_frame` (persistent target position) when repeated calls must hit the same phase.
- **Breakpoints are skipped by `run_frame`, `run_frames`, `run_tstates`, `run_to_scanline`,
  `run_scanlines`, `run_to_pixel`, `run_to_interrupt` and `skip_until`** (the defaults in
  `emulator.h`); they fire under `step` and `steps` only (the handlers pass `false`).
  `step_over` / `step_out` honor breakpoints as `StepOver` / `StepOut` do (not traced for this recipe).
- `run_scanlines` is capped at 1000, `run_frames` at 10 000, `steps` at 100 000.
- `run_to_scanline` completes the current frame first when the position is already past the
  target line, so it never runs backward.
- `resume`, `step`, `steps` and `stepover` are refused with **409 `Run-control held`** while another
  surface (a GDB client) holds the run-control claim; the message names it. `resume` of a
  machine that is not paused returns 400.
- A **TTD replay** drives the machine from the journal; direct runs during a replay are
  not covered by this recipe (unconfirmed; use `time_travel` to move through a replay).

## MCP (preferred)

`control_execution` actions (the handler in `mcp-tools.cpp`); all take `target` (default `auto`):

```text
control_execution {"action":"pause"}                       # reply carries the registers
control_execution {"action":"resume"}                      # "run" is the same call
control_execution {"action":"step"}
control_execution {"action":"step_n","count":100}
control_execution {"action":"step_over"}
control_execution {"action":"step_out"}
control_execution {"action":"run_frame"}
control_execution {"action":"run_frames","frames":50}
control_execution {"action":"run_tstates","tstates":3500}
control_execution {"action":"run_to_interrupt"}
control_execution {"action":"bp_add","address":56,"type":"execution"}
control_execution {"action":"bp_list"}
control_execution {"action":"bp_disable","bp_id":"1"}
control_execution {"action":"bp_enable","bp_id":"1"}
control_execution {"action":"bp_remove","bp_id":"1"}
control_execution {"action":"bp_clear"}
emulator_manage   {"action":"reset"}                       # also pause / resume
```

Not in `control_execution`; use `invoke_api` (paths as in the WebAPI section; `{id}` is
resolved):

```text
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/run_to_scanline","body":{"scanline":100}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/run_scanlines","body":{"count":8}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/run_to_pixel"}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/skip_until","body":{"pc":"0x8000","max_tstates":700000}}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/video/beam"}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/breakpoints/status"}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/nmi","body":{"magic":false}}
```

Differences to know:

- **`run_frames` reads `frames` on MCP; the WebAPI reads `count` (and also accepts `frames`).** `run_frame`
  and `run_frames` both call `/run_frames`; the MCP default is 1.
- `step_n` sends `count`; `run_tstates` sends `tstates`; `bp_id` is a string; `bp_add`'s
  `type` defaults to `execution` (`execution`, `read`, `write`, `port_in`, `port_out`; the WebAPI also takes `exec`,
  `bp`, `r`, `w`, `in`, `out`).
- Every execution-advancing action appends the registers: `structuredContent` carries the
  WebAPI reply plus `registers`. A step ended by a breakpoint says
  `Stopped at breakpoint #N (access) at $ADDR after K instruction(s)`; assert on `stop`.
- `pause` failing with a non-2xx status is an error result; `resume` / `run` treat a 400 / 409 as
  "already running" and succeed.
- A 409 from a stepping action gets the hint "the machine may need to be paused first":
  that hint is generic; a 409 here is normally the run-control claim.

## WebAPI

```bash
BASE=http://localhost:8090/api/v1/emulator; ID=<id>; J='Content-Type: application/json'
curl -s -X POST $BASE/$ID/pause  | jq '{status, state}'
curl -s -X POST $BASE/$ID/resume | jq '{status, state}'
curl -s -X POST $BASE/$ID/reset  | jq '{status, state}'
curl -s -X POST $BASE/$ID/nmi -H "$J" -d '{"magic":false}'      # Scorpion: "magic":true pages the Shadow Monitor in

curl -s -X POST $BASE/$ID/step   | jq '{pc, sp, executed, stop, state}'
curl -s -X POST $BASE/$ID/steps  -H "$J" -d '{"count":1000}' | jq '{pc, executed, stop}'
curl -s -X POST $BASE/$ID/stepover | jq '{pc, sp, state}'
curl -s -X POST $BASE/$ID/stepout  | jq '{pc, sp, state}'

curl -s -X POST $BASE/$ID/run_frame
curl -s -X POST $BASE/$ID/run_frames    -H "$J" -d '{"count":50}'       # or {"frames":50}
curl -s -X POST $BASE/$ID/run_tstates   -H "$J" -d '{"tstates":3500}'
curl -s -X POST $BASE/$ID/run_scanlines -H "$J" -d '{"count":8}'
curl -s -X POST $BASE/$ID/run_to_scanline -H "$J" -d '{"scanline":100}'
curl -s -X POST $BASE/$ID/run_to_pixel
curl -s -X POST $BASE/$ID/run_to_interrupt
curl -s -X POST $BASE/$ID/skip_until -H "$J" -d '{"pc":"0x8000","max_tstates":700000}' | jq '{hit, pc, max_tstates}'

curl -s $BASE/$ID/video/beam | jq '{tstate_in_frame, frame, line, dot_in_line, zone, in_paper}'

curl -s -X POST $BASE/$ID/breakpoints -H "$J" -d '{"type":"exec","address":"0x38"}'   # 201, {id}
curl -s $BASE/$ID/breakpoints | jq '.breakpoints[] | {id, type, address, active}'
curl -s -X PUT    $BASE/$ID/breakpoints/1/disable
curl -s -X PUT    $BASE/$ID/breakpoints/1/enable
curl -s -X DELETE $BASE/$ID/breakpoints/1
curl -s -X DELETE $BASE/$ID/breakpoints                       # clear all
curl -s $BASE/$ID/breakpoints/status | jq '{is_paused, breakpoints_count, last_triggered_id, last_triggered_access, paused_by_breakpoint}'
```

Response fields worth asserting:

| Call | Fields |
|:--|:--|
| `pause`, `resume`, `reset` | `status` (`success` / `error`), `state` (`paused`, `running`, `initialized`, `stopped`) |
| `step` | `executed` (0 when an execution breakpoint stopped it before the instruction), `stop` (`reason` `step` or `breakpoint`, plus `breakpoint_id`, `address`, `access`), `pc`, `sp` |
| `steps` | `count` (clamped), `executed`, `stop`, `pc`, `sp` |
| `stepover`, `stepout`, `run_*` | `pc`, `sp`, `state`; `run_frames` and `run_scanlines` echo `count`, `run_tstates` echoes `tstates`, `run_to_scanline` echoes `scanline` |
| `skip_until` | `hit` (false = budget spent before PC got there), `max_tstates`, `pc` |
| `video/beam` | `tstate`, `tstate_in_frame`, `frame`, `line`, `dot_in_line`, `beam_x`, `beam_y`, `zone`, `vertical_zone`, `horizontal_zone`, `in_visible_area`, `in_paper`, `paper{x,x_end,y}`, `model`, `video_mode` |
| `breakpoints` (GET) | `count`, `breakpoints[]` with `id`, `type` (`memory` / `port` / `keyboard`), `address`, `execute`/`read`/`write` or `in`/`out`, `active`, `note`, `group` |
| `breakpoints/status` | `is_paused`, `breakpoints_count`, `last_triggered_*` (null when none), `paused_by_breakpoint` |

Assert on structured fields, never on `message`. `skip_until`'s `max_tstates` defaults to 100
frames of the model and is capped at 700 000 000. `pc` accepts a number or a string
(`"0x8000"`, `"#8000"`, `"$8000"`, decimal); an address above `0xFFFF` is a 400.

Status codes: 404 unknown id; 400 for a missing `scanline` / `pc`, a bad breakpoint type or
address, an `run_frames` body with neither `count` nor `frames`; 409 `Run-control held`;
404 from `DELETE`, `enable`, `disable` of an unknown breakpoint id; 500 with `error` and
`message` when the run throws. A non-numeric `bp_id` in the path raises an error (the
handler converts it with `std::stoul` and is not guarded: unconfirmed what the client sees).

## Interaction with TTD

[ttd-recording.md](ttd-recording.md) records the machine as it runs. Notes from the handlers:

- Scrubbing (seek, step-back, step-forward) pauses first and leaves the machine paused;
  during `recording` those return 409. `step` / `run_frames` are not among the guarded
  calls in the handlers above, so they run while recording is on: the direct run
  moves the machine ahead without a pause event of its own (unconfirmed what the journal
  holds after a long direct run: record, then verify with `ttd/position` before relying on it).
- `skip_until` states it plainly: frames rendered during the skip are not captured by the
  recording subsystem (raw CPU stepping). Do not use it inside a recording you intend to
  replay frame by frame.
- Prefer `run_frames` over wall-clock waiting when a deterministic point is needed; it is
  the unit TTD seeks in too (`ttd/seek` takes a frame and an optional T-state).
- Loading media wipes the TTD history (MCP instructions), so run to the target state first,
  then start recording.

## Pitfalls

- **Breakpoints and `run_frames`.** A breakpoint never stops a `run_frames` or `run_to_*`;
  `resume` and wait for the pause event, or use `steps`.
- **`resume` after `run_frames`.** The machine is paused; nothing runs until `resume`. A
  script that forgets it waits forever for something to happen.
- **Reading right after `resume`.** The state is `running`: registers and memory are a moving
  target. Pause first.
- **Paused state echoed from before.** `pause` replies `state`; for the free-running case
  confirm with a second `GET .../emulator/{id}` (`emulator_manage` `status`) if it matters.
- **`run_frames` count key.** WebAPI `count` (alias `frames`), MCP `frames`. A WebAPI body
  with a different key (for example `n`) is a 400; an empty body runs 1 frame.
- **Wrong frame phase.** After `step` / `steps` the next `run_frames N` starts mid-frame; use
  `run_to_interrupt` or `run_to_scanline 0` first when a frame-aligned start matters.
- **Beam position while running.** `video/beam` on a running machine returns whatever instant
  it hits; pause (or `run_to_*`) first.
- **Clients holding run-control.** A GDB client or another surface claims it; steps and
  `resume` get 409 until it releases.
