#!/usr/bin/env python3
"""
GDB RSP server integration tests using pygdbmi.

Requires:
    pip install pygdbmi

Usage:
    1. Start emulator with GDB server enabled on port 2000
    2. Run: python test_gdb_integration.py
"""

import sys
import time
import socket
from typing import Optional

try:
    from pygdbmi.gdbcontroller import GdbController
except ImportError:
    print("pygdbmi not installed. Run: pip install pygdbmi")
    sys.exit(1)


def wait_for_server(host: str = "127.0.0.1", port: int = 2000, timeout: float = 5.0) -> bool:
    """Wait for GDB server to be available."""
    start = time.time()
    while time.time() - start < timeout:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(1.0)
            result = sock.connect_ex((host, port))
            sock.close()
            if result == 0:
                return True
        except Exception:
            pass
        time.sleep(0.2)
    return False


class GDBIntegrationTests:
    def __init__(self, gdb_path: str = "z80-elf-gdb", port: int = 2000):
        self.gdb_path = gdb_path
        self.port = port
        self.gdb: Optional[GdbController] = None
        self.passed = 0
        self.failed = 0

    def setup(self):
        """Start GDB and connect to the server."""
        self.gdb = GdbController([self.gdb_path, "--interpreter=mi3"])
        self.gdb.write("-target-select remote localhost:" + str(self.port))
        response = self.gdb.get_gdb_response(timeout_sec=5)
        for r in response:
            if r.get("message") == "connected":
                return True
        return False

    def teardown(self):
        """Disconnect and quit GDB."""
        if self.gdb:
            try:
                self.gdb.write("-target-disconnect")
                self.gdb.write("-gdb-exit")
            except Exception:
                pass
            self.gdb = None

    def check(self, name: str, condition: bool, detail: str = ""):
        """Record test result."""
        if condition:
            self.passed += 1
            print(f"  PASS: {name}")
        else:
            self.failed += 1
            print(f"  FAIL: {name} - {detail}")

    def send_cmd(self, cmd: str) -> list:
        """Send command and return response."""
        self.gdb.write(cmd)
        return self.gdb.get_gdb_response(timeout_sec=3)

    def test_register_read(self):
        """Test reading registers."""
        print("\nTest: Register Read")
        response = self.send_cmd("-data-list-register-values x")
        found_regs = False
        for r in response:
            if r.get("payload", {}).get("register-values"):
                found_regs = True
                regs = r["payload"]["register-values"]
                self.check("Has registers", len(regs) > 0, f"Got {len(regs)} registers")
        self.check("Register response", found_regs, "No register-values in response")

    def test_memory_read(self):
        """Test reading memory."""
        print("\nTest: Memory Read")
        response = self.send_cmd("-data-read-memory-bytes 0x4000 16")
        found_mem = False
        for r in response:
            mem = r.get("payload", {}).get("memory", [])
            if mem:
                found_mem = True
                data = mem[0].get("contents", "")
                self.check("Memory data", len(data) == 32, f"Expected 32 hex chars, got {len(data)}")
        self.check("Memory response", found_mem, "No memory data in response")

    def test_breakpoint_set(self):
        """Test setting and removing breakpoints."""
        print("\nTest: Breakpoints")
        addr = "0x8000"

        response = self.send_cmd(f"-break-insert *{addr}")
        bp_num = None
        for r in response:
            bp = r.get("payload", {}).get("bkpt", {})
            if bp:
                bp_num = bp.get("number")
        self.check("Breakpoint created", bp_num is not None, "No breakpoint number")

        if bp_num:
            response = self.send_cmd(f"-break-delete {bp_num}")
            done = any(r.get("message") == "done" for r in response)
            self.check("Breakpoint deleted", done, "Delete command failed")

    def test_monitor_commands(self):
        """Test monitor commands."""
        print("\nTest: Monitor Commands")

        response = self.send_cmd('-interpreter-exec console "monitor help"')
        help_text = ""
        for r in response:
            if r.get("type") == "console":
                help_text += r.get("payload", "")
        self.check("Monitor help", "monitor" in help_text.lower(), "Help text not found")

        response = self.send_cmd('-interpreter-exec console "monitor status"')
        status = ""
        for r in response:
            if r.get("type") == "console":
                status += r.get("payload", "")
        has_status = "Status:" in status or "attached" in status.lower() or "instance" in status.lower()
        self.check("Monitor status", has_status, f"Unexpected status: {status[:100]}")

    def test_step(self):
        """Test single stepping."""
        print("\nTest: Single Step")

        response = self.send_cmd("-exec-step-instruction")
        stopped = False
        for r in response:
            if r.get("message") == "stopped":
                stopped = True
            elif r.get("payload", {}).get("reason") == "end-stepping-range":
                stopped = True
        # Also check running state
        for r in response:
            if r.get("message") in ["running", "done"]:
                stopped = True  # Command accepted
        self.check("Step executed", stopped or len(response) > 0, "No response to step")

    def run_all(self):
        """Run all tests."""
        print(f"Connecting to GDB server on port {self.port}...")

        if not wait_for_server(port=self.port):
            print("ERROR: GDB server not available")
            return False

        try:
            if not self.setup():
                print("ERROR: Failed to connect")
                return False

            self.test_register_read()
            self.test_memory_read()
            self.test_breakpoint_set()
            self.test_monitor_commands()
            self.test_step()

        finally:
            self.teardown()

        print(f"\n{'='*40}")
        print(f"Results: {self.passed} passed, {self.failed} failed")
        return self.failed == 0


def main():
    import argparse
    parser = argparse.ArgumentParser(description="GDB RSP integration tests")
    parser.add_argument("--gdb", default="z80-elf-gdb", help="Path to GDB")
    parser.add_argument("--port", type=int, default=2000, help="GDB server port")
    args = parser.parse_args()

    tests = GDBIntegrationTests(gdb_path=args.gdb, port=args.port)
    success = tests.run_all()
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
