# Execution arbiter: inventory of the current pause / resume use

Part of [README.md](README.md). Taken on 2026-10-09 from master `adfe1d66f`. Line numbers are from that commit and
will drift.

There are 151 call sites outside the tests: 108 outside `emulator.cpp` and 43 inside it. Profiler and CD-audio
`Pause` / `Resume` are left out because they are unrelated.

| Class | Count | Meaning |
|:--|--:|:--|
| a: intent | 24 | a user, automation or debugger front end says pause / run |
| b: park-mutate-restore | 102 | `wasRunning = IsRunning() && !IsPaused(); Pause(); ...; if (wasRunning) Resume();` |
| c: direct stepping | 13 | `Run*` on the caller's thread: frames, T-states, instructions |
| d: debugger stops | 6 | a breakpoint, step-over, TTD auto-pause at the end of a detached session |
| e: lifecycle | 6 | a model rebuild, a model switch, ZX-Poly seek, the video wall, `LoadROM` |

## 1. Today's mechanics (`core/src/emulator/emulator.cpp`)

**`Pause(broadcast)` :1305**
- It returns at once if `_isPaused` is already set. That read is outside the lock. The call does not wait for a park
  that another thread started and that is not confirmed yet.
- It sets `_isPaused` under `_pauseWaitMutex`.
- It writes `_lastStop`, even on a silent pause.
- It releases `_stepOverHostHold`.
- It waits for the main loop's confirmation for up to 500 ms. On timeout it goes on silently.
- With broadcast it also sets `StatePaused` and posts `NC_EMULATOR_STATE_CHANGE`.

**`Resume(broadcast)` :1399**
- It returns only when nothing is paused. It does not check who paused, and it does not check
  `IsDirectStepping()`: this is the root of the bug.
- It invalidates the confirmation, reconciles host output holds and clears `_isPaused`. Then it notifies.

**Parking**
- The main loop checks the flag once per frame (mainloop.cpp:144-201). It confirms, then polls with a 20 ms
  `wait_for`.
- `WaitWhilePaused` (:1471) parks mid-frame. It is entered from the per-instruction check in z80.cpp:1024 and from
  `OnBreakpointHit`.

**Direct stepping**
- `DirectStepScope` (:2704) is a depth counter plus `HostOutputHold(DirectRun)`.
- Every `Run*` method pauses with broadcast and stays paused.
- Nothing stops a `Resume` from another thread while a step runs.

**Run-control claim** (emulatorcontext.h:343): an advisory owner token. Only GDB takes it. Only the API resume and
step paths check it. Internal parks ignore it.

## 2. Call sites

### a: intent (24)

| Site | What |
|:--|:--|
| dezogdebugadapter.cpp:224 / 245 | DZRP pause / resume |
| dezogdebugadapter.cpp:914 | attach stops the target |
| dezogdebugadapter.cpp:955 | session close resumes if paused: lifts anyone's pause |
| gdbserver.cpp:1511 | `c` continue, then polls `IsPaused` |
| gdbserver.cpp:1558 | interrupt |
| gdbserver.cpp:1729 | attach |
| python_emulator.h:970 / 971 | `pause()` / `resume()` |
| cli-processor-instance.cpp:690 / 735 | CLI pause / resume (resume checks the claim) |
| emulatormanager.cpp:949 / 973 | GUI toolbar, WebAPI (holds `_emulatorsMutex` over the 500 ms wait) |
| debuggerwindow.cpp:995 / 1018 | Continue / Pause |
| speedcontrolwidget.cpp:288, 400, 466 / 414 | full-speed resume / timer stepping |
| ttdwidget.cpp:745, 806, 846 | before a .ttd load or export, before invalidate (left paused) |
| ttdcontrol.cpp:986 | TTD `resume` verb |
| ttdcontrol.cpp:1868 | `PauseAndConfirm` for seek / step / find / export |

### b: park-mutate-restore (102)

Each of these restores only if it parked the machine itself. None of them checks whether another party paused in
the meantime.

**Silent, with an explicit confirmation wait**

| Site | What |
|:--|:--|
| emulator.cpp:732 / 744 | `EditMemoryFromTool` during TTD |
| emulator.cpp:1081 / 1088 | `RequestNMI` |
| emulator.cpp:1119 / 1187 | `RequestMNI` |
| timetravelcontroller.cpp:201, 242, 351, 385 | `StartRecording` |
| timetravelcontroller.cpp:417 / 467 | `StopRecording` |
| timetravelcontroller.cpp:921 / 926 | **`SwitchWriteJournal`: the bug** |
| timetravelcontroller.cpp:956 / 979 | `SessionOperation` (RAII; many TTD calls) |
| timetravelcontroller.cpp:3682 / 3718 | `ResumeRecordingLive` |
| timetravelmanager.cpp:187, 228, 362, 388, 449, 898, 903, 933, 956, 3657, 3693 | the same in TTD V1 |
| rzxsession.cpp:268, 278, 290, 327 / 397, 413 | `Play` / `Stop` |
| rzxsession.cpp:467 / 511 | `Seek`: plays frames on the caller's thread inside the park |
| mediacontrol.cpp:186 / 194 | `ParkedEmulator` |
| cdaudiocontrol.cpp:109 / 114 | `Parked()` |
| analyzers_api.cpp:946 / 954 | `ScopedPause` (8 users) |
| machinestatetransfer.cpp:1239, 1244, 1261, 1263 | parks two machines |

**Silent, relying on `Pause`'s own 500 ms wait**

| Site | What |
|:--|:--|
| memory.cpp:2096 / 2192 | shared-memory migration |
| emulator.cpp:3454 / 3465 | `SetOverscanMode` |
| basicencoder.cpp:975 / 999 | `navigateToTRDOS` |

**Broadcast: the UI and GDB see a stop that is not one**

| Site | What |
|:--|:--|
| emulator.cpp:942 / 968 | `AutostartDisk` (sleeps 20 ms) |
| emulator.cpp:993 / 1070 | `Reset` (sleeps 20 ms) |
| emulator.cpp:1847 / 1898 | `LoadSnapshotStaged` (100 ms wait, then goes on anyway) |
| emulator.cpp:2120, 2126, 2145 | `SaveSnapshot` |
| emulator.cpp:2215, 2247, 2335, 2409, 2450, 2510 | tape load / eject, disk create / load / eject / save |
| python_emulator.h:380 / 389 | `EmulatorPauseBracket` (7 users, 10 ms sleep) |
| python_emulator.h:5273 / 5278 | video recording start |
| lua_emulator.h:310 / 319 | bracket (7 users) |
| lua_emulator.h:5513 / 5518 | video recording start |
| cli-processor-tape.cpp:38 / 47 | bracket (8 users) |
| emulatorbinding.cpp:30 / 39 | Qt bracket (5 users) |
| recording_api.cpp:224 / 263 | recording start |
| tape_disk_api.cpp:393, 462, 517, 575, 650, 739, 805 | tape play / stop / rewind / block info, ... (10 ms sleeps) |

### c: direct stepping (13)

| Site | What |
|:--|:--|
| emulator.cpp:2694, 2762, 2789, 2848, 2898, 2952, 2991, 3065, 3104, 3150 | `Run*`: broadcast pause, stays paused |
| gdbserver.cpp:1544 | pause before `RunSingleCPUCycle` |
| basicencoder.cpp:1192, 1313 | silent resume after `RunNFrames`' broadcast pause: the UI stays "paused" |

### d: debugger stops (6)

| Site | What |
|:--|:--|
| emulator.cpp:2636 | breakpoint, on the emulation thread |
| emulator.cpp:2576 | `CancelPendingStepOver`: parks, never resumes |
| emulator.cpp:3298 | `StepOver` resumes to its temporary breakpoint |
| timetravelcontroller.cpp:1331, timetravelmanager.cpp:1274 | auto-pause at the end of a detached session (whichever thread drives) |
| debuggerwindow.cpp:930 | pause on the interrupt-handler breakpoint |

### e: lifecycle (6)

| Site | What |
|:--|:--|
| mainwindow.cpp:2249 | `Pause(false)` + `Stop` before a model rebuild |
| modelswitch.cpp:141 / 153 | the old machine is parked; resumed only on failure |
| zxpolygroup.cpp:1553 | `SeekToFrame` leaves the master paused |
| VideoWallWindow.cpp:1604 | resumes the primary tile |
| emulator.cpp:3399 | `LoadROM` pauses and never resumes |

## 3. Races found

1. **A restore lifts a foreign pause.** This applies to every class b site. A pause that lands inside the window is
   a no-op, and the restore then resumes the machine. If the other party is stepping directly, two threads drive the
   Z80. The widest windows are TTD `SessionOperation` and `StartRecording`, RZX `Seek`, `LoadSnapshotStaged`, `Reset`,
   the broadcast tape / disk brackets, and `SwitchWriteJournal` (10-20 ms).
2. **Mutation without a confirmed park.**
   - A `Pause` that finds `_isPaused` already set returns before that park is confirmed.
   - The confirmation timeouts go on silently: `Pause` 500 ms, `RunFrame` 250 ms, `LoadSnapshotStaged` 100 ms.
   - The 10 / 20 ms sleeps in the brackets are guesses.
3. **Calls from the emulation thread.** `SwitchWriteJournal`, `ResumeRecordingLive`, V1 Start / Stop, `RzxSession`,
   `ParkedEmulator`, `Parked()` and the language brackets have no run-thread guard. On that thread the pair flips
   `_isPaused` and parks nothing.
4. **Silent parks are visible.** `IsPaused()` is true during a silent park, which causes three problems:
   - GDB `continue` (gdbserver.cpp:1514-1533) reports a stop;
   - DeZog and GDB skip their own pause;
   - `Pause(false)` overwrites `_lastStop`.
5. **Broadcast flags do not match.** basicencoder and mainwindow:2249 pause with one flag and resume with the other.
6. **Front ends lift other parties' pauses.** dezogdebugadapter.cpp:955 and ttdcontrol.cpp:986 resume whenever the
   machine is paused, whoever paused it.

## 4. What already exists and can be reused

| Mechanism | Where | Reuse |
|:--|:--|:--|
| `MainLoop::RunAtFrameBoundary` | mainloop.cpp:326, drained in `RunFrameTasks` :351 | posted work on the run thread, with a completion CV and a timeout |
| `Emulator::RunWhileParked` | emulator.cpp:3702 | runs work under `_pauseWaitMutex` only when parked, confirmed and not stepping: the closest thing to a lease today |
| `RunAtCoherentMoment` | emulator.cpp:3647 | parked, stopped or at a frame boundary, otherwise Busy |
| `MediaManager::ApplyPending` | mediamanager.cpp:669, mainloop.cpp:514 | pending media applied at a frame boundary, by whichever thread drives |
| TTD `SubmitLiveInput` / `SubmitMachineTask` | timetravelcontroller.cpp ~2060-2125 | queued and serviced per instruction through `SetStepWork` |
| run-control claim | emulatorcontext.h:343 | an existing ownership token |

## 5. Earlier documents

| Document | What |
|:--|:--|
| docs/inprogress/2026-01-23-pause-refactoring/ | four per-layer pause flags collapsed into `_isPaused` (the step before this one) |
| docs/emulator/design/ui/pause-stop-race-condition-fix.md, shutdown-race-condition-fix.md, emulator-lifecycle-management.md | lifecycle races |
| docs/inprogress/2026-09-28-debugger-model/rules.md | pause reasons; "while paused nothing changes by itself"; ZX-Poly "one pause stops all CPUs" (§3.2) |
| docs/inprogress/2026-10-04-debugger-snapshot/tdd.md §4.5 | coherent moment, seq |
| time-travel-debugging-tdd.md §7.2, gdb-reverse-debugging-tdd.md §3.3 | TTD pause discipline, run-control claim |
