# DeZog Integration Action Plan

## Summary

| Aspect | Details |
|--------|---------|
| **Goal** | VS Code debugging via DeZog extension |
| **Approach** | Implement DZRP protocol server |
| **Effort** | ~5 weeks (MVP in 2 weeks) |
| **Port** | 12000 (DZRP default) |
| **Dependencies** | Existing debug infrastructure |

## Prerequisites

- [x] GDB server implementation (reference for debug interface)
- [ ] Refactor shared debug interface from GDB server
- [ ] DZRP protocol specification review

## Phase 1: Foundation (Days 1-5)

### 1.1 Protocol Layer

| Task | File | Status |
|------|------|--------|
| DZRP message framing (length + seqno) | `dzrp-protocol.cpp` | ☐ |
| Command serialization | `dzrp-protocol.cpp` | ☐ |
| Response building | `dzrp-protocol.cpp` | ☐ |
| Unit tests for protocol | `tests/dzrp-protocol-test.cpp` | ☐ |

### 1.2 TCP Server

| Task | File | Status |
|------|------|--------|
| Socket listener | `dzrp-server.cpp` | ☐ |
| Connection accept/close | `dzrp-server.cpp` | ☐ |
| Message recv/send loop | `dzrp-server.cpp` | ☐ |
| Thread-safe queue | `dzrp-server.cpp` | ☐ |

### 1.3 Basic Commands

| Command | Code | Description | Status |
|---------|------|-------------|--------|
| CMD_INIT | 0x01 | Handshake, capabilities | ☐ |
| CMD_CLOSE | 0x02 | Graceful disconnect | ☐ |
| CMD_GET_REGISTERS | 0x03 | All Z80 registers | ☐ |
| CMD_SET_REGISTER | 0x04 | Write single register | ☐ |
| CMD_READ_MEM | 0x0C | Read memory range | ☐ |
| CMD_WRITE_MEM | 0x0D | Write memory | ☐ |

**Milestone 1:** DeZog connects, shows registers and memory.

## Phase 2: Execution Control (Days 6-10)

### 2.1 Run Control

| Command | Code | Status |
|---------|------|--------|
| CMD_CONTINUE | 0x06 | ☐ |
| CMD_PAUSE | 0x07 | ☐ |
| NTF_PAUSE | 0x01 | ☐ |

### 2.2 Breakpoints

| Command | Code | Status |
|---------|------|--------|
| CMD_ADD_BREAKPOINT | 0x08 | ☐ |
| CMD_REMOVE_BREAKPOINT | 0x09 | ☐ |

### 2.3 Watchpoints

| Command | Code | Status |
|---------|------|--------|
| CMD_ADD_WATCHPOINT | 0x0A | ☐ |
| CMD_REMOVE_WATCHPOINT | 0x0B | ☐ |

**Milestone 2:** Breakpoints work, can step through code.

## Phase 3: ZX Spectrum Features (Days 11-15)

### 3.1 Memory Banking

| Command | Code | Status |
|---------|------|--------|
| CMD_GET_SLOTS | 0x0E | ☐ |
| CMD_WRITE_BANK | 0x05 | ☐ |

### 3.2 State Management

| Command | Code | Status |
|---------|------|--------|
| CMD_READ_STATE | 0x0F | ☐ |
| CMD_WRITE_STATE | 0x10 | ☐ |

**Milestone 3:** 128K debugging, reverse debugging works.

## Phase 4: Integration (Days 16-20)

### 4.1 Testing

| Task | Status |
|------|--------|
| Manual test with DeZog | ☐ |
| Test all breakpoint types | ☐ |
| Test memory banking | ☐ |
| Test state save/restore | ☐ |
| Performance profiling | ☐ |

### 4.2 Documentation

| Document | Status |
|----------|--------|
| User guide | ☐ |
| launch.json examples | ☐ |
| Troubleshooting | ☐ |

## Future: DAP Server (Optional)

If generic VS Code support is needed beyond DeZog:

| Component | Notes |
|-----------|-------|
| DAP message framing | Content-Length header + JSON |
| Initialize/Capabilities | Advertise Z80 features |
| Launch/Attach | Start debugging session |
| Threads | Single Z80 thread |
| StackTrace | Parse call stack from memory |
| Scopes/Variables | Registers + memory |
| Breakpoints | Already implemented |
| Step/Continue | Map to Z80 operations |

**Effort:** Additional 2-3 weeks beyond DZRP.

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| DZRP protocol undocumented details | Reference CSpect plugin source |
| Reverse debugging complexity | Phase 3, can defer |
| DeZog version compatibility | Test with latest release |

## Decision Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-08-27 | DZRP over DAP | DeZog is primary Z80 debugger; simpler protocol |
| 2026-08-27 | Port 12000 | DeZog default, no conflict with GDB (1234) |

## Resources

- [DeZog source (TypeScript)](https://github.com/maziac/DeZog/tree/main/src)
- [CSpect plugin (C#)](https://github.com/maziac/DeZogPlugin)
- [DZRP protocol docs](https://github.com/maziac/DeZog/blob/main/design/DeZogProtocol.md)
