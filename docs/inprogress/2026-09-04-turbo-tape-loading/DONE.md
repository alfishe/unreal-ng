# DONE — Turbo tape loading (2026-09-04)

**Status:** complete.

## What landed
- Warp-speed signal playback for headerless/custom-loader blocks per `design.md`:
  the completion-detection watchdog engages/disengages turbo while the signal path
  plays bit-exact EAR timing; shipped as feature `turbotape` (`ttape`), exposed as the
  `turbo_tape` io-acceleration setting.

## Evidence
- `Features::kTurboTape` in `core/src/base/featuremanager.h:37` + registration in
  `featuremanager.cpp:325`; `turbo_tape` in `/settings` (2026-09-14 gap analysis
  citation); complements the fast-tape trap (done, 2026-08-30).

## Follow-ups
- None.
