# Protocol Comparison: DZRP vs DAP

## Quick Answer

**Does DeZog integration cover generic VS Code debug protocol?**

**No.** DeZog uses its own DZRP protocol, not the standard Debug Adapter Protocol (DAP). However:

| Protocol | VS Code | Other IDEs | Z80 Features |
|----------|---------|------------|--------------|
| **DZRP** | Via DeZog only | No | Full (reverse debug, banking) |
| **DAP** | Native | Neovim, Emacs, JetBrains | Basic (would need custom) |
| **GDB RSP** | Via CodeLLDB | Any GDB frontend | Basic |

## Architecture Comparison

### Option 1: DZRP (DeZog-specific)

```
VS Code ←→ DeZog Extension ←→ DZRP (binary) ←→ Unreal-NG
                                 port 12000
```

- **Pros:** Full Z80 feature support, reverse debugging, memory banking
- **Cons:** DeZog-only, not usable with generic DAP clients

### Option 2: DAP (Generic)

```
VS Code ←→ DAP (JSON) ←→ Unreal-NG Debug Adapter ←→ Emulator
           stdin/stdout or TCP
```

- **Pros:** Works with any DAP IDE, no extension needed
- **Cons:** More implementation work, custom Z80 features require extension

### Option 3: Both (Maximum Compatibility)

```
                         ┌─→ DZRP Server ──→ DeZog users
VS Code ─┬─→ DeZog Ext ──┤
         │               └─→ (fallback)
         │
         └─→ DAP Server ──────────────────→ Generic DAP users
         
Other IDEs ─→ DAP Server ─────────────────→ Neovim, Emacs, etc.
```

## Protocol Details

### DZRP (DeZog Remote Protocol)

| Aspect | Details |
|--------|---------|
| Transport | TCP socket |
| Encoding | Binary, little-endian |
| Framing | 4-byte length prefix |
| Auth | None (localhost only) |
| Features | Z80-specific (banking, sprites, Next regs) |

### DAP (Debug Adapter Protocol)

| Aspect | Details |
|--------|---------|
| Transport | stdin/stdout or TCP |
| Encoding | JSON |
| Framing | HTTP-style headers (`Content-Length: N`) |
| Auth | None |
| Features | Generic (extensible via custom requests) |

## Feature Matrix

| Feature | DZRP | DAP | GDB RSP |
|---------|------|-----|---------|
| Register read/write | ✓ | ✓ | ✓ |
| Memory read/write | ✓ | ✓ | ✓ |
| Breakpoints | ✓ | ✓ | ✓ |
| Watchpoints | ✓ | ✓ | ✓ |
| Step into/over/out | ✓ | ✓ | ✓ |
| Call stack | ✓ | ✓ | ✓ |
| **ZX Spectrum Banking** | ✓ | Custom | Partial |
| **Reverse debugging** | ✓ | Custom | No |
| **State save/restore** | ✓ | Custom | No |
| **Sprites/Next regs** | ✓ | No | No |

## Recommendation

### For Z80/ZX Spectrum Development: DZRP

DeZog is the dominant Z80 debugger for VS Code. Its features (reverse debugging, banking, SLD labels) are specifically designed for retro development.

### For Maximum IDE Support: Add DAP Later

If users need Neovim, Emacs, or other IDE support, implement a DAP server as a second phase. The debug interface can be shared.

### Implementation Priority

1. **DZRP** (Phase 1) — serves primary audience
2. **GDB** (Done) — serves Ghidra, command-line users  
3. **DAP** (Future) — serves other IDEs

## Code Reuse

The internal debug interface serves all protocols:

```cpp
// Shared by GDB, DZRP, and future DAP
class IDebugInterface {
    void pause();
    void resume();
    Z80Registers getRegisters();
    void readMemory(addr, len);
    void addBreakpoint(addr);
    // ...
};

// Protocol-specific servers
class GDBServer    { IDebugInterface* debug; /* GDB RSP */ };
class DZRPServer   { IDebugInterface* debug; /* DZRP binary */ };
class DAPServer    { IDebugInterface* debug; /* DAP JSON */ };  // future
```

## Summary

| Question | Answer |
|----------|--------|
| Does DZRP cover generic DAP? | No, separate protocols |
| Should we implement DAP? | Not initially; DeZog covers VS Code |
| Can we add DAP later? | Yes, same debug interface |
| Recommended approach? | DZRP first, DAP as needed |
