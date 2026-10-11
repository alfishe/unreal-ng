# TODO: GUI instance lifetime (BUGS.md #11)

Status: **landed on master** (f38e72b8e, 2026-10-10). Open items below are optional follow-ups.

Design: [proposal.md](proposal.md). Tests: [test-plan.md](test-plan.md). Phases and effort: [roadmap.md](roadmap.md).

## Decisions (owner, 2026-10-10)

- [x] Option C; every removal wait bounded
- [x] A retired instance reports a new `StateRetired`
- [x] It refuses execution and machine-changing operations; reads allowed
- [x] Busy-GUI stress for this work only, not in the regular sanitizer routine

## Phase 0: evidence and ratchet (~1 day)

Found on the way: on the offscreen platform the macOS child-window docking messaged a window id that is not an
NSView and crashed opening the first tool window (`platform/macos/childwindow_macos.mm`, fixed: Cocoa only).

- [x] `UNREAL_QT_STRESS_WINDOWS` in unreal-qt: open the listed windows at startup, keep them across rebinds
- [x] Row for it in `docs/emulator/environment-variables.md`
- [x] `tools/sanitize/stress_round.sh --busy-gui`
- [x] Baseline ASan round on master with `--busy-gui` (with the CLI selection): hung after ~3 min, then crashed in
      `ZXPolyGroup::AdvanceSlaves`; without the selection 15 minutes clean (the GUI never bound to a removed instance)
- [x] `tools/verification/gui-instance-reads.sh` + `.baseline` (raw `GetContext()` outside the binding, stored
      `Emulator*` / `EmulatorContext*` members); step in `.github/workflows/cmake-ci.yml`

## Phase 1: core retire / free split (~2-3 days)

- [x] `Emulator::Retire()`: retirement flag, host input detach, step-over hold, coordinator session, media op owner,
      MessageCenter observers, audio callback; frees nothing
- [x] The free moves to `~Emulator()`; `Release()` = `Retire()` + free for direct callers
- [x] `RemoveEmulatorInstance()` and `ShutdownAllEmulators()` call `Retire()` instead of `Release()`
- [x] Bounded waits: `kLeaseWaitTimeout` 2 s (`std::shared_timed_mutex`), drain 5 s (kept),
      `kStopJoinTimeout` 5 s (stuck instance: retired, never freed, logged), `kShutdownFreeTimeout` 3 s
- [x] Retired-alive counter and watchdog: warn after `kRetiredAliveWarnAfter` 10 s (id, `use_count`), then every 60 s
- [x] Arbiter refuses execution on a retired instance; coordinator refuses machine-changing ops
- [x] Audit device destructors that own threads (network cards, NeoGS, sound) for joins and blocking posts
- [x] ZX-Poly group: retire / free per instance
- [x] `EmulatorLifetime_Test` (all rows of test-plan §1, timeouts included)
- [x] Full `test.sh`, gcc:16 -O3 on the changed core files
- [ ] `docker/linux/build.sh --test`: skipped by the owner

## Phase 2: binding owns the instance (~2 days)

- [x] `EmulatorBinding::bind(std::shared_ptr<Emulator>)`, `context()`, `emulatorShared()`; `unbind()` drops the
      reference after `unbound()`; the MessageCenter / frame-end threads read it under a mutex (they read the raw
      pointer unsynchronized before)
- [x] Audio settings, video recording: hold a `shared_ptr`; HUD settings dialog: its unused context dropped; HUD model:
      reviewed exception (`lifetime-ok`), its pointer is cleared on the MessageCenter worker inside the drain
- [x] `StatusBarManager::_biosIssuesContext`, `Ft812DebugWindow::_measuring` became instance ids (a new context can sit
      at a removed one's address)
- [x] `MainWindow` unbind (both paths) releases the TTD panel, the tool bar and the recording panel
      (`releasePanelHolders`)
- [x] Dead `GUIEmulatorContext` removed
- [x] Ratchet baseline lowered: members 25 -> 0, getcontext 137 -> 131

## Phase 3: windows (~3-4 days)

- [x] Debugger window and its children, debug visualisation, the dialogs (disk files, disk sector, device memory,
      breakpoint dialog / editor / group, PC history): `std::shared_ptr<Emulator>` instead of `Emulator*`
- [x] The debugger's visualisation window followed the first instance forever (created once, never updated: a
      dangling pointer before, a kept reference now): `DebuggerWindow::propagateEmulator()` updates it with the
      speed control and the live source window
- [x] Media panel, slots, network / traffic, MIDI, int parameters, temporal effects, remote control guard, menu,
      status bar: no stored pointer; they read through a holder (binding, `MainWindow::_emulator`) per handler
- [x] Live source window: keeps the id, looks the instance up (already safe; tested)
- [x] `RemovalRace_Test` (unreal-qt-tests): device memory, disk sector, disk files, PC history, live source, traffic,
      TTD panel
- [ ] P3, deferred (owner 2026-10-11): `RemovalRace` for the debugger window, the main window and the menu. They are
      not in unreal-qt-tests (it compiles 30 of the 111 GUI sources one by one); the way is a library of the GUI
      sources linked into both the application and the tests - risky and about a day. Busy-GUI stress meanwhile
- [ ] "Instance removed" state in the dialogs: not done. A dialog left open shows the retired machine as it was;
      changes are refused by the core
- [x] Ratchet: 0 stored members. `getcontext` stays a non-growing count, not 0 (see proposal, "As built")
- [x] Busy-GUI stress with selection, ASan, 15 min: master hung after ~3 min and crashed in
      `ZXPolyGroup::AdvanceSlaves` (a slave's context gone under the master's loop); this branch clean
      (1364 cycles, 645 GUI selections of the instance being deleted)
- [ ] More rounds (ASan × 2, TSan × 1, Linux ASan × 1): skipped by the owner 2026-10-10
- [ ] Manual check (test-plan §5)

## Phase 4: close (~0.5 day)

- [x] Rules in `docs/guidelines/coding-guidelines.md` (General Architecture): instance from the binding or an own
      `shared_ptr`, no stored raw pointers, `lifetime-ok` exceptions, leases mean "live"
- [x] `sanitizers.md`: mention `--busy-gui` as an optional mode (not part of the routine)
- [x] BUGS.md #11 fixed with the commit id
- [ ] `DONE.md` for this folder once the follow-ups are settled
