# DONE — TTD reverse search index (2026-08-20)

**Status:** complete.

## What landed
- The coverage index that accelerates reverse search: per-frame executed/read address
  sets used to prune `FindLastAccess` and M1 enumeration; index telemetry surfaced in
  `/ttd/status` (`coverage_index_frames/_bytes`).

## Evidence
- `TTDSearchQuery` + `CanPruneByCoverage` (`core/src/debugger/ttd/ttdprobe.h`,
  `timetravelmanager.h:784`), `FindLastAccess` (timetravelmanager.cpp:3157+);
  the 2026-09-15 TTD coverage evaluation §2.1 confirms it tested and in use.

## Follow-ups
- Querying the index directly (TD-7/G-5) is ~80% covered by TD-2 range `find-last`;
  the dedicated endpoint stays deferred — [../PLAN.md](../PLAN.md) #20.
