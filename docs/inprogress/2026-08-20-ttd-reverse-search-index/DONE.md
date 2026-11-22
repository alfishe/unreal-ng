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

## Re-check on v2 (2026-11-21)
The README §5 leftovers, with numbers and tests in [README §5](README.md#5-not-done-and-why):
- **RSI-wide**: done. Wide and wrapping ranges are pruned with an exact offset set
  (`TTDCoverageOffsetSet`, `TimeTravelController::CoverageOffsets`); coverage-scan, rare and
  touched are exact for wrapping ranges. A wrapping read: 148.98 ms down to 3.49 ms.
- **RSI-bpset**: done. Reverse continue decodes a frame once for all its PCs. The rest (offset
  aliasing past a few PCs: 16 PCs 286 against 332 ms) is inherent to the index, not worth it.
- **RSI-cond**: done. Read watchpoints from read coverage were already in place; `reverse-continue`
  takes a `condition` on every plane, and the Qt Run Back passes breakpoint conditions; eCat Run
  Back too since 2026-11-21 (RSI-cond-ecat).
- **RSI-models**: checked and closed. The v1 blockers are gone. The lost warning for a page past the
  bound is back, as a counter, with a sweep over every creatable model.
- **RSI-zone**: not worth it. The worst case it would remove is a full journal scan (2.88 M records
  in 25 ms, about 50 ms for the whole 64 MB ring), and port searches take under 1 ms.

## Follow-ups
- Querying the index directly (TD-7/G-5) is ~80% covered by TD-2 range `find-last`;
  the dedicated endpoint stays deferred — [../PLAN.md](../PLAN.md) #20.
