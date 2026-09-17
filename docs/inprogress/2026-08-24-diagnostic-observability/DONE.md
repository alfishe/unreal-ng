# DONE — Diagnostic observability exports (2026-08-24)

**Status:** complete.

## What landed
- The observability exports scoped here: frame cost accounting, AY register log +
  audio capture, coverage analyzer, porttrace sessions, recording control — all on the
  automation surfaces with parity.

## Evidence
- `analyze_performance` (coverage_*/frame_cost/profile_*/porttrace) verified 2026-09-10;
  `core/automation/webapi/src/api/{porttrace,profiler,recording,state_audio}_api.cpp`.

## Follow-ups
- None.
