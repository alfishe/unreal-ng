# Time-Travel Debugging — Tracking

This folder tracks all sprints and phases of the Time-Travel Debugging (TTD) +
GDB reverse-debugging implementation for Unreal-NG.

## Source-of-truth documents

- **Implementation plan**: [`docs/emulator/design/debugger/time-travel-debug/implementation-plan.md`](../../emulator/design/debugger/time-travel-debug/implementation-plan.md)
- **Parent TDD**: [`docs/emulator/design/debugger/time-travel-debug/time-travel-debugging-tdd.md`](../../emulator/design/debugger/time-travel-debug/time-travel-debugging-tdd.md)
- **GDB TDD**: [`docs/emulator/design/debugger/time-travel-debug/gdb-reverse-debugging-tdd.md`](../../emulator/design/debugger/time-travel-debug/gdb-reverse-debugging-tdd.md)
- **Overhead & gating**: [`docs/emulator/design/debugger/time-travel-debug/overhead-and-gating.md`](../../emulator/design/debugger/time-travel-debug/overhead-and-gating.md)
- **UX**: [`docs/emulator/design/debugger/time-travel-debug/time-travel-ux.md`](../../emulator/design/debugger/time-travel-debug/time-travel-ux.md)

## Cross-cutting decisions

See [`decisions.md`](decisions.md) for naming conventions and flag-layer
choices that apply forward to every sprint and phase.

## Sprint / phase index

| Sprint / Phase | Status | Notes |
|---|---|---|
| [Sprint 0 — Foundations](sprint-0-foundations.md) | ✅ Complete (2026-07-19) | 50 new tests, build green, baseline benchmarks recorded |
| Phase 1 — TTD recorder | ⏳ Not started | Per-frame capture + ring buffer + restore/replay |
| Phase 2 — TTD seek + UI | ⏳ Not started | Run-control claim enforcement, Qt timeline, seek API |
| Phase G1 — GDB RSP server (live debug) | ⏳ Not started | `ENABLE_GDB_AUTOMATION` builds the actual server |
| Phase G2 — Reverse execution | ⏳ Not started | `bc`/`bs`/`bt`, last-access queries |

## Per-sprint file convention

Each sprint or phase gets its own `sprint-N-<slug>.md` or `phase-<id>-<slug>.md`
file in this folder. The file records:

1. **Items delivered** (table: # / item / files / tests)
2. **Design decisions worth recording** (anything a future maintainer needs)
3. **Pre-existing test failures** that were verified to reproduce at baseline
4. **Benchmark numbers** (if the sprint moved any perf-relevant code)
5. **What this sprint deliberately does NOT ship** (explicit non-goals, to
   save reviewers from looking for things that were intentionally deferred)
