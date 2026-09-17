# TODO — MCP server automation (2026-08-17)

**Status:** partially done — the MCP bridge shipped and keeps full parity; this
folder is now the umbrella for its gap follow-ups.

## Progress
- MCP server (`unreal-mcp-bridge`) in production: all WebAPI functionality
  mirrored via loopback, feature parity maintained with CLI/Lua/Python
  (cross-interface parity rule). See
  [`../2026-08-14-mcp-server-automation/DONE.md`](../2026-08-14-mcp-server-automation/DONE.md).
- Roadmap phases 1–2 (core tools + emulator lifecycle/state) done.
- Two audit passes produced the current gap inventory:
  [2026-09-10-gap-report.md](2026-09-10-gap-report.md) and
  [2026-09-10-automation-reconciliation.md](2026-09-10-automation-reconciliation.md).

## Remaining (value order)
1. **TD-1: first-class TTD tooling in MCP** — record/replay/seek mirroring the
   WebAPI TTD surface with LLM-friendly summaries (highest-value open MCP gap;
   tracked live in `../2026-09-14-automation-triage-gaps/ttd-coverage-evaluation.md`).
2. Phase 3 (deferred roadmap items) — see [00-roadmap.md](00-roadmap.md).
3. MCP-specific ergonomics from the gap report (summary sizing, aspect
   enums) — fold into the 2026-09-14 program as they surface.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — items #1/#2 (T1).
- Active gap tracking lives in `../2026-09-14-automation-triage-gaps/`.
