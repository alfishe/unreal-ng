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
| ENABLE_DEZOG_AUTOMATION option (root, core/automation, unreal-qt, unreal-videowall) | ☑ |
| add_subdirectory(dezog) + link/include/definition in automation CMakeLists | ☑ |
| automation.h/cpp start/stop wiring (`AutomationDezog`) | ☑ |
| DezogDebugAdapter (IDebugInterface over the real Emulator) | ☑ |
| Port config | ☑ `UNREAL_DEZOG_PORT` env var (no ini plumbing exists for CLI/WebAPI ports either) |

### 4.2 Testing

| Task | Status |
|------|--------|
| GTest: protocol (framing, seq masking, limits, coalescing) | ☑ `DZRPProtocolTest` |
| GTest: adapter over live emulator (regs, memory, banking, BP/WP, notifications, state, border) | ☑ `DezogDebugAdapter_test` |
| GTest: real TCP session → server → adapter → emulator | ☑ `DZRPServer_test` |
| GTest: AutomationDezog lifecycle / port resolution / busy port | ☑ `AutomationDezog_test` |
| Python verifier vs mock server (all 18 commands + NTF_PAUSE + robustness) | ☑ `verify_dzrp_protocol.py` (34 tests) |
| Headless real-emulator host for unattended runs / DeZog attach | ☑ `dezog-emulator-host` |
| Python verifier vs real emulator (`--launch` headless host, 18 steps) | ☑ `verify_dzrp_emulator.py` |
| Test all breakpoint types (exec, temp/step, write WP, R/W WP, ROM BP via IM1 interrupt) | ☑ |
| Test memory banking (SET_SLOT, WRITE_BANK, ROM aliases) | ☑ |
| Real-world shapes: full 64K READ_MEM, rapid step loop (no lost/duplicate stops), client drop while running, CLOSE cleanup + resume | ☑ |
| 48K model (machine type 2, 2-slot layout) via `dezog-emulator-host 48K` | ☑ |
| Manual test with DeZog extension | ☐ |

### 4.3 Documentation

| Document | Status |
|----------|--------|
| User guide | ☐ |
| launch.json examples (remoteType: cspect) | ☑ `tools/verification/dezog/README.md` |

## Limitations (MVP)

- **Breakpoint conditions**: Ignored; DeZog falls back to slow mode
- **Banked breakpoints**: Requires emulator paging support (bank byte is bank+1 on wire, 0 = any bank)
- **Expression evaluator**: Separate track (F13)
- **supportedCommands required**: DeZog's cspect remote defaults disable WP (42/43) and state (50/51)
- **Watchpoint direction**: combined R/W watchpoints report `WATCHPOINT_READ` (the breakpoint payload carries no access direction); write-only watchpoints report `WATCHPOINT_WRITE`
- **GUI pauses**: a pause triggered from the Qt UI is not forwarded as NTF_PAUSE (DeZog would not expect it); pressing pause in VS Code afterwards still yields a MANUAL stop
- **Single emulator**: the adapter follows `EmulatorManager::GetMostRecentEmulator()`; multi-instance selection is not exposed over DZRP
- **No instance yet**: unreal-qt opens the DZRP port before the user starts an emulator; until then CMD_INIT reports machine `UNKNOWN` and commands are no-ops (use `dezog-emulator-host` for headless/unattended sessions)

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
