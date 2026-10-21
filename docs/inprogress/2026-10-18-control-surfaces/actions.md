# Control surfaces: actions, the seek, marks and readouts

| | |
|---|---|
| **Date** | 2026-10-18 |
| **Status** | Design, for review |
| **Proposals** | P2, P3, P7, P8 in [README.md](README.md#proposals-for-the-owner-2026-10-18) |
| **Extends** | [debug-and-ttd.md §3 action catalogue](../2026-10-17-controller-manager/debug-and-ttd.md#3-action-catalogue), `unreal-qt/src/controllers/defaultactions.cpp` |

## Contents

- [1. v1 catalogue](#1-v1-catalogue)
- [2. ttd.seek_by](#2-ttdseek_by)
- [3. Timeline marks](#3-timeline-marks)
- [4. run.step_by](#4-runstep_by)
- [5. Readouts](#5-readouts)
- [6. Planes](#6-planes)

## 1. v1 catalogue

The ids of [debug-and-ttd §3](../2026-10-17-controller-manager/debug-and-ttd.md#3-action-catalogue) stay. v1 moves
every working action of `unreal-qt/src/controllers/defaultactions.cpp` into the core registry (the stubs stay stubs)
and adds three. Kind: P press, D delta, L level, T text (L added in S1 for the analog scrub). Scope rules as the companion gate already decides the routes
(`automationgate.cpp`); "pause" = needs a paused machine (`CompanionNeedsPause`).

| Id | Kind | Scope rule | Executor | |
|:--|:--|:--|:--|:--|
| `run.pause_toggle` | P | pausing: free; resuming: run | `RunControl` | moved |
| `run.step_in`, `run.step_over`, `run.step_out`, `run.frame_step` | P | debug, pause | `RunControl` | moved |
| `run.wait_int`, `run.to_scanline`, `run.n_scanlines` | P | debug, pause | `RunControl` | moved |
| `run.turbo_hold` | P, held | run | `RunControl` | moved |
| `run.step_by` | D | debug, pause | `RunControl` / `TTDControl` ([§4](#4-runstep_by)) | new |
| `ttd.record_toggle` | P | free (TTD start / stop) | `TTDControl` | moved |
| `ttd.seek_by` | D | rewind | `TTDControl::Seek` ([§2](#2-ttdseek_by)) | new |
| `ttd.step_back_frame`, `ttd.step_forward_frame`, `ttd.resume_live` | P | rewind | `TTDControl` | moved |
| `ttd.step_back_instruction`, `ttd.step_forward_instruction` | P | debug, pause | `TTDControl` | moved |
| `ttd.reverse_step`, `ttd.reverse_continue`, `ttd.bookmark_add` | P | debug | `TTDControl` | moved |
| `cap.screenshot` | P | free (captures) | the capture route's logic | stays a GUI action in S1 (the clipboard copy of the window's picture), `gui` |
| `mach.reset` (held 1 s), `mach.nmi`, `mach.turbo_toggle` | P | run | `RunControl` | moved |
| `ctx.set` | T | free | `ContextState` | new |
| `dbg.open` | P | GUI | Qt | moved, `gui` |
| `ttd.jog_frames`, `ttd.jog_instructions` | D | rewind; debug, pause | `TTDControl` (landed on master after this design, `720fbb53e`) | moved |
| `ttd.scrub_back`, `ttd.scrub_forward` | L (level 0..1) | rewind | Qt timer → `ttd.jog_frames` on the worker | `gui` |

As built in S1 (2026-10-20, `core/src/emulator/actions/builtinactions.cpp`): `run.to_scanline` and `run.n_scanlines`
take `params.line` (default 0) and `params.count` (default 1) instead of the Qt dialog's prompt; the stubs answer
`Not Implemented` ("not available yet"); the debugger window's pause no longer switches the debug mode on (the
action pauses as `POST /pause` does).

The stubs (`ttd.find_last_write`, `ttd.bookmark_prev/next`, `dbg.focus_*`, ...) get executors in C7
of the [controller manager TODO](../2026-10-17-controller-manager/TODO.md), not here.

## 2. ttd.seek_by

Value: `delta` (steps, signed), `rate` (steps/s), `params {unit, min, max, rate_lo, rate_hi}`. Defaults
`unit: frames`, `rate_lo: 4`, `rate_hi: 40` (the owner's tuning on the Stream Deck, 2026-10-17).

```
t        = clamp((rate - rate_lo) / (rate_hi - rate_lo), 0, 1)
per      = min * (max / min) ^ t                       // per step, in the unit
perFrame = unit == seconds ? max(1, llround(per * fps)) : max(1, llround(per))
frames   = llround(delta * perFrame)
target   = clamp(cursor.frame + frames, cursor.first, cursor.end)
Seek {frame: target, tinframe: 0}                      // tinframe 0: the frame's start
```

- `fps` from the machine's frame period ([architecture §7](architecture.md#7-the-ttd-published-cursor-and-readouts)).
  The conversion to frames happens before rounding, so 0.02 s on a Pentagon is one frame, not zero.
- **Clamping** to the history: a target past the end would come back as a success with `halt_reason:
  out_of_range` and no move; the executor clamps first, so it never sends one. A clamped target is an `edge` mark.
- **Availability**: without a history ("record first"). While recording, the default backend pauses the
  recording for browsing (TTD D8) and the seek goes on; `end` is then the paused end.
- **Result**: the route's body (`reached`, `arrived_at`, `halt_reason`, `earliest`, `blocking_marker`, `state`) plus
  `marks` ([§3](#3-timeline-marks)).
- A seek that halts at an external-event marker (`halt_reason: external_event`, a replay barrier,
  `/ttd/markers`) gives a `barrier` mark; the next turn in the same direction stops there again (the barrier is the
  TTD's rule, not ours).
- The curve is applied here only. A router binding passes its parameters in `params`; a companion passes them in its
  call (the Stream Deck's three dials: 1 → 100, 10 → 1000, 100 → 10 000).

As built (S2, 2026-10-20, `builtinactions.cpp` `SeekBy`): the base is the published cursor's frame; `rate` comes
from the value, else from `params.rate` (the CLI and Python pass it as a parameter); `min` / `max` default to 1 / 100
frames or 0.02 / 2 s; a parameter out of range is `Bad Request`. A move that rounds to 0 frames, or a clamped target
equal to the position, sends no seek: success with the place it stands (and `edge` when clamped). The body adds
`from`, `frame` (the target), `frames`, `fps`, `marks` [{`kind`, `frame`}] strongest first. The v1 backend refuses a
seek while it records (its rule); the engine pauses the recording for browsing (D8).

## 3. Timeline marks

`TimelineMarks::Crossed(from, to, first, fps, bookmarks)` (`core/src/debugger/ttd/timelinemarks.*`): pure, cheap
(arithmetic and the bookmark list), no history access.

| Kind | Rule | Pattern (default profile) |
|:--|:--|:--|
| `second` | the whole second `floor((f - first) / fps)` differs between `from` and `to` (fps is not an integer: 48.83 on Pentagon, so seconds start at frames 0, 49, 98, 147, …, never drifting) | `tick` |
| `ten_seconds` | same with `floor((f - first) / (10 × fps))`; replaces `second` there | `double` |
| `edge` | the target was clamped to `first` or `end` | `buzz` |
| `barrier` | the seek halted at an external-event marker | `buzz` |
| `bookmark` | a bookmark lies in the interval | `double` |

As built: at most one `second` / `ten_seconds` mark per seek, at the boundary nearest the target (forward the start of
the target's second, back the start of the second after it), and one `bookmark`, the nearest the target in
`(from, to]` forward, `[to, from)` back (`ttd::timeline_marks::Crossed`, `timelinemarks.h`).

Several marks in one seek: the strongest plays (`edge`/`barrier` > `bookmark` = `ten_seconds` > `second`); the
40 ms rate limit of [architecture §9](architecture.md#9-haptic-feedback) merges the rest.

## 4. run.step_by

One dial both ways (owner, 2026-10-17: "step in forward, step back backward"):

- `delta > 0`: `RunControl` step in, `min(delta, 16)` times; needs a pause.
- `delta < 0`: `TTDControl` instruction step back, `min(-delta, 16)` times; needs a history (else `not_now`, "record
  first").
- A breakpoint hit while stepping stops the loop; the result names it.

As built (S2): the result is the last step's body plus `steps` (instructions executed: the step that met an execution
breakpoint executed none), `direction` (`forward` / `back`) and `stopped_by` (`breakpoint`, or the error that ended
the loop after the first step). Availability: back without a history is `not_now` "record first"; both ways need a
pause.

## 5. Readouts

| Id | Value | When |
|:--|:--|:--|
| `ttd.position` | `{frame, tinframe, first, end, state, fps}` | the cursor changed, ≤ 50 Hz |
| `run.state` | `{state: running/paused, speed}` | on change |

The context is not a readout: it is the `ctx` message ([protocol.md §3](protocol.md#3-messages)).

## 6. Planes

Every plane (automation planes guidelines; built in S1 except the `surface` topic). Reference:
[command-interface.md §16](../../emulator/design/control-interfaces/command-interface.md#16-host-actions).

| Plane | List | Run |
|:--|:--|:--|
| CLI | `action list [--json]` | `action run <id> [value] [param=val ..] [--json]` |
| Python | `emu.actions()` | `emu.action(id, value=None, **params)` |
| Lua | `actions()` | `action(id, [value], [params])` |
| MCP | `actions` `list` | `actions` `run` (`id`, `value`, `params`, `target`) |
| REST | `GET /api/v1/controllers/actions` (`?instance=`: id, label, group, kind, held, gui, scope, `needs_pause`, `available`, `error`, `reason`) | `POST /api/v1/controllers/actions/{id}` `{instance?, value?, params?}` |
| WebSocket | `{"op":"call","method":"GET","path":"/api/v1/controllers/actions"}` on `/api/v1/ws` | `{"op":"call","method":"POST","path":"/api/v1/controllers/actions/{id}","body":…}`; push: the `surface` topic (`action`, `result`, `ctx`, readouts, S4) |
| Qt | menus and toolbars call the registry (C7); a device's action bindings run through it (`unreal-qt/src/controllers/guiactions.cpp`) | |

Rows for the [controller manager parity table](../2026-10-17-controller-manager/surfaces.md#5-parity-table).
