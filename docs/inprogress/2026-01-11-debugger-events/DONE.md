# DONE — Debugger events crash fixes (2026-01-11)

**Status:** complete.

## What landed
- Crash root cause (debugger event flow) fixed by the two hotfixes documented here:
  deferred disassembly and the debugger state guard.
- The stabilized event flow is the substrate the later automation surfaces build on;
  no recurrence since.

## Evidence
- `hotfix-deferred-disassembly.md`, `hotfix-state-guard.md` (as-built records);
  debugger event handling in `core/src/debugger/` has run under the full automation
  stack since without this crash class.

## Follow-ups
- None.
