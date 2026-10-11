# 008 — disassembly

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

One of the nine debugger widget layout POCs 001–009, all added in one commit (`eb99122eb`, 2026-08-25,
"UI widget POCs") and not changed since.

## Goals

Compare candidate layouts for the disassembly view of the Qt debugger. The window ("Disassembly View
POC") shows nine variants: basic columns, compact, with T-states, with comments, with flags affected, flow
markers, subroutine blocks, breakpoint gutter, memory context; an instruction popup shows details of one
instruction.

## Requirements

- Qt6 Widgets, C++17, one self-contained `main.cpp`; light / dark theme switch.
- A fixed sample instruction list (and fake memory for the memory-context variant); no disassembler and
  no emulator connection.
- Anything else: not recorded.

## Constraints

Not recorded. The code is standalone and links nothing from `core/` or `unreal-qt/`.

## Acceptance criteria / expected results

Not recorded. No chosen layout is recorded.

## Status

**abandoned (pre-guideline)** — inferred: no change since 2026-08-25 and no decision recorded. Nothing in
production uses this code or names this POC. The production disassembler views
(`unreal-qt/src/debugger/disassembler*.*`) were developed separately. Related feature by subject:
`docs/inprogress/2026-08-26-debugger-enhancements/` (Phase 1 disassembly navigation, listed as remaining).

## Files

- `main.cpp` — sample instructions, instruction popup / rows, the nine layouts and the comparison window.
- `CMakeLists.txt` — standalone Qt6 project; not included by `tools/poc/CMakeLists.txt`. Build on its own
  into `scratch/YYYY-MM-DD-poc-008/`.
