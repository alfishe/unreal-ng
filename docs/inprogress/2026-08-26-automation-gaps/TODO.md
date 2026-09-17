# TODO — Automation gaps action plan (2026-08-26)

**Status:** partially done — and **historically superseded**: this was the first
automation gap inventory. The live gap tracking moved to
`../2026-09-14-automation-triage-gaps/` (recommendations + TTD coverage
evaluation). Keep this folder as the original analysis; do not extend it.

## Progress (as of supersession)
- Phase 0 (GDB server basics): 15/20 done.
- Phase 1A (GDB hardening): 48/49 done.
- Phase 1B (DeZog): the summary table says 0/6 — **stale**; the DeZog DZRP
  module actually shipped (`../2026-08-27-dezog-integration/`, near-complete).
- Phase 1C (MCP): the summary table is **stale**; the MCP bridge shipped
  (`../2026-08-14-mcp-server-automation/DONE.md`).
- Feature-parity matrix in [feature-parity.md](feature-parity.md) — largely
  satisfied by the four-interface parity program (WebAPI → MCP/CLI/Lua/Python).

## Remaining
- Everything still open from here is re-tracked in the 2026-09-14 program:
  expression evaluator/conditional breakpoints, screen-state video modes
  (P1-1), porttrace rules (P1-4), capabilities discovery (P2-3), per-machine
  resources (P3), TTD first-class + docs truth pass (TD-1/TD-6).
- Original Phase 2 (advanced debugging) / Phase 3 (screen & determinism) /
  Phase 4 (infrastructure) ideas remain the backlog source for that program.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — the T1–T4 items.
- Current gap truth: [`../2026-09-14-automation-triage-gaps/recommendations.md`](../2026-09-14-automation-triage-gaps/recommendations.md).
