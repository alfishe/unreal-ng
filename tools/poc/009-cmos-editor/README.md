# 009 — cmos-editor

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

One of the nine debugger widget layout POCs 001–009, all added in one commit (`eb99122eb`, 2026-08-25,
"UI widget POCs") and not changed since.

## Goals

Compare candidate layouts for viewing and editing the CMOS / RTC cells (clock registers, status registers
A–D, NVRAM). The window ("CMOS/RTC Editor POC") shows nine variants: classic hex grid (16 per row),
8 bytes + ASCII, RTC focus, descriptive list, grouped by type, table view, binary editor, visual map,
quick edit; a popup describes a cell, and edits propagate to all variants.

## Requirements

- Qt6 Widgets, C++17, one self-contained `main.cpp`; light / dark theme switch.
- Sample register contents; no emulator connection.
- Anything else: not recorded.

## Constraints

Not recorded. The code is standalone and links nothing from `core/` or `unreal-qt/`.

## Acceptance criteria / expected results

Not recorded. No chosen layout is recorded.

## Status

**abandoned (pre-guideline)** — inferred: no change since 2026-08-25 and no decision recorded. Nothing in
production uses this code or names this POC. The CMOS / RTC panel is item 6 of
`docs/inprogress/2026-08-26-debugger-enhancements/TODO.md` (added 2026-09-29, not built): a Qt view over
the core's `DeviceState::Rtc` / `RtcAccess::Write`, which landed on the automation interfaces in
`5c09bb19c` (2026-09-29).

## Files

- `main.cpp` — RTC register model and change notifier, cell popup, the nine layouts and the comparison
  window.
- `CMakeLists.txt` — standalone Qt6 project; not included by `tools/poc/CMakeLists.txt`. Build on its own
  into `scratch/YYYY-MM-DD-poc-009/`.
