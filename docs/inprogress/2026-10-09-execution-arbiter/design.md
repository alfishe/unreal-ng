# Execution arbiter: design

Part of [README.md](README.md). The current call sites and races are in [inventory.md](inventory.md).

## 1. The bug that started this

unreal-qt adopts a machine created through automation. On adoption, `TtdWidget::applyJournalPreference` calls
`TimeTravelController::SwitchWriteJournal`, which does `Pause(false)`, allocates the write journal (10-20 ms) and
calls `Resume(false)`.

The automation flow lands inside that window:

1. `media insert`, `reset`, `pause`: the pause is a no-op, because `_isPaused` is already set.
2. `run_frames 1500`: `RunNFrames` steps the Z80 on the automation thread.
3. The silent `Resume` clears `_isPaused`, and the main loop wakes up. Two threads now drive one Z80.
4. The boot diverges. The BIOS ends in a random place, often the POST error blinker at `#01DF`.

Traced on 2026-10-09 with a thread-tagged trace and a backtrace of the silent pause. The disk format and the
instance number do not matter: an instance created 2 s before the same flow boots 5 times out of 5.

The fault is in the model, not in `SwitchWriteJournal`. Three different things share one bool:

| Kind | Example | What it needs |
|:--|:--|:--|
| intent | the pause button, `pause` from the API, a breakpoint | a desired state: remembered, the last one wins, broadcast |
| command | media insert, reset, snapshot load, poke, TTD start, `run_frames` | an event with a result: done in order, never merged, never lost |
| park | inside a command: the CPU still while state changes | invisible; never touches the intent |

The inventory shows the effect: 102 sites restore a pause they think they own, and six classes of race follow from
that (inventory §3).

## 2. Goals

| # | Requirement |
|:--|:--|
| G1 | One object per machine, the arbiter, decides whether the Z80 executes. Every thread submits requests to it. Nobody sets flags. |
| G2 | Requests are ordered. Each one gets a sequence number and a ticket, and a synchronous caller waits for its own ticket. "pause returned" means the pause is applied. |
| G3 | Intent requests coalesce: of consecutive intents, the last one is applied. A configurable throttle window bounds how long they gather. |
| G4 | A command is a barrier. Intents never coalesce across it, and commands never merge, drop or reorder. |
| G5 | Exactly one driver at a time: the main loop or one command that steps the Z80. |
| G6 | A park never changes the intent, and its end never lifts a pause. |
| G7 | Only applied intent changes are broadcast. Parks and commands are invisible to the UI, GDB, DeZog and `LastStop()`. |
| G8 | No silent timeouts and no sleeps. A request that cannot be served in its time is answered Busy, with the holder named. |
| G9 | Requests from the emulation thread (hooks, frame tasks, breakpoints) are legal and cannot deadlock. |
| G10 | The arbiter's state is inspectable on every automation surface. |
| G11 | The hot path costs what it costs today: one relaxed atomic load per instruction. |
| G12 | Every mode is tested, fuzzing included (§8). |

## 3. The model

```
  any thread                       ExecutionArbiter (one per machine; a ZX-Poly group uses the master's)
  ─────────                        ───────────────────────────────────────────────────────────────────────
  Submit(intent)   ──┐             queue:  [ #41 intent Pause (cli) | #42 cmd reset | #43 intent Run (gui) | ... ]
  Submit(command)  ──┼──► ticket   current: the command being served (or none)
  priority lane    ──┘             intent:  Run | Pause  + origin, reason, seq of the request that set it
                                   driver:  main loop | command #n on thread T
                                   phase:   not started | running | stopping | stopped
                                   mayRun = phase == running && intent == Run && no command is served
                                   yield  = !mayRun   (atomic; read per instruction by whoever drives)
```

One mutex and one condition variable guard the state. The mutex is never held while the Z80 executes.

### 3.1 Requests and tickets

`Submit` puts a request in the queue and returns a `Ticket`. Every request carries:
- an origin: GUI, WebAPI, CLI, MCP, Lua, Python, GDB, DeZog, debugger, TTD, RZX, lifecycle or internal;
- a tag that names it for logs and inspection;
- a deadline.

`ticket.Wait()` returns one of: Applied (with the result), Coalesced (a later intent replaced this one; the final
intent is returned), Refused (the reason: claim, phase) or Busy (the deadline passed, with the holder named).

Synchronous front ends (WebAPI, CLI, MCP, scripts) wait for their ticket. The GUI waits off its UI thread or does not
wait.

### 3.2 Serving the queue

The arbiter serves the queue at **decision points**:
- the end of a frame in the main loop;
- the per-instruction yield;
- the end of a command;
- a submit while nothing runs.

The service rules:

1. **Intents at the head of the queue** collapse into one: the last intent in that run wins. The arbiter applies it
   when the run is closed. A run is closed when:
   - a command follows it;
   - or the throttle window `[MISC] IntentThrottleMs` (default 0) has passed since the first intent of the run.

   With window 0, every intent present at a decision point coalesces, and nothing waits for a timer. A small window
   (for example 30 ms) absorbs GUI button flapping at the cost of that much latency.

   Coalesced tickets complete as Coalesced, and they return the intent that won.
2. **A command at the head** is served:
   - the driver yields at the next instruction boundary;
   - the command runs, either on its submitter's thread holding the CPU (a park) or on the driver's thread at a frame
     boundary when it asks for that;
   - its ticket completes;
   - the next request is taken.

   While a command is served, `mayRun` is false. This is the park: it is the inside of a command, not a request of
   its own. When the command ends, the intent is still what it was before; the queue decides what happens next.
3. **A command that drives the Z80** (`RunNFrames`, `step`, TTD replay, RZX seek) is a command whose body executes
   instructions on the submitter's thread. Only that thread is the driver while it runs.

   "Stepping leaves the machine paused" is kept as the command's effect: it applies intent Pause with origin "step" at
   its start. A run submitted during the step queues behind it and is applied when the step ends (G4, G5).
4. **Applying an intent** sets `intent`, recomputes `mayRun` and `yield`, wakes the main loop, and broadcasts
   `NC_EMULATOR_STATE_CHANGE` once per edge, with origin and reason. An intent equal to the current one completes
   without a broadcast.

### 3.3 The priority lane

Three things cannot wait in the queue.

| Request | Why | How |
|:--|:--|:--|
| a pause from the emulation thread (breakpoint, TTD end of a detached session, step-over target) | the submitting thread is the driver: waiting for the queue would deadlock | applied at once as intent Pause (debugger), and the driver parks at this instruction; queued intents after it still apply in order |
| abort of the command being served (pause during `RunNFrames 1500`, GDB interrupt, DeZog pause) | the user must be able to stop a long run | sets the command's cancel flag; the command ends at its next instruction boundary with result Aborted; then the pause is queued as a normal intent |
| stop / destroy | lifecycle | phase stopping: the queue is drained with Refused(stopping), the command in service is aborted, the loop joins |

### 3.4 Commands inside commands and the emulation thread

- **A command that submits another command** from inside its body (a reset inside a snapshot load) runs it inline:
  it already holds the CPU. The nested ticket completes at once, and nothing is queued behind the outer command, so
  it cannot deadlock.
- **A command submitted from the emulation thread** in a hook or frame task runs inline at that boundary, because that
  thread is the driver (G9). A hook that needs the queue's order posts the command and does not wait for it.
- **Several machines** (`MachineStateTransfer`): the transfer is one command per machine. The commands are submitted
  in a fixed order, by machine id, and the second one is entered while the first is held. Two transfers therefore
  cannot deadlock.

### 3.5 Snapshot loaders, media, reset

All of these are commands, so a pause or a run cannot slip past them, and none of them leaves an intent behind.
- A snapshot load keeps the intent that was in force.
- "Load and run" (autostart) is a command followed by intent Run, submitted as one pair so nothing can come between
  them.
- Reset keeps the intent. The 10 and 20 ms sleeps around media and reset go away: a served command holds a confirmed
  CPU.
- `MediaManager`'s own pending-insert path becomes a command that runs at a frame boundary.

### 3.6 Lifecycle and ZX-Poly

| Event | Arbiter |
|:--|:--|
| create, not started | commands run at once (nothing drives); intent Pause until `StartAsync` |
| `StartAsync` | phase running; the requested intent applied; the main loop drives while `mayRun` |
| `Stop` | priority lane (§3.3) |
| destroy | after Stop; waits for tickets still referenced (`LeaseContext`) |
| model switch | a command on the old machine; the new machine starts with the old intent |
| ZX-Poly | slaves have no loop and no arbiter: they use the master's. A command on a slave holds the whole group ("one pause stops all CPUs", debugger-model rules §3.2) |

### 3.7 Run-control claim

The claim moves into the arbiter. While GDB (or another holder) has it, intent Run and stepping commands from other
origins are Refused(claim). Pause is always accepted. Internal commands (media, TTD bookkeeping) ignore the claim, as
they do today.

### 3.8 Main loop and hot path

- The main loop parks on the arbiter's condition variable instead of the 20 ms `wait_for` poll. Parking reports
  `Parked(generation)`.
- The per-instruction check reads `yield`: one relaxed load, the same cost as `_isPaused` today (G11).
- `WaitForPauseConfirmation`, `IsPauseConfirmed`, `IsEmulationParked`, `RunWhileParked` and `RunAtCoherentMoment`
  become queries on the arbiter or commands with a zero deadline.
- `RunAtFrameBoundary` stays, and becomes the "on the driver's thread" form of a command.

### 3.9 Inspection

`arbiter.Snapshot()` returns:
- the phase;
- the intent, with origin, reason, seq and age;
- the driver;
- the command being served, with tag, origin, thread and age;
- the queue: each entry with seq, kind, tag, origin and age;
- counters: applied, coalesced, refused, busy, aborted.

It is exposed with the usual parity: WebAPI `GET /state/execution`, MCP `inspect_state` aspect `execution`, the CLI
`execution` command, Lua, Python and OpenAPI.

A command served longer than 1 s logs a warning with its tag. A Busy result names the holder.

## 4. API sketch

```cpp
namespace exec
{
enum class Intent : uint8_t { Run, Pause };
enum class Origin : uint8_t { Gui, WebApi, Cli, Mcp, Lua, Python, Gdb, DeZog, Debugger, Ttd, Rzx, Lifecycle, Internal };
enum class Outcome : uint8_t { Applied, Coalesced, Refused, Busy, Aborted };

class ExecutionArbiter
{
public:
    Ticket SubmitIntent(Intent intent, Origin origin, std::string tag, Deadline deadline);
    // body runs with the CPU held (CommandContext: Abort(), IsAborted(), the machine)
    Ticket SubmitCommand(Origin origin, std::string tag, CommandBody body, CommandOptions options);
    void PauseFromDriver(Origin origin, PauseReason reason);   // priority lane, emulation thread only
    void AbortCurrent(Origin origin, std::string tag);         // priority lane
    Snapshot GetSnapshot() const;
    Intent CurrentIntent() const;                                // IsPaused() == (CurrentIntent() == Pause)
    bool IsParked() const;                                       // nothing executes, confirmed
};
}
```

`CommandOptions` holds three things:
- `drivesZ80`: the body steps instructions;
- `onDriverThread`: run at a frame boundary on the driver's thread;
- `pairIntent`: an intent applied right after the body, with nothing in between (autostart).

## 5. Thread rules

| Caller | Intent | Command | Priority lane |
|:--|:--|:--|:--|
| control thread (GUI, API, CLI, scripts, GDB, DeZog) | queued | queued, waits for its ticket | abort, stop |
| emulation thread in a hook, frame task or breakpoint | `PauseFromDriver`; other intents are posted, not waited for | inline (it is the driver) | yes |
| a command body (any thread) | posted after the command | inline (nested) | its own abort flag |
| ZX-Poly worker | via the master | via the master | no |

## 6. Migration

The ~150 sites mostly go through about ten helpers. Converting those helpers covers most of them:
- `SessionOperation`, `ParkedEmulator`, `Parked()` and `ScopedPause`;
- the Python, Lua, CLI and Qt brackets;
- the tape / disk API brackets.

| Phase | Work | Check |
|:--|:--|:--|
| X0 | `ExecutionArbiter` alone: queue, tickets, coalescing, throttle, barriers, priority lane, Snapshot; a fake driver | unit tests + model-based fuzz (§8.1), TSan |
| X1 | wired into `Emulator` / `MainLoop`: `yield`, parked generation, the CV wait. `Pause(true)` / `Resume(true)` submit intents and wait. **Compat:** a silent `Pause(false)` … `Resume(false)` pair on one thread becomes one command that spans the pair. This fixes every silent pair, the bug included, before the sites are converted | the bug's repro (§8.3), full suite, hot-path A/B benchmark |
| X2 | stepping (`Run*`, `DirectStepScope`, TTD replay, RZX seek) becomes driving commands; two-driver assertion on | machine fuzz (§8.2) |
| X3 | class b sites and helpers become commands. The broadcast brackets stop broadcasting; the sleeps and silent timeouts go | suite; GDB `continue` sees no false stops |
| X4 | origins from every front end; the claim in the arbiter; DeZog close and the TTD `resume` verb per the owner's answer (§9) | GDB / DeZog / TTD tests |
| X5 | Snapshot on all surfaces; `IntentThrottleMs` in config and docs | parity tests |
| X6 | `Pause(bool)` / `Resume(bool)` removed | build zero warnings, gcc 16 -O3 |

The X1 compat changes behaviour only for unpaired silent calls:
- **`basicencoder.cpp:1192, 1313`** resume silently after the broadcast pause of `RunNFrames`. The resume stops
  working under the compat, so these sites are converted in X1.
- **`mainwindow.cpp:2249`** does `Pause(false)` and then `Stop`. Stop aborts the open command, so the behaviour stays
  the same.

## 6a. Automation sessions: visible remote control (X7)

Owner decisions, 2026-10-09:
- **No pause aborts a long direct run.** A human does not interfere with the operations of an automation session;
  the human can **take back control**, and that ends the whole session.
- **Explicit claim.** A client opens a session itself (take control), as GDB does today. One-off requests (read
  state, a single pause) open none.
- **The automation gate (owner, 2026-10-11; supersedes "explicit only").** One gate on every automation surface:
  a client that changes the machine without having taken control gets a session implicitly, and the GUI shows it
  and locks as for an explicit one. Reads, a pause, profilers, captures, labels / symbols, audio mixing and display
  settings open none.
  - **Who.** One client is identified by its surface, its client name (the User-Agent's product, or "MCP agent") and
    its peer host. The same client goes on in its session. Another is refused (`held_by_other`).
  - **Lease.** 15 s idle (an explicit HTTP session keeps 60 s): a client that never releases it holds the window
    only briefly. CLI: the connection. Scripts: 15 s, renewed by each
    control call.
  - **The client is told.** The WebAPI reply gets the headers `X-Unreal-Session` (the id), `X-Unreal-Session-Implicit`
    and `X-Unreal-Session-Advice`, and a JSON object reply gets an `automation_session` field. MCP appends a note to
    the tool result, once per session. The CLI prints a note. Python raises a `UserWarning`, Lua logs.
  - **The advice.** Take control explicitly before a job and release it after. An explicit take by the same client
    replaces its implicit session.
  - **After a take-back** the client is refused `revoked_by_user` until its cooldown ends.
  - **An explicit take takes over another client's implicit session** (2026-10-24, after an agent drove one machine
    from a host script over the WebAPI and from MCP: each plane's implicit session locked the other out for 15 s). An
    implicit session makes an un-sessioned client visible and revocable; it reserves nothing. So `POST
    /control/session`, MCP `take_control`, CLI `session take`, `take_control()` and a GDB attach succeed over it: it
    gives way (`NC_RUN_CONTROL_CHANGED` "superseded", `by` = who took over; the take's reply names it in
    `took_over`), a command of it under way finishes, and its next change meets the explicit holder. An explicit
    session is never taken over; only the user takes control back. A client in its re-take cooldown takes nothing
    over. Two implicit sessions still exclude each other (one holder at a time).
  - **A refusal resolves itself.** `held_by_other` carries the holder (surface, client, peer, `implicit`,
    `lease_seconds_left`) and says what to do: for an implicit holder, take control explicitly (which takes it over)
    or wait for its lease; for an explicit one, wait or ask the user.
  - **Ending one's own implicit session without its id.** The same client goes on in its implicit session without
    the id, so the id gives it nothing new: `DELETE /control/session` without the header (or with one of a session
    that is gone) ends the asker's own implicit session, and `GET /control/session` shows `session` (the id) and
    `yours` only to that client. An id is a bearer token: another client never sees it.
  - **A session header is no pass by itself.** A request whose `X-Unreal-Session` does not hold the machine it
    changes (expired, taken over, another machine's) meets the gate like an un-sessioned one.
  - **Code.** `EmulatorContext::EnterImplicitRunControl` / `ReleaseImplicitRunControl`; the classification in
    `core/src/emulator/execution/automationgate.h`; the WebAPI sync advice; `CLIProcessor::PassAutomationGate`;
    `execution::script::Check`.
- **The GUI while a session is open.** Every control action is disabled: toolbar, menus, shortcuts, debugger
  buttons, speed control, TTD widget, media panel, file drops. Viewing works. The one active action is
  **Take back control**.
- **Visible.** An indicator on the unreal-qt toolbar ("under remote control: MCP, client …, since 14:02"). The HUD
  shows a status / nudge while the session is open and removes it when the session ends. A message tells the user
  when control was taken back.
- **After a take-back.** The interrupted call and every later call of that session get an explicit error,
  `revoked_by_user` (HTTP 409 on the WebAPI), never a timeout. The client may open a new session; the indicator
  lights again.

Mechanics:
- **The session is the run-control claim, generalized.** `EmulatorContext::TakeRunControl` / `ReleaseRunControl`
  with an owner UUID and a surface label, mirrored into the arbiter (X4). New surfaces take it explicitly:
  - WebAPI `POST /control/session` returns a session id;
  - MCP `control_execution {action: take_control | release_control}`;
  - CLI `session take | release`;
  - Lua / Python `take_control()` / `release_control()`.

  A request of the session carries the id: the `X-Unreal-Session` header on HTTP, the connection on CLI, GDB and
  DeZog.
- **Take-back** (`EmulatorContext::RevokeRunControl(by GUI)`):
  - the claim is released and its owner is marked *revoked*;
  - the command it is serving, if it drives the Z80, is aborted at the next instruction (`HoldToken::Aborted()`,
    read by the `Run*` loops): the run returns, the machine stays paused;
  - every later request carrying a revoked owner is refused with `revoked_by_user` until that client takes control
    again.
- **An abandoned session** ends on its own:
  - GDB and DeZog sessions end when the socket closes;
  - HTTP sessions are leases renewed by every request of the session and released after
    `[AUTOMATION] SessionIdleTimeoutSec` (default 60) without one;
  - the CLI session ends with the connection.
- **Clients do not retry and do not rush back** (owner 2026-10-09). A take-back is final for the session:
  - **The refusal says so.** `revoked_by_user` carries `retry: false` and a text for a human or an agent ("the
    user took back control at the GUI; stop, and ask the user before taking control again").
  - **Our clients stop instead of retrying:**
    - MCP: the tool result tells the agent to stop and ask the user; the MCP server's instructions to agents state
      the rule.
    - Python / Lua: control calls raise `ControlRevoked`; no automatic retry, no automatic re-take.
    - CLI: prints the reason and ends the session.
    - `.recipe/` automation recipes state the rule.
  - **A server-side guard against clients that still push.** After a take-back, a new take by the same client
    (surface + client name) is refused with `revoked_cooldown` until the user allows it at the GUI or
    `[AUTOMATION] RetakeCooldownSec` (default 300) passes. Attempts do not extend the cooldown; they show in the
    indicator ("MCP tried to take control back").
- **Notifications.** `NC_RUN_CONTROL_CHANGED` (taken / released / revoked, with the holder) drives the toolbar
  indicator, the lock of the GUI actions and the HUD status. The arbiter's report (X5) shows the session too.
- **Companion clients** (owner, 2026-10-16; [design](../2026-10-16-companion-client/README.md)). A device the user
  paired at the GUI acts as the user's hands: with its token the gate opens no session for its actions (no indicator,
  no lock), any action within its scopes, the debugger included (steps need a pause: `not_now`); it is refused
  `held_by_session` while a session holds the machine, and it may take control back as the GUI does
  (`RevokeRunControl` with `by` = the companion). Origin and operation-log source `companion`.

## 7. Invariants (checked in every test, every mode)

| # | Invariant |
|:--|:--|
| I1 | At most one thread executes Z80 instructions of a machine (a ZX-Poly group) at any moment. A debug-and-test counter is entered and left in the step path, and it asserts. |
| I2 | The applied intents are a subsequence of the submitted intents, in seq order. At quiescence, the intent equals the last intent submitted and not Refused. |
| I3 | Commands complete in seq order of submission (except nested and priority-lane work), each exactly once. |
| I4 | No intent applies between a command and the intent it is paired with. No intent coalesces across a command. |
| I5 | Every ticket completes, and none is left waiting after Stop. |
| I6 | Exactly one broadcast per applied intent edge; none for commands, parks or coalesced requests. |
| I7 | `mayRun` is false whenever a command is served, the intent is Pause, or the phase is not running. |
| I8 | A Busy outcome names a holder that the snapshot showed at that moment. |
| I9 | No deadlock: under the fuzz load, every thread finishes within its budget. A watchdog dumps the snapshot on failure. |

## 8. Testing in every mode, with fuzzing

### 8.1 Model-based fuzz of the arbiter (X0, no machine)

The arbiter runs against a **sequential reference model**: a plain queue that implements §3.2 in about 100 lines.
A seeded generator drives it:
- N control threads (1-8), each submitting a random mix of intents, commands (short, long, nested, driving,
  paired), aborts and waits;
- one fake driver thread that executes "instructions" and honours `yield`;
- emulation-thread requests from inside the fake driver: `PauseFromDriver` and inline commands;
- random throttle windows (0, 1, 30 ms) and deadlines, including expired ones;
- Stop at a random point.

The arbiter logs every decision (seq, kind, outcome, the intent before and after). After each run the log is
replayed through the reference model, and I1-I9 are checked.

**Reproducibility.** A failure prints its seed. `UNREAL_FUZZ_SEED` and `UNREAL_FUZZ_ITERATIONS` replay it or run it
long. Their rows go in `docs/emulator/environment-variables.md`.

**Run sizes**
- The default suite runs a fixed set of seeds within the 50 ms budget per test.
- The long run (`UNREAL_FUZZ_ITERATIONS=100000`) is for local and nightly use.

**Interleavings.** The fake driver and the arbiter have test hooks at every lock release. A seeded scheduler can
force an interleaving, so the generator explores orders that the OS scheduler rarely produces.

### 8.2 Machine fuzz (X2 on, real emulator)

The same generator drives real machines.

**Requests** are the actual operations:
- pause, resume;
- `RunNFrames`, `RunTStates`, step, step-over;
- reset;
- media insert and eject;
- snapshot load and save;
- poke;
- TTD start, stop, seek, journal switch;
- RZX play and seek;
- NMI;
- model switch;
- shared-memory toggle;
- debug mode on and off.

**Checks**
- I1-I9 on every run.
- **Determinism.** Some sequences contain only commands and fuzz noise: noise threads that submit only commands which
  change nothing, so no Run intents. Such a sequence must leave the same machine state as the same commands run on a
  single thread: memory hash, registers, ports. This is exactly what the bug broke: a 48K boot reaches its known PC,
  and a SPRINTER boot from the test HDD image reaches `#A441` (env-gated, image path from testdata).
- **Mixed sequences** with Run intents are checked by the invariants only, because timing then matters.

### 8.3 The mode matrix

Every fuzz scenario runs across this matrix. The default suite takes a pairwise cover. The long run takes the full
product.

| Axis | Values |
|:--|:--|
| host | headless core (`core-tests`); unreal-qt adopted (Qt test target, `QT_QPA_PLATFORM=offscreen`, the real `TtdWidget` / `EmulatorBinding` adoption path: the bug's path) |
| front end | direct C++; WebAPI; CLI; MCP (via WebAPI); Lua; Python; GDB (claim held); DeZog |
| machine | 48K; Pentagon 128; SPRINTER; TS-Conf; ZX-Poly group (master + slaves, parallel workers) |
| execution | debug off / on; turbo; contention on / off; shared memory on / off |
| TTD | off; recording (both write-journal settings); replaying / seeking; V1 backend (env) |
| RZX | off; playing; seeking |
| caller thread | control; emulation thread (hooks, frame tasks, breakpoints); a command body; ZX-Poly worker |
| throttle | `IntentThrottleMs` 0; 1; 30 |
| lifecycle | before start; running; during a model switch; during Stop / destroy |

**Front-end fuzz from outside the process**
- `tools/exec-fuzz/` holds a Python driver. It talks WebAPI, CLI and MCP concurrently to an unreal-qt (offscreen) and
  to the headless test client.
- It checks the invariants exposed through `/state/execution` (I2, I5, I6 via a notification counter) and the
  determinism boot check.
- Its own tests run on a dry-run fake.

### 8.4 Sanitizers and platforms

- **TSan.** A `-DSANITIZE=thread` configure option (new). The arbiter fuzz and the machine fuzz run under it in the
  Linux docker build (`docker/linux/build.sh --tsan`, new). Data races in the touched paths are findings, not noise.
- **ASan / UBSan.** They run on the same fuzz sets.
- **Platforms.** macOS clang, Linux gcc 16 -O3 (CI flags), MinGW, MSVC: zero warnings. The fuzz smoke set runs on
  every platform in CI.

### 8.5 Regression tests named after the races

Each race in inventory §3 gets a deterministic test, forced with the scheduler hooks:

| Test | Race |
|:--|:--|
| `ForeignPauseSurvivesCommandEnd` | §3.1, the bug |
| `PauseWaitsForUnconfirmedPark` | §3.2 |
| `CommandFromEmulationThreadIsInline` | §3.3 |
| `CommandIsInvisibleToGdbContinue` | §3.4 |
| `BroadcastsMatchAppliedEdges` | §3.5 |
| `DezogCloseLiftsOnlyItsOwnPause` | §3.6 (per the owner's answer) |
| `SprinterBootUnderAdoptionJournalSwitch` | the original repro in the Qt test target |

## 9. Open questions for the owner (asked one at a time)

1. ~~A run during a direct step~~: answered 2026-10-09. Requests are accepted, coalesced within a throttle window and
   the last one applied. Commands are barriers, and the priority lane is as in §3.3.
2. ~~DeZog session close and the TTD `resume` verb~~: answered 2026-10-09. They lift only a pause that they set
   themselves: the intent is applied as Run only if the current intent's origin is still DeZog or TTD. Otherwise the
   ticket completes as Refused(not owner).
3. ~~The X1 compat~~: answered 2026-10-09, yes. X0 (arbiter + model fuzz), then X1 with the compat mapping and the
   bug's repro, then the gradual conversion.
4. ~~The default `IntentThrottleMs`~~: answered 2026-10-09, 0 ms. Intents coalesce at decision points, and no latency
   is added. A non-zero window stays configurable, and the fuzz matrix covers 0, 1 and 30 ms.
