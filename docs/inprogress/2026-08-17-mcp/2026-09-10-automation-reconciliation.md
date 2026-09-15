# Automation Module Reconciliation Report

> **Date**: 2026-09-10  
> **Scope**: CLI, WebAPI, MCP, Lua, Python, GDB — documentation vs. implementation

---

## Executive Summary

All automation modules have high feature parity. Key gaps:
- **ROM signatures**: WebAPI implemented (just added), MCP/CLI/Lua/Python need exposure
- **Extended paging (pEFF7)**: WebAPI implemented, not yet in MCP/CLI
- **stepout**: WebAPI/MCP/CLI/Lua/Python all documented and implemented
- **TTD**: GDB fully implemented, WebAPI has `GET /ttd/status`, scripting bindings planned

---

## Feature Matrix

### Core Execution Control

| Feature | WebAPI | MCP | CLI | Lua | Python | GDB |
|:--------|:------:|:---:|:---:|:---:|:------:|:---:|
| step | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| steps N | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| stepover | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| stepout | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| skip_until | ✅ | ✅¹ | ✅ | ✅ | ✅ | — |
| run_frame | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| run_frames | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| run_tstates | ✅ | ✅ | ✅ | — | — | — |
| run_to_scanline | ✅ | — | ✅ | — | — | — |
| run_to_interrupt | ✅ | — | ✅ | — | — | — |

¹ via `invoke_api`

### Breakpoints & Watchpoints

| Feature | WebAPI | MCP | CLI | Lua | Python | GDB |
|:--------|:------:|:---:|:---:|:---:|:------:|:---:|
| Execution BP | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Memory read WP | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Memory write WP | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Port IN BP | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Port OUT BP | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| BP groups | ✅ | — | ✅ | — | — | — |
| Conditional BP | — | — | — | — | — | — |

### Memory Inspection

| Feature | WebAPI | MCP | CLI | Lua | Python | GDB |
|:--------|:------:|:---:|:---:|:---:|:------:|:---:|
| Z80 read | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Z80 write | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Physical page read | ✅ | ✅¹ | ✅ | ✅ | ✅ | ✅ |
| Physical page write | ✅ | ✅¹ | ✅ | ✅ | ✅ | ✅ |
| Memory find | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| Bank mapping info | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **ROM signatures** | ✅² | ❌ | ❌ | ❌ | ❌ | — |
| **pEFF7 port** | ✅² | ❌ | — | — | — | — |

¹ via `invoke_api`  
² Just implemented — see gaps below

### Symbol Management

| Feature | WebAPI | MCP | CLI | Lua | Python |
|:--------|:------:|:---:|:---:|:---:|:------:|
| Load SLD/SYM/MAP | ✅ | ✅ | ✅ | ✅ | ✅ |
| List labels | ✅ | ✅ | ✅ | — | — |
| Resolve by name | ✅ | ✅ | ✅ | ✅ | ✅ |
| Resolve by address | ✅ | ✅ | ✅ | ✅ | ✅ |
| Source listing load | ✅ | ✅ | — | ✅ | ✅ |
| Source at address | ✅ | ✅ | — | ✅ | ✅ |
| Step to line | ✅ | ✅ | — | ✅ | ✅ |

### Profilers

| Feature | WebAPI | MCP | CLI | Lua | Python |
|:--------|:------:|:---:|:---:|:---:|:------:|
| Opcode profiler | ✅ | ✅ | ✅ | ✅ | ✅ |
| Memory profiler | ✅ | ✅ | ✅ | ✅ | ✅ |
| Call trace | ✅ | ✅ | ✅ | ✅ | ✅ |
| Coverage analyzer | ✅ | ✅ | ✅ | ✅ | ✅ |
| AY log | ✅ | ✅ | ✅ | ✅ | ✅ |
| Frame cost | ✅ | ✅ | — | ✅ | ✅ |

### Media & Capture

| Feature | WebAPI | MCP | CLI | Lua | Python |
|:--------|:------:|:---:|:---:|:---:|:------:|
| Screenshot | ✅ | ✅ | — | ✅ | ✅ |
| Screen digest | ✅ | ✅ | ✅ | ✅ | ✅ |
| GIF recording | ✅ | ✅ | ✅ | ✅ | ✅ |
| Audio capture | ✅ | ✅ | ✅ | ✅ | ✅ |
| Tape control | ✅ | ✅ | ✅ | ✅ | ✅ |
| Tape render | ✅ | ✅ | ✅ | ✅ | ✅ |
| Tape import | ✅ | ✅ | ✅ | ✅ | ✅ |

### Time-Travel Debugging

| Feature | WebAPI | MCP | GDB | Lua | Python |
|:--------|:------:|:---:|:---:|:---:|:------:|
| TTD status | ✅ | — | ✅ | ⏳ | ⏳ |
| TTD start/stop | ⏳ | — | ✅ | ⏳ | ⏳ |
| TTD seek | ⏳ | — | ✅ | ⏳ | ⏳ |
| TTD step back | ⏳ | — | ✅ | ⏳ | ⏳ |
| TTD find_last | ⏳ | — | ✅ | ⏳ | ⏳ |
| TTD bookmarks | ⏳ | — | — | ⏳ | ⏳ |

⏳ = documented, implementation pending

---

## Gaps to Address

### 1. ROM Signatures (High Priority)

**Status**: WebAPI implemented in `/state/memory/rom` — each ROM page now includes `signature` (SHA-256) and `title` (known ROM name lookup).

**Missing from**:
| Module | Action |
|:-------|:-------|
| MCP | Add `rom_signatures` aspect to `inspect_state` |
| CLI | Add `state rom` subcommand output |
| Lua | Add `emu.rom_signature(page)` |
| Python | Add `emu.rom_signature(page)` |

**Implementation**:
- Core: `ROM::CalculateSignature()` and `ROM::GetROMTitle()` exist
- WebAPI: `state_memory_api.cpp:getStateMemoryROM()` implemented
- Pattern: Other modules should call the same core methods

### 2. Extended Paging Ports (Medium Priority)

**Status**: WebAPI `/state/memory` now includes `port_eff7`, `port_eff7_hex`, `port_fe`, `port_fe_hex`.

**Missing from**:
| Module | Action |
|:-------|:-------|
| MCP | Include in `memory_banks` aspect |
| CLI | Include in `state memory` output |

### 3. OpenAPI Spec Updates (Low Priority)

The OpenAPI JSON at `/api/v1/openapi.json` should reflect:
- ROM signature fields in `/state/memory/rom` response
- Extended paging fields in `/state/memory` response

---

## Documentation vs. Implementation Discrepancies

### command-interface.md

| Documented | Implemented | Notes |
|:-----------|:-----------:|:------|
| `start` command | 🔮 Planned | Listed as planned in doc |
| `stop all` command | 🔮 Planned | Listed as planned in doc |
| Conditional BP | 🔮 Planned | Listed as planned in doc |
| `state ports` | 🔮 Planned | Listed as planned in doc |
| `state port watch` | 🔮 Planned | Listed as planned in doc |
| `state memory history` | 🔮 Planned | Listed as planned in doc |

### webapi-interface.md

| Documented | Implemented | Notes |
|:-----------|:-----------:|:------|
| TTD endpoints | ✅ `/ttd/status` | Rest are Phase 2-4 |
| WebSocket streaming | 🔮 Future | Documented as future |
| Rate limiting | 🔮 Future | Documented as future |
| Authentication | 🔮 Future | Documented as future |

### MCP README

| Documented | Implemented | Notes |
|:-----------|:-----------:|:------|
| 11 tools | ✅ | All operational |
| 6 resources | ✅ | All operational |
| SSE progress | ✅ | Working |
| stdio bridge | ✅ | Working |

---

## Consistency Checks

### Tape State Names

Verified identical across all interfaces:
- `"idle"`, `"playing"`, `"paused"`, `"ended"`

### Profiler Session States

Verified identical across all interfaces:
- `"stopped"`, `"capturing"`, `"paused"`

### Emulator States

Verified identical across all interfaces:
- `"running"`, `"paused"`, `"stopped"`, `"debug"`

---

## Recommendations

### Immediate (This Sprint)

1. **ROM signatures in MCP**: Add to `inspect_state` tool's `memory_banks` aspect
2. **Update OpenAPI spec**: Add new fields to schema definitions

### Short-Term

3. **ROM signatures in CLI**: Add to `state rom` output
4. **ROM signatures in Lua/Python**: Expose `rom_signature()` method
5. **pEFF7 in MCP**: Include in memory state response

### Medium-Term

6. **TTD in MCP**: Add `ttd_status` tool (read-only, safe to expose)
7. **Port breakpoints in GDB**: Already done via `monitor bport`

---

## Test Coverage Summary

| Module | Tests | Coverage |
|:-------|------:|:---------|
| MCP dispatcher | 24 | Protocol handling |
| MCP router | 18 | OpenAPI search |
| MCP tools | 31 | Core 5 tools |
| WebAPI state_memory | ~15 | Memory endpoints |
| CLI processor | ~40 | Command parsing |
| GDB protocol | ~20 | RSP packets |

---

## Appendix: File Locations

| Module | Implementation | Documentation |
|:-------|:---------------|:--------------|
| WebAPI | `core/automation/webapi/src/api/` | `docs/emulator/design/control-interfaces/webapi-interface.md` |
| MCP | `core/automation/mcp/src/` | `core/automation/mcp/README.md` |
| CLI | `core/automation/cli/src/commands/` | `docs/emulator/design/control-interfaces/cli-interface.md` |
| Lua | `core/automation/lua/src/` | `docs/emulator/design/control-interfaces/lua-interface.md` |
| Python | `core/automation/python/src/` | `docs/emulator/design/control-interfaces/python-interface.md` |
| GDB | `core/automation/gdb/src/` | `core/automation/gdb/README.md` |
