# DONE — SNA loader hardening + save (2026-01-19)

**Status:** complete.

## What landed
- SNA hardening (`sna-harden-sow.md`, task, walkthrough) and SNA save
  (`save-implementation-plan.md`, `save-task.md`).

## Evidence
- `core/src/loaders/snapshot/loader_sna.cpp` (load + save incl. MEMPTR handling);
  `POST /snapshot/save` used and verified in the TTD litmus workflow (2026-09-15).

## Follow-ups
- None.
