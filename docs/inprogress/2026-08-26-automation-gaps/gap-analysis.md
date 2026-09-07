# Automation API Gap Analysis

Analysis date: 2026-08-26

## Executive Summary

The automation layer has four transports (CLI, WebAPI, Lua, Python) with ~70 CLI commands but critical gaps that block adoption by the primary audiences: demosceners, reverse engineers, and tool authors.

## Current State

| Transport | LOC | Commands/Bindings | Maturity |
|-----------|-----|-------------------|----------|
| CLI/telnet | ~11k | ~70 commands | Most complete |
| WebAPI (Drogon) | - | REST + WebSocket | Near CLI parity |
| Lua (sol2) | - | ~90 bindings | Incomplete |
| Python (pybind11) | - | ~140 bindings | Missing run control |

## Critical Gaps (Ranked by Impact)

### 1. No Symbol Support
**Impact**: Blocks sjasmplus/DeZog workflow entirely

`LabelManager` exists in core but is not exposed to automation. Missing:
- `symbols load <file>` command
- sjasmplus format support (.sld, .lst, .map)
- Symbolic disassembly output
- `bp main.loop` instead of `bp 0x8A3F`

**Why it matters**: Demosceners work with labels, not hex addresses. Without this, they stay in their current emulator.

### 2. No Register Writes
**Impact**: Blocks unit testing, fault injection, GDB support

Confirmed by grep: no register setters anywhere. Blocks:
- Unit testing procedures (set regs → CALL → verify)
- `CMD_SET_REGISTER` / GDB `G` packet
- Manual state rollback
- Fault injection scenarios

### 3. No Event Model (Polling Only)
**Impact**: Blocks efficient debugging, wastes CPU

No subscriptions, callbacks, or push channels. Required:
- `subscribe breakpoint|frame|scanline|port|state` with async delivery
- WebSocket already exists but unused for events
- `on_breakpoint(fn)` in Lua/Python
- **Known bug**: `NC_EXECUTION_BREAKPOINT` lacks instance ID, breaks multi-instance

### 4. No GDB RSP / DZRP Protocol
**Impact**: No VS Code / DeZog integration

Modern ZX dev uses sjasmplus + DeZog + CSpect/ZEsarUX. Without protocol bridge, unreal-ng doesn't enter the daily workflow. Design docs exist — highest ROI per line of code.

### 5. No Conditional Breakpoints
**Impact**: Breakpoints are stop-hammers, not instruments

Missing:
- Hit count (`break after 1000 hits`)
- Conditions (`stop if A==0x7F`)
- Actions (`log and continue`, `dump`, `script`)

### 6. No Expressions / Watches
**Impact**: Manual hex parsing on every stop

`mem` outputs hex dump. Missing:
- Typed watches: `watch word 0x5C78`, `watch ptr HL`
- Register/memory arithmetic
- Snapshot of expressions on each stop

### 7. No Framebuffer Access from Scripts
**Impact**: No pixel-perfect regression testing

`screen_get_*` returns metadata only. `capture_screen` exists only in Python. Missing:
- Raw framebuffer access
- Beam position
- Docs promise `emu.get_screen_buffer()` as numpy — not in code

### 8. Broken Transport Parity
**Impact**: Documentation lies, users hit walls

**Python missing**: `run_frame`, `run_frames`, `run_tstates`, `run_to_scanline`, `run_to_pixel`, `run_until_condition` — exactly the RL example commands

**Lua missing**: keyboard, capture, analyzers, profilers

Docs show "✅ everywhere" — this is false. Fix: generate all four facades from single command spec.

### 9. No Schema Introspection (WebAPI)
**Impact**: MCP/tooling can't auto-discover commands

WebAPI needs:
- `commands` endpoint with argument schema (needed for MCP layer)
- Stable error codes
- `version` / `capabilities`

CLI stays human-readable — scripts needing structured output use WebAPI.

### 10. No Determinism Contract / Input Recording
**Impact**: Regression tests unreliable

Missing:
- RZX-like recording and replay
- Headless mode with "same input → same frame" guarantee

### 11. No Run-Control Ownership
**Impact**: Race conditions in multi-client scenarios

From GDB TDD: no advisory-claim. Qt menu or WebAPI can resume a target the debugger is holding. Multi-instance + multiple transports = guaranteed flaky bugs.

### 12. No CLI Atomicity (Low Priority)
**Impact**: Read-modify-step races on running emulator

REST has `/batch`, CLI does not. Most automation scripts should use WebAPI for batch operations. CLI atomicity is nice-to-have.

### 13. No Unified Address Syntax
**Impact**: Inconsistent UX across commands

Bank-aware breakpoints exist but no single notation (`5:3FFF`, `ROM:1234`, `main.loop+3`) across all commands.

## Documentation Discrepancies

| Issue | Location |
|-------|----------|
| Port 8765 vs 3333 | cli/README.md vs docs/features/automation.md — resolved 2026-09-05: all docs unified on 8765 |
| Command count mismatch | ~10 listed vs ~70 actual |
| Feature matrix false positives | Checkmarks for missing features |

## Low-Value Features (Deprioritize)

| Feature | Why Low Value |
|---------|---------------|
| videowall singlesync | 1-2 users (party stands) |
| OCR / rom-print analyzer | Only works with stock ROM font; demos/games use custom |
| Lua (current state) | Strictly less than Python, no hot-path callbacks |
| Embedded Python build | Massive cost, 90% covered by external Python over REST |
| disk_read_sector_hex | Host tools work better on host files |
