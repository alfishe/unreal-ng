# DONE — Time-Travel Debugging engine (2026-07-19)

**Status:** complete — engine and the four endpoint surfaces.

## What landed
- Full TTD engine per the phased docs in this folder: recorder (sprint-0, phase 1),
  seek engine (phase 2), reverse execution (phase 4), reverse search, codec PoC→ship
  (phase 5), session serialization (S1) and hardening (S2); decisions log current.
- Exposed with full parity on WebAPI/CLI/Lua/Python (16 routes) plus GDB reverse
  commands; bookmark support added 2026-09-15 (TD-4).

## Evidence
- `core/src/debugger/ttd/` (TimeTravelManager, probe, bookmarks, external events,
  peripheral registry, `.ksy` schema); `core/tests/debugger/ttd/` (incl.
  `ttdautomationcontract_test.cpp`); `core/automation/webapi/src/api/ttd_api.cpp`.

## Follow-ups (tracked elsewhere, not engine work)
- Registry end-to-end wiring Phases 2–5: [2026-09-10-ttd-registry-integration](../2026-09-10-ttd-registry-integration/) (TODO).
- Agent-ergonomics gaps G-1..G-10: [2026-09-14-automation-triage-gaps](../2026-09-14-automation-triage-gaps/) (TODO) — several already closed (TD-2/3/4).
