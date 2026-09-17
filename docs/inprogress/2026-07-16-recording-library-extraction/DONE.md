# DONE — Recording library extraction (2026-07-16)

**Status:** complete.

## What landed
- Recording extracted into a standalone library `core/recording/` (encoders, 3rd-party
  gif, capture plumbing) behind the `ENABLE_RECORDING` gate; surfaces report a clean
  error when compiled out.

## Evidence
- `core/recording/src/` (encoders, `3rdparty/gif`), `core/automation/webapi/src/api/recording_api.cpp`,
  `core/tests/emulator/recording/`, benchmark target in `core/benchmarks/emulator/recording/`.

## Follow-ups
- MP4/WebM recording deferred (MCP Phase 3) — [../PLAN.md](../PLAN.md) #29.
