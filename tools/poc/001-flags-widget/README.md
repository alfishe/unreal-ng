# 001 — flags-widget

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

One of the nine debugger widget layout POCs 001–009, all added in one commit (`eb99122eb`, 2026-08-25,
"UI widget POCs") and not changed since.

## Goals

Compare candidate layouts for the Z80 flags display of the Qt debugger. The window ("Flags Widget POC -
Layout Comparison") shows five variants side by side:

1. checkboxes + labels;
2. circular indicators;
3. toggle buttons;
4. vertical compact;
5. LED indicators (read-only style).

## Requirements

- Qt6 Widgets, C++17, one self-contained `main.cpp`.
- Demo state only (S and C set); no emulator connection.
- Anything else (machines, platforms, interaction rules): not recorded.

## Constraints

Not recorded. The code is standalone and links nothing from `core/` or `unreal-qt/`.

## Acceptance criteria / expected results

Not recorded. The commit records no choice criteria and no chosen layout.

## Status

**abandoned (pre-guideline)** — inferred: no change since 2026-08-25 and no decision recorded anywhere in
the repository. No production code references it. The related feature is
`docs/inprogress/2026-08-26-debugger-enhancements/` (features-parity.md §3 "Flags Widget Enhancement",
Phase 3 of proposal.md); those documents do not name this POC, and their TODO.md lists the flags work
as not done.

## Files

- `main.cpp` — the five layouts and the comparison window.
- `CMakeLists.txt` — standalone Qt6 project, target `flags-widget-poc`; not included by
  `tools/poc/CMakeLists.txt`. Build on its own, e.g.
  `cmake -S tools/poc/001-flags-widget -B scratch/YYYY-MM-DD-poc-001/build` with Qt6 on
  `CMAKE_PREFIX_PATH`.
