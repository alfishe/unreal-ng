# DONE — WD1793 FDC idle stall fix (2026-09-15)

**Status:** complete — root cause fixed, regression-tested, committed to master
(`13a997bc` "fix(fdc): restart motor on Type 2/3 commands and suppress spurious
INTRQ on timeout").

## What landed
- Motor/ready deadlock fix: `prolongFDDMotorRotation()` called unconditionally
  at the start of `startType2Command()` / `startType3Command()` (matching Type 1
  and Force Interrupt), so a sector command after motor timeout restarts the
  motor instead of aborting to `S_END_COMMAND` forever.
- Clean early exit on not-ready in `cmdReadSector/cmdWriteSector/cmdReadAddress/
  cmdReadTrack/cmdWriteTrack` (no FSM overwrite, no FIFO queueing).
- Removed the spurious `raiseIntrq()` on motor stop (real WD1793 has no motor
  pin; the bogus interrupt confused Beta128 `#FF` pollers).
- `modelsregression_test.cpp`: Pentagon128 golden row realigned to `RAM_128`
  after `PortDecoder_Pentagon1024` made `RAM_1024` select extended paging.

## Evidence
- [walkthrough.md](walkthrough.md) verification record: build clean under
  `-Wall -Wextra -Werror`; `WD1793_SleepTimeout_Test.*` 8/8 in 10 ms; all WD1793
  suites 105/105; sharded parallel run 100 % green; full sequential suite
  2 943 passed / 5 skipped / 0 failed.

## Follow-ups
- None.
