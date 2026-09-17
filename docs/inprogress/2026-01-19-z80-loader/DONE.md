# DONE — Z80 snapshot loader hardening + save (2026-01-19)

**Status:** complete.

## What landed
- Z80-format snapshot hardening (`z80-hardening.md`, task, walkthrough) and Z80 save
  (`save-implementation-plan.md`, `save-task.md`).

## Evidence
- `core/src/loaders/snapshot/` Z80 loader/writer; load/save round-trips covered by the
  snapshot test suites (invalid-format fixtures in `core/tests/_data/loaders/z80/`).

## Follow-ups
- None.
