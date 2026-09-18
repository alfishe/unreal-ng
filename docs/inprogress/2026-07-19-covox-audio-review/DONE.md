# DONE — Covox audio reference review (2026-07-19)

**Status:** complete.

## What landed
- Reference analysis (`reference-analysis.md`) of Covox/Pentagon-Sound behavior;
  conclusions applied to the covox device model in core.

## Evidence
- Covox device in `core/src/emulator/sound/` with TTD serialization (registered in
  `RegisterModelPeripherals`); covox volume-replay verification later reused by the TSFM
  program.

## Follow-ups
- The covox `DeviceState` automation report is still a placeholder — tracked as P2-4,
  [../PLAN.md](../PLAN.md) #17 (opportunistic).
