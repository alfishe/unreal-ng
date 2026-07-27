# Time-Travel Debugging — Tracking

This folder tracks all sprints and phases of the Time-Travel Debugging (TTD) +
GDB reverse-debugging implementation for Unreal-NG.

## Source-of-truth documents

- **Implementation plan**: [`docs/emulator/design/debugger/time-travel-debug/implementation-plan.md`](../../emulator/design/debugger/time-travel-debug/implementation-plan.md)
- **Parent TDD**: [`docs/emulator/design/debugger/time-travel-debug/time-travel-debugging-tdd.md`](../../emulator/design/debugger/time-travel-debug/time-travel-debugging-tdd.md)
- **GDB TDD**: [`docs/emulator/design/debugger/time-travel-debug/gdb-reverse-debugging-tdd.md`](../../emulator/design/debugger/time-travel-debug/gdb-reverse-debugging-tdd.md)
- **Overhead & gating**: [`docs/emulator/design/debugger/time-travel-debug/overhead-and-gating.md`](../../emulator/design/debugger/time-travel-debug/overhead-and-gating.md)
- **UX**: [`docs/emulator/design/debugger/time-travel-debug/time-travel-ux.md`](../../emulator/design/debugger/time-travel-debug/time-travel-ux.md)
- **.ttd schema (canonical)**: [`core/src/debugger/ttd/ttd.ksy`](../../../core/src/debugger/ttd/ttd.ksy) — Kaitai Struct, versioned, MIT-licensed; the single source of truth for the on-disk recording format. C++ constants in `core/src/debugger/ttd/ttd_dump_format.h` must stay in sync. Python analyzer parser at `tools/verification/ttd-analyzer/src/ttd_format.py` mirrors the same schema.

## Cross-cutting decisions

See [`decisions.md`](decisions.md) for naming conventions, flag-layer
choices, **the .ttd binary format / Kaitai schema / versioning policy**,
and other rules that apply forward to every sprint and phase.

## Sprint / phase index

| Sprint / Phase | Status | Notes |
|---|---|---|
| [Sprint 0 — Foundations](sprint-0-foundations.md) | ✅ Complete (2026-07-19) | 50 new tests, build green, baseline benchmarks recorded |
| [Phase 1 — TTD recorder](phase-1-recorder.md) | ✅ Complete (2026-07-19) | Per-frame capture, peripheral serializers, lifecycle hooks, status endpoint. 131 TTD tests green |
| [Phase 2 — TTD seek engine](phase-2-seek-engine.md) | ✅ Complete (2026-07-20) | RestoreCheckpoint, silent replay, SeekTo/StepBack, resume-from-past, external-event markers, divergence corpus + cost gate. 230 TTD tests green |
| [Phase S1 — Session serialization (.ttd)](phase-S1-session-serialization.md) | ✅ Complete (2026-07-23) | Kaitai schema + SerializeSession/DeserializeSession/CaptureRestoreSelfTest in core; standalone Python analyzer (parser, integrity, anomalies, framebuffer renderer, markdown report, CLI). 254 TTD tests green. **Side-phase** — orthogonal to Phase 3/4 |
| Phase 3 — TTD timeline UI (Qt) | ⏳ Not started | Blocked on Phase 2 (now unblocked) |
| Phase 4 — Reverse search + automation | ⏳ Not started | Blocked on Phase 2 (now unblocked) |
| Phase G1 — GDB RSP server (live debug) | ⏳ Not started | `ENABLE_GDB_AUTOMATION` builds the actual server |
| Phase G2 — Reverse execution | ⏳ Not started | Blocked on Phase 4 |

**Naming convention for side-phases.** Phase numbers are linear and imply
blocking dependencies (Phase 3 needs Phase 2). Side-phases — orthogonal
capabilities that ship whenever they're needed without affecting the
linear phase progression — use the `S` prefix (`S1`, `S2`, ...). This
keeps the linear numbering stable when a side-capability lands between
two linear phases.

## Per-sprint file convention

Each sprint or phase gets its own `sprint-N-<slug>.md` or `phase-<id>-<slug>.md`
file in this folder. The file records:

1. **Items delivered** (table: # / item / files / tests)
2. **Design decisions worth recording** (anything a future maintainer needs)
3. **Pre-existing test failures** that were verified to reproduce at baseline
4. **Benchmark numbers** (if the sprint moved any perf-relevant code)
5. **What this sprint deliberately does NOT ship** (explicit non-goals, to
   save reviewers from looking for things that were intentionally deferred)
