# DZRP Verification Plan

## Overview

Three-tier verification strategy:
1. **Unit tests** — protocol parsing, serialization
2. **Integration tests** — Python verification script
3. **End-to-end tests** — actual DeZog extension

## Unit Tests

### Protocol Layer Tests

| Test | Description |
|------|-------------|
| `test_parseCommand_valid` | Parse well-formed command |
| `test_parseCommand_truncated` | Handle incomplete data |
| `test_parseCommand_badLength` | Reject invalid length field |
| `test_serializeResponse_ack` | Serialize ACK response |
| `test_serializeResponse_nak` | Serialize NAK response |
| `test_serializeNotification` | Serialize notification |
| `test_readU16LE` | Little-endian 16-bit read |
| `test_readU32LE` | Little-endian 32-bit read |
| `test_writeU16LE` | Little-endian 16-bit write |
| `test_readNulString` | NUL-terminated string parsing |

### Command Handler Tests

| Test | Description |
|------|-------------|
| `test_handleInit_success` | Version exchange |
| `test_handleInit_versionMismatch` | Handle version difference |
| `test_handleGetRegisters` | Returns all registers + slots |
| `test_handleSetRegister_pc` | Set PC register |
| `test_handleSetRegister_8bit` | Set 8-bit register (A, F, etc.) |
| `test_handleReadMem` | Read memory range |
| `test_handleWriteMem` | Write memory |
| `test_handleAddBreakpoint` | Add breakpoint, get ID |
| `test_handleRemoveBreakpoint` | Remove by ID |
| `test_handleAddWatchpoint` | Add watchpoint |
| `test_handleContinue` | Resume execution |
| `test_handlePause` | Break execution |
| `test_handleGetSupportedCommands` | Returns correct bitfield |
| `test_dispatch_unknownCommand` | Returns NAK |

### Notification Tests

| Test | Description |
|------|-------------|
| `test_buildPauseNotification_breakpoint` | BP hit notification |
| `test_buildPauseNotification_watchpoint` | WP hit notification |
| `test_buildPauseNotification_manual` | Manual break notification |

---

## Integration Tests (Python)

### Test Script Structure

```
tools/verification/dezog/
├── verify_dzrp_protocol.py    # Main verification script
├── dzrp_client.py             # DZRP client implementation
├── test_sequences.py          # Test sequence definitions
└── README.md
```

### dzrp_client.py

```python
#!/usr/bin/env python3
"""DZRP protocol client for testing."""

import socket
import struct
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional, List

class DZRPCommand(IntEnum):
    CMD_INIT = 1
    CMD_CLOSE = 2
    CMD_GET_REGISTERS = 3
    CMD_SET_REGISTER = 4
    CMD_WRITE_BANK = 5
    CMD_CONTINUE = 6
    CMD_PAUSE = 7
    CMD_READ_MEM = 8
    CMD_WRITE_MEM = 9
    CMD_SET_SLOT = 10
    CMD_GET_SUPPORTED_COMMANDS = 24
    CMD_ADD_BREAKPOINT = 40
    CMD_REMOVE_BREAKPOINT = 41
    CMD_ADD_WATCHPOINT = 42
    CMD_REMOVE_WATCHPOINT = 43
    CMD_READ_STATE = 50
    CMD_WRITE_STATE = 51

class DZRPNotification(IntEnum):
    NTF_PAUSE = 1

@dataclass
class DZRPResponse:
    seq_no: int
    nak: bool
    payload: bytes

@dataclass
class DZRPPauseNotification:
    reason: int
    address: int
    bank: int
    message: str

class DZRPClient:
    def __init__(self, host: str = "localhost", port: int = 12000):
        self.host = host
        self.port = port
        self.sock: Optional[socket.socket] = None
        self.seq_no = 0
    
    def connect(self) -> bool:
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(5.0)
            self.sock.connect((self.host, self.port))
            return True
        except Exception as e:
            print(f"Connect failed: {e}")
            return False
    
    def disconnect(self):
        if self.sock:
            self.sock.close()
            self.sock = None
    
    def _next_seq(self) -> int:
        self.seq_no = (self.seq_no % 15) + 1
        return self.seq_no
    
    def _send_command(self, cmd_id: int, payload: bytes = b"") -> DZRPResponse:
        seq = self._next_seq()
        # Build command: length(4) + seq(1) + cmd(1) + payload
        data = struct.pack("<I", len(payload)) + bytes([seq, cmd_id]) + payload
        self.sock.sendall(data)
        return self._recv_response()
    
    def _recv_response(self) -> DZRPResponse:
        # Read length
        length_data = self._recv_exact(4)
        length = struct.unpack("<I", length_data)[0]
        # Read payload
        data = self._recv_exact(length)
        seq_nak = data[0]
        nak = bool(seq_nak & 0x80)
        seq_no = seq_nak & 0x0F
        return DZRPResponse(seq_no, nak, data[1:])
    
    def _recv_exact(self, n: int) -> bytes:
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("Connection closed")
            data += chunk
        return data
    
    # High-level commands
    def cmd_init(self, version: tuple = (2, 2, 0), name: str = "TestClient") -> dict:
        payload = bytes(version) + name.encode() + b"\x00"
        resp = self._send_command(DZRPCommand.CMD_INIT, payload)
        if resp.nak:
            return {"error": "NAK"}
        return {
            "error": resp.payload[0],
            "version": tuple(resp.payload[1:4]),
            "machine": resp.payload[4],
            "name": resp.payload[5:].rstrip(b"\x00").decode(),
        }
    
    def cmd_close(self) -> bool:
        resp = self._send_command(DZRPCommand.CMD_CLOSE)
        return not resp.nak
    
    def cmd_get_registers(self) -> dict:
        resp = self._send_command(DZRPCommand.CMD_GET_REGISTERS)
        if resp.nak or len(resp.payload) < 29:
            return {"error": "Invalid response"}
        p = resp.payload
        nslots = p[28]
        return {
            "pc": struct.unpack("<H", p[0:2])[0],
            "sp": struct.unpack("<H", p[2:4])[0],
            "af": struct.unpack("<H", p[4:6])[0],
            "bc": struct.unpack("<H", p[6:8])[0],
            "de": struct.unpack("<H", p[8:10])[0],
            "hl": struct.unpack("<H", p[10:12])[0],
            "ix": struct.unpack("<H", p[12:14])[0],
            "iy": struct.unpack("<H", p[14:16])[0],
            "af2": struct.unpack("<H", p[16:18])[0],
            "bc2": struct.unpack("<H", p[18:20])[0],
            "de2": struct.unpack("<H", p[20:22])[0],
            "hl2": struct.unpack("<H", p[22:24])[0],
            "r": p[24],
            "i": p[25],
            "im": p[26],
            "slots": list(p[29:29+nslots]),
        }
    
    def cmd_set_register(self, reg_id: int, value: int) -> bool:
        payload = bytes([reg_id]) + struct.pack("<H", value)
        resp = self._send_command(DZRPCommand.CMD_SET_REGISTER, payload)
        return not resp.nak
    
    def cmd_read_mem(self, addr: int, size: int) -> bytes:
        payload = bytes([0]) + struct.pack("<HH", addr, size)
        resp = self._send_command(DZRPCommand.CMD_READ_MEM, payload)
        if resp.nak:
            return b""
        return resp.payload
    
    def cmd_write_mem(self, addr: int, data: bytes) -> bool:
        payload = bytes([0]) + struct.pack("<H", addr) + data
        resp = self._send_command(DZRPCommand.CMD_WRITE_MEM, payload)
        return not resp.nak
    
    def cmd_continue(self) -> bool:
        # No breakpoints, no alternate command
        payload = bytes([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
        resp = self._send_command(DZRPCommand.CMD_CONTINUE, payload)
        return not resp.nak
    
    def cmd_pause(self) -> bool:
        resp = self._send_command(DZRPCommand.CMD_PAUSE)
        return not resp.nak
    
    def cmd_add_breakpoint(self, addr: int, bank: int = 0, condition: str = "") -> int:
        payload = struct.pack("<H", addr) + bytes([bank]) + condition.encode() + b"\x00"
        resp = self._send_command(DZRPCommand.CMD_ADD_BREAKPOINT, payload)
        if resp.nak or len(resp.payload) < 2:
            return 0
        return struct.unpack("<H", resp.payload[0:2])[0]
    
    def cmd_remove_breakpoint(self, bp_id: int) -> bool:
        payload = struct.pack("<H", bp_id)
        resp = self._send_command(DZRPCommand.CMD_REMOVE_BREAKPOINT, payload)
        return not resp.nak
    
    def cmd_get_supported_commands(self) -> set:
        resp = self._send_command(DZRPCommand.CMD_GET_SUPPORTED_COMMANDS)
        if resp.nak or len(resp.payload) < 32:
            return set()
        supported = set()
        for byte_idx in range(32):
            byte_val = resp.payload[byte_idx]
            for bit in range(8):
                if byte_val & (1 << bit):
                    supported.add(byte_idx * 8 + bit)
        return supported
    
    def wait_notification(self, timeout: float = 10.0) -> Optional[DZRPPauseNotification]:
        self.sock.settimeout(timeout)
        try:
            length_data = self._recv_exact(4)
            length = struct.unpack("<I", length_data)[0]
            data = self._recv_exact(length)
            seq = data[0]
            if seq != 0:  # Not a notification
                return None
            notif_id = data[1]
            if notif_id != DZRPNotification.NTF_PAUSE:
                return None
            reason = data[2]
            addr = struct.unpack("<H", data[3:5])[0]
            bank = data[5]
            msg_end = data.find(0, 6)
            message = data[6:msg_end].decode() if msg_end > 6 else ""
            return DZRPPauseNotification(reason, addr, bank, message)
        except socket.timeout:
            return None
```

### verify_dzrp_protocol.py

```python
#!/usr/bin/env python3
"""DZRP protocol verification script."""

import sys
import argparse
from dzrp_client import DZRPClient, DZRPCommand

class DZRPVerifier:
    def __init__(self, host: str, port: int):
        self.client = DZRPClient(host, port)
        self.passed = 0
        self.failed = 0
        self.results = []
    
    def run_all(self):
        tests = [
            ("Connection", self.test_connection),
            ("CMD_INIT", self.test_init),
            ("CMD_GET_SUPPORTED_COMMANDS", self.test_supported_commands),
            ("CMD_GET_REGISTERS", self.test_get_registers),
            ("CMD_SET_REGISTER", self.test_set_register),
            ("CMD_READ_MEM", self.test_read_mem),
            ("CMD_WRITE_MEM", self.test_write_mem),
            ("CMD_ADD_BREAKPOINT", self.test_add_breakpoint),
            ("CMD_REMOVE_BREAKPOINT", self.test_remove_breakpoint),
            ("CMD_PAUSE", self.test_pause),
            ("CMD_CLOSE", self.test_close),
        ]
        
        for name, test_func in tests:
            try:
                result = test_func()
                if result:
                    self.passed += 1
                    self.results.append((name, "PASS", ""))
                else:
                    self.failed += 1
                    self.results.append((name, "FAIL", "Test returned False"))
            except Exception as e:
                self.failed += 1
                self.results.append((name, "FAIL", str(e)))
        
        self.print_results()
        return self.failed == 0
    
    def test_connection(self) -> bool:
        return self.client.connect()
    
    def test_init(self) -> bool:
        resp = self.client.cmd_init()
        if "error" in resp and resp["error"] == "NAK":
            return False
        return resp["error"] == 0 and len(resp["name"]) > 0
    
    def test_supported_commands(self) -> bool:
        supported = self.client.cmd_get_supported_commands()
        # At minimum, these must be supported
        required = {1, 2, 3, 4, 6, 7, 8, 9, 24}  # INIT, CLOSE, GET_REGS, etc.
        return required.issubset(supported)
    
    def test_get_registers(self) -> bool:
        regs = self.client.cmd_get_registers()
        return "pc" in regs and "sp" in regs and "af" in regs
    
    def test_set_register(self) -> bool:
        # Set PC to 0x1234
        if not self.client.cmd_set_register(0, 0x1234):
            return False
        regs = self.client.cmd_get_registers()
        return regs.get("pc") == 0x1234
    
    def test_read_mem(self) -> bool:
        data = self.client.cmd_read_mem(0x4000, 16)
        return len(data) == 16
    
    def test_write_mem(self) -> bool:
        test_data = bytes([0xAA, 0xBB, 0xCC, 0xDD])
        if not self.client.cmd_write_mem(0x8000, test_data):
            return False
        read_back = self.client.cmd_read_mem(0x8000, 4)
        return read_back == test_data
    
    def test_add_breakpoint(self) -> bool:
        bp_id = self.client.cmd_add_breakpoint(0x8000)
        return bp_id > 0
    
    def test_remove_breakpoint(self) -> bool:
        bp_id = self.client.cmd_add_breakpoint(0x8001)
        if bp_id == 0:
            return False
        return self.client.cmd_remove_breakpoint(bp_id)
    
    def test_pause(self) -> bool:
        return self.client.cmd_pause()
    
    def test_close(self) -> bool:
        result = self.client.cmd_close()
        self.client.disconnect()
        return result
    
    def print_results(self):
        print("\n" + "=" * 60)
        print("DZRP Protocol Verification Results")
        print("=" * 60)
        for name, status, msg in self.results:
            status_str = f"[{status}]"
            if status == "PASS":
                print(f"  {status_str:8} {name}")
            else:
                print(f"  {status_str:8} {name}: {msg}")
        print("-" * 60)
        print(f"  Total: {self.passed} passed, {self.failed} failed")
        print("=" * 60)

def main():
    parser = argparse.ArgumentParser(description="DZRP protocol verifier")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=12000)
    args = parser.parse_args()
    
    verifier = DZRPVerifier(args.host, args.port)
    success = verifier.run_all()
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
```

---

## End-to-End Tests

### DeZog Extension Test Checklist

| Test | Steps | Expected |
|------|-------|----------|
| **Connection** | Start emulator, launch DeZog | Connects, shows registers |
| **Registers** | View Registers panel | All Z80 registers displayed |
| **Memory** | View memory at 0x4000 | Screen memory visible |
| **Set Register** | Modify PC in panel | PC changes, disasm updates |
| **Breakpoint** | Set BP at 0x8000, continue | Breaks at 0x8000 |
| **Step** | Step Into | PC increments by instruction |
| **Step Over** | Step Over on CALL | Skips subroutine |
| **Watchpoint** | Set WP on 0x5800, continue | Breaks on attribute write |
| **Memory Edit** | Edit byte in memory view | Memory changes |
| **Disconnect** | Stop debugging | Clean disconnect |

### DeZog launch.json Template

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "type": "dezog",
            "request": "launch",
            "name": "Unreal-NG Test",
            "remoteType": "dzrp",
            "dzrp": {
                "hostname": "localhost",
                "port": 12000
            },
            "rootFolder": "${workspaceFolder}",
            "listFiles": [
                {
                    "path": "test.lst",
                    "srcDirs": [""]
                }
            ],
            "startAutomatically": false,
            "history": {
                "reverseDebugInstructionCount": 10000
            }
        }
    ]
}
```

---

## Test Matrix

| Feature | Unit | Integration | E2E |
|---------|------|-------------|-----|
| Message framing | ✓ | ✓ | - |
| CMD_INIT | ✓ | ✓ | ✓ |
| CMD_CLOSE | ✓ | ✓ | ✓ |
| CMD_GET_REGISTERS | ✓ | ✓ | ✓ |
| CMD_SET_REGISTER | ✓ | ✓ | ✓ |
| CMD_READ_MEM | ✓ | ✓ | ✓ |
| CMD_WRITE_MEM | ✓ | ✓ | ✓ |
| CMD_CONTINUE | ✓ | ✓ | ✓ |
| CMD_PAUSE | ✓ | ✓ | ✓ |
| CMD_ADD_BREAKPOINT | ✓ | ✓ | ✓ |
| CMD_REMOVE_BREAKPOINT | ✓ | ✓ | ✓ |
| CMD_ADD_WATCHPOINT | ✓ | ✓ | ✓ |
| NTF_PAUSE | ✓ | - | ✓ |
| CMD_SET_SLOT | ✓ | ✓ | ✓ |
| CMD_READ_STATE | ✓ | ✓ | ✓ |
| CMD_WRITE_STATE | ✓ | ✓ | ✓ |
| NAK handling | ✓ | ✓ | - |

---

## Acceptance Criteria

### MVP (Tier 1)

- [ ] DeZog connects successfully
- [ ] Registers displayed correctly
- [ ] Memory view works
- [ ] Can set/modify registers
- [ ] Continue/pause works
- [ ] Clean disconnect

### Full Implementation

- [ ] Breakpoints work
- [ ] Watchpoints work
- [ ] Step into/over works
- [ ] Memory banking works (128K)
- [ ] Reverse debugging works
- [ ] All 39 verification tests pass
