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
- [x] `ExecutionArbiterFuzz_Test`: model-based fuzz.
  - **The load.** Seeded control threads (1-6) issue intents, synchronous, nested, driving, paired and posted
    commands, deadlines, aborts, claims, and bursts that build deep mixed queues. A fake loop runs instructions of
    varying length and fires `PauseFromDriver` and inline commands from its hooks. Throttle 0 / 1 / 3 ms, runs with
    and without a loop, stop midway.
  - **The checks.** After each run, I1-I9 and the decision log are replayed through the sequential model: intent
    replay, edges, the owner model, the priority lane, command order and completeness, barriers. A watchdog reports
    hangs.
  - **The default suite** runs 12 seeds (about 50 ms). `UNREAL_EXEC_FUZZ_SEED` replays one seed;
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
- [ ] `-DSANITIZE=thread|address` configure option and `docker/linux/build.sh --tsan`: the standalone build above
  stands in until X1.
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
- [x] **`ExecutionArbiterMachineFuzz_Test`** (design §8.2): 2-5 threads drive one running Pentagon in a seeded mix.
  - **Operations:** pause, resume, silent pairs, `RunNFrames` (some counted inside a driving hold),
    `RunTStates`, `RunSingleCPUCycle`, arbiter commands that poke memory, status reads.
  - **Checks:** no second driver; a counted `RunNFrames(k)` advances exactly k frames; no hang (a watchdog prints
    the state); the machine parks after a pause and runs after a resume.
  - **Size:** 3 seeds in the suite, about 80 ms.
  - **Mutation check:** with the scope's hold switched off it fails at once, with up to 11958 moments of two
    drivers and inexact steps.
- [x] **Arbiter fuzz:** holds also with `drivesZ80` / `pauseFirst`.
- [ ] **Still open:**
  - abort of a long direct run from the priority lane (`HoldToken::Aborted()` exists; the `Run*` loops do not read
    it yet);
  - `Reset`, media and snapshot brackets are still broadcast Pause / Resume pairs (X3);
  - the machine fuzz's determinism check (§8.2: commands-only sequences equal to a single-thread run) and the full
    mode matrix (§8.3).

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
- [x] **`ExecutionArbiterMachineFuzz_Test.CommandsFromOtherThreadsNeverReachIntoAStep`** (design §8.2,
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
- [ ] **Still on the X1 compat (correct, not yet explicit):** the silent pairs in TTD (`StartRecording`,
  `StopRecording`, `SwitchWriteJournal`, `SessionOperation`, `ResumeRecordingLive`, both backends), `RzxSession`,
  `Memory` (shared-memory migration), `ModelSwitch`, `MachineStateTransfer`.
- [ ] **Front ends:** their own origins, the claim inside the arbiter, DeZog close / TTD `resume` owner-only (X4).
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

## X4 - X6

As in [design.md](design.md) §6: the class b sites; the
front-end origins and the claim; Snapshot on every surface; removal of `Pause(bool)` / `Resume(bool)`. The rest of the
mode matrix (§8.3) comes with X3.
