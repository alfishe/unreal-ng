# DONE — Keyboard injection TDD (2026-01-28)

**Status:** complete.

## What landed
- Keyboard injection per `keyboard-injection-tdd.md`: press/release/tap/type/combo/macro
  on every surface, with the running-loop requirement handled by the automation
  dispatcher (keyboard API queues into the emu loop).

## Evidence
- `type_input` MCP tool (verified 2026-09-10); `core/automation/webapi/src/api/keyboard_api.cpp`;
  TTD input journal replays injected keys (kempston-mouse program extended the same funnel).

## Follow-ups
- None.
