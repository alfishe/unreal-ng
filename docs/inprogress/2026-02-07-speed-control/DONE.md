# DONE — Speed control (2026-02-07)

**Status:** complete.

## What landed
- Speed-control design (SPEED-CONTROL.md + widget mockups): speed multiplier with UI
  control, surfaced to automation (`speed_multiplier` in machine identity, P0-1) and
  respected by turbo workflows.

## Evidence
- Permanent design doc `docs/emulator/design/core/speed-control.md` (referenced by the
  turbo-tape design); `speed_multiplier` reported by `GET /emulator/{id}` since `34546478`.

## Follow-ups
- None.
