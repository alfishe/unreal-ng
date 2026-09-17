# DONE — WD1793 test coverage & reference-data tests (2026-01-24)

**Status:** complete.

## What landed
- WD1793 test matrix (`test-matrix.md`), reference-data tests, and the diskimage
  modernization review pass that seeded the later universal track model.

## Evidence
- 105/105 WD1793 tests green (2026-09-15 run), including write-protect, sleep/timeout
  and reference-data cases; suites in `core/tests/emulator/io/fdc/`.

## Follow-ups
- The "non-standard track layouts" half of the modernization plan was superseded by
  [2026-09-02-universal-track-model](../2026-09-02-universal-track-model/) (TODO: HFE/SCP only);
  DiskManager/save-modes/heatmaps remain in [../PLAN.md](../PLAN.md) #30.
