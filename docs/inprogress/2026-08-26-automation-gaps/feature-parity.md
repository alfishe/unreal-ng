# Automation Feature Parity Matrix

Analysis date: 2026-08-26

## Legend

- ✅ Implemented and working
- ⚠️ Partial / undocumented
- ❌ Missing
- 🔧 Needs verification

## Core Debugging

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **Registers read** | ✅ | ✅ | ✅ | ✅ | - |
| **Registers write** | ❌ | ❌ | ❌ | ❌ | **P0** |
| **Memory read** | ✅ | ✅ | ✅ | ✅ | - |
| **Memory write** | ✅ | ✅ | ✅ | ✅ | - |
| **Bank-aware read/write** | ✅ | ✅ | ⚠️ | ⚠️ | P1 |
| **Disassembly** | ✅ | ✅ | ✅ | ✅ | - |
| **Symbolic disassembly** | ❌ | ❌ | ❌ | ❌ | **P0** |

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
| **Step over** | ✅ | ✅ | ⚠️ | ⚠️ | P2 |
| **Run to address** | ✅ | ✅ | ✅ | ✅ | - |
| **run_frame** | ✅ | ✅ | ✅ | ❌ | **P0** |
| **run_frames(n)** | ✅ | ✅ | ✅ | ❌ | **P0** |
| **run_tstates** | ✅ | ✅ | ✅ | ❌ | **P0** |
| **run_to_scanline** | ✅ | ✅ | ⚠️ | ❌ | **P0** |
| **run_to_pixel** | ✅ | ✅ | ⚠️ | ❌ | **P0** |
| **run_until_condition** | ✅ | ⚠️ | ❌ | ❌ | P1 |

## TTD (Time-Travel Debugging)

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **ttd_start/stop** | ✅ | ✅ | ✅ | ✅ | - |
| **ttd_reverse_step** | ✅ | ✅ | ✅ | ✅ | - |
| **ttd_seek** | ✅ | ✅ | ✅ | ✅ | - |
| **ttd_find_last** | ✅ | ✅ | ⚠️ | ⚠️ | P2 |
| **ttd_markers** | ✅ | ✅ | ⚠️ | ⚠️ | P2 |

## Profiling & Analysis

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **Profiler** | ✅ | ✅ | ❌ | ⚠️ | P1 |
| **Memory counters** | ✅ | ✅ | ⚠️ | ⚠️ | P1 |
| **Call trace** | ✅ | ✅ | ❌ | ⚠️ | P2 |
| **Code/data map** | ✅ | ✅ | ❌ | ❌ | P2 |
| **Analyzer** | ✅ | ✅ | ❌ | ⚠️ | P2 |

## Screen & Capture

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **screen_get_mode** | ✅ | ✅ | ✅ | ✅ | - |
| **capture_screen (file)** | ✅ | ✅ | ❌ | ✅ | P1 |
| **get_framebuffer (raw)** | ❌ | ❌ | ❌ | ❌ | **P0** |
| **get_beam_position** | ❌ | ❌ | ❌ | ❌ | P1 |

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
| **symbols load** | ❌ | ❌ | ❌ | ❌ | **P0** |
| **symbols list** | ❌ | ❌ | ❌ | ❌ | **P0** |
| **symbols lookup** | ❌ | ❌ | ❌ | ❌ | **P0** |
| **sjasmplus .sld** | ❌ | ❌ | ❌ | ❌ | **P0** |
| **sjasmplus .lst** | ❌ | ❌ | ❌ | ❌ | P1 |
| **z88dk .map** | ❌ | ❌ | ❌ | ❌ | P2 |

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
| **MCP (Claude)** | ❌ | ❌ | - | - | P2 |

## Infrastructure

| Feature | CLI | WebAPI | Lua | Python | Priority |
|---------|-----|--------|-----|--------|----------|
| **JSON output** | N/A | ✅ | - | - | - |
| **version/capabilities** | ⚠️ | ⚠️ | ❌ | ❌ | P1 |
| **schema introspection** | N/A | ⚠️ | ❌ | ❌ | P1 |
| **batch/atomic** | ❌ | ✅ | - | - | P3 |
| **unified address syntax** | ❌ | ❌ | ❌ | ❌ | P2 |

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
| **P0** | 15 | Blocking adoption, must fix first |
| P1 | 18 | Important for daily workflow |
| P2 | 14 | Nice to have, quality of life |
