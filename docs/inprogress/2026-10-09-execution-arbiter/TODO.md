# Execution arbiter: TODO

Part of [README.md](README.md). Phases from [design.md](design.md) §6.

## X0: the arbiter alone

- [x] `execution::ExecutionArbiter` (`core/src/emulator/execution/executionarbiter.{h,cpp}`):
  - the queue, tickets and outcomes;
  - intent coalescing with the throttle window (default 0);
  - commands as barriers: synchronous `Execute`, posted `Post`, `drivesZ80`, `pauseFirst`, `thenIntent`;
  - inline commands for the thread that holds the CPU;
  - the priority lane: `PauseFromDriver`, `AbortCurrent`, `BeginStop`;
  - the run-control claim; owner-only Run;
  - the main-loop interface (`AttachMainLoop`, `ShouldYield`, `LoopWait`, `DetachMainLoop`);
  - Snapshot, the decision log and the interleave hook.
- [x] `ExecutionArbiter_Test`: 18 tests, one rule each, plus the bug as `ForeignPauseSurvivesCommandEnd`.
- [x] `ExecutionArbiterFuzz_Functional`: model-based fuzz.
  - **The load.** Seeded control threads (1-6) issue intents, synchronous, nested, driving, paired and posted
    commands, deadlines, aborts, claims, and bursts that build deep mixed queues. A fake loop runs instructions of
    varying length and fires `PauseFromDriver` and inline commands from its hooks. Throttle 0 / 1 / 3 ms, runs with
    and without a loop, stop midway.
  - **The checks.** After each run, I1-I9 and the decision log are replayed through the sequential model: intent
    replay, edges, the owner model, the priority lane, command order and completeness, barriers. A watchdog reports
    hangs.
  - **The functional suite** (`UNREAL_FUNCTIONAL_TESTS=1`, no longer the default run: the fuzz uses real deadlines and noise threads) runs 12 seeds (about 50 ms). `UNREAL_EXEC_FUZZ_SEED` replays one seed;
    `UNREAL_EXEC_FUZZ_ITERATIONS` runs more seeds.
- [x] Found by the fuzz and fixed: `NextWakeLocked` returned a past throttle time while a command was served. The
  waiters spun on the mutex and starved the thread that runs the command, a livelock (seed 14, about 1 run in 8).
- [x] Mutation check: six injected faults, each caught by the fuzz alone, at 300 seeds:

  | Mutant | What it breaks |
  |:--|:--|
  | M1 | the loop runs while a command is served |
  | M2 | a command is served before the loop parked |
  | M3 | intents coalesce across a command |
  | M4 | the old restore: a command's end lifts the pause |
  | M5 | an equal intent takes over the owner |
  | M6 | a priority pause leaves queued intents |

  Before the fuzz was strengthened it missed M2, M3, M5 and M6. The additions were: varying instruction length,
  bursts, an owner model, a priority check and a completeness check.
- [x] TSan, ASan + UBSan: the arbiter and both test files built standalone with clang `-fsanitize`, 300 fuzz seeds.
  No reports.
- [x] `-DSANITIZE=<list>` (`cmake/Sanitize.cmake`, the tree and a standalone `core/tests`): `tools/sanitize/
  configure.sh asan|tsan` and `docker/linux/build.sh --sanitize LIST` / `--asan` / `--tsan` configure with it.
- [x] gcc 16 -O3 with the CI flags: the arbiter and both test files compile with zero warnings.

## X1: wiring and the compat

- [x] `Emulator` owns the arbiter (`GetArbiter()`); `MainLoop` runs on it:
  - **Startup.** `StartAsync` announces the loop (`ExpectMainLoop`) and submits the start's Run intent before the
    thread exists. A Pause issued after `StartAsync` returns is newer, so it wins. `Run()` attaches and parks once
    before its first frame.
  - **Parking.** The frame-end check reads `ShouldPark()`: one relaxed atomic load, inline. Both checks park in
    `LoopWait` on the arbiter's condition variable, which replaces the 20 ms poll.
  - **Correction, found in X3.** The per-instruction check in `Z80FrameCycle` still read `IsPaused()`, the intent,
    until X3 moved it to `ShouldPark()`. Until then a command was served at the frame end rather than mid-frame:
    correct, only up to a frame later. The X1 A/B measured that state. The pause confirmations (`_isPausedConfirmed`, `_pauseCV`,
    `ConfirmPauseFromCpu`, `InvalidatePauseConfirmation`) are gone. `WaitForPauseConfirmation` is now
    `WaitParked`.
  - **Stop.** `Stop` sets the stop request, then `BeginStop`. After the join it calls `DetachMainLoop` and ends any
    silent pair still open.
- [x] **Pause and Resume.**
  - `Pause(true)` / `Resume(true)` submit intents. From the emulation thread, `Pause` is `PauseFromDriver`.
  - `Pause(false)` … `Resume(false)` on one thread is one hold of the CPU (`BeginHold` / `EndHold`, a per-thread
    stack, nested holds inline). `Resume(false)` never lifts a pause.
  - The edge's synchronous part runs in the gate, under the arbiter's lock and before the loop can run: the
    registers at the stop, the reconciliation of host output holds, the stop cause. The broadcast part runs in the
    listener: `SetState` and `NC_EMULATOR_STATE_CHANGE`.
- [x] Semantics that changed, and why:
  - **`IsPaused()` is the intent.** A silent pause is invisible: GDB's `continue` no longer reads one as a stop.
    `IsEmulationParked()` answers "nothing executes".
  - **`RunWhileParked` needs a paused, parked machine.** A running machine is read between two frames, even while
    its loop has not started yet or another thread's command holds it.
  - **Stepping inside a silent pair leaves the intent alone.** The `Run*` methods' "pause first" is skipped when the
    calling thread holds the CPU.
  - **A wait on an intent that times out keeps it queued.** It is never lost.
  - **`ShouldYield` is set only while a loop is attached.** ZX-Poly slaves and machines that were never started
    read false.
  - **The TTD `SessionOperation` publishes when the machine is parked**, not when it is paused.
- [x] `basicencoder.cpp` resumes with a broadcast after `RunNFrames` (the old silent resume ended a broadcast pause).
- [x] `ExecutionArbiterMachine_Test`: four tests on a running Pentagon.
  - `SilentPairsOnAnotherThreadNeverResumeAStep` is the bug without its GUI: silent pairs on one thread, Pause +
    `RunNFrames` on another. On master `2bc17c86c` it fails 10 runs out of 10; on the branch, 0 out of 10
    (about 50 ms).
- [x] The original GUI flow, unreal-qt offscreen from the branch:
  - create SPRINTER, CHD insert, reset, pause, 1500 frames, six times in one process: `#A441` six times. Before,
    the second instance onwards ended anywhere.
  - the IMG disk: `#9549` / `#954C`, the same as a first instance in a fresh process.
- [x] gcc 16 -O3 with the CI flags: every changed C++ file, zero warnings.
- [x] Hot-path A/B benchmark (performance-guidelines §4). A = master `2bc17c86c`, B = the branch; Release builds;
  rounds A B A B A B B A B A. Load was 10.5 (1 min) and 19 (5 min) at the start, 7.3 / 15.4 at the end. CPU time
  per host frame:

  | Benchmark | min A → min B | mean of the paired differences | sign per pair |
  |:--|--:|--:|:--|
  | `BM_HostFrame_48K_Fast` | +1.2 % | +0.6 % | ++-++ |
  | `BM_HostFrame_48K_Debug` | +0.8 % | +0.6 % | ++-++ |
  | `BM_HostFrame_Pentagon_Fast` | +1.2 % | +0.2 % | --+++ |
  | `BM_HostFrame_Pentagon_Debug` | +0.5 % | +0.2 % | -++++ |
  | `BM_HostFrame_TSConf_Fast` | -0.1 % | -0.3 % | -+--- |
  | `BM_HostFrame_TSConf_Debug` | +0.4 % | +0.4 % | -+++- |

  Every paired mean is under the 1 % that the guidelines take as a real shift, and no sign holds across all pairs:
  no measurable change. The per-instruction check went from an out-of-line `IsPaused()` call to an inline relaxed
  load. Its gain is below what a not-quite-quiet machine resolves.
- [x] Not covered in X1 (both closed in X2): a direct step was not a driving command, so a hold from another thread
  could be granted while `RunNFrames` stepped, and a broadcast `Resume` could unpark the loop during a direct step.

## X2: direct runs are driving commands

- [x] **`DirectStepScope` holds the CPU.** Every `Run*` method takes it: `RunSingleCPUCycle`, `RunNCPUCycles`,
  `RunFrame`, `RunNFrames`, `RunTStates`, `RunUntilScanline`, `RunNScanlines`, `RunUntilNextScreenPixel`,
  `RunUntilInterrupt`, `RunUntilCondition`. TTD replay and RZX seek go through `RunTStates` / `RunNFrames`.
  - On a running machine the scope takes a hold with `drivesZ80` and `pauseFirst`. The outermost run applies the
    pause ("stepping leaves the machine paused"); the ten "pause if running" blocks are gone.
  - A hold nested in the caller's own hold, or on the loop's thread, is inline: no pause, no new driver.
  - A machine that is not running takes no hold (fixtures, ZX-Poly slaves stepped by their master), so the slaves'
    hot path stays the same.
- [x] **Consequences.**
  - A command or a silent pause from another thread waits for the run.
  - A `Resume` queues behind the run and applies after it.
  - `WaitForPauseConfirmation` and `IsEmulationParked` are true for the thread that holds the CPU.
- [x] **Driver count (I1).** `Emulator::DriverEnter` / `DriverLeave` are called around each main-loop frame (left
  while parked mid-frame) and around each outermost direct run. A second driver counts in `DriverViolations()` and
  is logged once. Every machine test's `TearDown` asserts 0.
- [x] **`ExecutionArbiterMachine_Test`**, two more tests:
  - `ADirectRunKeepsEveryOtherThreadOut`: another thread's command is served only after `RunNFrames(30)`, which
    steps exactly 30 frames.
  - `AResumeDuringADirectRunAppliesAfterIt`.
- [x] **`ExecutionArbiterMachineFuzz_Functional`** (design §8.2): 2-5 threads drive one running Pentagon in a seeded mix.
  - **Operations:** pause, resume, silent pairs, `RunNFrames` (some counted inside a driving hold),
    `RunTStates`, `RunSingleCPUCycle`, arbiter commands that poke memory, status reads.
  - **Checks:** no second driver; a counted `RunNFrames(k)` advances exactly k frames; no hang (a watchdog prints
    the state); the machine parks after a pause and runs after a resume.
  - **Size:** 3 seeds in the suite, about 80 ms.
  - **Mutation check:** with the scope's hold switched off it fails at once, with up to 11958 moments of two
    drivers and inexact steps.
- [x] **Arbiter fuzz:** holds also with `drivesZ80` / `pauseFirst`.
- [x] **Closed later:**
  - abort of a long direct run: by taking control back (X7: `AbortDriving`, `RunHalted` reads it; a pause never
    aborts);
  - the `Reset`, media and snapshot brackets became CPU holds (X3);
  - the determinism check (`CommandsFromOtherThreadsNeverReachIntoAStep`) and the mode matrix (X6: TTD; ZX-Poly
    and RZX below).

## X3: the Pause / Resume brackets become CPU holds

- [x] **Fresh master merged** (`328f867a3`). One conflict: master's screen catch-up in the mid-frame park, kept.
- [x] **`Emulator::CpuHold`** (RAII): holds the CPU for a piece of work. It never changes the pause, never
  broadcasts, is free when nested, and logs when the CPU is not handed over in time. `CanMutateNow()` answers "may
  this thread change the machine now": not running, paused, or holding the CPU. `MediaManager::CanApplyNow` uses it.
- [x] **Converted in `emulator.cpp`.** Each lost its broadcast Pause / Resume (or silent pair), its sleeps and its
  silent timeouts:
  - `Reset`, `AutostartDisk`;
  - `LoadSnapshotStaged`, `SaveSnapshot`;
  - `LoadTape`, `EjectTape`, `CreateBlankDisk`, `LoadDisk`, `EjectDisk`, `SaveDisk`;
  - `SetOverscanMode`, `RequestNMI`, `RequestMNI`, `EditMemoryFromTool`, `LoadROM` (it still leaves the machine
    paused, as before).
- [x] **Converted outside it:**
  - the WebAPI tape / disk handlers (7) and recording start;
  - the Python, Lua, CLI and Qt `EmulatorPauseBracket`s (7 + 7 + 8 + 5 users);
  - Python and Lua recording start;
  - `ParkedEmulator` (media control), `Parked()` (CD audio), `ScopedPause` (analyzers);
  - `basicencoder`'s injection.
- [x] **Per-instruction check:** `ShouldPark()` (see the X1 correction).
- [x] **`ExecutionArbiterMachine_Test.ResetAndMachineOperationsNeitherPauseNorBroadcast`:** `Reset`, overscan and
  NMI on a running machine: it runs on, with no Paused / Resumed broadcast. A paused machine stays paused through
  a reset.
- [x] **The machine fuzz** also does `Reset`, overscan, NMI and snapshot save + load from other threads. A counted
  `RunNFrames(k)` stays exact.
- [x] **`ExecutionArbiterMachineFuzz_Functional.CommandsFromOtherThreadsNeverReachIntoAStep`** (design §8.2,
  determinism):
  - the same 40 frames from one snapshot, quiet and with 3 threads submitting commands that flip FRAMES and restore
    it;
  - the end states (RAM + registers) must be equal;
  - with the direct run's hold switched off it fails 5 runs out of 5.
- [x] **Found by the machine fuzz under parallel load** (6 processes, 17 runs out of 18 aborted on heap corruption)
  and fixed:
  - **Cause.** `SaveSnapshot` wrote `_lastSaveResult` (strings) before it held the CPU, so two threads saving at once
    tore it. The race exists on master too; no lock ever ordered the saves.
  - **Fix.** The hold is taken first, so saves take turns: 18 runs out of 18 clean afterwards.
  - **How it was narrowed down.** A temporary op mask; snapshot save + load was the only op that failed. The race was
    not the mid-frame park: with frame-end parking for commands it still failed.
- [x] **The last silent pairs are explicit holds now.** No `Pause(false)` / `Resume(false)` is left outside
  `Emulator`; the compat stays until X6 for any caller that is still missed.
  - TTD, both backends: `StartRecording`, `StopRecording`, `SwitchWriteJournal`, `ResumeRecordingLive` use
    `std::optional<CpuHold>`; `SessionOperation` keeps a `HoldToken`. Each is taken under the same condition as the
    compat (a running, unpaused machine), so a TTD operation never waits for the CPU that a direct run on another
    thread holds while that run waits for the TTD control mutex.
  - `RzxSession` `Play` / `Stop` / `Seek`, the shared-memory migration, `ModelSwitch`.
  - `MachineStateTransfer` holds both machines, in the order of their ids.
  - MainWindow's `Pause(false)` before `Stop()` is gone (Stop parks and joins by itself).
- [x] **Found by the GUI repro after the change: an old race in the Qt debugger, fixed.** Six runs of six SPRINTERs
  each: one unreal-qt crash in three to four runs.
  - **Symptom.** A SIGSEGV in `Z80Disassembler` with a null `Memory`. `DebuggerWindow::updateState`, queued by a
    step's end (`cpuStepComplete`), ran while an automation `destroy` released the machine.
  - **Fix.** The refresh holds `Emulator::LeaseContext()`, so the removal frees nothing until the refresh ends and the
    lease is refused once the removal has begun. Six runs of six afterwards: 36 out of 36 booted, no crash.
  - **For comparison**, the pre-arbiter build in the same flow: no crash in 6 runs, but about half of the second and
    later SPRINTERs ended anywhere in the BIOS (the original bug).
- [x] **A/B of the hot path again** (the per-instruction check changed in X3).
  - **Setup.** A = master `328f867a3`, B = the branch; Release builds; 20 rounds (ABAB... then BABA...).
  - **No wait for a quiet machine** (owner 2026-10-09: the load never drops on this host, so more rounds instead).
    Load 14.9 / 21.1 / 24.9 at the start, 24.0 / 21.7 / 24.1 at the end.
  - **Result:** no measurable change. Every paired mean is within ±0.6 %, and the signs are mixed.

  | Benchmark | min A → min B | median | mean of the paired differences (+/−) |
  |:--|--:|--:|--:|
  | `BM_HostFrame_48K_Fast` | +0.3 % | +0.3 % | +0.1 % (6/4) |
  | `BM_HostFrame_48K_Debug` | -0.3 % | +0.7 % | +0.3 % (6/4) |
  | `BM_HostFrame_Pentagon_Fast` | +0.9 % | -0.0 % | -0.0 % (4/6) |
  | `BM_HostFrame_Pentagon_Debug` | +0.4 % | -0.3 % | +0.0 % (6/4) |
  | `BM_HostFrame_TSConf_Fast` | -0.5 % | -0.1 % | +0.3 % (6/4) |
  | `BM_HostFrame_TSConf_Debug` | -0.2 % | +0.1 % | +0.6 % (7/3) |
- [x] **Verification at the end of X3:**
  - macOS: full build with zero warnings; `test.sh`, 20 shards green;
  - the machine fuzz under parallel load: 2 rounds × 6 processes × 100 seeds, all clean;
  - the arbiter fuzz: 1000 seeds;
  - TSan and ASan + UBSan: no reports;
  - gcc 16 -O3 with the CI flags: the 18 changed C++ files, clean;
  - Linux docker: 8712 tests, 8615 passed, the rest skipped by their conditions, 0 failed;
  - the GUI repro (unreal-qt offscreen): 6 SPRINTERs in one process all reach `#A441`.

## X4: origins, the claim, owner-only resumes

- [x] **`Emulator::Pause(Requester, reason)` / `Resume(Requester, onlyIfOwner)`** return the arbiter's
  `execution::Result`. `Pause(bool)` / `Resume(bool)` stay as the Internal-origin (and silent-hold) compat.
- [x] **Every front end passes its origin:**

  | Front end | Origin |
  |:--|:--|
  | Qt toolbar and menus (`EmulatorManager::PauseEmulator` / `ResumeEmulator`), debugger window, speed control, TTD widget, video wall | Gui |
  | WebAPI lifecycle (also MCP, which goes through it) | WebApi |
  | CLI | Cli |
  | Python, Lua | Python, Lua |
  | GDB | Gdb, with its session UUID |
  | DeZog | DeZog |
  | TTD control (`PauseAndConfirm`, the resume verb) and its session-end auto-pause | Ttd |

  The origin of the pause in force shows in the arbiter's snapshot.
- [x] **`EmulatorManager::PauseEmulator` no longer holds `_emulatorsMutex`** across the 500 ms park wait
  (inventory §1): it takes the instance under the lock and pauses outside it.
- [x] **The run-control claim is enforced by the arbiter.** `EmulatorContext::TakeRunControl` /
  `ReleaseRunControl` mirror it there (`OriginFromName(label)` + owner UUID). Another surface's run (intent or
  direct-run command) is refused while it is held; a pause never is; Internal requests are not affected.
- [x] **Owner-only resumes** (owner decision 2026-10-09): DeZog's session close and the TTD resume verb lift only a
  pause they set themselves. A pause the user asked for stays, and a second pause on top does not take the ownership
  over.
- [x] **A breakpoint's pause belongs to the breakpoint's owner.** A DeZog breakpoint's stop is DeZog's, so its
  session close lifts it; a GDB breakpoint's stop is GDB's; an interactive one is the debugger's. Found by the DZRP
  tests: a client dropping while stopped at its own breakpoint left the machine paused.
- [x] **`ExecutionArbiterMachine_Test`, two more tests:** `OwnerOnlyResumesLiftOnlyTheirOwnPause` (DeZog and TTD)
  and `TheRunControlClaimRefusesOtherSurfacesRuns`.
- [x] **DeZog tests follow the decision.**
  - The shared fixture pauses as an attached DeZog session does (origin DeZog), so the five session-close tests
    (adapter, history, DZRP server, ZEsarUX quit) keep their meaning.
  - New `DezogDebugAdapter_test.SessionCloseKeepsAPauseTheUserAskedFor`.
- [x] **Found by the QA runs: a test of mine aborted a full shard under load.**
  - **Cause.** `AResumeDuringADirectRunAppliesAfterIt` and `ADirectRunKeepsEveryOtherThreadOut` waited to see a
    `RunNFrames(30)` in progress. In turbo it can end before the first poll; the `ASSERT` then returned with a live
    `std::thread`, which is `std::terminate`.
  - **Fix.** Both now use `GatedDirectRun`: a `RunUntilCondition` whose predicate blocks until the test releases it,
    so the run is in progress by construction, and RAII joins the thread on every path. The other tests with live
    threads use `EXPECT`, and the observer test removes its observer with RAII.
  - **Result:** the machine tests ×30 in 6 parallel processes, all clean; `test.sh` twice, green.

## X5: the arbiter's state on every surface

- [x] **`DeviceState::Execution(context)`** builds one `StateNode` report: phase; intent with `intent_set_by`
  (origin, client), `intent_reason`, `intent_seq` and `intent_age_ms`; `paused`; `main_loop` (attached, parked);
  `may_run`; `parked`; `serving` (the command holding the CPU, with `drives_z80`); `queue`; `claim`; `counters`;
  `driver_violations`.
- [x] **The surfaces render it with their existing converters:**

  | Surface | Entry |
  |:--|:--|
  | WebAPI | `GET /api/v1/emulator/{id}/state/execution` and the active-emulator form |
  | OpenAPI | `openapi_execution.inc` |
  | MCP | `inspect_state` aspect `execution`, with a one-line text summary |
  | CLI | `state execution` / `state exec`, plus a help line |
  | Lua, Python | `execution_state()` |
  | Docs | the WebAPI, command, Lua and Python interface docs |
- [x] **Checked live** in unreal-qt (offscreen). After a WebAPI pause the report shows `intent pause`,
  `intent_set_by webapi`, parked. The CLI showed the adoption's TTD journal hold queued while the loop parked.
- [x] **`ExecutionArbiterMachine_Test.TheExecutionReportShowsTheIntentItsOwnerAndTheHolder`.**

## Open: one GUI boot miss

During the X5 QA, one SPRINTER of six in the GUI repro ended at `#91CD` after 1500 frames instead of `#A441`.
`#91CD` is the BIOS's text drawing loop, which a good boot passes near frame 375; the pre-arbiter build's failures
showed the same address.

- **Not reproduced since.** 126 boots after X4 missed once. A campaign of 60 boots (half of them under 6 parallel fuzz
  processes) with the arbiter's report read after each: 60 out of 60 reached `#A441` with `driver_violations` 0.
  Another 12 under load and 24 more were clean as well.
- **Ruled out:** an RTC wait (the code at `#91CD` draws), and the media swap delay (it applies only when a disk
  replaces a disk; the slot was empty).
- **Next.** The campaign script (`driver_violations`, the arbiter counters, 1500 more frames on a miss, the IDE state)
  stays the trap for the next occurrence: a nonzero violation count would point at the arbiter, a zero one elsewhere
  (the CHD reader, the BIOS's own timing).

## X6: the compat goes; TTD in the fuzz matrix

- [x] **`Pause(bool)` / `Resume(bool)` are gone**, and with them the X1 compat: the per-thread hold stack,
  `BeginCompatHold` / `EndCompatHold` / `ReleaseCompatHolds`.
  - `Pause()` / `Resume()` are intents with the Internal origin; a surface passes its own (`Pause(Requester)`).
  - A still CPU for a piece of work is `Emulator::CpuHold`; no `Pause(false)` was left outside the arbiter's own
    tests.
  - The tests that modelled the silent pair use `CpuHold` (`CpuHoldsOnAnotherThreadNeverResumeAStep`,
    `APauseDuringACpuHoldSurvivesItsEnd`, `StepsAndNestedHoldsInsideACpuHoldKeepTheMachineRunning`,
    `StopWhileHoldingTheCpu`).
- [x] **The mode matrix, TTD: `ExecutionArbiterMachineFuzz_Functional.TtdRecordingUnderConcurrentControl`.**
  - **Setup:** a running Pentagon with debug mode and TTD on.
  - **Load:** one thread starts and stops recordings and switches the write journal on and off (the bug's path);
    two others pause, resume, hold the CPU, run counted `RunNFrames(k)` and `RunTStates`.
  - **Checks:** no second driver, every counted step exact, no hang.
  - **Result:** all machine fuzz tests, 6 processes × 30 seeds in parallel: all clean.
- [x] **ZX-Poly groups and RZX playback in the fuzz matrix** (`ExecutionArbiterMachineFuzz_Functional.
  ZXPolyGroupUnderConcurrentControl`, `RzxPlaybackUnderConcurrentControl`): a running group / a playing 48K, one
  thread through the group (recording, stop / seek / resume, keys, parallel slaves) or the playback (play, stop,
  seek, status), two through the control. The ZX-Poly one found two drivers on a slave (2 of 216 runs): with
  pipelined slaves (turbo, parallel) a boundary hands the slaves to workers and returns; the group's operations
  waited for the slaves but did not hold the master, so its next boundary posted new slave work while, say, the
  group TTD resume drove a slave too. Now every operation from outside the master's frame takes `GroupHold` (the
  master's CPU, then the slaves' pipelined work finished): loads, replicate, port check / trace, RunFrames, the
  group TTD start / stop / resume, parallel / pipelined switches (the seek already held it). Under ASan both
  scenarios run clean.
- [ ] **Open: one frame too many in the ZX-Poly scenario.** Under heavy stress (6 processes x 30 seeds, host load
  40-70) about 1 run in 250 (1 scenario in ~7500) counts a held RunNFrames(k) on the master as k+1 frames
  ("before 2 after 5 ran 2", "before 14 after 16 ran 1", "before 14 after 17 ran 2" - always early frames). No two
  drivers (the module counters stay 0), ASan clean. Ruled out: a direct run starting without the CPU (it now waits,
  never runs without it - no "still waits" in those runs), the TTD detached replay after a seek, keys, the parallel
  slaves switch: `DISABLED_ZXPolyMasterRunNFramesIsExactSingleThreaded` (3000 runs with all of them, one thread) is
  exact. Only with the group thread and two control threads at once. The check stays strict.
- [x] **Abort of a long direct run:** replaced by the owner's decision on automation sessions (X7). A pause never
  aborts; taking back control does.

## X7: automation sessions - visible remote control, take back control (design §6a)

- [x] **Core (X7a).** `EmulatorContext`:
  - **Take.** `TakeRunControl(owner, label, reason, client, lease)` records the client, the time and the idle lease
    (ms).
  - **Take back.** `RevokeRunControl(by)` releases the claim, marks the owner revoked (`IsRunControlRevoked`: its
    requests answer `revoked_by_user`) and starts the client's re-take cooldown (`revoked_cooldown`, 300 s by default,
    `AllowRunControlRetake` lifts it). It also aborts the session's direct run.
  - **Lease and announcements.** `TouchRunControl` renews a lease; an expired lease ends the session ("expired").
    `NC_RUN_CONTROL_CHANGED` announces taken / released / revoked / expired / retake_refused.
- [x] **The abort path.** The arbiter's `AbortDriving()` aborts only a direct run, and `ServingAborted()` is one
  relaxed load read by `RunHalted()` in every `Run*` loop. `Emulator::LastDirectRunAborted()` tells the surfaces to
  answer that call with `revoked_by_user`. The execution report shows `session` and `retake_cooldowns`.
- [x] **Tests.** `TakingControlBackEndsTheSessionAndItsRun`: a `RunNFrames(1000000)` ends at once and leaves the
  machine paused, the owner is revoked, the same client is in its cooldown, another client takes control, and the
  "allow again" works. `AnIdleSessionExpiresAndEveryChangeIsAnnounced`.
- [x] **Surfaces (X7b):**

  | Surface | What |
  |:--|:--|
  | WebAPI | `POST / GET / DELETE /api/v1/emulator/{id}/control/session` (`session_api.cpp`, OpenAPI `openapi_session.inc`). The `X-Unreal-Session` header: the global advice answers a taken-back session's request `409 revoked_by_user` (`retry: false`) and renews a live one's lease. The claim checks and the lifecycle pause / resume let the holding session through (`webapi_session::RequesterOf`). Every direct-run handler answers `revoked_by_user` when its run was aborted |
  | MCP | `control_execution take_control / release_control`. The WebAPI client keeps the session header. Whatever a tool made of a `revoked_by_user` 409 (resume used to read every 409 as "already running"), the dispatcher turns the result into one stop error. The server instructions state the rule |
  | CLI | `session take [name] / release / (status)`. The session ends with the connection. A taken-back session's command does not run; a command that ran while it was taken back answers `revoked_by_user` too |
  | Python, Lua | `take_control(name) / release_control()` (`execution::script`). The control calls (pause, resume, step*, run_*) raise `ControlRevoked` (Python) / an error (Lua) after a take-back, once |
  | Docs | the interface docs; `.recipe/README.md` conventions: stop, do not retry, ask the user |
- [x] **Checked live** in unreal-qt (offscreen), WebAPI:
  - take → pause and resume as the session: 200;
  - resume without the header: 409 held;
  - another client's take: 409 `held_by_other`, `retry: false`;
  - release: 200.
- [x] **Tests:**
  - `McpDispatcher_Test.TakeControlKeepsTheSessionAndReleaseControlDropsIt` and
    `ARevokedSessionTurnsTheResultIntoAStopError`;
  - `ExecutionArbiterMachine_Test.AScriptSessionStopsWithControlRevoked`.
- [x] **unreal-qt (X7c), `RemoteControlGuard`:**
  - **Lock, cascaded to every widget of every window** (the debugger, its dialogs, the label manager included):
    - controls (actions, shortcuts, buttons, sliders, combo and spin boxes) are disabled;
    - editors (line / text edits, hex views) turn read-only, and item views stop editing; scrolling, selecting
      and copying stay;
    - an application event filter swallows what custom widgets act on: double clicks, keys other than
      navigation / copy / find, drops;
    - a dialog or context menu shown during the session is locked as it shows;
    - the places that change breakpoints, labels, symbol sets, ports and sectors refuse via
      `RemoteControlGuard::Refuse` (a tooltip says why): disassembler toggle, breakpoint dialog / editor / groups,
      label editor add / edit / delete / load / import / scan, symbol sets on / priority / drop, Port OUT, sector
      write.
    Exempt: Take Back Control, Allow Automation Again, full screen, Exit.
  - **Restore.** The guard gives back only what it disabled, and the windows' own enablement rules apply again.
  - **Re-lock.** A 250 ms pass while locked catches re-enabled controls and dialogs opened later; it also notices an
    idle session's lease running out.
  - **Input.** The machine gets no key presses, clicks or mouse capture from the GUI; a captured mouse is
    released. A drop or a file-open is refused with a toast.
  - **The remote-control bar** (owner-reviewed 2026-10-10): a row of its own under the toolbar (never overflows),
    the system accent colour; the antenna icon, "Remote control: DeZog (ZRCP) · 127.0.0.1 · 00:01:23" in the
    system fixed-pitch font at 1.14x (the time ticks once a second; the timer runs only while the bar shows a
    session), Take Back Control in the toolbar's font; one row layout, one vertical centre. After a take-back:
    the window colour, "Control taken back from ...", Allow Automation Again, Dismiss. Both actions are also in
    the Run menu; the full address and time in the tooltip.
  - **HUD:** one of its own indicators (the antenna, the client's name), a toast when a session starts (who, from
    where) and on take-back. Turned off in the settings, the HUD shows only the session's elements
    (`HudModel::setAlwaysShownPrefix("remote")`); turned on, everything as set plus the session.
  - **Who:** every session carries its peer (`TakeRunControl(..., peer)`, the execution report's `session.peer`):
    the WebAPI's HTTP peer, the MCP agent's (with its `initialize` clientInfo as the default name), the CLI / GDB /
    DeZog / ZRCP socket peer; scripts are local. DeZog over DZRP / ZRCP takes run control on connect
    ("DeZog (DZRP)" / "DeZog (ZRCP)"); after a take-back DZRP sends NTF_PAUSE with the reason, ZRCP an error line,
    then both close the connection.
  - **Allow Automation Again** shows while a client is in its re-take cooldown.
  - **Tests.** `RemoteControlGuard_Test` (Qt): the lock, the exempt controls and views, re-lock, restore;
    take-back with the signal, cooldown, allow-again; the cascade (editors, tables, swallowed input, looking keys
    pass, a window shown during the session, Refuse, all given back).
- [x] **GDB.** After a take-back, the packets that run, step or write (`c` `s` `vCont` `b` `G` `P` `M` `X`) get
  the reason as an `O` console packet and `E5E`; reads and detach stay. A waiting continue ends.
  `GdbSessionTakeBack_Test` (socketpair).
- [x] **Checked live** in unreal-qt (offscreen):
  - WebAPI session: pause, `run_frames` and resume pass; `run_frames` without the header gets 409;
  - an idle lease of 5 s ends the session;
  - the CLI `session take` shows as `cli` and ends when the connection closes;
  - no crash with the lock running.
- [x] **GDB / DeZog / ZRCP are remote control too** (owner decision 2026-10-10): the GUI shows the adapter and the
  host it connects from.
- [x] **Checked live** by the owner in unreal-qt, every kind (WebAPI, MCP, CLI, Lua, GDB, DeZog, ZRCP), with and
  without GPU acceleration; the take-back answered the client `revoked_by_user`, its re-take `revoked_cooldown`.
  Python was not in that build (`ENABLE_PYTHON_AUTOMATION=OFF`).
- [x] **The crash in `VideoWriteLog::BeginFrame` (heap corruption) - found and fixed.** An ASan soak (unreal-qt, 5
  machines running free, 4 threads of automation: state reads, pause / run_frames / resume, run_tstates, sessions)
  hit it in a minute: `heap-use-after-free` in `DeviceState::VideoChanges` against `Emulator::RunNFrames`. Paused, the
  readers of the video write log (`DeviceState::VideoChanges`, `VideoMapService::SourcesAtBeam`) read the frame in
  place while a direct run (run_frames on the paused machine) recorded into it, reallocating its vector. The race is
  older than the branch (it is on master). Now, paused, they take a `CpuHold` first (250 ms; not handed the CPU: the
  published copy of the last completed frame, as while running); the recording hot path is untouched.
  - `ExecutionArbiterMachine_Test.ReadingTheVideoLogDuringADirectRunNeverReadsAFrameBeingWritten`: under ASan it
    fails 5 of 5 with the fix taken out, passes 5 of 5 with it.
  - The 20-minute ASan soak again with the fix (~415 000 requests): no report, the process up, 0 connection errors.
- [x] **Checked end to end over HTTP** with the owner clicking (`scratch/reinst/inflight_takeback.py`): the
  run_frames in flight answered 409 `revoked_by_user` (`retry: false`), the session's next request the same, a
  re-take by the client `revoked_cooldown`, after Allow Automation Again the re-take succeeded. It also found the bar
  losing Allow Automation Again (the guard announced the take-back before its own refresh): fixed, the order tested.
- [x] **The #91CD boot miss - closed.** 300 SPRINTER boots in 3 unreal-qt processes (the first half hour under the
  arbiter fuzz in parallel): 300 reached `#A441`, `driver_violations` 0 everywhere.
- [x] **"Paused is idle" - wrong since direct runs: fixed.** The campaign's fuzz load aborted 4 of 750 runs (heap
  corruption: TTD's StopRecording drained the journals while a direct run on another thread recorded into them).
  The X3 conversion had kept "if running: hold" - a paused machine driven by run_frames got no hold. Now off the
  machine's thread the hold is taken always (it waits for a direct run; free when nested): TTD (both engines) start /
  stop / write journal / resume / session operations, the port-journal search (held, not refused only while
  running), the step-over cancel, the ZX-Poly seek. Two tests that counted frames right after a `Pause()` (it returns
  after 500 ms on a starved machine) wait for the pause to apply. Again: 432 runs under the same load, 0 failures.
- [x] **A long direct run lends the CPU between frames** (`ExecutionArbiter::LendCpu`, called at every frame boundary
  of the outermost direct run in `Emulator::ExecuteStep`). The commands queued behind it that do not drive the Z80
  and whose submitter waits (holds, Execute: a TTD stop, a save, media, readers) are served while it waits; then it
  goes on where it stood, exactly its frames. Not lent to: intents (a pause applies at the run's end), driving
  commands (two direct runs never interleave), commands that change the intent (`thenIntent`, `pauseFirst`). The
  pump waits for the lender (`_lender`); a take-back (`AbortDriving`) or a stop during the lend aborts the run when it
  has the CPU back. Lent only once a borrower has waited 20 ms: with holds arriving all the time a run still moves in
  long stretches (without it the machine fuzz under a loaded host went from 3 s to 156 s - a frame per thread
  wake-up). A hold now waits at most ~20 ms plus a frame, not the 5 s timeout of a long run_frames.
  Tests: `ALongDirectRunLetsAHoldInBetweenFrames` (fails 3 of 3 with the lend taken out),
  `ALongDirectRunLendsToNeitherAPauseNorAnotherRun`, `TakingControlBackWhileTheRunLendsTheCpuStopsTheRun`.
- [x] **`Pause()` - the hybrid (owner decision 2026-10-10).** Behind a command another thread serves (a direct run, a
  hold) the pause is queued and the call returns at once: Pending, the command named (`ExecutionArbiter::
  ServingCommand`); it applies when the command ends (a pause never aborts one). Nothing ahead: it waits for the park,
  up to 5 s (was 500 ms; a starved host). `Emulator::PauseAndWait(requester, budget)` waits behind the command too;
  past the budget: Busy, still queued. Surfaces say it honestly:
  - WebAPI `POST /pause`: `200 success`, `202 queued` with `waiting_for`, `?wait=true|<ms>` (`202 busy` after it),
    `409` refused (run control);
  - MCP `control_execution pause`: the queued message, no registers (a running machine's);
  - CLI: "Pause queued behind ..."; Lua / Python `pause()` returns "paused" / "queued: ..." / "refused: ...".
  Tests: `APauseBehindACommandReturnsAtOnceQueuedAndApplyLater`, `APauseWithNothingAheadIsInForceWhenItReturns`,
  `PauseAndWaitWaitsForTheCommandWithinItsBudget`.
- [x] **GDB socket close** releases run control: `GdbSessionTakeBack_Test.AClosedSocketEndsTheSession`.
- [x] **No retry, no rush back:** `revoked_by_user` with `retry: false`, the MCP stop error and server
  instructions, Python / Lua `ControlRevoked`, the CLI message, the recipe rule, the server's per-client re-take
  cooldown (X7a), the GUI's Allow Automation Again (X7c).
- [x] **Tests:** the take-back during a long run_frames (core: the run aborted, the session refused; over HTTP: the
  owner's end-to-end check above), the cooldown and allow-again (Qt and core), the idle lease
  (`AnIdleSessionExpiresAndEveryChangeIsAnnounced`), the GDB socket close, Qt: the lock and its release, the HUD's
  always-shown elements.