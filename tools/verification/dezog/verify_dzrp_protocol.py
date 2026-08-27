#!/usr/bin/env python3
"""DZRP protocol verification script."""

import sys
import argparse
import struct
import time
from dzrp_client import DZRPClient, DZRPCommand, DZRPResponse

class DZRPVerifier:
    def __init__(self, host: str, port: int):
        self.client = DZRPClient(host, port)
        self.passed = 0
        self.failed = 0
        self.results = []

    def run_all(self):
        tests = [
            # Connection & handshake
            ("Connection", self.test_connection),
            ("CMD_INIT", self.test_init),
            ("CMD_INIT version", self.test_init_version),
            ("CMD_INIT DeZog version (2,0,0)", self.test_init_dezog_version),
            ("CMD_GET_SUPPORTED_COMMANDS", self.test_supported_commands),
        
            # Registers
            ("CMD_GET_REGISTERS", self.test_get_registers),
            ("CMD_GET_REGISTERS slots", self.test_get_registers_slots),
            ("CMD_SET_REGISTER PC", self.test_set_register_pc),
            ("CMD_SET_REGISTER 8-bit", self.test_set_register_8bit),
        
            # Memory
            ("CMD_READ_MEM", self.test_read_mem),
            ("CMD_READ_MEM pattern", self.test_read_mem_pattern),
            ("CMD_READ_MEM boundary", self.test_read_mem_boundary),
            ("CMD_WRITE_MEM", self.test_write_mem),
        
            # Banking & machine
            ("CMD_SET_SLOT", self.test_set_slot),
            ("CMD_WRITE_BANK", self.test_write_bank),
            ("CMD_SET_BORDER", self.test_set_border),
        
            # Breakpoints
            ("CMD_ADD_BREAKPOINT", self.test_add_breakpoint),
            ("CMD_ADD_BREAKPOINT with condition", self.test_add_breakpoint_condition),
            ("CMD_REMOVE_BREAKPOINT", self.test_remove_breakpoint),
            ("CMD_REMOVE_BREAKPOINT invalid id", self.test_remove_breakpoint_invalid),
        
            # Watchpoints
            ("CMD_ADD_WATCHPOINT read", self.test_add_watchpoint_read),
            ("CMD_ADD_WATCHPOINT write", self.test_add_watchpoint_write),
            ("CMD_REMOVE_WATCHPOINT", self.test_remove_watchpoint),
        
            # Execution control
            ("CMD_PAUSE", self.test_pause),
            ("CMD_CONTINUE", self.test_continue),
            ("CMD_CONTINUE temp BPs", self.test_continue_temp_bps),
            ("NTF_PAUSE notification", self.test_ntf_pause),
        
            # State
            ("CMD_READ_STATE", self.test_read_state),
            ("CMD_WRITE_STATE", self.test_write_state),
        
            # DeZog compatibility & robustness
            ("Unknown cmd \u2192 empty ACK", self.test_unknown_command_empty_ack),
            ("Seq number echo", self.test_seq_number_echo),
            ("Fragmented frames", self.test_fragmented_frames),
            ("Coalesced frames", self.test_coalesced_frames),
        
            # Cleanup
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

    # --- Connection & handshake ---

    def test_connection(self) -> bool:
        return self.client.connect()

    def test_init(self) -> bool:
        resp = self.client.cmd_init()
        if "error" in resp and resp["error"] == "NAK":
            return False
        return resp["error"] == 0 and len(resp["name"]) > 0

    def test_init_version(self) -> bool:
        resp = self.client.cmd_init()
        # Should be DZRP 2.x
        return resp.get("version", (0,))[0] == 2

    def test_init_dezog_version(self) -> bool:
        # Real DeZog sends version [2,0,0]; the server must accept it
        resp = self.client.cmd_init(version=(2, 0, 0))
        return resp.get("error") == 0 and len(resp.get("name", "")) > 0

    def test_supported_commands(self) -> bool:
        supported = self.client.cmd_get_supported_commands()
        # Full advertised set: every command the server claims must be present
        required = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 24, 40, 41, 42, 43, 50, 51}
        return required.issubset(supported)

    # --- Registers ---

    def test_get_registers(self) -> bool:
        regs = self.client.cmd_get_registers()
        required = ["pc", "sp", "af", "bc", "de", "hl", "ix", "iy",
                    "af2", "bc2", "de2", "hl2", "r", "i", "im"]
        return all(k in regs for k in required)

    def test_get_registers_slots(self) -> bool:
        regs = self.client.cmd_get_registers()
        return "slots" in regs and isinstance(regs["slots"], list)

    def test_set_register_pc(self) -> bool:
        if not self.client.cmd_set_register(0, 0x1234):
            return False
        regs = self.client.cmd_get_registers()
        return regs.get("pc") == 0x1234

    def test_set_register_8bit(self) -> bool:
        # Set A register (ID 15)
        if not self.client.cmd_set_register(15, 0x42):
            return False
        regs = self.client.cmd_get_registers()
        return (regs.get("af", 0) >> 8) == 0x42

    # --- Memory ---

    def test_read_mem(self) -> bool:
        data = self.client.cmd_read_mem(0x4000, 16)
        return len(data) == 16

    def test_read_mem_pattern(self) -> bool:
        # Mock initializes 0x4000.. with i & 0xFF
        return self.client.cmd_read_mem(0x4000, 16) == bytes(range(16))

    def test_read_mem_boundary(self) -> bool:
        # Round-trip across the 64K wrap (write wraps, read wraps)
        pattern = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88])
        if not self.client.cmd_write_mem(0xFFFC, pattern):
            return False
        return self.client.cmd_read_mem(0xFFFC, 8) == pattern

    def test_write_mem(self) -> bool:
        test_data = bytes([0xAA, 0xBB, 0xCC, 0xDD])
        if not self.client.cmd_write_mem(0x8000, test_data):
            return False
        read_back = self.client.cmd_read_mem(0x8000, 4)
        return read_back == test_data

    # --- Banking & machine ---

    def test_set_slot(self) -> bool:
        if not self.client.cmd_set_slot(2, 7):
            return False
        regs = self.client.cmd_get_registers()
        slots = regs.get("slots", [])
        return len(slots) > 2 and slots[2] == 7

    def test_write_bank(self) -> bool:
        return self.client.cmd_write_bank(5, bytes([0xEE] * 16))

    def test_set_border(self) -> bool:
        # Server masks to 3 bits; must still ACK
        return self.client.cmd_set_border(0xFF)

    # --- Breakpoints ---

    def test_add_breakpoint(self) -> bool:
        bp_id = self.client.cmd_add_breakpoint(0x8000)
        return bp_id > 0

    def test_add_breakpoint_condition(self) -> bool:
        # Add with condition (conditions ignored in MVP, but should accept)
        bp_id = self.client.cmd_add_breakpoint(0x8002, condition="A==0")
        return bp_id > 0

    def test_remove_breakpoint(self) -> bool:
        bp_id = self.client.cmd_add_breakpoint(0x8001)
        if bp_id == 0:
            return False
        return self.client.cmd_remove_breakpoint(bp_id)

    def test_remove_breakpoint_invalid(self) -> bool:
        # Removing an unknown id must not NAK or kill the session
        if not self.client.cmd_remove_breakpoint(0xBEEF):
            return False
        return "pc" in self.client.cmd_get_registers()

    # --- Watchpoints ---

    def test_add_watchpoint_read(self) -> bool:
        return self.client.cmd_add_watchpoint(0x5800, 256, access=1)  # read

    def test_add_watchpoint_write(self) -> bool:
        return self.client.cmd_add_watchpoint(0x5900, 256, access=2)  # write

    def test_remove_watchpoint(self) -> bool:
        # Remove the read watchpoint added earlier (same key bytes)
        return self.client.cmd_remove_watchpoint(0x5800, 256, access=1)

    # --- Execution control ---

    def test_pause(self) -> bool:
        return self.client.cmd_pause()

    def test_continue(self) -> bool:
        return self.client.cmd_continue()

    def test_continue_temp_bps(self) -> bool:
        # CMD_CONTINUE with both temp breakpoints enabled (DeZog step-over/
        # step-out shape), followed by the async stop notification
        if not self.client.cmd_continue_with_temp_bps(0x8010, 0x8020):
            return False
        ntf = self.client.wait_notification(timeout=5.0)
        if ntf is None:
            return False
        return ntf.reason == 2 and ntf.address == 0x8010

    def test_ntf_pause(self) -> bool:
        # bp2-disabled shape plus notification round-trip: reason=BREAKPOINT,
        # address must match the temporary breakpoint that "hit"
        self.client.cmd_pause()
        if not self.client.cmd_continue_with_temp_bps(0x8030):
            return False
        ntf = self.client.wait_notification(timeout=5.0)
        if ntf is None:
            return False
        return ntf.reason == 2 and ntf.address == 0x8030

    # --- State ---

    def test_read_state(self) -> bool:
        state = self.client.cmd_read_state()
        return len(state) > 0

    def test_write_state(self) -> bool:
        state = self.client.cmd_read_state()
        if len(state) == 0:
            return False
        return self.client.cmd_write_state(state)

    # --- DeZog compatibility ---

    def test_unknown_command_empty_ack(self) -> bool:
        """Critical: unknown commands must return empty ACK, not NAK.
        DeZog doesn't handle NAK and would error on 'wrong SeqNo'."""
        resp = self.client._send_raw_command(99, b"")  # Unknown command
        # Must NOT have NAK bit set, and seq must match
        return not resp.nak

    def test_seq_number_echo(self) -> bool:
        """Verify server echoes sequence number correctly."""
        # Send multiple commands and verify seq echo
        for _ in range(3):
            expected_seq = self.client._next_seq()
            # Rewind so _send_command uses same seq
            self.client.seq_no = (self.client.seq_no - 1) % 15 or 15
            resp = self.client._send_command(DZRPCommand.CMD_PAUSE, b"")
            if resp.seq_no != expected_seq:
                return False
        return True

    def test_fragmented_frames(self) -> bool:
        """One command delivered byte-by-byte: server must buffer partial frames."""
        seq = self.client._next_seq()
        self.client.seq_no = (self.client.seq_no - 1) % 15 or 15
        msg = struct.pack("<I", 2) + bytes([seq, int(DZRPCommand.CMD_GET_REGISTERS)])
        for b in msg:
            self.client.sock.sendall(bytes([b]))
            time.sleep(0.002)
        resp = self.client._recv_response()
        return resp.seq_no == seq and not resp.nak and len(resp.payload) >= 29

    def test_coalesced_frames(self) -> bool:
        """Two commands in a single TCP segment: both responses must come back."""
        s1 = self.client._next_seq()
        s2 = self.client._next_seq()
        m1 = struct.pack("<I", 2) + bytes([s1, int(DZRPCommand.CMD_PAUSE)])
        m2 = struct.pack("<I", 2) + bytes([s2, int(DZRPCommand.CMD_PAUSE)])
        self.client.sock.sendall(m1 + m2)
        r1 = self.client._recv_response()
        r2 = self.client._recv_response()
        return r1.seq_no == s1 and not r1.nak and r2.seq_no == s2 and not r2.nak

    # --- Cleanup ---

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
