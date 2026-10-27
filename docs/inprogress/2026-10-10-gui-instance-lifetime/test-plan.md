# Test plan

The bug is a timing window, so a clean stress run proves little. The plan makes the window deterministic in
tests, measures coverage in the stress, and stops regressions statically.

## 1. Core tests (`core-tests`, phase 1)

New suite `EmulatorLifetime_Test` (`core/tests/emulator/emulatorlifetime_test.cpp`). Each test is under 50 ms
except where it says "boot-bound".

| Test | Proves |
|------|--------|
| `RemovedInstanceStaysReadableWhileHeld` | Hold a `shared_ptr`, `RemoveEmulator()`: `GetContext()`, `GetMemory()`, the Z80 state, the debug manager and the media manager are non-null and readable; `IsRetiring()` is true |
| `LastReferenceFrees` | Drop the last `shared_ptr`: the free runs. An instrumented device counter, or ASan, shows nothing outlives it |
| `RetiredInstanceRefusesExecution` | `Start`, `Resume`, `RunNFrames`, `RunTStates`, step: refused; the emulation thread never starts again |
| `RetiredInstanceRefusesMachineChanges` | Media insert, reset, snapshot load, TTD seek through the coordinator: refused with a clear error |
| `RetiredInstanceHasNoObserversOrHostInput` | After `Retire()` no MessageCenter observer of the instance is registered; keyboard and mouse are detached; the audio callback is cleared |
| `FreeOnPlainThread` / `FreeOnMessageCenterWorker` | The last reference dropped on a `std::thread` and inside a MessageCenter handler: no deadlock (`TestWait::For` bound), clean under TSan |
| `ConcurrentReaderDuringRemove` (boot-bound) | A reader thread loops over context reads while another thread removes the instance, 200 rounds; ASan and TSan builds report nothing |
| `RetiredAliveCounterReturnsToZero` | 100 create / hold / remove / drop cycles: the counter is 0 at the end |
| `LeaseWaitTimesOut` | A lease held past `kLeaseWaitTimeout`: the removal returns after the timeout with a warning; the holder keeps reading a retired machine; ASan clean |
| `StuckEmulationThreadIsNeverFreed` | A test hook keeps the emulation thread from joining: `Stop()` gives up after `kStopJoinTimeout`, the instance is marked stuck and not freed, the error is logged |
| `RetiredAliveWatchdogWarns` | A held retired instance past `kRetiredAliveWarnAfter` (shortened for the test): one warning with id and `use_count` |
| `ShutdownDoesNotWaitForHolders` | `ShutdownAllEmulators()` with a held instance returns within `kShutdownFreeTimeout` and logs the survivor |
| `ZxPolyGroupRetiresAndFrees` | A ZX-Poly master removed with its slaves held: all readable, all freed on drop |

The existing `EmulatorManager` and lease tests must pass unchanged. A failure there means the split changed
observable behaviour.

## 2. Qt tests (`unreal-qt-tests`, phases 2-3)

`unreal-qt/tests/qt/` already constructs real widgets against real instances (`ttdwidget_test.cpp`,
`diskfilesdialog_test.cpp`, `devicememorydialog_test.cpp`, ...). A shared fixture `RemovalRace` adds:

1. Create an instance and bind it through an `EmulatorBinding`.
2. Open the window under test.
3. Remove the instance **from a `std::thread`**, as the WebAPI does.
4. Before the queued unbind runs, fire the window's late paths directly: its refresh timer slot, a queued
   `ready()` / `stateChanged()` from the binding, a click on each action that reads the machine.
5. Pump the event loop until `unbound()` arrives (`TestWait::For`).
6. Expect: no crash (the ASan build turns a regression into a report), the window shows the unbound or "removed"
   state, and the instance is freed once the window closes.

Windows covered, one test each: debugger (registers, memory, disassembler, stack), TTD widget, media panel,
memory viewers (16K, pages, pages visualisation), device memory, disk files, disk sector, breakpoint dialog and
editor, PC history, slots window, network and traffic windows, FT812 window, audio and HUD settings, and the
main window's menu actions (`updateMenuStates`, the toggles, `loadFile`, `handleSystemReset`).

Step 4 is what makes this deterministic: the test stands inside the window instead of hoping a removal lands in
it.

## 3. Sanitizer stress with a busy GUI (phase 3)

`tools/sanitize/stress_round.sh` today drives an offscreen GUI with no windows open (see
[gap-analysis.md §5](gap-analysis.md#5-why-per-site-fixing-does-not-converge)). Extension:

- A new environment variable `UNREAL_QT_STRESS_WINDOWS=debugger,ttd,media,memory,slots,network` (a row in
  `docs/emulator/environment-variables.md`). At startup, unreal-qt opens those windows and keeps them open
  across rebinds, so their timers and refresh paths run during every removal.
- `stress_round.sh` gets a `--busy-gui` flag that sets it.
- Acceptance: 3 × 15-minute rounds under ASan and one under TSan, clean, with `--busy-gui`, on macOS. One ASan
  round on Linux (Docker).

Before phase 1 lands, one baseline round with `--busy-gui` on master is expected to fail. That failure is the
evidence that the stress now reaches the inventory. If it does not fail, the stress is not reaching the windows
and must be fixed first.

## 4. Ratchet on raw reads (phase 2 onward)

`tools/verification/gui-instance-reads.sh` (new; the repo has no lint step yet) counts, in `unreal-qt/src`:

- `GetContext()` calls outside `EmulatorBinding`;
- stored `Emulator*` / `EmulatorContext*` members.

It fails when either count is above the number recorded in `tools/verification/gui-instance-reads.baseline`. Each GUI
phase lowers the baseline; the end state is 0 stored members and `GetContext()` only inside the binding. It runs
as a new step in `.github/workflows/cmake-ci.yml` and by hand before a GUI commit.

## 5. Manual check (once, at the end)

With the GUI open on the debugger, TTD and media panel, delete the bound instance via the WebAPI
(`DELETE /api/v1/emulator/{id}`) 20 times while stepping, seeking and loading. The GUI rebinds or goes empty
each time; nothing hangs; the log shows the retired instance freed.

## Commands

```bash
tools/build/test.sh --gtest_filter='EmulatorLifetime_Test.*'
tools/build/test.sh --gtest_filter='RemovalRace*'          # unreal-qt-tests joins the full run
tools/sanitize/configure.sh asan && tools/sanitize/stress_round.sh 900 busy-1 --busy-gui
tools/sanitize/configure.sh tsan && tools/sanitize/stress_round.sh 900 busy-t cmake-build-tsan --busy-gui
docker/linux/build.sh --test
```
