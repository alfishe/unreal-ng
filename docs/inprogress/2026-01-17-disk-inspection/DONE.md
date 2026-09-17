# DONE — Disk inspection & empty-disk creation (2026-01-17)

**Status:** complete.

## What landed
- Disk inspection (`disk-inspection-implementation_plan.md`) and empty-disk creation
  (`empty-disk-implementation_plan.md`) on the automation surfaces.

## Evidence
- `core/automation/webapi/src/api/tape_disk_api.cpp` (disk catalog/inspection, image
  operations incl. create/empty, 2400+ lines); same capabilities on CLI/Lua/Python/MCP.

## Follow-ups
- None.
