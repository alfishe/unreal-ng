# DONE — Memory management commands (2026-01-22)

**Status:** complete.

## What landed
- Memory management command set per `implementation_plan.md`/`task.md`; verified in
  `walkthrough.md` — memory find/search/fill/dump operations across the surfaces.

## Evidence
- Memory endpoints in `core/automation/webapi/src/api/state_memory_api.cpp` and
  `debug_api.cpp` (find patterns, reads/writes); memory ops on CLI/Lua/Python/MCP
  (`debug_code find_bytes`, `invoke_api`-reachable).

## Follow-ups
- LLM-optimized extraction (map/hexdump/sparse/dump) landed 2026-09 via TD-3.
