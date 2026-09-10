# DeZog Integration Design

## Overview

This document outlines the integration of DeZog debugger with the Unreal-NG ZX Spectrum emulator, providing VS Code debugging capabilities for Z80 assembly development.

## Background

### What is DeZog?

[DeZog](https://github.com/maziac/DeZog) is a Visual Studio Code extension for debugging Z80 assembler programs. It provides:
- Source-level debugging with labels and symbols
- Register and memory views
- Breakpoints, watchpoints, step execution
- Call stack visualization
- Reverse debugging (time travel)
- Code coverage

### Protocol Stack

```
┌─────────────────────────────────────────────────────────────┐
│                    VS Code UI                                │
├─────────────────────────────────────────────────────────────┤
│              Debug Adapter Protocol (DAP)                    │
│         JSON messages over stdin/stdout                      │
├─────────────────────────────────────────────────────────────┤
│                    DeZog Extension                           │
├──────────────────────┬──────────────────────────────────────┤
│   DZRP (socket)      │   ZRCP (ZEsarUX)  │  GDB RSP (MAME)  │
├──────────────────────┼──────────────────────────────────────┤
│   CSpect Plugin      │   ZEsarUX         │  MAME gdbstub    │
│   ZX Next HW         │                   │                  │
│   Internal Sim       │                   │                  │
└──────────────────────┴──────────────────────────────────────┘
```

## Integration Options

### Option A: DZRP Implementation (Recommended)

Implement DeZog's native protocol (DZRP) for direct integration.

**Pros:**
- Full feature support (reverse debugging, code coverage)
- Native integration, best user experience
- Maintained protocol with active development

**Cons:**
- DeZog-specific, won't work with other DAP clients
- Need to track DZRP protocol changes

### Option B: DAP Server Implementation

Implement Debug Adapter Protocol directly in the emulator.

**Pros:**
- Works with any DAP-compatible IDE (VS Code, Neovim, Emacs)
- Standard protocol (v1.71.0 specification)
- IDE-agnostic

**Cons:**
- More complex implementation
- Doesn't leverage DeZog's Z80-specific features
- Would duplicate DeZog's functionality

### Option C: GDB RSP (Future)

Implement GDB Remote Serial Protocol for Ghidra/GDB compatibility.

**Pros:**
- Works with GDB, Ghidra, any GDB frontend
- Industry-standard protocol

**Cons:**
- DeZog's GDB support is limited (MAME gdbstub only)
- Missing Z80-specific features
- Not yet implemented (ENABLE_GDB_AUTOMATION is a stub)

### Recommendation

**Option A (DZRP)** is recommended because:
1. DeZog is the primary Z80 debugger for VS Code
2. Full feature support including reverse debugging
3. Simpler than DAP (DZRP is Z80-focused)
4. Can share IDebugInterface with future GDB implementation

## DZRP Protocol Specification

### Transport Layer

- TCP socket connection (default port: 12000)
- Binary protocol with length-prefixed messages
- Little-endian byte order

### Message Format (DZRP 2.2.0)

```
┌─────────────┬─────────────┬─────────────────────────┐
│ Length (4B) │ SeqNo (1B)  │ Payload (variable)      │
└─────────────┴─────────────┴─────────────────────────┘

SeqNo: bits 0-3 = sequence (1-15), bit 7 = NAK flag

Payload:
┌─────────────┬─────────────────────────────────────┐
│ Command (1B)│ Data (variable)                     │
└─────────────┴─────────────────────────────────────┘
```

### Core Commands (DZRP 2.2.0)

| Command | ID | Description |
|---------|-----|-------------|
| CMD_INIT | 1 | Initialize connection |
| CMD_CLOSE | 2 | Close connection |
| CMD_GET_REGISTERS | 3 | Read Z80 registers + slots |
| CMD_SET_REGISTER | 4 | Write single register |
| CMD_WRITE_BANK | 5 | Write memory bank |
| CMD_CONTINUE | 6 | Continue execution |
| CMD_PAUSE | 7 | Pause execution |
| CMD_READ_MEM | 8 | Read memory |
| CMD_WRITE_MEM | 9 | Write memory |
| CMD_SET_SLOT | 10 | Configure memory slot |
| CMD_SET_BORDER | 12 | Set border color |
| CMD_GET_SUPPORTED_COMMANDS | 24 | Query capabilities |
| CMD_ADD_BREAKPOINT | 40 | Add breakpoint |
| CMD_REMOVE_BREAKPOINT | 41 | Remove breakpoint |
| CMD_ADD_WATCHPOINT | 42 | Add memory watchpoint |
| CMD_REMOVE_WATCHPOINT | 43 | Remove watchpoint |
| CMD_READ_STATE | 50 | Read emulator state (reverse debug) |
| CMD_WRITE_STATE | 51 | Restore emulator state |

Note: CMD_GET_SLOTS was removed in DZRP 2.0.0 - slot info now returned with CMD_GET_REGISTERS.

### Notification Events

| Event | ID | Description |
|-------|-----|-------------|
| NTF_PAUSE | 1 | Execution paused (breakpoint hit, manual, watchpoint) |

## Architecture

### Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     Unreal-NG Emulator                       │
│  ┌─────────────────────────────────────────────────────┐    │
│  │                  Automation Layer                    │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │    │
│  │  │ DZRP Server  │  │  (GDB stub)  │  │  Web API  │  │    │
│  │  │ (port 12000) │  │   (future)   │  │           │  │    │
│  │  └──────┬───────┘  └──────┬───────┘  └───────────┘  │    │
│  │         │                 │                          │    │
│  │         └────────┬────────┘                          │    │
│  │                  ▼                                   │    │
│  │         ┌──────────────────┐                         │    │
│  │         │  IDebugInterface │                         │    │
│  │         │  (shared logic)  │                         │    │
│  │         └────────┬─────────┘                         │    │
│  └──────────────────┼──────────────────────────────────┘    │
│                     ▼                                        │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                   Z80 CPU Core                        │   │
│  │  • Registers  • Memory  • Breakpoints  • State       │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### Shared Debug Interface

Both GDB and DZRP servers will use a common debug interface:

```cpp
class IDebugInterface
{
public:
    // Execution control
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void step() = 0;
    virtual void stepOver() = 0;
    
    // Registers
    virtual Z80Registers getRegisters() = 0;
    virtual void setRegister(Register reg, uint16_t value) = 0;
    
    // Memory
    virtual std::vector<uint8_t> readMemory(uint16_t addr, size_t len) = 0;
    virtual void writeMemory(uint16_t addr, const std::vector<uint8_t>& data) = 0;
    
    // Breakpoints
    virtual BreakpointId addBreakpoint(uint16_t addr) = 0;
    virtual void removeBreakpoint(BreakpointId id) = 0;
    virtual BreakpointId addWatchpoint(uint16_t addr, size_t len, WatchType type) = 0;
    
    // State (for reverse debugging)
    virtual EmulatorState captureState() = 0;
    virtual void restoreState(const EmulatorState& state) = 0;
    
    // Memory banking (ZX Spectrum specific)
    virtual std::vector<SlotInfo> getSlots() = 0;
    virtual void writeBank(uint8_t bank, const std::vector<uint8_t>& data) = 0;
    
    // Events
    virtual void setBreakHandler(std::function<void(BreakReason)> handler) = 0;
};
```

## Implementation Plan

### Phase 1: Core Protocol (Week 1-2)

1. **DZRP Message Parser**
   - Binary message framing
   - Command/response serialization
   - Sequence number tracking

2. **TCP Server**
   - Socket listener on port 12000
   - Connection management
   - Async message handling

3. **Basic Commands**
   - CMD_INIT / CMD_CLOSE
   - CMD_GET_REGISTERS / CMD_SET_REGISTER
   - CMD_READ_MEM / CMD_WRITE_MEM
   - CMD_CONTINUE / CMD_PAUSE

### Phase 2: Debugging Features (Week 3)

1. **Breakpoints**
   - CMD_ADD_BREAKPOINT / CMD_REMOVE_BREAKPOINT
   - NTF_PAUSE notification
   - Breakpoint hit reporting

2. **Watchpoints**
   - CMD_ADD_WATCHPOINT / CMD_REMOVE_WATCHPOINT
   - Read/write/both types
   - Memory range support

3. **Stepping**
   - Step into (single instruction)
   - Step over (skip CALLs)
   - Step out (run until RET)

### Phase 3: ZX Spectrum Features (Week 4)

1. **Memory Banking**
   - CMD_GET_SLOTS
   - CMD_WRITE_BANK
   - 128K memory paging

2. **State Capture**
   - CMD_READ_STATE / CMD_WRITE_STATE
   - Full emulator state serialization
   - Reverse debugging support

### Phase 4: Testing & Polish (Week 5)

1. **Integration Testing**
   - Test with actual DeZog extension
   - Verify all commands work
   - Performance optimization

2. **Documentation**
   - User guide
   - Configuration examples
   - Troubleshooting

## File Structure

```
core/automation/dezog/
├── CMakeLists.txt
├── include/
│   ├── dzrp-server.h
│   ├── dzrp-protocol.h
│   └── dzrp-commands.h
└── src/
    ├── dzrp-server.cpp
    ├── dzrp-protocol.cpp
    └── dzrp-commands.cpp
```

## Configuration

### Emulator Settings

```json
{
  "dezog": {
    "enabled": true,
    "port": 12000,
    "autoStart": false
  }
}
```

### DeZog launch.json

Note: DeZog uses `remoteType: "cspect"` for DZRP connections (CSpect uses the same protocol).

**Important:** CSpect remote defaults disable watchpoints (42/43) and state save/restore (50/51).
You must explicitly enable them via `supportedCommands` to use WPMEM/assertions and reverse debugging.

```json
{
  "type": "dezog",
  "request": "launch",
  "name": "Unreal-NG Debug",
  "remoteType": "cspect",
  "cspect": {
    "hostname": "localhost",
    "port": 12000,
    "supportedCommands": "1,2,3,4,5,6,7,8,9,10,12,24,40,41,42,43,50,51"
  },
  "sjasmplus": [{
    "path": "main.sld"
  }],
  "rootFolder": "${workspaceFolder}",
  "topOfStack": "stack_top"
}
```

Note: CMD_GET_SUPPORTED_COMMANDS (24) is implemented but DeZog's cspect remote
doesn't query it - capability negotiation is purely via launch.json config.

## VS Code DAP Comparison

For reference, here's how DZRP maps to standard DAP:

| DAP Request | DZRP Command |
|-------------|--------------|
| initialize | CMD_INIT |
| disconnect | CMD_CLOSE |
| threads | (single Z80 thread) |
| stackTrace | (via CMD_READ_MEM + SP) |
| scopes | CMD_GET_REGISTERS |
| variables | CMD_READ_MEM |
| setVariable | CMD_SET_REGISTER |
| continue | CMD_CONTINUE |
| pause | CMD_PAUSE |
| next | (step over via breakpoints) |
| stepIn | CMD_CONTINUE + single step |
| setBreakpoints | CMD_ADD/REMOVE_BREAKPOINT |
| evaluate | CMD_READ_MEM |

**Note:** Implementing DAP directly would require more work but would enable any DAP client. This could be a future enhancement.

## Security Considerations

- DZRP server binds to localhost only by default
- No authentication (trusted local connection)
- Optional: configurable bind address for remote debugging

## References

- [DeZog GitHub Repository](https://github.com/maziac/DeZog)
- [DeZog Protocol Documentation](https://github.com/maziac/DeZog/blob/main/design/DeZogProtocol.md)
- [Adding New Remotes to DeZog](https://github.com/maziac/DeZog/blob/main/design/AddingNewRemotes.md)
- [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
- [VS Code Debugger Extension Guide](https://code.visualstudio.com/api/extension-guides/debugger-extension)
