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

### Option C: GDB RSP (Current Implementation)

Extend existing GDB server for DeZog compatibility.

**Pros:**
- Already implemented
- Works with GDB, Ghidra

**Cons:**
- DeZog's GDB support is limited (MAME gdbstub only)
- Missing Z80-specific features
- Protocol mismatch issues discovered

### Recommendation

**Option A (DZRP)** is recommended because:
1. DeZog is the primary Z80 debugger for VS Code
2. Full feature support including reverse debugging
3. Simpler than DAP (DZRP is Z80-focused)
4. Complements existing GDB server (different use cases)

## DZRP Protocol Specification

### Transport Layer

- TCP socket connection (default port: 12000)
- Binary protocol with length-prefixed messages
- Little-endian byte order

### Message Format

```
┌─────────────┬─────────────┬─────────────────────────┐
│ Length (4B) │ SeqNo (4B)  │ Payload (variable)      │
└─────────────┴─────────────┴─────────────────────────┘

Payload:
┌─────────────┬─────────────────────────────────────┐
│ Command (1B)│ Data (variable)                     │
└─────────────┴─────────────────────────────────────┘
```

### Core Commands

| Command | Value | Description |
|---------|-------|-------------|
| CMD_INIT | 0x01 | Initialize connection |
| CMD_CLOSE | 0x02 | Close connection |
| CMD_GET_REGISTERS | 0x03 | Read Z80 registers |
| CMD_SET_REGISTER | 0x04 | Write single register |
| CMD_WRITE_BANK | 0x05 | Write memory bank |
| CMD_CONTINUE | 0x06 | Continue execution |
| CMD_PAUSE | 0x07 | Pause execution |
| CMD_ADD_BREAKPOINT | 0x08 | Add breakpoint |
| CMD_REMOVE_BREAKPOINT | 0x09 | Remove breakpoint |
| CMD_ADD_WATCHPOINT | 0x0A | Add memory watchpoint |
| CMD_REMOVE_WATCHPOINT | 0x0B | Remove watchpoint |
| CMD_READ_MEM | 0x0C | Read memory |
| CMD_WRITE_MEM | 0x0D | Write memory |
| CMD_GET_SLOTS | 0x0E | Get memory slot configuration |
| CMD_READ_STATE | 0x0F | Read emulator state (for reverse debug) |
| CMD_WRITE_STATE | 0x10 | Restore emulator state |
| CMD_GET_TBBLUE_REG | 0x11 | ZX Next specific |
| CMD_GET_SPRITES_PALETTE | 0x12 | ZX Next specific |
| CMD_GET_SPRITES | 0x13 | ZX Next specific |
| CMD_GET_SPRITE_PATTERNS | 0x14 | ZX Next specific |
| CMD_GET_SPRITES_CLIP_WINDOW | 0x15 | ZX Next specific |
| CMD_SET_BORDER | 0x16 | Set border color |

### Notification Events

| Event | Value | Description |
|-------|-------|-------------|
| NTF_PAUSE | 0x01 | Execution paused (breakpoint hit) |

## Architecture

### Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     Unreal-NG Emulator                       │
│  ┌─────────────────────────────────────────────────────┐    │
│  │                  Automation Layer                    │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │    │
│  │  │  GDB Server  │  │ DZRP Server  │  │  Web API  │  │    │
│  │  │  (port 1234) │  │ (port 12000) │  │           │  │    │
│  │  └──────┬───────┘  └──────┬───────┘  └───────────┘  │    │
│  │         │                 │                          │    │
│  │         └────────┬────────┘                          │    │
│  │                  ▼                                   │    │
│  │         ┌──────────────────┐                         │    │
│  │         │  Debug Interface │                         │    │
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

```json
{
  "type": "dezog",
  "request": "launch",
  "name": "Unreal-NG Debug",
  "remoteType": "dzrp",
  "dzrp": {
    "hostname": "localhost",
    "port": 12000
  },
  "sjasmplus": [{
    "path": "main.sld"
  }],
  "rootFolder": "${workspaceFolder}",
  "topOfStack": "stack_top"
}
```

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
