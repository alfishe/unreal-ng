# DONE — Snapshot loading optimizations (2026-01-13)

**Status:** complete.

## What landed
- Discovery findings (`discovery-report.md`) consumed; snapshot loading optimizations
  implemented per `snapshot-loading-optimizations.md`; task list (`task-list.md`) closed.

## Evidence
- Snapshot load path in `core/src/loaders/snapshot/` (SNA/Z80/SZX); snapshot API
  (`core/automation/webapi/src/api/snapshot_api.cpp`) used routinely by automation
  and the TTD litmus workflow (2026-09-15).

## Follow-ups
- Per-format hardening followed in [2026-01-19-sna-loader](../2026-01-19-sna-loader/) and
  [2026-01-19-z80-loader](../2026-01-19-z80-loader/) (both done).
