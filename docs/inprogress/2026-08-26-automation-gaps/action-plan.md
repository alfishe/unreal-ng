# Automation Modernization Action Plan

Last updated: 2026-08-26

## Phase 0: Foundation

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 0.1.1 | Add Z80 register setters in core | TODO | | |
| 0.1.2 | Expose reg write via CLI | TODO | | |
| 0.1.3 | Expose reg write via WebAPI | TODO | | |
| 0.1.4 | Expose reg write via Lua/Python | TODO | | |
| 0.2.1 | Expose LabelManager to automation | TODO | | |
| 0.2.2 | `symbols load` command | TODO | | sjasmplus .sld primary |
| 0.2.3 | Symbolic disassembly output | TODO | | |
| 0.2.4 | Symbolic breakpoints `bp label` | TODO | | |
| 0.3.1 | Add instance ID to NC_EXECUTION_BREAKPOINT | TODO | | |
| 0.3.2 | Add instance ID to NC_EMULATOR_STATE_CHANGE | TODO | | |
| 0.3.3 | WebSocket push for events | TODO | | |
| 0.3.4 | `subscribe` command | TODO | | |
| 0.3.5 | Lua `on_breakpoint(callback)` | TODO | | |
| 0.3.6 | Python `on_breakpoint(callback)` | TODO | | |
| 0.4.1 | Python: add `run_frame` | TODO | | |
| 0.4.2 | Python: add `run_frames` | TODO | | |
| 0.4.3 | Python: add `run_tstates` | TODO | | |
| 0.4.4 | Python: add `run_to_scanline` | TODO | | |
| 0.4.5 | Python: add `run_to_pixel` | TODO | | |

## Phase 1: Protocol Bridges

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 1.1.1 | GDB RSP server skeleton | TODO | | Standalone module |
| 1.1.2 | GDB `g`/`G` packets (registers) | TODO | | Requires 0.1.x |
| 1.1.3 | GDB `m`/`M` packets (memory) | TODO | | |
| 1.1.4 | GDB `Z`/`z` packets (breakpoints) | TODO | | |
| 1.1.5 | GDB `c`/`s` packets (run control) | TODO | | |
| 1.1.6 | GDB `?` packet (stop reason) | TODO | | |
| 1.1.7 | Run-control ownership claim | TODO | | |
| 1.2.1 | DZRP server skeleton | TODO | | Standalone module |
| 1.2.2 | DZRP register commands | TODO | | |
| 1.2.3 | DZRP bank-aware memory | TODO | | |
| 1.2.4 | DZRP condition expressions | TODO | | |
| 1.3.1 | DeZog launch.json template | TODO | | |
| 1.3.2 | Document sjasmplus workflow | TODO | | |
| 1.4.1 | MCP server skeleton | TODO | | Standalone module |
| 1.4.2 | MCP tool schema from command registry | TODO | | |

## Phase 2: Advanced Debugging

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 2.1.1 | Expression evaluator | TODO | | `A == 0x7F` |
| 2.1.2 | Hit count breakpoints | TODO | | `--hit 1000` |
| 2.1.3 | Conditional breakpoints | TODO | | `--if "A == 0"` |
| 2.2.1 | BP action: log | TODO | | |
| 2.2.2 | BP action: dump | TODO | | |
| 2.2.3 | BP action: script | TODO | | |
| 2.3.1 | `watch add` command | TODO | | |
| 2.3.2 | Expression arithmetic | TODO | | |
| 2.3.3 | Watch snapshot on stop | TODO | | |

## Phase 3: Screen & Determinism

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 3.1.1 | `screen_get_buffer` raw data | TODO | | |
| 3.1.2 | Beam position query | TODO | | |
| 3.1.3 | Python numpy wrapper | TODO | | |
| 3.2.1 | Headless deterministic mode | TODO | | |
| 3.2.2 | Input recording | TODO | | RZX-compatible |
| 3.2.3 | Input playback | TODO | | |

## Phase 4: Infrastructure

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 4.1.1 | Command schema definition | TODO | | JSON/YAML |
| 4.1.2 | Generate CLI from schema | TODO | | |
| 4.1.3 | Generate WebAPI from schema | TODO | | |
| 4.1.4 | Generate Lua bindings from schema | TODO | | |
| 4.1.5 | Generate Python bindings from schema | TODO | | |
| 4.1.6 | Generate docs from schema | TODO | | |
| 4.2.1 | Stable error codes enum | TODO | | |
| 4.2.2 | `version` command | TODO | | |
| 4.3.1 | CLI atomicity (low priority) | TODO | | WebAPI has /batch |
| 4.4.1 | Merge CLI/automation docs | TODO | | |
| 4.4.2 | Fix port discrepancy | TODO | | 8765 vs 3333 |

## Status Legend

| Status | Meaning |
|--------|---------|
| TODO | Not started |
| WIP | In progress |
| REVIEW | Code complete, needs review |
| DONE | Merged to master |
| BLOCKED | Waiting on dependency |
| DROPPED | Decided not to implement |

## Dependencies

| Task | Blocked By |
|------|------------|
| 1.1.2 | 0.1.1, 0.1.2 |
| 1.2.2 | 0.1.1 |
| 2.1.3 | 2.1.1 |
| 4.1.2-4.1.5 | 4.1.1 |

## Summary

| Phase | Total | Done | WIP | TODO |
|-------|-------|------|-----|------|
| Phase 0 | 19 | 0 | 0 | 19 |
| Phase 1 | 14 | 0 | 0 | 14 |
| Phase 2 | 9 | 0 | 0 | 9 |
| Phase 3 | 6 | 0 | 0 | 6 |
| Phase 4 | 11 | 0 | 0 | 11 |
| **Total** | **59** | **0** | **0** | **59** |
