# DZRP Protocol Verification

Verification tools for the DeZog Remote Protocol implementation.

## Files

| File | Description |
|------|-------------|
| `dzrp_client.py` | Python DZRP client library |
| `verify_dzrp_protocol.py` | Protocol verification script |

## Usage

### Run Test Server

```bash
# Build and run standalone test server
cd core/automation/dezog
mkdir build && cd build
cmake .. && make
./dezog-test-server [port]
```

### Run Verification

```bash
# With default port 12000
python3 tools/verification/dezog/verify_dzrp_protocol.py

# With custom host/port
python3 tools/verification/dezog/verify_dzrp_protocol.py --host localhost --port 12000
```

### Run Unit Tests

The protocol unit tests are wired into the main `core-tests` binary:

```bash
ninja -C cmake-build-release
./cmake-build-release/bin/core-tests --gtest_filter='DZRPProtocolTest.*'
```

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
