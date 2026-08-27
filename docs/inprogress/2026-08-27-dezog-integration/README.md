# DeZog Integration

VS Code debugging support for Unreal-NG ZX Spectrum emulator via DeZog extension.

## Documents

| Document | Description |
|----------|-------------|
| [design.md](design.md) | High-level architecture and approach |
| [dzrp-protocol-spec.md](dzrp-protocol-spec.md) | DZRP v2.2.0 byte-level specification |
| [module-design.md](module-design.md) | C++ class design and integration |
| [verification-plan.md](verification-plan.md) | Unit, integration, and E2E test plan |
| [action-plan.md](action-plan.md) | Implementation phases and task tracking |
| [protocol-comparison.md](protocol-comparison.md) | DZRP vs DAP vs GDB comparison |

## Quick Reference

| Aspect | Value |
|--------|-------|
| Protocol | DZRP v2.2.0 (binary, little-endian) |
| Port | 12000 |
| Mode | NORMAL_MODE (use ADD/REMOVE_BREAKPOINT) |
| Machine | ZX48K=2, ZX128K=3 |

## Commands to Implement

### Tier 1 (MVP)
CMD_INIT(1), CMD_CLOSE(2), CMD_GET_REGISTERS(3), CMD_SET_REGISTER(4), 
CMD_CONTINUE(6), CMD_PAUSE(7), CMD_READ_MEM(8), CMD_WRITE_MEM(9),
CMD_GET_SUPPORTED_COMMANDS(24)

### Tier 2 (Debugging)
CMD_ADD_BREAKPOINT(40), CMD_REMOVE_BREAKPOINT(41),
CMD_ADD_WATCHPOINT(42), CMD_REMOVE_WATCHPOINT(43)

### Tier 3 (ZX Features)
CMD_WRITE_BANK(5), CMD_SET_SLOT(10), CMD_SET_BORDER(12),
CMD_READ_STATE(50), CMD_WRITE_STATE(51)

### Notification
NTF_PAUSE(1)

## File Structure

```
core/automation/dezog/
├── CMakeLists.txt
├── include/
│   ├── dzrp-server.h
│   ├── dzrp-protocol.h
│   ├── dzrp-commands.h
│   └── dzrp-types.h
└── src/
    ├── dzrp-server.cpp
    ├── dzrp-protocol.cpp
    └── dzrp-commands.cpp

tools/verification/dezog/
├── verify_dzrp_protocol.py
├── dzrp_client.py
└── README.md
```

## Status

| Phase | Status | Description |
|-------|--------|-------------|
| 1. Foundation | ☐ | Protocol, TCP server, basic commands |
| 2. Debugging | ☐ | Breakpoints, watchpoints, stepping |
| 3. ZX Features | ☐ | Banking, state capture |
| 4. Verification | ☐ | Testing, documentation |

## References

- [DeZog Repository](https://github.com/maziac/DeZog)
- [DZRP Protocol Spec](https://github.com/maziac/DeZog/blob/main/design/DeZogProtocol.md)
- [Adding New Remotes](https://github.com/maziac/DeZog/blob/main/design/AddingNewRemotes.md)
