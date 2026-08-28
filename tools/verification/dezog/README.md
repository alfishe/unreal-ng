# DZRP Protocol Verification

Verification tools for the DeZog Remote Protocol implementation.

## Files

| File | Description |
|------|-------------|
| `dzrp_client.py` | Python DZRP client library (DeZog-shaped: tolerates interleaved notifications) |
| `verify_dzrp_protocol.py` | Protocol verification against the **mock** `dezog-test-server` |
| `verify_dzrp_emulator.py` | End-to-end verification against a **real emulator** (unreal-qt) |

## Usage

### Mock server verification (protocol layer only)

```bash
# Build (part of the normal tree; also buildable standalone from core/automation/dezog)
ninja -C cmake-build-release dezog-test-server

./cmake-build-release/bin/dezog-test-server 12345 &
python3 tools/verification/dezog/verify_dzrp_protocol.py --port 12345
```

### Real emulator verification (production module)

The DeZog module is started by `Automation::start()` in every host
(unreal-qt, unreal-videowall, standalone `automation`) when the build has
`ENABLE_DEZOG_AUTOMATION=ON` (default). Port: `UNREAL_DEZOG_PORT` env var,
else 12000.

```bash
# Unattended: launch the headless host on a scratch port, verify, stop it
ninja -C cmake-build-release dezog-emulator-host
python3 tools/verification/dezog/verify_dzrp_emulator.py --launch --port 12010

# Against an already running host on the default port 12000
# (unreal-qt with an emulator instance started from the UI, or dezog-emulator-host)
python3 tools/verification/dezog/verify_dzrp_emulator.py
```

`dezog-emulator-host [port] [model]` (built from `core/automation/dezog/test/emulator-host.cpp`)
creates one real emulator instance, starts it and exposes it through the
production `AutomationDezog` module — no GUI, no CLI/WebAPI ports. It is also
the easiest way to attach the DeZog VS Code extension to a headless emulator.
Note that unreal-qt only creates an emulator instance when the user starts one
from the UI; until then a connected DeZog sees machine type `UNKNOWN`.

The script pauses the running instance, captures its state, installs a small
Z80 loop at 0x8000, exercises breakpoints / step (temporary BPs) / write
watchpoints / pause / banking / state round-trip, then restores the captured
state and resumes — the host emulator is left as it was found.

### Unit / integration tests (GTest)

All DeZog suites are wired into the main `core-tests` binary and run against a
live emulator instance (no external process, ephemeral ports):

```bash
ninja -C cmake-build-release core-tests
./cmake-build-release/bin/core-tests \
    --gtest_filter='DZRPProtocolTest.*:DezogDebugAdapter_test.*:DZRPServer_test.*:AutomationDezog_test.*'
```

| Suite | Covers |
|-------|--------|
| `DZRPProtocolTest` | framing, LE helpers, seq masking, oversize/coalesced frames, capability bitfield |
| `DezogDebugAdapter_test` | registers (16/8-bit, alt, IX/IY halves), memory wrap, slots/ROM aliases, WRITE_BANK, BP owner/temp tracking, WP per-byte expansion + clamping, MANUAL/BREAKPOINT/WATCHPOINT notifications, cross-instance filtering, state round-trip, border |
| `DZRPServer_test` | real TCP session: INIT, capabilities, unknown cmd → empty ACK, regs/mem/slots/bank/border over the wire, CONTINUE → NTF_PAUSE, temp-BP step + auto-clear, PAUSE ordering (ACK before NTF), watchpoints, READ/WRITE_STATE, reconnect |
| `AutomationDezog_test` | port resolution (arg / env / default / garbage), start/stop idempotency, client handshake through the module, busy-port failure |

## Test Coverage

The verifier exercises **all 18 advertised commands** plus notifications and
protocol robustness (34 tests total). Seq-number echo is asserted on every
command/response exchange.

| Test | Command | Description |
|------|---------|-------------|
| Connection | - | TCP connect |
| CMD_INIT | 1 | Version handshake (default + DeZog's real `(2,0,0)`) |
| CMD_GET_SUPPORTED_COMMANDS | 24 | Capability query (full 18-command set asserted) |
| CMD_GET_REGISTERS | 3 | Read all Z80 registers + slots |
| CMD_SET_REGISTER | 4 | Write PC + 8-bit sub-register (A, merge semantics) |
| CMD_READ_MEM | 8 | Length, known-pattern values, 64K wrap round-trip |
| CMD_WRITE_MEM | 9 | Write + read-back |
| CMD_SET_SLOT | 10 | Slot mapping, verified via GET_REGISTERS slots |
| CMD_WRITE_BANK | 5 | Bank write (ACK + error byte) |
| CMD_SET_BORDER | 12 | Border color (masked to 3 bits) |
| CMD_ADD_BREAKPOINT | 40 | Plain + condition string |
| CMD_REMOVE_BREAKPOINT | 41 | Valid id + invalid id (session survives) |
| CMD_ADD_WATCHPOINT | 42 | Read and write access types |
| CMD_REMOVE_WATCHPOINT | 43 | Remove by key |
| CMD_PAUSE | 7 | Pause execution |
| CMD_CONTINUE | 6 | Resume, with temp BPs (DeZog step-over shape) |
| NTF_PAUSE | - | Async stop notification: reason + address asserted |
| CMD_READ_STATE | 50 | Capture state |
| CMD_WRITE_STATE | 51 | Restore state |
| Unknown cmd | - | Empty ACK (no NAK bit), session survives |
| Seq echo | - | Sequence number echo validation |
| Fragmented frames | - | Byte-at-a-time delivery |
| Coalesced frames | - | Two commands in one TCP segment |
| CMD_CLOSE | 2 | Disconnect |

## DeZog Integration

To test with actual DeZog extension, use this `launch.json`:

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
    "rootFolder": "${workspaceFolder}",
    "startAutomatically": false
}
```

**Important notes:**
- DeZog uses `remoteType: "cspect"` for DZRP connections (CSpect uses the same protocol)
- `supportedCommands` is required to enable watchpoints (42/43) and state save/restore (50/51)
- Without it, WPMEM/assertions and reverse debugging are disabled client-side
- CMD_GET_SUPPORTED_COMMANDS (24) is implemented but DeZog doesn't query it
