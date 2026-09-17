# TODO — Automation triage gaps program (2026-09-14) — ACTIVE UMBRELLA

**Status:** active. This folder is the live gap tracker for the automation
program; it supersedes `../2026-08-26-automation-gaps/`. Last status sync in
[recommendations.md](recommendations.md): 2026-09-15.

## Progress
- **Done:** P0-1..P0-4 (model/session attribution, `34546478` + `cab13b99`),
  P1-2 (PortTag/paging state across all interfaces, `fde6f905`), P1-3, P1-5,
  P2-1 (mouse routing + `mouse` aspect); four-interface parity replication of
  all done surfaces (2026-09-15); `GetVideoModeName` identity fix with
  regressions.
- **Done (TTD coverage evaluation):** TD-2 (`212b7098`), TD-3 phase 1
  (`372c3840`), TD-4 bookmarks (`f4fdcf74`).

## Remaining (priority order from recommendations.md)
1. **P1-1** — mode-aware screen state (`/state/screen` still reports
   `display_mode: "standard"`; `state_screen_api.cpp:79,215`, re-verified
   2026-09-16). Unblocks ATM triage. → PLAN #3.
2. **TD-1** — finish first-class TTD in MCP/CLI/Lua/Python (record/replay/
   seek + summaries). → PLAN #2.
3. **TD-6** — TTD docs truth pass. → PLAN #1.
4. **P1-4** — porttrace rules (noise filtering per decode semantics).
5. **P2-2** — MoonSound automation section (before implementation).
6. **P2-3** — capabilities discovery endpoint; **P2-4** — GS/Covox device
   state (GS DeviceState placeholder → PLAN #19).
7. **P3-1..P3-3** — per-machine resources, triage recipes, ROM catalog.
8. TTD coverage gaps G-1..G-10 remainder: TD-3 phase 2 (`POST /memory/dump` +
   `TempFileTracker` → PLAN #4), TD-5 timeline, TD-7, TD-8.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — items #1–#4 (T1) live here.
