# Automation Feature Parity Matrix

Analysis date: 2026-08-26 · Last updated: 2026-09-06

> **2026-09-06 update**: the Phase-2 automation surface (step out, skip-until,
> memory search, screen digest, beam position, frame cost, coverage, AY log,
> audio capture, video recording, assembler, source listings, label resolve)
> landed on **all four interfaces** (CLI, WebAPI, Lua, Python), closing most of
> the original P0 gaps. The MCP server exposes the same capabilities through
> smart tools on a loopback WebAPI transport (Streamable HTTP :8092 with SSE
> progress, plus a zero-dependency stdio bridge) rather than endpoint parity —
> see [docs/features/mcp/README.md](../../features/mcp/README.md).

## Legend

- ✅ Implemented and working
- ⚠️ Partial / undocumented
- 🔧 Needs verification
- ❌ Missing

## Core Debugging

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **Registers read** | ✅ | ✅ | ✅ | ✅ | - |
| **Registers write** | ❌ | ✅ | ✅ | ✅ | P1 (CLI only) |
| **Memory read** | ✅ | ✅ | ✅ | ✅ | - |
| **Memory write** | ✅ | ✅ | ✅ | ✅ | - |
| **Bank-aware read/write** | ✅ | ✅ | ✅ | ✅ | - |
| **Memory search (find)** | ✅ | ✅ | ✅ | ✅ | - |
| **Disassembly** | ✅ | ✅ | ✅ | ✅ | - |
| **Symbolic disassembly (labels in disasm)** | ❌ | ✅ | ❌ | ❌ | P1 |

## Breakpoints

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **Execution BP** | ✅ | ✅ | ✅ | ✅ | - |
| **Memory BP (read/write)** | ✅ | ✅ | ✅ | ✅ | - |
| **Port BP** | ✅ | ✅ | ⚠️ | ⚠️ | P2 |
| **Bank-aware BP** | ✅ | ✅ | ⚠️ | ⚠️ | P1 |
| **BP groups** | ✅ | ✅ | ❌ | ❌ | P2 |
| **Conditional BP** | ❌ | ❌ | ❌ | ❌ | **P0** |
| **Hit count** | ❌ | ❌ | ❌ | ❌ | **P0** |
| **BP actions (log/dump/script)** | ❌ | ❌ | ❌ | ❌ | P1 |

## Run Control

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **Run/Pause/Stop** | ✅ | ✅ | ✅ | ✅ | - |
| **Step (instruction)** | ✅ | ✅ | ✅ | ✅ | - |
| **Step over** | ✅ | ✅ | ✅ | ✅ | - |
| **Step out** | ✅ | ✅ | ✅ | ✅ | - |
| **Skip until PC (skip_until)** | ✅ | ✅ | ✅ | ✅ | - |
| **Run to address** | ✅ | ✅ | ✅ | ✅ | - |
| **run_frame** | ✅ | ✅ | ✅ | ✅ | - |
| **run_frames(n)** | ✅ | ✅ | ✅ | ✅ | - |
| **run_tstates** | ✅ | ✅ | ✅ | ✅ | - |
| **run_to_scanline** | ✅ | ✅ | ✅ | ✅ | - |
| **run_to_pixel** | ✅ | ✅ | ✅ | ✅ | - |
| **run_until_condition (generic)** | ❌ | ⚠️ | ❌ | ❌ | P2 (skip_until covers the common case) |

## TTD (Time-Travel Debugging)

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **ttd_start/stop** | ✅ | ✅ | ✅ | ✅ | - |
| **ttd_reverse_step** | ✅ | ✅ | ✅ | ✅ | - |
| **ttd_seek** | ✅ | ✅ | ✅ | ✅ | - |
| **ttd_find_last** | ✅ | ✅ | ⚠️ | ⚠️ | P2 |
| **ttd_markers** | ✅ | ✅ | ⚠️ | ⚠️ | P2 |
| **ttd dump/load (session files)** | ✅ | ✅ | ❌ | ❌ | P2 |

## Profiling & Analysis

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **Profiler** | ✅ | ✅ | ❌ | ⚠️ | P1 |
| **Memory counters** | ✅ | ✅ | ⚠️ | ⚠️ | P1 |
| **Call trace** | ✅ | ✅ | ❌ | ⚠️ | P2 |
| **Code/data map** | ✅ | ✅ | ❌ | ❌ | P2 |
| **Analyzer manager** | ✅ | ✅ | ❌ | ⚠️ | P2 |
| **Coverage analyzer** | ✅ | ✅ | ✅ | ✅ | - |
| **AY register log** | ✅ | ✅ | ✅ | ✅ | - |
| **Frame cost accounting** | ✅ | ✅ | ✅ | ✅ | - |

## Screen & Capture

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **screen_get_mode** | ✅ | ✅ | ✅ | ✅ | - |
| **capture_screen (file)** | ✅ | ✅ | ❌ | ✅ | P1 |
| **get_framebuffer (raw)** | ❌ | ❌ | ❌ | ❌ | **P0** |
| **get_beam_position** | ✅ | ✅ | ✅ | ✅ | - |
| **screen digest (change detection)** | ✅ | ✅ | ✅ | ✅ | - |
| **Audio capture (WAV export)** | ✅ | ✅ | ✅ | ✅ | - |
| **Video recording** | ✅ ¹ | ✅ | ✅ ¹ | ✅ ¹ | - |

¹ Requires a build with `ENABLE_RECORDING`; the interfaces report a clean
error when compiled out.

## Assembler & Source Listings

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **assemble (in-place Z80 assembly)** | ✅ | ✅ | ✅ | ✅ | - |
| **listing load (.lst)** | ✅ | ✅ | ✅ | ✅ | - |
| **listing source_at** | ✅ | ✅ | ✅ | ✅ | - |
| **listing step_line** | ✅ | ✅ | ✅ | ✅ | - |
| **listing run_to_line** | ✅ | ✅ | ✅ | ✅ | - |

## Input Simulation

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **key_press/release** | ✅ | ✅ | ❌ | ✅ | P1 |
| **key_macro** | ✅ | ✅ | ❌ | ✅ | P2 |
| **key_type** | ✅ | ✅ | ❌ | ✅ | P2 |
| **trdos_command** | ✅ | ✅ | ❌ | ✅ | P2 |

## Symbols & Labels

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **symbols load** | ✅ | ✅ | ✅ | ✅ | - |
| **symbols save** | ✅ | ✅ | ✅ | ✅ | - |
| **symbols list/lookup** | ✅ | ✅ | ✅ | ✅ | - |
| **sjasmplus .sld** | ✅ | ✅ | ✅ | ✅ | - |
| **sjasmplus .lst (listings)** | ✅ | ✅ | ✅ | ✅ | - |
| **z88dk .map** | ❌ | ❌ | ❌ | ❌ | P2 |
| **label resolve (name ↔ address + context)** | ✅ | ✅ | ✅ | ✅ | - |

## Events & Subscriptions

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **subscribe breakpoint** | ❌ | ❌ | ❌ | ❌ | **P0** |
| **subscribe frame** | ❌ | ❌ | ❌ | ❌ | P1 |
| **subscribe port** | ❌ | ❌ | ❌ | ❌ | P2 |
| **on_breakpoint callback** | ❌ | ❌ | ❌ | ❌ | **P0** |
| **Instance ID in events** | ❌ | ❌ | ❌ | ❌ | **P0** |

## Protocol Bridges

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **GDB RSP** | ❌ | ❌ | - | - | **P0** |
| **DZRP (DeZog)** | ❌ | ❌ | - | - | P1 |
| **MCP (Claude)** | - | - | - | - | ✅ implemented (Streamable HTTP + stdio bridge, smart tools over WebAPI — [docs](../../features/mcp/README.md)) |

## Infrastructure

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **JSON output** | N/A | ✅ | - | - | - |
| **version/capabilities** | ⚠️ | ⚠️ | ❌ | ❌ | P1 |
| **schema introspection** | N/A | ✅ | ❌ | ❌ | P1 |
| **batch/atomic** | ❌ | ✅ | - | - | P3 |
| **unified address syntax** | ❌ | ❌ | ❌ | ❌ | P2 |
| **OpenAPI manifest** | - | ✅ | - | - | - (complete, `/api/v1/openapi.json`) |

## Instance Management

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **emu_create** | ✅ | ✅ | ✅ | ✅ | - |
| **emu_select** | ✅ | ✅ | ⚠️ | ❌ | P1 |
| **emu_destroy** | ✅ | ✅ | ✅ | ✅ | - |
| **run-control ownership** | ❌ | ❌ | ❌ | ❌ | P1 |

## Determinism & Recording

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **RZX record** | ❌ | ❌ | ❌ | ❌ | P2 |
| **RZX playback** | ❌ | ❌ | ❌ | ❌ | P2 |
| **Headless deterministic** | ❌ | ❌ | ❌ | ❌ | P1 |

## Summary

| Priority | Count | Description |
|----------|-------|-------------|
| **P0** | 6 | Blocking adoption: conditional BP, hit count, framebuffer access, event subscriptions (3) |
| P1 | 12 | Important for daily workflow (registers write in CLI, symbolic disasm outside WebAPI, ...) |
| P2 | 14 | Nice to have, quality of life |
| P3 | 1 | Deferred |

Compared to the 2026-08-26 snapshot (15 × P0): registers write, memory
search, symbolic data tooling (symbols/sld/lst), run_* completeness for
Python, and the entire analysis/capture/assembly family moved out of the gap
list; MCP shipped as its own surface.
