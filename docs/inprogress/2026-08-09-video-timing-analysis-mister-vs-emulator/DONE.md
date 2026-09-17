# DONE — Video timing: MiSTer reference vs emulator (2026-08-09)

**Status:** complete — analysis and all fixes (docs 01–20).

## What landed
- Full timing re-derivation against the MiSTer reference model: border timing,
  multicolor latching, memory contention, INT signal timings + correction,
  master reference, floating bus, INT jitter, across-the-edge fix, timing-model sync,
  INT fine tuning, MiSTer frame formation, HC/t-state model, INT response
  double-counting fix, INT-to-paper geometry, I/O port write t-state placement,
  INT self-locking/strict sampling.

## Evidence
- Timing model in core with hardware-timing regression tests (the
  "hardware timing regression testing pattern"); numbered docs 01–20 in this folder
  each end in a landed fix or an authoritative reference section.

## Follow-ups
- None.
