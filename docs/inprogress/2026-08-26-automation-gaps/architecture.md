# Automation Architecture

## Current State

```
┌─────────────────────────────────────────────────────────────┐
│                      Emulator Core                          │
│  (Z80, Memory, IO, TTD, LabelManager, Breakpoints, etc.)   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Automation Layer                         │
│         (No common base class, no command registry)         │
└─────────────────────────────────────────────────────────────┘
        │              │              │              │
        ▼              ▼              ▼              ▼
   ┌────────┐    ┌─────────┐    ┌────────┐    ┌─────────┐
   │  CLI   │    │ WebAPI  │    │  Lua   │    │ Python  │
   │ telnet │    │ Drogon  │    │  sol2  │    │pybind11 │
   │ :3333  │    │ :8090   │    │embedded│    │embedded │
   └────────┘    └─────────┘    └────────┘    └─────────┘
```

**Problems**:
- Each transport duplicates command parsing logic
- Adding a command requires changes in 4+ places
- No shared command registry or schema
- Parity drift is inevitable

## Target State

```
┌─────────────────────────────────────────────────────────────┐
│                      Emulator Core                          │
│  (Z80, Memory, IO, TTD, LabelManager, Breakpoints, etc.)   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   Automation Core API                       │
│     (C++ interface: commands, events, subscriptions)        │
│     (Single command registry with schema)                   │
└─────────────────────────────────────────────────────────────┘
                              │
         ┌────────────────────┼────────────────────┐
         │                    │                    │
         ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│   Transports    │  │Protocol Bridges │  │   Scripting     │
│   (Frontends)   │  │   (Standalone)  │  │  (Embedded)     │
├─────────────────┤  ├─────────────────┤  ├─────────────────┤
│ • CLI/telnet    │  │ • GDB RSP       │  │ • Lua           │
│ • WebAPI/REST   │  │ • DZRP (DeZog)  │  │ • Python        │
│ • WebSocket     │  │ • MCP (Claude)  │  │                 │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

## Module Classification

### Transports (Human-Facing)
Interactive interfaces for direct user interaction.

| Module | Port | Purpose | Users |
|--------|------|---------|-------|
| CLI/telnet | 3333 | Interactive debugging | Developers |
| WebAPI/REST | 8090 | HTTP automation | Scripts, CI |
| WebSocket | 8090 | Real-time events | Web UIs |

### Protocol Bridges (Tool-Facing)
Standalone servers implementing standard debug protocols. **Not integrated into CLI/WebAPI** — separate processes or threads with own lifecycle.

| Module | Port | Protocol | Purpose | Users |
|--------|------|----------|---------|-------|
| GDB Server | 1234 | GDB RSP | Standard debugger protocol | GDB, LLDB |
| DZRP Server | 12000 | DZRP | DeZog Remote Protocol | DeZog/VS Code |
| MCP Server | stdio | MCP | Model Context Protocol | Claude, AI agents |

### Scripting (Embedded)
In-process scripting for hot-path callbacks and complex automation.

| Module | Purpose | Justification |
|--------|---------|---------------|
| Lua | Per-instruction hooks | Zero IPC overhead |
| Python | Complex analysis | Rich ecosystem |

## Protocol Bridges Detail

### GDB RSP Server
**Location**: `core/automation/gdb/`  
**Protocol**: GDB Remote Serial Protocol  
**Docs**: https://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html

```
┌──────────┐         ┌─────────────┐         ┌──────────────┐
│   GDB    │◄───────►│ GDB Server  │◄───────►│ Automation   │
│  client  │  RSP    │  (unreal)   │  C++ API│    Core      │
└──────────┘         └─────────────┘         └──────────────┘
```

Required packets:
- `g` / `G` — read/write registers
- `m` / `M` — read/write memory
- `c` / `s` — continue/step
- `Z` / `z` — breakpoints
- `?` — stop reason
- `qSupported` — capabilities

### DZRP Server (DeZog)
**Location**: `core/automation/dzrp/`  
**Protocol**: DeZog Remote Protocol  
**Docs**: https://github.com/maziac/DeZog/blob/main/design/DeZogProtocol.md

```
┌──────────┐         ┌─────────────┐         ┌──────────────┐
│  DeZog   │◄───────►│ DZRP Server │◄───────►│ Automation   │
│ (VS Code)│  DZRP   │  (unreal)   │  C++ API│    Core      │
└──────────┘         └─────────────┘         └──────────────┘
```

DZRP advantages over GDB RSP:
- Z80-native (IM, IFF, banks)
- Bank-aware memory access
- Condition expressions
- Slot/bank mapping

### MCP Server (Claude)
**Location**: `core/automation/mcp/`  
**Protocol**: Model Context Protocol  
**Docs**: https://modelcontextprotocol.io/

```
┌──────────┐         ┌─────────────┐         ┌──────────────┐
│  Claude  │◄───────►│ MCP Server  │◄───────►│ Automation   │
│   Code   │  stdio  │  (unreal)   │  C++ API│    Core      │
└──────────┘         └─────────────┘         └──────────────┘
```

MCP tools to expose:
- `debug_step`, `debug_continue`, `debug_pause`
- `memory_read`, `memory_write`
- `registers_get`, `registers_set`
- `breakpoint_set`, `breakpoint_clear`
- `disassemble`
- `symbols_load`, `symbols_lookup`
- `screen_capture`
- `ttd_*` (time-travel debugging)

## Implementation Notes

### Each Protocol Bridge Is:
- **Standalone module** with own CMake target
- **Optional** — can be disabled at build time
- **Independent lifecycle** — start/stop separately from emulator
- **Uses Automation Core API** — no direct core access

### Automation Core Must Provide:
1. **Command registry** with schema (for MCP tool generation)
2. **Event subscriptions** with instance ID
3. **Run-control ownership** (advisory lock)
4. **Thread-safe API** (bridges may run in separate threads)

### Build Configuration
```cmake
option(ENABLE_GDB_SERVER "Build GDB RSP server" ON)
option(ENABLE_DZRP_SERVER "Build DeZog DZRP server" ON)
option(ENABLE_MCP_SERVER "Build MCP server for AI agents" ON)
```

## Priority

| Module | Priority | Rationale |
|--------|----------|-----------|
| GDB RSP | P1 | Standard protocol, broad tool support |
| DZRP | P1 | VS Code/DeZog is the modern ZX dev workflow |
| MCP | P2 | AI-assisted debugging, growing ecosystem |

## Dependencies

```
Automation Core API
    │
    ├── Command Registry ──────► MCP (tool schema)
    │
    ├── Register Read/Write ───► GDB (g/G packets)
    │                          ► DZRP (register commands)
    │
    ├── Event Subscriptions ───► All (stop notifications)
    │
    └── Run-Control Lock ──────► All (ownership claim)
```
