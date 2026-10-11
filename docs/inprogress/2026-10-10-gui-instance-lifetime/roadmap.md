# Roadmap

Effort is in working days of one developer (or agent) including tests, reviews of the diff, and the gcc / Linux
check. Each phase lands on master separately and leaves master better than before.

| Phase | Contents | Effort | Lands with | Gives |
|-------|----------|--------|------------|-------|
| 0 | `--busy-gui` stress mode and the ratchet script; one baseline round on master | 1 | the stress mode, the script, the baseline numbers | Evidence: the class reproduces when the windows are open. The baseline stops the count growing from today |
| 1 | Core: `Retire()` / free split, bounded waits and the retired-alive watchdog, refusals in the arbiter and the coordinator, the device destructor audit | 2-3 | `EmulatorLifetime_Test`, full `core-tests`, ASan stress (not busy) | Memory safety for every `shared_ptr` holder: `MainWindow`, the dialogs that get one, all automation handlers |
| 2 | GUI core: `EmulatorBinding` owns a `shared_ptr` and gets `context()`; `MainWindow`, menu, status bar, tool bar, screen wrapper, settings widgets, HUD | 2 | `RemovalRace` tests for the main window and the menu; ratchet lowered | The always-open surfaces are safe by construction |
| 3 | GUI windows: debugger and its widgets, TTD widget, media panel, memory viewers, disk / device / breakpoint dialogs, slots, network, FT812 | 3-4 | `RemovalRace` per window; busy-GUI stress, ASan × 3 and TSan × 1 | The full inventory, 0 stored raw pointers, the ratchet at its end state |
| 4 | Clean-up: guidelines and the Qt README rules; BUGS.md #11 closed; leases documented as "live only" | 0.5 | docs | The rule is written where new code is written |

**Total: about 8-11 days.** Phases 0 and 1 (about 3-4 days) already remove most of the risk, because the core
change protects every holder of a `shared_ptr`. The GUI phases then make sure no GUI code reads without one.

## Order and dependencies

```
0 (stress + ratchet) ─► 1 (core split) ─► 2 (binding owns) ─► 3 (windows) ─► 4 (docs, close #11)
```

Phase 1 must not wait for the GUI. Phases 2 and 3 can split further, one window group per commit, under the
ratchet.

## Decisions for the owner

Decided:

- 2026-10-10: option C. Every wait in a removal is bounded ([proposal.md, Bounded waits](proposal.md#bounded-waits-owner-decision-2026-10-10)).
- 2026-10-10: a retired instance reports a new `StateRetired` (visible in the WebAPI state field).
- 2026-10-10: a retired instance refuses execution and every machine-changing operation; reads stay allowed.
- 2026-10-10: the busy-GUI stress serves this work only; it does not join the regular sanitizer routine.

## Out of scope

- The automation handlers' own audit (771 reads). Phase 1 makes them memory-safe; whether each should answer
  "instance removed" instead of reading a retired machine is a separate, smaller task.
- TTD v1 removal (waits for its soak, not before 2026-10-22).
