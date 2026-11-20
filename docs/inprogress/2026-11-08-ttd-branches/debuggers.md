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
- The moves (`seek`, the steps, `reverse-*`, `resume`, `branch-switch`, `branch-create`, `load`) were refused there
  (409): the rest of the card's run and of the frame end would go on over the restored machine. Since 2026-11-20 they
  leave it first (below).

**As built (2026-11-20): moves leave the card's run first.** A live card step at the history's end (F7 / F8 /
Ctrl+F8 on a card's tab, D-3) parks the machine inside the card's catch-up, so Step Back, Frame Back and every other
move right after it answered 409. Now `TTDControl` calls `Emulator::LeaveCardRun` before such a move:

- the machine resumes (an internal requester) with `_leavingCardRun` set: the card targets drop every stop on the way
  (`GSCardTarget::Park` returns at once), and `DebugManager::CardTight` holds tight mode on, so the cards catch up at
  each main CPU instruction start;
- the first main instruction start after the card's run (`DebugManager::CatchUpCards`, after its catch-up) calls
  `Emulator::NoteMainBoundaryAfterCardRun`, which pauses the machine; the loop parks at its clean per-instruction
  point after that instruction (live, unrecorded, as D-3's live steps), not inside the start hooks;
- the caller waits (`WaitDebugChange`, 2 s at most) until the machine is parked outside the card's run, then the
  move goes on. A card step back from there lands at the history's newest card instruction (`history_end`), as the
  main CPU's step back from a live position lands on its last recorded instruction.

The refusal (409, with the reason) stays only where leaving cannot work: the call runs on the machine's own thread
or holds its CPU, the resume is refused, or the machine is not out within 2 s. The extra checks run only while
leaving (an atomic load on the card park and catch-up paths, which run in tight / debug mode only); the live hot path
is unchanged. Moves that bypass `TTDControl` (direct `TimeTravelController` calls) are not covered: every plane and
both debuggers' time groups go through `TTDControl`.

Test: `core/tests/debugger/ttd/ttdcontrol_cardpark_test.cpp` (a start pending inside the frame end's card run; a frame
step back and a card step back right after a live `RunControl::CardStep`). The eCat self-check no longer runs frames before its
`start`.

## 8. Positions on a sound card's CPU (2026-11-18)

The core's card positions ([gs-debugger design §8](../2026-09-27-gs-debugger/design.md),
[command-interface.md](../../emulator/design/control-interfaces/command-interface.md#ttd-on-a-sound-cards-cpu)) in the
debuggers' time group, by the CPU in front.

- **eCat** (in process and over REST alike; `ecat/dialogs/debugger/ttdcontrols.cpp`). With a card's tab in front
  (`gs`, `ms`): Step Back is `step-instruction {cpu, dir: back}`, Run Back `reverse-continue {cpu}` (the card's own
  breakpoints, any kind), and the window's Step (F7) in the past (`detached`) is `step-instruction {cpu, dir:
  forward}`; live, F7 stays the card's own step. Since 2026-11-19 Step Over Back (Shift+F8) is `reverse-step-over
  {cpu}` (back past a whole card routine when the instruction before is its return), and Step over (F8) / Step out
  (Ctrl+F8) in the past are `step-instruction {cpu, kind: over | out}` (the card's step through the history, as a
  live card step would go; its breakpoints end it). At the history's end (`at_end`) F7 / F8 / Ctrl+F8 run the
  card's live step, unrecorded, as the main CPU's steps do there (D-3); no message. Since 2026-11-20 Run to cursor
  (F4) on a card's tab in the past is `step-instruction {cpu, kind: to, pc}` (the card's next instruction at the
  cursor, found by replay; not reached: a message, the live machine untouched; at the history's end the live run),
  and the moves right after a live card step work (§7). Frame Back stays the frame's. The main CPU's tab is unchanged. A stop that names no
  processor (a pause, a move in time) no longer brings the main CPU's tab forward: the card's tab stays in front
  after its back step (`DebuggerWindow::on_debug_event`).
- **The moment** (eCat's state label) names both CPUs at a card position, from `cards[]` of `position` (the poll
  already made): `gs at instruction 1200, PC #0A3C; main CPU PC #8009, +7 T into the next instruction`, or `..., at
  its instruction boundary`. Without one, the card's tab adds where the card stands.
- **Refusals and answers** (`no_breakpoints`, `no_count`, `no_card`; `at_start`, `at_end`, `no_hit`) are said in a
  message (`TtdControls::inform`, a box; the self-check reads them) and in the branch button's tooltip.
- **Embedded** has processor tabs above its panes (`unreal-qt/src/debugger/debuggercpu.cpp`, `DebuggerCpuTabs`):
  Main CPU and one per card CPU fitted (GS / NeoGS, MultiSound GS); with the main CPU alone there is no bar. The tab
  in front is the CPU the registers, stack, memory pages, memory view (its 64 KB), disassembly (its labels and
  breakpoints), the steps, run to cursor and the time group act on. With a card in front the time group routes as
  eCat's (`DebuggerTimeTravel::setCpu`): Step Back `step-instruction {cpu, dir: back}`, Step In in the past
  `step-instruction {cpu, dir: forward}`, Step Over / Step Out in the past `step-instruction {cpu, kind}` (live and
  at the history's end: `RunControl::CardStep`), Step Over Back `reverse-step-over {cpu}`, Run Back
  `reverse-continue {cpu}`. Run to cursor on a card's tab in the past is `step-instruction {cpu, kind: to, pc}`
  (`DebuggerTimeTravel::stepForward("to", address)`; at the history's end the live card run).
  The card's labels (its own `LabelManager`) are the ones its disassembly, memory tooltip and Label Manager show;
  the main CPU's labels and markup never appear on a card's tab (eCat's card disassembly likewise). A breakpoint or step stop brings its CPU's tab to the front once per `Emulator::LastStop().serial`; a
  pause and a move in time leave the tab alone, so the card's tab stays after its Step Back. The moment label
  (`DebuggerTimeTravel::CardMoment`, from `TimeTravelController::PublishedCardPositions` while the machine is paused
  in the history) names both CPUs at a card position; without one, a card's tab adds where its card stands.

Tests: `DebuggerTimeTravelCard_Test` (unreal-qt-tests: the readout's words, a card position in the moment, a main move
ending it; Step Over Back, Step Over / Out in the past and the hand-over at the history's end on the card's tab), `DebuggerCpuTabs_Test` (the tabs, the readers per CPU, a card stop once, a card Step Back); `unreal-debugger --self-check` in process and `--connect` (the card's tab: Step Back, F7 in the past, Run
Back with and without a card breakpoint, at the start, a missing card, the main CPU's tab again).

