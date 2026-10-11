# 004 — register-dump

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

One of the nine debugger widget layout POCs 001–009, all added in one commit (`eb99122eb`, 2026-08-25,
"UI widget POCs") and not changed since.

## Goals

Compare candidate layouts for the Z80 register display of the Qt debugger, including the memory each
register pair points to. The window ("Register Dump POC - Layout Comparison") has one tab per variant:
Xpeccy classic ("exact format from reference debugger"), with ASCII, table, compact, main / alternate,
detail panel (memory at the selected register), grouped, wide format, and with annotations (IY-based
system variables); a popup shows a memory dump for a register.

## Requirements

- Qt6 Widgets, C++17, one self-contained `main.cpp`; light / dark theme switch.
- Static sample Z80 state; no emulator connection.
- Anything else: not recorded.

## Constraints

Not recorded. The code is standalone and links nothing from `core/` or `unreal-qt/`.

## Acceptance criteria / expected results

Not recorded. No chosen layout is recorded.

## Status

**abandoned (pre-guideline)** — inferred: no change since 2026-08-25 and no decision recorded. Nothing in
production uses this code; the production register view is
`unreal-qt/src/debugger/widgets/registerswidget.*`, extended later on its own (MEMPTR / HALT / IFF,
2026-10-03, `896a0d7ba`, `30bfe1029`) with no reference to this POC. Related feature by subject:
`docs/inprogress/2026-08-26-debugger-enhancements/` (Xpeccy parity), which does not name this POC.

## Files

- `main.cpp` — Z80 sample state, register definitions, the layouts, the memory-dump popup and the tabbed
  comparison window.
- `CMakeLists.txt` — standalone Qt6 project; not included by `tools/poc/CMakeLists.txt`. Build on its own
  into `scratch/YYYY-MM-DD-poc-004/`.
