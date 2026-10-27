# 003 — memory-mapping

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

One of the nine debugger widget layout POCs 001–009, all added in one commit (`eb99122eb`, 2026-08-25,
"UI widget POCs") and not changed since.

## Goals

Compare candidate layouts for showing which ROM / RAM bank is mapped into each 16K slot of the Z80 address
space. The window ("Memory Mapping POC - Layout Comparison") shows nine variants: vertical stack,
horizontal bar, bank pool + slots, table view, compact inline, visual bar, with paging controls, address
breakdown, vertical memory bar; clicking a slot or bank opens a popup with its details.

## Requirements

- Qt6 Widgets, C++17, one self-contained `main.cpp`; light / dark theme switch.
- Static memory models for ZX Spectrum 48K and 128K, Pentagon 512K, Scorpion 256K, ATM Turbo 2+ (4 MB);
  no emulator connection.
- Anything else: not recorded.

## Constraints

Not recorded. The code is standalone and links nothing from `core/` or `unreal-qt/`.

## Acceptance criteria / expected results

Not recorded. No chosen layout is recorded.

## Status

**abandoned (pre-guideline)** — inferred: no change since 2026-08-25 and no decision recorded. Nothing in
production uses this code. The production memory map views are
`unreal-qt/src/debugger/widgets/memorypageswidget.*` and `memorypagesviswidget.*`;
`docs/inprogress/2026-08-26-debugger-enhancements/TODO.md` counts them as adjacent work landed "by other
routes", and no document names this POC.

## Files

- `main.cpp` — memory models, the nine layouts, bank / page popups and the comparison window.
- `CMakeLists.txt` — standalone Qt6 project; not included by `tools/poc/CMakeLists.txt`. Build on its own
  into `scratch/YYYY-MM-DD-poc-003/`.
