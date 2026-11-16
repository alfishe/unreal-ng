# TTD branches: time and branches in the debuggers

Part of [TTD branches](README.md). Status: **done** (2026-11-12). §4 is in both debuggers: Embedded
(`unreal-qt/src/debugger/debuggertimetravel.cpp`, the branch commands shared with the TTD panel in
`unreal-qt/src/widgets/ttdbranchcommands.cpp`; tests `DebuggerTimeTravel_Test`) and eCat
(`debuggers/qt/ecat/dialogs/debugger/ttdcontrols.cpp` over `Emulator::ttd_call`; `unreal-debugger --self-check`).
§5 is in the core: `reverse-step-over` on every plane, and `forks_here` in the `branches` answer.

## 1. Scope

| Debugger | In scope | Talks to the core |
|---|---|---|
| Embedded (`unreal-qt/src/debugger/debuggerwindow.cpp`, Debug > Debugger > Embedded) | yes | in process: `ttd::TTDControl`, the published session info |
| eCat (`debuggers/qt`, `unreal-debugger`) | yes | in process (`unrealmachine.cpp`) or a running unreal-qt over REST (`remotemachine.cpp`) |
| TUI classic, TUI TS-Conf (`debuggers/tui/`) | **no, never** | historical debuggers: they stay as they are (owner 2026-11-12) |
| GDB, DeZog | unchanged protocol (surfaces.md §6) | not part of this page |

## 2. What exists today

- Neither debugger can move in time. The Embedded one redraws when the TTD panel seeks (`frameRefresh`); eCat has
  "find the last write" through the GUI control plane (`ttd.find_last_write`). Nothing shows the branch.
- In the past (session `detached`), a forward step or run **replays the recorded history**: the journal owns the
  input, live input is refused, and the machine pauses itself at the end of the session. A tool edit in the past is
  refused. A branch starts only by an explicit act (owner Q-1 (a), 2026-11-08): `resume` in the past.
- The core verbs a debugger needs: `step-instruction {dir}`, `reverse-step {count | tstates | scanlines}`,
  `reverse-continue {pcs, ...}` (back to the last time one of these PCs ran), `seek`, `resume`, `branches`,
  `branch-switch`, and the published session info (`branches`, `activeBranch`, `viewedBranch`). There is no reverse
  step over / step out yet.

## 3. The rules

1. Without a time travel session the debuggers look as today: nothing new shows.
2. Without branches (trunk alone) only the time controls show; nothing about branches.
3. Every forward action in the past (Step, Step Over, Step Out, Run, Run to cursor) replays the active lineage and
   says so; it never forks. The fork is one explicit button, **Branch Here**, with the TTD panel's wording.
4. The debugger stays on the active lineage, like GDB: a branch switch is the TTD panel's or the branch menu's act,
   and every debugger view redraws after it.
5. Extend existing widgets: the debuggers' own toolbars and state label. No new docks or windows.
6. Every action exists on the automation planes already (verbs); the debuggers add no capability of their own.
7. Nothing records unless the user asked for it (owner 2026-11-12): no debugger action starts or continues a
   recording by itself. Only Start Rec, Continue Rec and Branch Here record.

## 4. Proposal

### 4.1 Time controls (both debuggers)

In the toolbar, right after the forward steps, a group shown only while a session exists:

| Button | Key (eCat / Embedded) | Verb |
|---|---|---|
| Step back (one instruction) | Shift+F7 | `step-instruction {dir: back}` |
| Run back to a breakpoint | Shift+F9 | `reverse-continue {pcs: <the enabled execution breakpoints>}` |
| Frame back | - | `step-back` |
| Step over back | Shift+F8 | `reverse-step-over` (new, §5): back over a CALL / RST as one step |
| Branch Here / Continue Rec | - | `seek` to the end when needed, then `resume` (the TTD panel's flow) |

There is no reverse step out (D-2).

### 4.2 Where the machine is (both debuggers)

The state text (eCat's toolbar label `m_state`, the Embedded window's title or its toolbar label) gains the moment:

- live: `Recording` (as today plus the TTD state)
- in the past: `History: frame 1 234 of 1 900, replaying` - forward actions replay
- with branches: `... on '+00:00:02.785' (3 branches)`
- at a fork point: `... fork here: 2 other futures` while the moment is one where another branch leaves

### 4.3 Branches (both debuggers)

The branch part of the state text is a drop-down (a `QToolButton` menu on the toolbar):

- the branches of the session, the active lineage first, the active one checked; picking one switches
  (`branch-switch`, at its end or at the same frame when it is on the lineage)
- "Branches leaving here" when the moment is a fork point
- full management in both (D-1 (b)): under the list, for the branch picked in a submenu - Switch, Rename...,
  Tags..., Note..., Pin / Unpin, Delete... (the TTD panel's question: children, pins, and "the timeline runs on it:
  switches to the parent first"), Save Lineage...; the trunk is never deleted here (Clear in the TTD panel)
- Embedded also has "Show in the TTD panel" (opens the panel's Branches row)
- eCat remote (over REST) uses the same verbs: `PATCH /ttd/branches/{branch}`, `DELETE`, `POST .../switch`

## 5. Core work this needs

- **Reverse step over** (D-2): one instruction back, but when that instruction is the return of a routine the
  previous instruction called (a RET / RETI / RETN or a conditional return, reached from a CALL / RST of the same
  stack level), back to that CALL / RST in one step; an interrupt handler entered in between is stepped over too.
  A new verb on every plane (CLI, WebAPI, WebSocket, MCP, Lua, Python) with its docs, like `step-instruction`.
- `reverse-continue` with the debugger's breakpoint list as `pcs`, and memory / port breakpoints (`find-last`).
- A "fork here" query: the branches leaving at the current moment (the published view has it; a REST field for eCat
  remote).

## 6. Open questions (one decision each)

- **D-1. Branch management in the debuggers.** (a) Show + switch only, the rest in the TTD panel; (b) full
  management (rename, pin, delete) in both debuggers; (c) show only.
  **Decided 2026-11-12 (owner): (b), full management in both debuggers.**
- **D-2. Reverse step over / out.** (a) In the first delivery, with the core verbs; (b) later, step back and run back
  first.
  **Decided 2026-11-12 (owner): reverse step over in the first delivery, on every automation plane and in the docs;
  no reverse step out (rarely wanted).**
- **D-3. Forward actions at the end of a stopped recording.** (a) Run live, unrecorded (today); (b) ask "Continue
  recording?"; (c) refuse until Continue Rec.
  **Decided 2026-11-12 (owner): (a), live and unrecorded as today.** The machine pauses itself at the end once; the
  next Run / Step goes on live and the state text says "Live, not recorded"; recording again is Continue Rec (it goes
  back to the end first).

## 7. Fixed: a recording started inside a frame end

Found by `unreal-debugger --self-check` (2026-11-13), fixed the same day.

A breakpoint of a card's CPU (General Sound) that fires in the card's catch-up at the frame end parks the machine's
thread inside `MainLoop::CompleteFrame`: `Core::AdjustFrameCounters` has already moved `frame_counter` (N, the CPU at
its overshoot), the TTD hook `OnFrameBoundary` for N has not run yet. A recording started there took its first
checkpoint as (N, overshoot) before the frame's boundary work (`OnFrameEnd`, `OnFrameStart`, `BeginFrame`, media,
port decoder); when the machine ran on, the boundary hook captured N again and the engine refused it ("frame N does
not follow the last recorded frame"): the recording stopped at once with `last_stop_reason: capture-failed`.

The fix: `Emulator::IsParkedInsideCardRun` marks the machine's thread parked inside another CPU's run (a breakpoint or
a step of the card; at the frame end or in a host port access, neither a moment a checkpoint can hold).

- `StartRecording` there does not capture: the start is pending (`start_pending` in `status` and in the `start`
  reply, `state` idle) and `OnFrameBoundary` starts the recording at the next frame boundary, its baseline the frame's
  start as every other checkpoint. `stop`, `invalidate` and a session end cancel it.
- The moves (`seek`, the steps, `reverse-*`, `resume`, `branch-switch`, `load`) are refused there (409): the rest of
  the card's run and of the frame end would go on over the restored machine.

Test: `core/tests/debugger/ttd/ttdcontrol_cardpark_test.cpp`. The eCat self-check no longer runs frames before its
`start`.
