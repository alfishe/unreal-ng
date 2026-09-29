# TODO — RZX replay integration (2026-09-29)

**Status:** requirements and design written, no code. PLAN row **#27** (T2).

## Documents
- [requirements.md](requirements.md) — why a loader is not enough (four run-time mechanisms), scope, functional (RZ-F1…F21), non-functional (RZ-N1 zero cost when off … RZ-N7), automation / UI, tests and acceptance.
- [design.md](design.md) — answers to "loader or runtime / zero cost / reuse TTD", module layout, the `IN` hook next to the TTD port journal, fetch counting from R behind the existing per-step gate, the existing frame-interrupt mask, fixed video frames with RZX frames as interrupt intervals, snapshots and models, shortcut locks, desync and conventions, lifecycle, TTD reuse vs autonomy, recording, surfaces, cost analysis, tests, phases R0-R5, open decisions.

## Next
- [ ] Decide design §17 (IN hook form, desync default, EI convention, model mismatch, module location).
- [ ] R0: compression library (with SZX #64), `RzxReader`, parser tests and fuzzing.
- [ ] R1: player hooks, fetch counter, desync, locks, SNA / Z80 start snapshots; benchmark gate (zero cost when off).
- [ ] R2: surfaces, notifications, docs, recipe.
- [ ] R3: span-based snapshot loaders, SZX start snapshots, mid-stream snapshot blocks.
- [ ] R4: recording.
- [ ] R5: TTD interop.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — #27, #64, #40.
- SZX: [../2026-09-29-szx-snapshots/](../2026-09-29-szx-snapshots/TODO.md).
- TTD port journals: [ttd-port-read-journal.md](../../emulator/design/debugger/time-travel-debug/ttd-port-read-journal.md).
- Research (not yet in master): `docs/inprogress/2026-09-28-debugger-family/rzx-ttd.md`.
