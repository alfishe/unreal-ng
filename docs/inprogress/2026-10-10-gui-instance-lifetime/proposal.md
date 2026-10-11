# Proposal

## Goal

No GUI code path, existing or future, can read freed instance memory, whatever thread removes the instance and
whenever a GUI handler runs. The fix must not depend on each site remembering a rule.

Non-goals: changing what the GUI shows after a removal (it already rebinds to another instance or goes empty);
the TTD v1 removal; the automation surfaces' own ordering (covered as a side effect, see §4).

## Options

### A. A lease at every site

Wrap each of the 159 functions in `if (auto lease = emulator->LeaseContext()) { ... }`.

- Effort: 3-4 days of mechanical edits, plus review of every long handler against the lease rule (no lease held
  across a wait for a thread that may be removing: load file, reset, step out, TTD seek).
- Weakness: opt-in. The next widget that forgets is the next bug; the stress cannot reach most windows to catch
  it (see [gap-analysis.md §5](gap-analysis.md#5-why-per-site-fixing-does-not-converge)). Raw `Emulator*` in the
  dialogs stays a separate dangling-pointer problem. Long handlers either hold the lease too long (risking a
  deadlock with a remover that waits for leases) or split into several leases with re-checks.
- Verdict: what #10 did, scaled up. Does not converge.

### B. The GUI unbinds synchronously before the free

Removal waits until the Qt thread has run the unbind: either the MessageCenter handler switches to a blocking
invoke, or `EmulatorManager` calls a registered UI "detach" hook that blocks until the Qt thread has run it.

- Effort: 1-2 days for the mechanism, more for the deadlock analysis.
- Weakness: a cross-thread wait on the Qt thread.
  - A GUI-initiated removal runs on the Qt thread itself and must take an inline path.
  - The unbind removes MessageCenter observers, and `RemoveObserver` waits for a dispatch in flight. If that
    dispatch is the blocked destroy handler, this deadlocks.
  - Any Qt-thread wait on a core thread (a `Pause()`, a CPU hold, a TTD seek) that is itself waiting on the
    remover deadlocks. It needs timeouts, and a timeout reopens the use-after-free.
- It does not cover handlers already queued with a captured raw pointer, or dialogs that are not unbound
  through the binding.
- Verdict: fixes the common order and adds a new class of hangs.

### C. Retire at removal, free at the last owner (recommended)

Split `Emulator::Release()` in two:

| Phase | When | What |
|-------|------|------|
| **Retire** | `RemoveEmulator()`, on the removing thread, after `Stop()` | The instance stops being a live machine: no emulation thread, no host input, no message observers, no audio callback, no coordinator session, not in the manager. Execution and machine-changing operations are refused. **Nothing is freed.** |
| **Free** | `~Emulator()`, when the last `shared_ptr<Emulator>` goes | Today's deletes (debug manager, TTD, main loop, slots, drives, `Core`, media manager, config, context), unchanged in order |

A GUI handler that runs late reads a stopped, valid machine. The handler cannot tell, and does not need to: the
queued unbind follows within milliseconds and the view goes empty or rebinds. The protection does not depend on
any site: it holds for every holder of a `shared_ptr<Emulator>`, in the GUI and in the automation surfaces.

What C needs from the GUI is that it **holds a `shared_ptr`, not a raw pointer**, wherever it reads. Today only
`MainWindow::_emulator` does. Hence the GUI half below.

- Effort: see [roadmap.md](roadmap.md); about 8-11 days in four landable phases.
- Verdict: the only option that closes the class. Recommended.

## Design of C

### Core

1. **`Emulator::Retire()`** (new), called by `EmulatorManager::RemoveEmulatorInstance()` and by `ShutdownAllEmulators()`
   in place of `Release()`, after `Stop()`. It contains the non-freeing part of today's `ReleaseNoGuard()`:
   - `BeginRetirement()`; state `StateRetired` (new; owner decision 2026-10-10).
   - `DetachHostInput()` for the keyboard and the mouse, release `_stepOverHostHold`, finish a pending step-over.
   - Unlink the coordinator session (`_coordinator->SetSession(nullptr)`) and disown the media manager's op owner.
   - Remove the instance's MessageCenter observers and clear the audio callback.

   After `Retire()` no thread other than a holder's own enters the instance, and no core thread calls out of it.

2. **The free moves to `~Emulator()`**, which already calls `Release()` when the instance was never released.
   `Release()` stays for direct callers (tests, a failed `Init`) and becomes `Retire()` + free.

3. **Refusals on a retired instance**, so a late GUI handler cannot restart or change the machine:
   - Execution: `Start`, `Resume`, `RunNFrames`, `RunTStates`, step commands. The arbiter refuses when
     `IsRetiring()`; it is the one place that decides whether the Z80 executes.
   - Machine-changing operations through the coordinator (media, reset, snapshot load, TTD seek). The coordinator
     refuses ops for a retired machine. This is one check at its entry, not per operation.
   - Reads stay allowed. They are what late handlers do, and they are now safe.

4. **Where the free runs.** It runs on whatever thread drops the last reference: usually the Qt thread in the
   queued unbind, or a WebAPI worker at the end of its request. The free must therefore:
   - not wait for any thread (the emulation thread is joined at `Stop()`; observers, host input and audio are
     detached at `Retire()`);
   - not post blocking work to the Qt or MessageCenter threads;
   - be correct on the MessageCenter worker too (a handler that held the last copy).

   Phase 1 audits the device destructors that own threads (network cards, NeoGS, the sound engine) for joins and
   blocking posts.

5. **Leases** stay with their current API. They now mean "the machine is live, not retired", which is what a
   site that is about to change the machine wants to know. `WaitForContextLeases()` stays before `Stop()`.
   Memory safety no longer depends on them.

6. **Visibility.** `EmulatorManager` counts retired instances that are still alive and logs at shutdown any that
   were never freed: a holder that never drops its `shared_ptr` is a leak, and this makes it visible.
   `EmulatorContext` gets no new fields.

### Bounded waits (owner decision, 2026-10-10)

A removal must never wait forever: not for a lease holder, not for the GUI, not for a stuck thread. C makes this
cheap, because a timeout on any wait before the free is safe: `Retire()` frees nothing, so giving up on a wait
cannot leave a reader on freed memory. The free never waits at all; it happens when the last holder lets go.

| Wait | Today | After C | On timeout |
|------|-------|---------|------------|
| MessageCenter drain (`WaitForOutstandingMessages`) | 5 s, then frees anyway (a use-after-free window) | 5 s | Warning; continue to `Retire()`. Safe: nothing is freed |
| `WaitForContextLeases()` | Unbounded (`unique_lock`) | `try_lock_for`, **2 s** (`_leaseMutex` becomes a `std::shared_timed_mutex`) | Warning naming the instance; continue to `Stop()` / `Retire()`. A lease holder that still runs reads a retired machine |
| `Stop()` joining the emulation thread | Unbounded | **5 s** | Error; the instance is marked **stuck**: retired, never freed (a deliberate leak, logged), because freeing under a running thread is the 2026-10-02 crash |
| Last `shared_ptr` dropped (the free) | n/a | No wait: removal returns right after `Retire()` | A watchdog in `EmulatorManager` logs a retired instance that is still alive after **10 s** (id, `use_count`), once, then every 60 s |
| Application exit (`ShutdownAllEmulators`) | Releases all | Retires all, drops the manager's references, waits up to **3 s** for the retired-alive count to reach 0 | Logs the survivors and exits without freeing them (the process ends; no destructor runs on a live reader) |

The values are named constants in `EmulatorManager` (`kLeaseWaitTimeout`, `kStopJoinTimeout`, `kRetiredAliveWarnAfter`,
`kShutdownFreeTimeout`), each with a test (see [test-plan.md](test-plan.md)). A timeout that fires is a bug report,
not a normal path: the warnings carry enough (instance id, which wait, how long, `use_count`) to find the holder.

### GUI

1. **`EmulatorBinding` owns the instance.**
   - `bind(std::shared_ptr<Emulator>)`, with `std::shared_ptr<Emulator> m_emulator`.
   - `emulator()` and a new `context()` return raw pointers that are valid for the current Qt handler. On a
     retired instance they return the same pointers, now safe to read.
   - `unbind()` emits `unbound()` first, so widgets clear, and drops the `shared_ptr` last.

2. **No stored raw pointers.** The 21 `Emulator*` / `EmulatorContext*` members (see
   [gap-analysis.md §4](gap-analysis.md#4-inventory)) go:
   - Widgets that follow the active instance take `EmulatorBinding*` and read `binding->emulator()` per call.
     Most debugger widgets already have `getEmulatorContext()` / `getMemory()` helpers; those helpers become
     the one place to change.
   - Dialogs tied to one instance (disk files, disk sector, device memory, breakpoint dialogs, PC history) hold a
     `std::shared_ptr<Emulator>`. They keep the retired machine readable while open and show "the instance was
     removed" when `IsRetiring()`.
   - `GuiEmulatorContext`, the HUD model and the settings dialogs get the binding or a `shared_ptr`.
     `StatusBarManager::_biosIssuesContext` and `Ft812DebugWindow::_measuring` compare identity only: they
     become the instance id.

3. **`MainWindow::_emulator`** stays the adoption holder (it is already a `shared_ptr`). The queued destroy
   lambda keeps its current order and drops `_emulator` after `m_binding->unbind()`.

4. **`GetContext()` call sites** become `m_binding->context()`, or the widget helper. This is mechanical:
   159 functions, most of them with the null check they already have. Leases taken today stay; they are now
   redundant for memory but harmless.

5. **Non-Qt threads in the GUI.** The GL render thread and the audio callback are cut in the drain
   (`clearFrameSource`) and in `Retire()` (audio). They hold no `shared_ptr` and must not get one: a render
   thread that owns the last reference would run the free on that thread.

### Rules after the change (go into `docs/guidelines/` and the Qt README)

- GUI code gets the instance from `EmulatorBinding` (or a `shared_ptr` it owns), per handler. It never stores
  `Emulator*` or `EmulatorContext*`.
- A retired instance is readable and refuses changes. Code that changes the machine checks the refusal it gets
  back, as for any other failed operation.
- Leases are for "act only on a live machine", not for memory safety.

## What it gives

- The use-after-free class of #11 closes: every GUI site, the ones not yet found and the ones not yet written.
- The same protection for the 771 `GetContext()` reads in the WebAPI, CLI and MCP handlers. A request in flight
  during a removal reads a retired machine.
- `Release()` becomes simple and testable: retire (detach) and free (delete) are two functions with one job each.
- The WebAPI stress can run with the debugger, TTD and media windows open and expect a clean run (see
  [test-plan.md](test-plan.md)).
- #10's two leases and the per-site checks stop being load-bearing.

## Costs and risks

| Risk | Mitigation |
|------|------------|
| The free runs on an unexpected thread and a destructor waits or posts | Phase 1 audit of thread-owning devices; a test that drops the last reference on a plain `std::thread` and on the MessageCenter worker, under TSan |
| A late GUI handler changes a retired machine | Arbiter and coordinator refusals (core §3), tested directly |
| A holder never drops: a retired machine stays in memory | Retired-alive counter and shutdown log (core §6); a test that removes 100 instances with the GUI bound and checks the counter returns to 0 |
| ZX-Poly: the group removes slaves after the master | The same retire / free split per instance; the group's own test runs under the new order |
| The GUI migration is large and touches many windows | Mechanical, phased per window group; the ratchet in [test-plan.md](test-plan.md) stops new raw reads during and after it |

## As built (2026-10-10)

What differs from the design above, and why:

- **The `getcontext` ratchet is not driven to 0.** With the free at the last owner, a `GetContext()` through an
  instance some holder keeps (`MainWindow::_emulator`, the binding, a dialog's `shared_ptr`) is safe for the
  handler it runs in. The invariant that closes the class is: **no GUI object stores `Emulator*` /
  `EmulatorContext*`** (ratchet `members` = 0, exceptions marked `lifetime-ok` with the reason). Rewriting the 131
  remaining calls to `binding->context()` would change nothing about lifetime; the count stays as a ratchet so the
  raw idiom does not spread.
- **Dialogs have no "removed" banner.** A dialog left open on a removed instance shows it as it was; the arbiter and
  the coordinator refuse changes, so a click does nothing. The banner is a UI nicety for later.
- **The busy-GUI stress also selects.** With only the windows open, the GUI stayed bound to the first neighbour and
  never to a removed instance: 15 minutes on master were clean, i.e. the stress did not reach #11.
  `webapi_stress.py` now selects half of the instances through the CLI before deleting them
  (`STRESS_SELECT_CLI_PORT`, set by `--busy-gui`).
- **Found on the way** (fixed in the same change):
  - offscreen platform: the macOS child-window docking messaged a window id that is not an `NSView` (crash on
    the first tool window opened offscreen);
  - the debugger's visualisation window kept the instance it was created with forever;
  - `EmulatorBinding::m_emulator` was read on the MessageCenter and frame-end threads without synchronisation;
  - the status bar and the FT812 window compared context addresses: a new context can reuse a removed one's
    address.
- **Not covered by tests:** the debugger window, the main window and the menu are not in unreal-qt-tests; the
  busy-GUI stress covers them.
