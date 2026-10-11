# 006 — signals-ports

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

One of the nine debugger widget layout POCs 001–009, all added in one commit (`eb99122eb`, 2026-08-25,
"UI widget POCs") and not changed since.

## Goals

Compare candidate layouts for a debugger panel showing machine signals (DOS, ROM, CP/M, INT, NMI, HALT,
MREQ, IORQ) and I/O port values with their bit meanings. The window ("Signals & Ports POC") shows nine
variants: LED signals + port list, vertical compact, table view, bit grid, dashboard, hex dump style,
signal timeline, port history, compact combined; popups explain a signal or a port.

## Requirements

- Qt6 Widgets, C++17, one self-contained `main.cpp`; light / dark theme switch.
- Static signal and port definitions with sample values; no emulator connection.
- Anything else: not recorded.

## Constraints

Not recorded. The code is standalone and links nothing from `core/` or `unreal-qt/`.

## Acceptance criteria / expected results

Not recorded. No chosen layout is recorded.

## Status

**abandoned (pre-guideline)** — inferred: no change since 2026-08-25 and no decision recorded. Nothing in
production uses this code. Related feature by subject: `docs/inprogress/2026-08-26-debugger-enhancements/`
(Phase 5 port watch, signal indicators DOS / ROM / INT), which does not name this POC; its TODO.md lists
both as remaining.

## Files

- `main.cpp` — signal / port definitions, popups, the nine layouts and the comparison window.
- `CMakeLists.txt` — standalone Qt6 project; not included by `tools/poc/CMakeLists.txt`. Build on its own
  into `scratch/YYYY-MM-DD-poc-006/`.
