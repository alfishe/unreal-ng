# DONE — Z80 reference test integration & block I/O fixes (2026-01-18)

**Status:** complete.

## What landed
- Z80 reference test suite integration (`z80test-gtest-proposal.md`, technical analysis)
  and the block I/O instruction fixes (`z80_block_io_fixes.md`); reference status
  documented in `z80_reference_status.md`.

## Evidence
- Z80 test suites in `core/tests/` (part of the green full-suite runs; MEMPTR-aware
  cases exercise the hidden-flag work from the sibling folder).

## Follow-ups
- None.
