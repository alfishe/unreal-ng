# Run until, and frame watches: subscriptions instead of polling

Status: implemented 2026-11-03. Roadmap item "subscriptions instead of polling" of the agent TTD UX work (after the
timeline, cause chain, find event, explain frame, diff of two moments and agent bookmarks).

## 1. The problem

An agent that waits for the machine to get somewhere polls. The recipes
([waiting-and-watching.md](../../../.recipe/analysis/waiting-and-watching.md)) list the loops:

| Waiting for | The loop an agent runs today |
|:--|:--|
| a text on screen (a menu, "PRESS ANY KEY", the 48K copyright) | `run_frames` 10 + `capture/ocr`, repeated |
| the screen to stop changing / to change | `run_frames` + `state/screen/digest`, repeated |
| a byte to reach a value (lives, a level, a flag) | `run_frames` + `memory` read, repeated; or a conditional write breakpoint + resume + the `debug/wait` loop |
| a game event (a profile's `died`) | `run_frames` + `monitor/game.events`, repeated |

Each round is two calls, and the stop lands up to a chunk late (the condition held mid-chunk). The breakpoint
alternative needs the breakpoints feature, a resume, a wait loop with seq checks, and stops mid-instruction, not
at a frame where a picture is complete.

`debug/wait` (long-poll) and the WebSocket `debug.events` already push stops, breakpoint changes and the
`game.events` monitor; nothing pushes "the condition I care about holds now".

## 2. The design

One spec, checked at every **frame end** (`MainLoop::CompleteFrame`, after every other hook of the boundary):

| Rule | Option | Holds when |
|:--|:--|:--|
| condition | `if` (Python `condition`) | the conditional-breakpoint expression (`M(x)`, `W(x)`, registers, `FRAME`, `FD`, `PG0..3`, `x@prev`, labels; no access operands) is true |
| text | `text` | the ROM-font OCR of the screen (as `capture/ocr`; the text layer in text modes) contains it |
| screen | `screen` = `changed` / `stable` (`stable_frames`, default 25) | the active screen's digest (as `state/screen/digest?mode=active`, border folded in) differs from the start / held N frame ends |
| event | `event` | the profile's game event fired after the start |
| combination | `match` = `any` (default) / `all` | any rule / every given rule at the same frame end |

Two consumers of the same spec (`core/src/debugger/watch/framewatch.h`):

- **run_until** (`RunControl::RunUntil` -> `Emulator::RunFramesUntil`): a direct run of at most `max_frames` (default
  3000, at most 200000) that stops, paused, at the end of the first frame where the spec holds. Deterministic: a
  frame boundary, the same on every host, the next frame's first instruction not run. One call replaces the loop.
  Breakpoints are skipped unless `breakpoints: true`. The answer: `met`, `outcome` (met / budget / breakpoint /
  aborted), `frames`, `frame`, `pc`, `ttd_frame` (a recording's cursor), `ms`, `hit` (`reasons`, `text_line`,
  `stable_for`, `event_fires`) and a one-line `text`.
- **armed watches** (`framewatch::Manager`, at most 16): the spec checked on the running machine. A hit is published
  (`NC_FRAME_WATCH` -> `watch_hit` on the WebSocket `debug.events`: frame, reasons, text_line, paused, removed),
  moves the debugger seq (`debug/wait` wakes) and, with `then: pause` (the default), pauses the machine at that
  frame end; the stop's reason is `watch` (`Emulator::DebugStop::Reason::Watch`, the snapshot's `pause` names
  `watch_id`, `watch_label`, `frame`, `reasons`). `once` (default true) removes it with the hit. A direct run is
  not paused by a watch (it ends paused itself); a TTD silent replay (a seek) checks nothing.

Cost: nothing armed = one relaxed atomic load per frame (`Manager::Armed`). A condition is a compiled
`BreakpointConditionProgram`; the screen rule hashes the active screen pages once a frame; the text rule OCRs 768
cells once a frame.

## 3. Planes

| Plane | run_until | watches |
|:--|:--|:--|
| WebAPI | `POST /run_until {if?, text?, screen?, stable_frames?, event?, match?, max_frames?, breakpoints?}` (long-call pool) | `GET /watches`, `POST /watches {... label?, then?, once?}`, `DELETE /watches/{id\|all}` |
| WebSocket | every route as a `call`; `watch_hit` / `watch_added` / `watch_removed` / `watch_error` on `debug.events` | |
| CLI | `run_until [N] key=value ...` | `watch add key=value ...`, `watch list`, `watch del <id\|all>` |
| Lua | `run_until{text = "...", max_frames = N}` | `watch_add{...}`, `watches()`, `watch_remove(id)` |
| Python | `emu.run_until(text="...", condition="...")` | `emu.watch_add(...)`, `emu.watches()`, `emu.watch_remove(id)` |
| MCP | `control_execution {"action":"run_until", ...}` (`condition` for `if`) | `watch_add`, `watch_list`, `watch_remove` (`watch_id`); `wait` names the watch that paused |

## 4. Acceptance: measured through WebAPI and MCP

See [§5](#5-measurement). Accepted only if the one call beats the loop an agent runs today in calls, bytes and
time, and lands on the exact frame.

## 5. Measurement

Script `scratch/run-until-watches/live_run_until.py` (in the worktree); results below.

(filled in after the live run)

## 6. Limits

- The text rule is the ROM font only (as `capture/ocr`): a game's own font is not read.
- The screen rule hashes screen memory, not the rendered picture: a border-only change counts (the border is
  folded in), FLASH does not.
- An armed watch is not saved in a snapshot or a `.ttd`; it belongs to the session that armed it.
- A game event rule needs a profile with `events:` set (a play profile); without one the spec is refused.
