# 007 — t-states

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

One of the nine debugger widget layout POCs 001–009, all added in one commit (`eb99122eb`, 2026-08-25,
"UI widget POCs") and not changed since.

## Goals

Compare candidate layouts for a debugger display of CPU timing: total and in-frame T-states, frame number,
raster line / pixel, frame position in percent and microseconds. It is a display widget, not a timing
analysis. The window ("T-States POC - Layout Comparison") shows nine variants: simple horizontal counters,
compact single line with frame counter, labelled grid, progress bar for the frame position, raster
position indicator, LED segment display, accumulate-mode toggle ("like Xpeccy checkbox"), vertical stack
with time conversion, CPU frequency selector; hovering opens a timing popup.

## Requirements

- Qt6 Widgets, C++17, one self-contained `main.cpp`; light / dark theme switch.
- Fixed sample values for a 3.5 MHz, 69 888 T-states-per-frame, 224 T-states-per-line machine; no emulator
  connection.
- Anything else: not recorded.

## Constraints

Not recorded. The code is standalone and links nothing from `core/` or `unreal-qt/`.

## Acceptance criteria / expected results

Not recorded. No chosen layout is recorded.

## Status

**abandoned (pre-guideline)** — inferred: no change since 2026-08-25 and no decision recorded. Nothing in
production uses this code. The production beam / timing view is
`unreal-qt/src/debugger/widgets/ulabeamwidget.*`, which
`docs/inprogress/2026-08-26-debugger-enhancements/TODO.md` counts as adjacent work landed by another
route; no document names this POC.

## Files

- `main.cpp` — sample timing data, the timing popup, the nine layouts and the comparison window.
- `CMakeLists.txt` — standalone Qt6 project; not included by `tools/poc/CMakeLists.txt`. Build on its own
  into `scratch/YYYY-MM-DD-poc-007/`.
