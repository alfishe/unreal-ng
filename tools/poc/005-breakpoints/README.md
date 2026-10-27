# 005 — breakpoints

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

One of the nine debugger widget layout POCs 001–009, all added in one commit (`eb99122eb`, 2026-08-25,
"UI widget POCs") and not changed since.

## Goals

Compare candidate layouts for the breakpoint list of the Qt debugger and try a conditional-breakpoint
editor. The window ("Breakpoints Widget POC") shows nine list variants (classic table, compact icons,
cards, grouped tree, minimal list, detail cards, address bar, dense matrix, Xpeccy style) plus a condition
editor dialog with a syntax-highlighted text expression and a visual clause builder (operand / operator /
operand clauses joined by AND / OR).

## Requirements

- Qt6 Widgets, C++17, one self-contained `main.cpp`; light / dark theme switch.
- Sample breakpoint list; no emulator connection, conditions are not evaluated.
- Anything else: not recorded.

## Constraints

Not recorded. The code is standalone and links nothing from `core/` or `unreal-qt/`.

## Acceptance criteria / expected results

Not recorded. No chosen layout is recorded.

## Status

**abandoned (pre-guideline)** — inferred: no change since 2026-08-25 and no decision recorded. Nothing in
production uses this code or names this POC. The breakpoint UI in production is
`unreal-qt/src/debugger/breakpointeditor.*`, `breakpointdialog.*` and `breakpointgroupdialog.*`,
developed separately (conditional-breakpoint matching was measured in POC 022 and landed in `099ea9f12`,
2026-10-04). Related feature by subject: `docs/inprogress/2026-08-17-conditional-breakpoints/`.

## Files

- `main.cpp` — breakpoint model, popup, condition highlighter, visual expression builder, condition
  editor dialog, the nine layouts and the comparison window.
- `CMakeLists.txt` — standalone Qt6 project; not included by `tools/poc/CMakeLists.txt`. Build on its own
  into `scratch/YYYY-MM-DD-poc-005/`.
