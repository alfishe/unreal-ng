# Automation Modernization Action Plan

Last updated: 2026-08-26

> **AI Agent Instructions:**
> 1. Update task **Status** to DONE immediately when implementation is complete
> 2. Add **Commit** ID immediately after each commit is made
> 3. Update **Summary** counts when task statuses change
> 4. Keep **Notes** concise but informative

## Phase 0: Foundation

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 0.1.1 | Add Z80 register setters in core | DONE | 4bd9e24e | Z80State has public members |
| 0.1.2 | Expose reg write via CLI | DONE | 4bd9e24e | `reg set <name> <value>` |
| 0.1.3 | Expose reg write via WebAPI | DONE | 4bd9e24e | PUT `/api/v1/emulator/{id}/registers/{name}` |
| 0.1.4 | Expose reg write via Lua | DONE | 4bd9e24e | `get_register(name)`, `set_register(name, value)` |
| 0.1.5 | Expose reg write via Python | DONE | 4bd9e24e | `get_register(name)`, `set_register(name, value)` |
| 0.2.1 | Expose LabelManager to automation | DONE | 70a8ceb2 | Core filtering API + CLI/WebAPI/Lua/Python bindings complete. See [label-manager.md](../../emulator/design/debugger/label-manager.md) |
| 0.2.2 | `symbols load` command | POSTPONED | | sjasmplus .sld primary |
| 0.2.3 | Symbolic disassembly output | DONE | 4332ed6e | `CALL label (#8000)` format for JP/CALL/JR when label exists |
| 0.2.4 | Symbolic breakpoints `bp label` | DESIGN | | Not clear yet how to handle re-positioning breakpoint when label address changes|
| 0.3.1 | Add instance ID to NC_EXECUTION_BREAKPOINT | DONE | 3bd53395 | z80.cpp was using SimpleNumberPayload |
| 0.3.2 | Add instance ID to NC_EMULATOR_STATE_CHANGE | DONE | 3bd53395 | Already implemented |
| 0.3.3 | WebSocket push for events | DESIGN | | |
| 0.3.4 | `subscribe` command | DESIGN | | |
| 0.3.5 | Lua `on_breakpoint(callback)` | DESIGN | | Needs callback protection strategy |
| 0.3.6 | Python `on_breakpoint(callback)` | DESIGN | | Needs callback protection strategy |
| 0.4.1 | Python: add `run_frame` | DONE | ef117f47 | Full parity with CLI/WebAPI/Lua |
| 0.4.2 | Python: add `run_frames` | DONE | ef117f47 | |
| 0.4.3 | Python: add `run_tstates` | DONE | ef117f47 | |
| 0.4.4 | Python: add `run_to_scanline` | DONE | ef117f47 | Also added `run_scanlines`, `run_to_interrupt` |
| 0.4.5 | Python: add `run_to_pixel` | DONE | ef117f47 | |
| 0.5.1 | Update automation.md with command parity | DONE | ef117f47 | Fixed CLI README port, added execution control docs |

## Phase 1A: GDB RSP Server (Forward Debugging)

See [gdb-protocol.md](../../emulator/design/control-interfaces/gdb-protocol.md) and [gdb-reverse-debugging-tdd.md](../../emulator/design/debugger/time-travel-debug/gdb-reverse-debugging-tdd.md) for full design.

### G1: Forward-Only Stub

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 1A.1.1 | Directory structure + CMake | DONE | dadc9430 | `core/automation/gdb/` with ENABLE_GDB_AUTOMATION gate |
| 1A.1.2 | RSP packet framing | DONE | dadc9430 | `$data#checksum`, ack/nack, escaping, RLE |
| 1A.1.3 | TCP listener + session thread | DONE | dadc9430 | Port 2000 default, 127.0.0.1 only |
| 1A.1.4 | `qSupported` handshake | DONE | dadc9430 | Capability negotiation, NoAckMode |
| 1A.1.5 | `qXfer:features:read:target.xml` | DONE | 59573a60 | Z80 register description, flat XML |
| 1A.1.6 | `qXfer:osdata:read:processes` | DONE | 59573a60 | Instance list for `vAttach` selection |
| 1A.1.7 | `vAttach` instance binding | DONE | 59573a60 | Takes run-control claim via UUID |
| 1A.2.1 | `g` packet (read all regs) | DONE | 59573a60 | Z80State codec per target.xml order |
| 1A.2.2 | `G` packet (write all regs) | DONE | 59573a60 | Refused while detached in history |
| 1A.2.3 | `p`/`P` packets (single reg) | DONE | 59573a60 | Including pseudo-regs (paging latches) |
| 1A.3.1 | `m` packet (read memory) | DONE | 59573a60 | Z80 view + physical view (0x01XX'XXXX) |
| 1A.3.2 | `M`/`X` packets (write memory) | DONE | 59573a60 | Refused while detached |
| 1A.4.1 | `Z0`/`z0` execution breakpoints | DONE | 59573a60 | AddExecutionBreakpoint with owner="gdb" |
| 1A.5.1 | `c` continue | DONE | 59573a60 | Emulator::Resume(), async stop-reply |
| 1A.5.2 | `s` single-step | DONE | 59573a60 | RunSingleCPUCycle |
| 1A.5.3 | `0x03` interrupt (Ctrl-C) | DONE | 59573a60 | Emulator::Pause() → T02 |
| 1A.6.1 | `?` stop reason query | DONE | 59573a60 | T05 with swbreak:/watch:/rwatch:/awatch: |
| 1A.6.2 | `T05` stop replies (exact forms) | DONE | 59573a60 | Byte-exact per §4.7.1 |
| 1A.7.1 | Run-control claim in EmulatorContext | DONE | 00c977fb | UUID-based claim via TakeRunControl |
| 1A.7.2 | Refuse Resume/Step from other surfaces | TODO | | Return error with "busy: GDB session" |
| 1A.7.3 | External pause → T05 stop-reply | TODO | | Via NC_EMULATOR_STATE_CHANGE |

### G2: Watchpoints + Monitor

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 1A.8.1 | `Z2`/`z2` write watchpoints | DONE | 59573a60 | len ≤ 16 via per-address descriptors |
| 1A.8.2 | `Z3`/`z3` read watchpoints | DONE | 59573a60 | |
| 1A.8.3 | `Z4`/`z4` access watchpoints | DONE | 59573a60 | Combined R\|W |
| 1A.8.4 | `watch:` stop replies | DONE | 59573a60 | T05watch:ADDR;thread:1; |
| 1A.9.1 | `qRcmd` monitor framework | DONE | 00c977fb | Hex-encoded text output |
| 1A.9.2 | `monitor model` | DONE | 00c977fb | Shows "ZX Spectrum" (config name to be added later) |
| 1A.9.3 | `monitor instances` | DONE | 00c977fb | pid, symbolic id, model, state |
| 1A.9.4 | `monitor bankinfo` | DONE | | Shows ROM/RAM pages for each bank |
| 1A.9.5 | `monitor frame` | DONE | 00c977fb | T-state + PC display |
| 1A.9.6 | `monitor load snap/tape/disk` | DONE | | Supports sna/z80/szx/tap/tzx/trd/scl/fdi |
| 1A.9.7 | `monitor reset` | DONE | 00c977fb | Paused only, preserves model |

### G3: Reverse Execution (TTD Integration)

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 1A.10.1 | Conditional `ReverseStep+`/`ReverseContinue+` | TODO | | Only when TTD enabled + recording |
| 1A.10.2 | `bs` backward step | TODO | | TTDManager::StepBackInstruction() |
| 1A.10.3 | `bc` backward continue | TODO | | FindLastAccess over armed bp/wp |
| 1A.10.4 | `replaylog:begin` stop reason | TODO | | History exhausted / barrier |
| 1A.10.5 | Detached read-only enforcement | TODO | | G/P/M refused with E0D |
| 1A.10.6 | `monitor ttd status` | TODO | | Session bounds, position, barriers |
| 1A.10.7 | `monitor ttd start` | TODO | | Arm recording mid-session |
| 1A.10.8 | `monitor ttd seek <frame>` | TODO | | SeekTo frame boundary |
| 1A.10.9 | `monitor ttd findlast w <addr>` | TODO | | Reverse search without arming wp |

### G4: Polish

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 1A.11.1 | Physical memory view (0x01XX'XXXX) | TODO | | Raw page access |
| 1A.11.2 | Ephemeral dedicated ports | TODO | | `monitor gdbport <pid>` for legacy clients |
| 1A.11.3 | Range descriptors for len > 16 | TODO | | BreakpointRangeDescription |
| 1A.11.4 | Port breakpoints `monitor bport` | TODO | | IN/OUT breakpoints via BRK_IO |
| 1A.11.5 | Paging pseudo-register writes | TODO | | Route through port decoder |
| 1A.11.6 | Per-client setup docs | TODO | | GDB, IDA Pro, Ghidra, VS Code |
| 1A.11.7 | Fuzz-lite packet tests | TODO | | Malformed packets never crash |
| 1A.11.8 | Integration test with pygdbmi | TODO | | End-to-end proof TTD+RSP compose |

## Phase 1B: DeZog / VS Code DAP

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 1B.1 | DZRP server skeleton | TODO | | Standalone module |
| 1B.2 | DZRP register commands | TODO | | |
| 1B.3 | DZRP bank-aware memory | TODO | | |
| 1B.4 | DZRP condition expressions | TODO | | |
| 1B.5 | DeZog launch.json template | TODO | | |
| 1B.6 | Document sjasmplus workflow | TODO | | |

## Phase 1C: MCP Server

| ID | Task | Status | Commit | Notes |
|----|------|--------|--------|-------|
| 1C.1 | MCP server skeleton | DESIGN | | Standalone module |
| 1C.2 | MCP tool schema from command registry | DESIGN | | |

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
| 3.1.3 | Python numpy wrapper | TODO | | Enables ML/CV pipelines (RL agents, sprite extraction) |
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
| DESIGN | Needs design/specification first |
| POSTPONED | Deferred to later phase |
| DROPPED | Decided not to implement |

## Design Documents

- [Label Manager](../../emulator/design/debugger/label-manager.md) - Symbolic debugging, labels, filtering API

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
| Phase 0 | 20 | 15 | 0 | 5 |
| Phase 1A (GDB) | 49 | 30 | 0 | 19 |
| Phase 1B (DeZog) | 6 | 0 | 0 | 6 |
| Phase 1C (MCP) | 2 | 0 | 0 | 2 |
| Phase 2 | 9 | 0 | 0 | 9 |
| Phase 3 | 6 | 0 | 0 | 6 |
| Phase 4 | 11 | 0 | 0 | 11 |
| **Total** | **103** | **45** | **0** | **58** |
