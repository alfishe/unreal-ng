# TODO — ATM debugging session findings (2026-09-10)

**Status:** partially done — 4 of 6 bugs fixed on master; one tracked as
P1-1 in the triage program; one ATM-branch renderer investigation remains.

## Progress (against [bug-report.md](bug-report.md))
- **#1 capture dimensions** — fixed: `screencapture.cpp` now sizes from
  `rasterDescriptors[videoMode]`.
- **#2 bank reporting** — superseded by the decoded paging surface:
  `GET /state/paging` bank table with tags/roles/signatures (`fde6f905`);
  legacy `GetRAMPageFromAddress` pointer back-map unchanged (acceptable).
- **#4 TTD seek framebuffer** — fixed: restore + repaint +
  `FlushAndPresentFramebuffer` in `timetravelmanager.cpp`.
- **#6 model names** — fixed in-session (`Config::GetModelFullName`).
- **#3 screen-state video modes** — **open as P1-1** in
  `../2026-09-14-automation-triage-gaps/` (`state_screen_api.cpp` still
  hardcodes `display_mode = "standard"`; verified 2026-09-16).
- **#5 ATM16 black screen** — open, ATM-specific, lives on the `atm` branch.

## Remaining (value order)
1. **P1-1 mode-aware screen state** (master) — PLAN T1 item #3.
2. **ATM16 renderer fix** (`DrawATMMode` investigation) on the `atm` branch.
3. **ATM/ZX-Evo/TS-Conf branch merge** — these machines are not creatable on
   master (missing port decoders/config folders); PLAN T2 item #9 tracks the
   merge that would carry #5 and the ATM decoders (incl. Kempston-mouse ATM
   decoders) into master.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — items #3, #10.
