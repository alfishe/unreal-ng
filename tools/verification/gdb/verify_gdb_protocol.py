#!/usr/bin/env python3
"""
GDB Remote Serial Protocol Verification Tool

Tests the GDB server implementation against real-world client behavior.
Simulates the exact packet sequences used by GDB and Ghidra.

Usage:
    python3 verify_gdb_protocol.py [--host HOST] [--port PORT] [--verbose]
"""

import argparse
import socket
import sys
import time
from dataclasses import dataclass
from typing import Optional, List, Tuple


@dataclass
class TestResult:
    name: str
    passed: bool
    expected: str
    actual: str
    notes: str = ""


class GDBClient:
    """Simple GDB RSP client for testing."""

    def __init__(self, host: str = "127.0.0.1", port: int = 2000, timeout: float = 2.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.socket: Optional[socket.socket] = None
        self.no_ack_mode = False
        self.verbose = False

    def connect(self) -> bool:
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(self.timeout)
            self.socket.connect((self.host, self.port))
            return True
        except Exception as e:
            print(f"Connection failed: {e}")
            return False

    def disconnect(self):
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None

    @staticmethod
    def checksum(data: str) -> int:
        return sum(ord(c) for c in data) & 0xFF

    def make_packet(self, data: str) -> str:
        cs = self.checksum(data)
        return f"${data}#{cs:02x}"

    def send_packet(self, data: str, desc: str = "") -> Optional[str]:
        """Send a GDB packet and return the response data (without framing)."""
        if not self.socket:
            return None

        packet = self.make_packet(data)
        if self.verbose:
            display = packet[:60] + "..." if len(packet) > 60 else packet
            print(f"  >>> {display}" + (f" ({desc})" if desc else ""))

        try:
            self.socket.sendall(packet.encode("latin-1"))
            time.sleep(0.05)

            response = b""
            start_time = time.time()
            while time.time() - start_time < self.timeout:
                try:
                    self.socket.settimeout(0.5)
                    chunk = self.socket.recv(4096)
                    if not chunk:
                        break
                    response += chunk
                    # Check if we have a complete response
                    resp_str = response.decode("latin-1")
                    if "#" in resp_str:
                        hash_pos = resp_str.rfind("#")
                        if len(resp_str) >= hash_pos + 3:
                            break
                except socket.timeout:
                    if response:
                        break
                    continue

            if not response:
                if self.verbose:
                    print(f"  <<< TIMEOUT")
                return None

            resp_str = response.decode("latin-1")
            if self.verbose:
                display = resp_str[:60] + "..." if len(resp_str) > 60 else resp_str
                print(f"  <<< {display}")

            # Parse response
            if resp_str.startswith("+$") and "#" in resp_str:
                start = resp_str.index("$") + 1
                end = resp_str.rindex("#")
                return resp_str[start:end]
            elif resp_str == "+":
                return None
            elif resp_str.startswith("+"):
                if "$" in resp_str and "#" in resp_str:
                    start = resp_str.index("$") + 1
                    end = resp_str.rindex("#")
                    return resp_str[start:end]
            elif resp_str.startswith("$") and "#" in resp_str:
                # No-ack mode response
                start = 1
                end = resp_str.rindex("#")
                return resp_str[start:end]

            return None

        except Exception as e:
            if self.verbose:
                print(f"  !!! Error: {e}")
            return None


class GDBProtocolVerifier:
    """Verifies GDB RSP protocol compliance with realistic packet sequences."""

    def __init__(self, host: str, port: int, verbose: bool = False):
        self.host = host
        self.port = port
        self.verbose = verbose
        self.results: List[TestResult] = []

    def log(self, msg: str):
        if self.verbose:
            print(f"  {msg}")

    def test(self, name: str, expected: str, actual: Optional[str], notes: str = "") -> bool:
        passed = actual == expected
        self.results.append(TestResult(name, passed, expected, actual or "(timeout)", notes))
        return passed

    def test_contains(self, name: str, substring: str, actual: Optional[str], notes: str = "") -> bool:
        passed = actual is not None and substring in actual
        self.results.append(TestResult(name, passed, f"contains '{substring}'", actual or "(timeout)", notes))
        return passed

    def test_not_timeout(self, name: str, actual: Optional[str], notes: str = "") -> bool:
        passed = actual is not None
        self.results.append(TestResult(name, passed, "any response", actual or "(timeout)", notes))
        return passed

    def test_response_or_empty(self, name: str, actual: Optional[str], notes: str = "") -> bool:
        """Test that we got a response (including empty packet) rather than timeout."""
        passed = actual is not None
        self.results.append(TestResult(name, passed, "response (may be empty)", actual if actual is not None else "(timeout)", notes))
        return passed

    def run_all_tests(self) -> Tuple[int, int]:
        """Run all protocol tests. Returns (passed, total)."""
        print(f"\nGDB Protocol Verification - {self.host}:{self.port}")
        print("=" * 60)

        self.test_full_ghidra_sequence()
        self.test_gdb_backtrace_sequence()
        self.test_execution_and_breakpoints()
        self.test_ghidra_info_stack()

        # Summary
        passed = sum(1 for r in self.results if r.passed)
        total = len(self.results)

        print("\n" + "=" * 60)
        print(f"Results: {passed}/{total} tests passed")

        if passed < total:
            print("\nFailed tests:")
            for r in self.results:
                if not r.passed:
                    print(f"  - {r.name}")
                    print(f"    Expected: {r.expected}")
                    print(f"    Actual:   {r.actual}")
                    if r.notes:
                        print(f"    Notes:    {r.notes}")

        return passed, total

    def test_full_ghidra_sequence(self):
        """Test the exact packet sequence that Ghidra/GDB sends on connect."""
        print("\n[Ghidra Connection Sequence]")

        client = GDBClient(self.host, self.port)
        client.verbose = self.verbose
        if not client.connect():
            self.results.append(TestResult("connect", False, "connected", "failed", ""))
            return

        try:
            # 1. qSupported - capability negotiation
            resp = client.send_packet(
                "qSupported:multiprocess+;swbreak+;hwbreak+;qRelocInsn+;fork-events+;"
                "vfork-events+;exec-events+;vContSupported+;QThreadEvents+;no-resumed+",
                "qSupported"
            )
            self.test_contains("qSupported", "PacketSize", resp)

            # 2. vMustReplyEmpty - protocol test
            resp = client.send_packet("vMustReplyEmpty", "vMustReplyEmpty")
            self.test("vMustReplyEmpty", "", resp, "Must return empty packet")

            # 3. QStartNoAckMode
            resp = client.send_packet("QStartNoAckMode", "QStartNoAckMode")
            self.test("QStartNoAckMode", "OK", resp)

            # 4. Stop reason query
            resp = client.send_packet("?", "stop reason")
            self.test_contains("? (stop reason)", "T", resp)
            self.test_contains("? (thread)", "thread:", resp)

            # 5. Target description
            resp = client.send_packet("qXfer:features:read:target.xml:0,1000", "target.xml")
            self.test_contains("qXfer:features", "<?xml", resp)

            # 6. Trace status (empty = not supported)
            resp = client.send_packet("qTStatus", "qTStatus")
            self.test("qTStatus", "", resp, "Empty = no tracing")

            # 7. Trace variables
            resp = client.send_packet("qTfV", "qTfV")
            self.test("qTfV", "", resp)

            # 8. Thread enumeration
            resp = client.send_packet("qfThreadInfo", "qfThreadInfo")
            self.test_contains("qfThreadInfo", "m", resp, "m followed by thread ID")

            resp = client.send_packet("qsThreadInfo", "qsThreadInfo")
            self.test("qsThreadInfo", "l", resp, "l = end of list")

            # 9. Thread extra info (used by 'info thread')
            resp = client.send_packet("qThreadExtraInfo,01", "qThreadExtraInfo")
            self.test_not_timeout("qThreadExtraInfo", resp, "Should return hex-encoded string")

            # 10. qAttached
            resp = client.send_packet("qAttached", "qAttached")
            self.test("qAttached", "1", resp)

            # 11. Current thread
            resp = client.send_packet("qC", "qC")
            self.test_contains("qC", "QC", resp)

            # 12. Thread selection
            resp = client.send_packet("Hg0", "Hg0")
            self.test("Hg0", "OK", resp)

            resp = client.send_packet("Hc-1", "Hc-1")
            self.test("Hc-1", "OK", resp)

            # 13. Read registers
            resp = client.send_packet("g", "read all regs")
            self.test_not_timeout("g (registers)", resp)

            # 14. Symbol handling
            resp = client.send_packet("qSymbol::", "qSymbol")
            self.test("qSymbol::", "OK", resp)

            # 15. Offsets (bare metal = empty)
            resp = client.send_packet("qOffsets", "qOffsets")
            self.test("qOffsets", "", resp)

        finally:
            client.disconnect()

    def test_gdb_backtrace_sequence(self):
        """Test packets sent when GDB does 'bt' (backtrace)."""
        print("\n[Backtrace Sequence]")

        client = GDBClient(self.host, self.port)
        client.verbose = self.verbose
        if not client.connect():
            return

        try:
            # Initialize
            client.send_packet("qSupported:multiprocess+", "init")
            client.send_packet("?", "stop")

            # Thread properties (qP) - old format
            resp = client.send_packet("qP0000001f0000000000000001", "qP thread props")
            self.test_response_or_empty("qP (thread props)", resp, "Empty = use defaults")

            # qL - old thread list format
            resp = client.send_packet("qL1200000000000000000", "qL thread list")
            self.test_response_or_empty("qL (old thread list)", resp)

            # Memory reads for stack unwinding
            resp = client.send_packet("m0000,4", "read mem 0x0000")
            self.test_not_timeout("m0000,4", resp)

            # Read SP area (assuming SP around 0xFD6C based on earlier)
            resp = client.send_packet("mfd6c,20", "read stack")
            self.test_not_timeout("mfd6c,20 (stack)", resp)

            # Single register read (SP)
            resp = client.send_packet("p6", "read SP reg")
            self.test_not_timeout("p6 (SP)", resp)

        finally:
            client.disconnect()

    def test_execution_and_breakpoints(self):
        """Test execution control and breakpoint packets."""
        print("\n[Execution Control]")

        client = GDBClient(self.host, self.port)
        client.verbose = self.verbose
        if not client.connect():
            return

        try:
            # Initialize
            client.send_packet("qSupported:multiprocess+", "init")
            client.send_packet("?", "stop")

            # vCont support query
            resp = client.send_packet("vCont?", "vCont?")
            self.test_contains("vCont?", "vCont;c", resp, "Must support continue")

            # Set breakpoint
            resp = client.send_packet("Z0,8000,1", "set bp at 0x8000")
            self.test("Z0,8000,1 (set bp)", "OK", resp)

            # Remove breakpoint
            resp = client.send_packet("z0,8000,1", "remove bp at 0x8000")
            self.test("z0,8000,1 (remove bp)", "OK", resp)

            # Set watchpoint (write)
            resp = client.send_packet("Z2,4000,2", "set write wp")
            self.test("Z2,4000,2 (write wp)", "OK", resp)

            resp = client.send_packet("z2,4000,2", "remove wp")
            self.test("z2,4000,2 (remove wp)", "OK", resp)

            # Thread alive
            resp = client.send_packet("T01", "thread 1 alive?")
            self.test("T01 (thread alive)", "OK", resp)

            # Kill/detach
            resp = client.send_packet("D", "detach")
            self.test("D (detach)", "OK", resp)

        finally:
            client.disconnect()


    def test_ghidra_info_stack(self):
        """Test packets sent when Ghidra does info stack / put_frames."""
        print("\n[Ghidra Info Stack Sequence]")

        client = GDBClient(self.host, self.port)
        client.verbose = self.verbose
        if not client.connect():
            return

        try:
            # Initialize
            client.send_packet("qSupported:multiprocess+", "init")
            client.send_packet("?", "stop")

            # Frame selection
            resp = client.send_packet("Hg0", "select thread 0")
            self.test("Hg0", "OK", resp)

            # qXfer:threads - Ghidra may request this
            resp = client.send_packet("qXfer:threads:read::0,1000", "qXfer:threads")
            self.test_response_or_empty("qXfer:threads", resp, "Empty = not supported")

            # qXfer:exec-file - Ghidra may request executable path
            resp = client.send_packet("qXfer:exec-file:read::0,1000", "qXfer:exec-file")
            self.test_response_or_empty("qXfer:exec-file", resp)

            # qXfer:auxv - auxiliary vector
            resp = client.send_packet("qXfer:auxv:read::0,1000", "qXfer:auxv")
            self.test_response_or_empty("qXfer:auxv", resp)

            # qXfer:libraries - loaded libraries
            resp = client.send_packet("qXfer:libraries:read::0,1000", "qXfer:libraries")
            self.test_response_or_empty("qXfer:libraries", resp)

            # Multiple register reads (simulating frame unwind)
            resp = client.send_packet("p6", "read SP")
            self.test_not_timeout("p6 (SP)", resp)

            resp = client.send_packet("pf", "read PC (reg 15)")
            self.test_not_timeout("pf (maybe PC)", resp)

            # Read memory at SP for stack frame
            # Assuming SP is around 0x5bf5 based on earlier test
            resp = client.send_packet("m5bf0,40", "read stack memory")
            self.test_not_timeout("m5bf0,40 (stack)", resp)

            # Rapid-fire memory reads (simulating stack unwind)
            for offset in range(0, 0x100, 0x10):
                addr = 0x5b00 + offset
                resp = client.send_packet(f"m{addr:04x},10", f"stack scan {addr:04x}")
                if resp is None:
                    self.results.append(TestResult(f"stack scan 0x{addr:04x}", False,
                                                   "response", "(timeout)", "Connection may have dropped"))
                    return

            self.results.append(TestResult("stack scan complete", True, "all responded", "all responded", ""))

        finally:
            client.disconnect()


def main():
    parser = argparse.ArgumentParser(description="Verify GDB RSP protocol implementation")
    parser.add_argument("--host", default="127.0.0.1", help="GDB server host")
    parser.add_argument("--port", type=int, default=2000, help="GDB server port")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show all packets")
    args = parser.parse_args()

    verifier = GDBProtocolVerifier(args.host, args.port, args.verbose)
    passed, total = verifier.run_all_tests()

    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
