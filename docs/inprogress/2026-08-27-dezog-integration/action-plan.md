# DeZog Integration Action Plan

## Summary

| Aspect | Details |
|--------|---------|
| **Goal** | VS Code debugging via DeZog extension |
| **Approach** | Implement DZRP v2.2.0 protocol server |
| **Effort** | ~5 weeks (MVP in 2 weeks) |
| **Port** | 12000 (DZRP default) |
| **Dependencies** | IDebugInterface abstraction |

## Prerequisites

- [ ] DZRP protocol specification review (v2.2.0)
- [ ] IDebugInterface design (shared with future GDB support)

## Phase 1: Foundation (Days 1-5)

### 1.1 Protocol Layer

| Task | File | Status |
|------|------|--------|
| DZRP message framing (length + seqno) | `dzrpprotocol.cpp` | ☑ |
| Command serialization | `dzrpprotocol.cpp` | ☑ |
| Response building | `dzrpprotocol.cpp` | ☑ |
| Unit tests for protocol | `core/tests/automation/dezog/dzrpprotocol_test.cpp` (wired into core-tests) | ☑ |

### 1.2 TCP Server

| Task | File | Status |
|------|------|--------|
| Socket listener | `dzrpserver.cpp` | ☑ |
| Connection accept/close | `dzrpserver.cpp` | ☑ |
| Message recv/send loop | `dzrpserver.cpp` | ☑ |
| Mutex-protected writes | `dzrpserver.cpp` | ☑ |

### 1.3 Basic Commands (DZRP 2.2.0)

| Command | ID | Description | Status |
|---------|-----|-------------|--------|
| CMD_INIT | 1 | Handshake, version exchange | ☑ |
| CMD_CLOSE | 2 | Graceful disconnect | ☑ |
| CMD_GET_REGISTERS | 3 | All Z80 registers + slots | ☑ |
| CMD_SET_REGISTER | 4 | Write single register | ☑ |
| CMD_READ_MEM | 8 | Read memory range | ☑ |
| CMD_WRITE_MEM | 9 | Write memory | ☑ |
| CMD_GET_SUPPORTED_COMMANDS | 24 | Capability bitfield | ☑ |

**Milestone 1:** DeZog connects, shows registers and memory. ☑

## Phase 2: Execution Control (Days 6-10)

### 2.1 Run Control

| Command | ID | Status |
|---------|-----|--------|
| CMD_CONTINUE | 6 | ☑ (with temp BPs) |
| CMD_PAUSE | 7 | ☑ |
| NTF_PAUSE | 1 | ☑ (mock trigger in test-server, verified by `test_ntf_pause`) |

### 2.2 Breakpoints

| Command | ID | Status |
|---------|-----|--------|
| CMD_ADD_BREAKPOINT | 40 | ☑ |
| CMD_REMOVE_BREAKPOINT | 41 | ☑ |

Note: Breakpoint conditions ignored in MVP (DeZog slow mode). Conditional
breakpoints require expression evaluator (separate track).

### 2.3 Watchpoints

| Command | ID | Status |
|---------|-----|--------|
| CMD_ADD_WATCHPOINT | 42 | ☑ |
| CMD_REMOVE_WATCHPOINT | 43 | ☑ |

Note: Watchpoint capacity is defined by the IDebugInterface implementation;
the mock accepts all. No fixed limit exists in the protocol layer.

**Milestone 2:** Breakpoints work, can step through code (verified against the
mock via the NTF_PAUSE test; real-emulator verification pending wiring). ☑

## Phase 3: ZX Spectrum Features (Days 11-15)

### 3.1 Memory Banking

| Command | ID | Status |
|---------|-----|--------|
| CMD_SET_SLOT | 10 | ☑ |
| CMD_WRITE_BANK | 5 | ☑ |

Note: CMD_GET_SLOTS removed in DZRP 2.0.0 - slot info now returned with
CMD_GET_REGISTERS. Banked breakpoints (bank != 0) require paging support.

### 3.2 State Management

| Command | ID | Status |
|---------|-----|--------|
| CMD_READ_STATE | 50 | ☑ |
| CMD_WRITE_STATE | 51 | ☑ |

### 3.3 Misc

| Command | ID | Status |
|---------|-----|--------|
| CMD_SET_BORDER | 12 | ☑ |

**Milestone 3:** 128K debugging, reverse debugging works.

## Phase 4: Integration (Days 16-20)

### 4.1 Build Integration

| Task | Status |
|------|--------|
| ENABLE_DEZOG_AUTOMATION option | ☐ |
| add_subdirectory(dezog) in automation CMakeLists | ☐ |
| automation.h/cpp start/stop wiring | ☐ |
| unreal.ini dezog_port config | ☐ |

### 4.2 Testing

| Task | Status |
|------|--------|
| GTest suite (framing, seq masking, limits, coalescing) | ☑ |
| Python verifier (all 18 commands + NTF_PAUSE + robustness) | ☑ |
| Manual test with DeZog extension | ☐ |
| Test all breakpoint types | ☐ |
| Test memory banking | ☐ |

### 4.3 Documentation

| Document | Status |
|----------|--------|
| User guide | ☐ |
| launch.json examples (remoteType: cspect) | ☐ |

## Limitations (MVP)

- **Breakpoint conditions**: Ignored; DeZog falls back to slow mode
- **Banked breakpoints**: Requires emulator paging support (bank byte is bank+1 on wire, 0 = any bank)
- **Expression evaluator**: Separate track (F13)
- **supportedCommands required**: DeZog's cspect remote defaults disable WP (42/43) and state (50/51)

## Decision Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-08-27 | DZRP over DAP | DeZog is primary Z80 debugger; simpler protocol |
| 2026-08-27 | Port 12000 | DeZog default |
| 2026-08-27 | remoteType: cspect | DeZog uses cspect remote for DZRP connections |

## Resources

- [DeZog source (TypeScript)](https://github.com/maziac/DeZog/tree/main/src)
- [CSpect plugin (C#)](https://github.com/maziac/DeZogPlugin)
- [DZRP protocol docs](https://github.com/maziac/DeZog/blob/main/design/DeZogProtocol.md)
