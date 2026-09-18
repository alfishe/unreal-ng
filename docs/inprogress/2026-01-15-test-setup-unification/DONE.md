# DONE — Test setup unification (2026-01-15)

**Status:** complete.

## What landed
- Unified deterministic emulator test setup (the `README.md` in this folder) became the
  standard fixture pattern for the whole suite.

## Evidence
- The conventions are codified in `core/tests/README.md` and the helpers
  (`TestWait::For*`, `TestPathHelper`, turbo-mode boot pattern) referenced from
  `AGENTS.md` — all in daily use by the ~3000-test suite.

## Follow-ups
- None.
