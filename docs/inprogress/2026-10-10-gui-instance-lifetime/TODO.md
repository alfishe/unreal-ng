# TODO: GUI instance lifetime (BUGS.md #11)

Status: **option C chosen, every removal wait bounded** (owner, 2026-10-10). Not started.

Design: [proposal.md](proposal.md). Tests: [test-plan.md](test-plan.md). Phases and effort: [roadmap.md](roadmap.md).

## Open decisions (ask one at a time)

- [ ] A retired instance's state: `StateDestroying` or a new `StateRetired`
- [ ] What a retired instance refuses: execution only, or execution plus machine-changing operations (recommended)
- [ ] Busy-GUI stress in the regular sanitizer routine (`docs/testing/sanitizers.md`)

## Phase 0: evidence and ratchet (~1 day)

- [ ] `UNREAL_QT_STRESS_WINDOWS` in unreal-qt: open the listed windows at startup, keep them across rebinds
- [ ] Row for it in `docs/emulator/environment-variables.md`
- [ ] `tools/sanitize/stress_round.sh --busy-gui`
- [ ] Baseline ASan round on master with `--busy-gui`: expected to fail; record the report here
- [ ] `tools/verification/gui-instance-reads.sh` + `.baseline` (raw `GetContext()` outside the binding, stored
      `Emulator*` / `EmulatorContext*` members); step in `.github/workflows/cmake-ci.yml`

## Phase 1: core retire / free split (~2-3 days)

- [ ] `Emulator::Retire()`: retirement flag, host input detach, step-over hold, coordinator session, media op owner,
      MessageCenter observers, audio callback; frees nothing
- [ ] The free moves to `~Emulator()`; `Release()` = `Retire()` + free for direct callers
- [ ] `RemoveEmulatorInstance()` and `ShutdownAllEmulators()` call `Retire()` instead of `Release()`
- [ ] Bounded waits: `kLeaseWaitTimeout` 2 s (`std::shared_timed_mutex`), drain 5 s (kept),
      `kStopJoinTimeout` 5 s (stuck instance: retired, never freed, logged), `kShutdownFreeTimeout` 3 s
- [ ] Retired-alive counter and watchdog: warn after `kRetiredAliveWarnAfter` 10 s (id, `use_count`), then every 60 s
- [ ] Arbiter refuses execution on a retired instance; coordinator refuses machine-changing ops
- [ ] Audit device destructors that own threads (network cards, NeoGS, sound) for joins and blocking posts
- [ ] ZX-Poly group: retire / free per instance
- [ ] `EmulatorLifetime_Test` (all rows of test-plan §1, timeouts included)
- [ ] Full `core-tests`, ASan stress (not busy), gcc:16 -O3, `docker/linux/build.sh --test`

## Phase 2: binding owns the instance (~2 days)

- [ ] `EmulatorBinding::bind(std::shared_ptr<Emulator>)`, `context()`, `unbind()` drops the reference last
- [ ] `MainWindow`, menu, status bar, tool bar, screen wrapper read through the binding
- [ ] Audio settings, video recording, HUD model / settings: binding or `shared_ptr`
- [ ] `StatusBarManager::_biosIssuesContext`, `Ft812DebugWindow::_measuring` become instance ids
- [ ] `RemovalRace` fixture; tests for the main window and the menu actions
- [ ] Lower the ratchet baseline

## Phase 3: windows (~3-4 days)

- [ ] Debugger window and widgets (registers, memory, disassembler, stack, memory pages, 16K, border timing, ULA
      beam, floppy, speed control, video recording)
- [ ] TTD widget
- [ ] Media panel
- [ ] Dialogs holding one instance as `shared_ptr` with a "removed" state: disk files, disk sector, device memory,
      breakpoint dialog / editor / group dialog, PC history, debug visualisation
- [ ] Slots window, slot change controller, MIDI activity, network and traffic windows, FT812 window, int
      parameters, temporal effects, remote control guard, live source window
- [ ] `RemovalRace` test per window
- [ ] Ratchet at 0 stored members and no `GetContext()` outside the binding
- [ ] Busy-GUI stress: ASan × 3 rounds of 15 min, TSan × 1, Linux ASan × 1, all clean
- [ ] Manual check (test-plan §5)

## Phase 4: close (~0.5 day)

- [ ] Rules in `docs/guidelines/` and the Qt README: instance from the binding, no stored raw pointers, leases mean
      "live"
- [ ] `sanitizers.md`: the busy-GUI mode
- [ ] BUGS.md #11 fixed with commit ids; move this folder's summary to `DONE.md`
