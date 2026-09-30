# TODO — In-memory font finder

Requirements and TDD drafted 2026-09-29, revision 2 each (2026-09-29:
four detection methods — static pattern, signature catalog, live runtime
correlation, TTD-based offline correlation — plus a shared, font-agnostic
`AccessPatternTracer` primitive meant to be reused later by a sprite/tile
finder). No code yet.

`AccessPatternTracer` (the shared correlation primitive behind Methods C/D)
has its own design doc:
[access-pattern-tracer-design.md](access-pattern-tracer-design.md) — memory
(~256 KB/session, two-tier), CPU overhead (planning estimate, pending
benchmark), and TTD integration (builds on today's `TTDWriteJournal` +
silent replay, **not** blocked on TTD v2).

Plan: [PLAN.md](../PLAN.md) #75 (T3).

Next: P0 (scoring core against synthetic fixtures, [tdd.md](tdd.md) §5) — no
emulator dependency, so it can start independently of everything else in
flight.
