# 002 — stack-widget

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

One of the nine debugger widget layout POCs 001–009, all added in one commit (`eb99122eb`, 2026-08-25,
"UI widget POCs") and not changed since.

## Goals

Compare candidate layouts for the stack view of the Qt debugger. The window ("Stack Widget POC - Layout
Comparison") shows nine variants:

1. simple 4-entry;
2. extended 9-entry + SP-2;
3. table with headers;
4. horizontal compact;
5. hover preview (hold 1 s) with a memory content popup;
6. call stack analysis;
7. stack pattern view;
8. with annotations;
9. single row minimal.

## Requirements

- Qt6 Widgets, C++17, one self-contained `main.cpp`.
- Light / dark theme switch (`ThemeManager`).
- Generated sample data (stack entries, random memory for the popup); no emulator connection.
- Anything else: not recorded.

## Constraints

Not recorded. The code is standalone and links nothing from `core/` or `unreal-qt/`.

## Acceptance criteria / expected results

Not recorded. No chosen layout is recorded.

## Status

**abandoned (pre-guideline)** — inferred: no change since 2026-08-25 and no decision recorded. Nothing in
production uses this code; the production stack view is `unreal-qt/src/debugger/widgets/stackwidget.*`,
which existed independently. The related feature is `docs/inprogress/2026-08-26-debugger-enhancements/`:
its "Phase 2 stack widget" (9 entries, return-address hints; features-parity.md §1, hover popup after
1 s in §4) matches variants 2 and 5 by subject, but the documents do not name this POC, and TODO.md lists
the stack widget work as remaining.

## Files

- `main.cpp` — the nine layouts, the theme manager, the content popup and the comparison window.
- `CMakeLists.txt` — standalone Qt6 project, target `stack-widget-poc`; not included by
  `tools/poc/CMakeLists.txt`. Build on its own into `scratch/YYYY-MM-DD-poc-002/`.
