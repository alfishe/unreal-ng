#!/usr/bin/env python3
"""DZRP verification against a REAL emulator instance (unreal-qt / automation host).

Unlike verify_dzrp_protocol.py (which targets the mock dezog-test-server), this
script drives the production DeZog module wired into the emulator:

  DeZog-shaped DZRP client  ->  dzrp::Server  ->  DezogDebugAdapter  ->  Emulator

It installs a tiny Z80 loop in RAM and checks that execution breakpoints,
temporary (step) breakpoints, write watchpoints, CMD_PAUSE, banking and
state save/restore all behave end-to-end. The emulator state captured at the
start is restored at the end, so the host keeps running untouched.

Usage:
    # Launch the headless dezog-emulator-host from the build dir, verify, stop it
    python3 tools/verification/dezog/verify_dzrp_emulator.py --launch

    # Against an already running host (unreal-qt with an emulator started, or
    # dezog-emulator-host) on the default port 12000
    python3 tools/verification/dezog/verify_dzrp_emulator.py

    # Custom build dir / port / model
    python3 tools/verification/dezog/verify_dzrp_emulator.py --launch \
        --build-dir cmake-build-debug --port 12010 --model PENTAGON

Note: unreal-qt does not auto-create an emulator instance on launch (the user
starts one from the UI), so for unattended runs use --launch (headless host).
"""

import argparse
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dzrp_client import DZRPClient, DZRPCommand  # noqa: E402

# Test program (mirrors core/tests/automation/dezog/dezogtestfixture.h)
#   8000: F3           DI
#   8001: 3E 01        LD   A,1
#   8003: 32 00 90     LD   (9000h),A
#   8006: C3 01 80     JP   8001h
PROGRAM = bytes([0xF3, 0x3E, 0x01, 0x32, 0x00, 0x90, 0xC3, 0x01, 0x80])
PROGRAM_START = 0x8000
PROGRAM_LOOP = 0x8001
PROGRAM_STORE = 0x8003
PROGRAM_JP = 0x8006
WATCH_TARGET = 0x9000

REASON_MANUAL = 1
REASON_BREAKPOINT = 2
REASON_WATCHPOINT_READ = 3
REASON_WATCHPOINT_WRITE = 4

MACHINE_ZX48K = 2
MACHINE_ZX128K = 3


def find_project_root() -> Path:
    current = Path(__file__).resolve().parent
    for _ in range(10):
        if (current / "CMakeLists.txt").exists() and (current / "core").is_dir():
            return current
        if current.parent == current:
            break
        current = current.parent
    raise RuntimeError("Could not find project root")


def wait_for_port(port: int, host: str = "localhost", timeout: float = 30.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(0.5)
            if s.connect_ex((host, port)) == 0:
                return True
        time.sleep(0.25)
    return False


def locate_host_binary(build_dir: Path):
    candidates = [
        build_dir / "bin" / "dezog-emulator-host",
        build_dir / "bin" / "dezog-emulator-host.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None


class EmulatorVerifier:
    def __init__(self, host: str, port: int, instance_timeout: float = 20.0):
        self.client = DZRPClient(host, port)
        self.results = []
        self.passed = 0
        self.failed = 0
        self.saved_state = b""
        self.machine = None
        self.instance_timeout = instance_timeout

    # --- helpers ---

    def check(self, name: str, ok: bool, detail: str = ""):
        if ok:
            self.passed += 1
            self.results.append((name, "PASS", ""))
        else:
            self.failed += 1
            self.results.append((name, "FAIL", detail))
        return ok

    def run_step(self, name, func):
        try:
            ok = func()
            if ok is None:
                ok = True
            self.check(name, bool(ok))
        except Exception as e:  # noqa: BLE001
            self.check(name, False, str(e))

    def install_program(self):
        assert self.client.cmd_write_mem(PROGRAM_START, PROGRAM)
        assert self.client.cmd_write_mem(WATCH_TARGET, b"\x00")
        assert self.client.cmd_set_register(0, PROGRAM_START)  # PC
        assert self.client.cmd_set_register(1, 0xFF00)         # SP

    def expect_notification(self, reason, address=None, timeout=5.0):
        ntf = self.client.wait_notification(timeout=timeout)
        if ntf is None:
            raise AssertionError("no NTF_PAUSE within %.1fs" % timeout)
        if ntf.reason != reason:
            raise AssertionError(f"reason {ntf.reason} != {reason}")
        if address is not None and ntf.address != address:
            raise AssertionError(f"address 0x{ntf.address:04X} != 0x{address:04X}")
        return ntf

    # --- steps ---

    def step_connect(self):
        return self.client.connect()

    def step_init(self):
        # The host may open the DZRP port before its first emulator instance
        # exists (machine reported as UNKNOWN until then) - poll like a user
        # would retry "attach" in VS Code.
        started = time.time()
        deadline = started + self.instance_timeout
        resp = {}
        while True:
            resp = self.client.cmd_init(version=(2, 0, 0), name="DeZog")
            self.machine = resp.get("machine")
            if resp.get("error") == 0 and self.machine in (MACHINE_ZX48K, MACHINE_ZX128K):
                print(f"  emulator instance visible after {time.time() - started:.1f}s "
                      f"(machine={self.machine}, server='{resp.get('name', '')}')")
                break
            if time.time() >= deadline:
                print(f"  no emulator instance after {self.instance_timeout:.0f}s "
                      f"(last CMD_INIT: {resp})")
                break
            time.sleep(0.5)
        return resp.get("error") == 0 and self.machine in (MACHINE_ZX48K, MACHINE_ZX128K) \
            and "Unreal" in resp.get("name", "")

    def step_pause(self):
        # Emulator may be running or already paused - either way one MANUAL notification
        assert self.client.cmd_pause()
        self.expect_notification(REASON_MANUAL)
        return not self.client.has_pending_notification()

    def step_capture_state(self):
        self.saved_state = self.client.cmd_read_state()
        return len(self.saved_state) > 0

    def step_registers_and_slots(self):
        regs = self.client.cmd_get_registers()
        slots = regs.get("slots", [])
        if self.machine == MACHINE_ZX128K:
            return len(slots) == 4 and slots[1] == 5 and slots[2] == 2
        return len(slots) == 2

    def step_set_register(self):
        assert self.client.cmd_set_register(0, 0x1234)      # PC
        assert self.client.cmd_set_register(15, 0x77)       # A
        assert self.client.cmd_set_register(5, 0xBEEF)      # HL
        regs = self.client.cmd_get_registers()
        return regs["pc"] == 0x1234 and (regs["af"] >> 8) == 0x77 and regs["hl"] == 0xBEEF

    def step_memory_roundtrip(self):
        data = bytes([0xDE, 0xAD, 0xBE, 0xEF])
        assert self.client.cmd_write_mem(0x8100, data)
        return self.client.cmd_read_mem(0x8100, 4) == data

    def step_breakpoint_hit(self):
        self.install_program()
        self.bp_id = self.client.cmd_add_breakpoint(PROGRAM_JP)
        assert self.bp_id > 0
        assert self.client.cmd_continue()
        self.expect_notification(REASON_BREAKPOINT, PROGRAM_JP)
        regs = self.client.cmd_get_registers()
        return regs["pc"] == PROGRAM_JP

    def step_breakpoint_rehit(self):
        assert self.client.cmd_continue()
        self.expect_notification(REASON_BREAKPOINT, PROGRAM_JP)
        return self.client.cmd_remove_breakpoint(self.bp_id)

    def step_step_over_shape(self):
        # From the JP, DeZog "step" places temp bp1 at the loop head
        assert self.client.cmd_continue_with_temp_bps(PROGRAM_LOOP, PROGRAM_STORE)
        self.expect_notification(REASON_BREAKPOINT, PROGRAM_LOOP)
        # Temp breakpoints are gone: a plain continue must not stop again
        assert self.client.cmd_continue()
        time.sleep(0.2)
        assert self.client.cmd_pause()
        self.expect_notification(REASON_MANUAL)
        return not self.client.has_pending_notification()

    def step_rapid_step_loop(self):
        # 8001 → 8003 → 8006 → 8001 ... exactly one NTF per CMD_CONTINUE, no strays
        assert self.client.cmd_set_register(0, PROGRAM_LOOP)
        pc = PROGRAM_LOOP
        for i in range(30):
            target = {PROGRAM_LOOP: PROGRAM_STORE, PROGRAM_STORE: PROGRAM_JP, PROGRAM_JP: PROGRAM_LOOP}[pc]
            assert self.client.cmd_continue_with_temp_bps(target), f"step {i}"
            self.expect_notification(REASON_BREAKPOINT, target)
            assert not self.client.has_pending_notification(), f"stray notification at step {i}"
            pc = target
        return True

    def step_rom_breakpoint_via_interrupt(self):
        # EI; loop: JR loop → IM1 interrupt every frame vectors to ROM 0x0038
        assert self.client.cmd_write_mem(PROGRAM_START, bytes([0xFB, 0x18, 0xFE]))
        assert self.client.cmd_set_register(0, PROGRAM_START)
        assert self.client.cmd_set_register(13, 1)  # IM 1
        bp = self.client.cmd_add_breakpoint(0x0038)
        assert bp > 0
        assert self.client.cmd_continue()
        self.expect_notification(REASON_BREAKPOINT, 0x0038)
        assert self.client.cmd_remove_breakpoint(bp)
        self.install_program()  # back to the store/jp loop for the next steps
        return True

    def step_read_full_64k(self):
        data = self.client.cmd_read_mem(0x0000, 0xFFFF)
        return len(data) == 0xFFFF and data[PROGRAM_START:PROGRAM_START + len(PROGRAM)] == PROGRAM

    def step_history_info(self):
        info = self.client.cmd_get_history_info()
        return info["available"] and info["recording"]

    def step_history_walk_back(self):
        # Reverse-debug through several loop iterations. We assert coherence
        # invariants (opcodes match memory at the entry PC; PCs are in-program;
        # positions run out eventually) rather than a hard-coded PC-per-index
        # trace, because the exact instruction each index lands on is a property
        # of the TTD M1-granular reverse-seek.
        program_pcs = {PROGRAM_START, PROGRAM_LOOP, PROGRAM_STORE, PROGRAM_JP}
        self.install_program()
        bp = self.client.cmd_add_breakpoint(PROGRAM_JP)
        assert bp > 0
        for _ in range(4):
            assert self.client.cmd_continue()
            self.expect_notification(REASON_BREAKPOINT, PROGRAM_JP)
        assert self.client.cmd_remove_breakpoint(bp)

        t0 = time.time()
        entries = []
        for i in range(10):
            e = self.client.cmd_get_history_entry(i)
            assert e is not None, f"entry {i} missing"
            assert e["pc"] in program_pcs, f"entry {i}: pc {e['pc']:04X} not in program"
            mem = self.client.cmd_read_mem(e["pc"], 4)  # browsing seeks here
            assert e["opcodes"] == mem, f"entry {i}: opcodes {e['opcodes'].hex()} != mem {mem.hex()}"
            entries.append(e)
        t1 = time.time()

        # Forward within history: index 0 must resolve to the same PC as before (cached)
        e0 = self.client.cmd_get_history_entry(0)
        assert e0 is not None and e0["pc"] == entries[0]["pc"]
        t2 = time.time()

        self.history_ms_per_entry = (t1 - t0) * 1000 / 10
        print(f"  history: {self.history_ms_per_entry:.1f} ms/entry sequential (incl. a READ_MEM each), "
              f"forward revisit {(t2 - t1) * 1000:.0f} ms")

        # Reverse walk is coherent and forward-revisit is stable; that is the
        # invariant we assert. (Total history depth on a live host is large and
        # not exhausted here.)
        return e0["opcodes"] == self.client.cmd_read_mem(e0["pc"], 4)

    def step_history_continue_after_browse(self):
        bp = self.client.cmd_add_breakpoint(PROGRAM_JP)
        assert bp > 0
        assert self.client.cmd_continue()  # returns to present first, then runs
        self.expect_notification(REASON_BREAKPOINT, PROGRAM_JP)
        assert self.client.cmd_remove_breakpoint(bp)
        regs = self.client.cmd_get_registers()
        info = self.client.cmd_get_history_info()
        return regs["pc"] == PROGRAM_JP and info["recording"] and \
            self.client.cmd_read_mem(WATCH_TARGET, 1) == b"\x01" and \
            not self.client.has_pending_notification()

    def step_watchpoint_write(self):
        assert self.client.cmd_set_register(0, PROGRAM_START)
        assert self.client.cmd_add_watchpoint(WATCH_TARGET, 1, access=2)
        assert self.client.cmd_continue()
        self.expect_notification(REASON_WATCHPOINT_WRITE, WATCH_TARGET)
        assert self.client.cmd_remove_watchpoint(WATCH_TARGET, 1, access=2)
        return self.client.cmd_read_mem(WATCH_TARGET, 1) == b"\x01"

    def step_pause_running(self):
        assert self.client.cmd_set_register(0, PROGRAM_START)
        assert self.client.cmd_continue()
        time.sleep(0.2)
        assert self.client.cmd_pause()
        ntf = self.expect_notification(REASON_MANUAL)
        return PROGRAM_START <= ntf.address <= PROGRAM_JP + 2

    def step_banking(self):
        if self.machine != MACHINE_ZX128K:
            return True  # 48K: nothing to page
        bank_data = bytes([0x3C] * 8)
        assert self.client.cmd_write_bank(6, bank_data)
        assert self.client.cmd_set_slot(3, 6)
        regs = self.client.cmd_get_registers()
        ok = regs["slots"][3] == 6 and self.client.cmd_read_mem(0xC000, 8) == bank_data
        assert self.client.cmd_set_slot(3, 0)
        return ok

    def step_border(self):
        return self.client.cmd_set_border(2)

    def step_unknown_command(self):
        resp = self.client._send_raw_command(99, b"")
        return not resp.nak and len(resp.payload) == 0

    def step_state_roundtrip(self):
        assert self.client.cmd_write_mem(0x8200, bytes([0xA1, 0xB2]))
        assert self.client.cmd_set_register(0, 0x8200)
        state = self.client.cmd_read_state()
        assert len(state) > 0
        assert self.client.cmd_write_mem(0x8200, bytes([0x00, 0x00]))
        assert self.client.cmd_set_register(0, 0x0000)
        assert self.client.cmd_write_state(state)
        regs = self.client.cmd_get_registers()
        return self.client.cmd_read_mem(0x8200, 2) == bytes([0xA1, 0xB2]) and regs["pc"] == 0x8200

    def step_restore_original(self):
        assert self.client.cmd_write_state(self.saved_state)
        return self.client.cmd_continue()

    def step_close(self):
        ok = self.client.cmd_close()
        self.client.disconnect()
        return ok

    def run(self) -> bool:
        steps = [
            ("Connect", self.step_connect),
            ("CMD_INIT (machine + name)", self.step_init),
            ("CMD_PAUSE -> NTF_PAUSE MANUAL", self.step_pause),
            ("CMD_READ_STATE (capture original)", self.step_capture_state),
            ("CMD_GET_REGISTERS slots layout", self.step_registers_and_slots),
            ("CMD_SET_REGISTER PC/A/HL", self.step_set_register),
            ("CMD_WRITE_MEM/READ_MEM round-trip", self.step_memory_roundtrip),
            ("Execution breakpoint hit", self.step_breakpoint_hit),
            ("Breakpoint re-hit + remove", self.step_breakpoint_rehit),
            ("CMD_CONTINUE temp BPs (step) + auto-clear", self.step_step_over_shape),
            ("Rapid step loop x30 (one NTF per step)", self.step_rapid_step_loop),
            ("ROM breakpoint 0x0038 via IM1 interrupt", self.step_rom_breakpoint_via_interrupt),
            ("CMD_READ_MEM full 64K", self.step_read_full_64k),
            ("History: CMD_GET_HISTORY_INFO (available + recording)", self.step_history_info),
            ("History: walk back 10 entries via TTD (+ latency)", self.step_history_walk_back),
            ("History: continue after browsing", self.step_history_continue_after_browse),
            ("Write watchpoint hit", self.step_watchpoint_write),
            ("CMD_PAUSE while running", self.step_pause_running),
            ("CMD_WRITE_BANK + CMD_SET_SLOT", self.step_banking),
            ("CMD_SET_BORDER", self.step_border),
            ("Unknown cmd -> empty ACK", self.step_unknown_command),
            ("CMD_READ_STATE/WRITE_STATE round-trip", self.step_state_roundtrip),
            ("Restore original state + CONTINUE", self.step_restore_original),
            ("CMD_CLOSE", self.step_close),
        ]
        for name, func in steps:
            self.run_step(name, func)
        self.print_results()
        return self.failed == 0

    def print_results(self):
        print("\n" + "=" * 60)
        print("DZRP Real-Emulator Verification Results")
        print("=" * 60)
        for name, status, msg in self.results:
            line = f"  [{status}]   {name}" if status == "PASS" else f"  [{status}]   {name}: {msg}"
            print(line)
        print("-" * 60)
        print(f"  Total: {self.passed} passed, {self.failed} failed")
        print("=" * 60)


def main():
    parser = argparse.ArgumentParser(description="DZRP verification against a real emulator")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=12000, help="DeZog server port")
    parser.add_argument("--launch", action="store_true",
                        help="Launch the headless dezog-emulator-host from --build-dir first")
    parser.add_argument("--build-dir", default="cmake-build-release")
    parser.add_argument("--model", default="PENTAGON", help="Emulator model for --launch")
    parser.add_argument("--startup-timeout", type=float, default=40.0,
                        help="seconds to wait for the DZRP port when using --launch")
    parser.add_argument("--instance-timeout", type=float, default=20.0,
                        help="seconds to wait for the host to expose an emulator instance")
    args = parser.parse_args()

    process = None
    log_path = None
    if args.launch:
        root = find_project_root()
        build_dir = root / args.build_dir
        binary = locate_host_binary(build_dir)
        if binary is None:
            print(f"ERROR: dezog-emulator-host not found under {build_dir}/bin "
                  f"(build target: dezog-emulator-host)")
            return 2
        log_path = Path(os.environ.get("TMPDIR", "/tmp")) / "dezog-emulator-host.log"
        print(f"Launching {binary} port={args.port} model={args.model} (log: {log_path})")
        with open(log_path, "w") as log:
            process = subprocess.Popen([str(binary), str(args.port), args.model],
                                       stdout=log, stderr=subprocess.STDOUT,
                                       cwd=binary.parent)
        if not wait_for_port(args.port, args.host, args.startup_timeout):
            print(f"ERROR: DeZog port {args.port} did not open within {args.startup_timeout}s")
            process.terminate()
            return 2

    try:
        verifier = EmulatorVerifier(args.host, args.port, args.instance_timeout)
        success = verifier.run()
    finally:
        if process is not None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
            print(f"dezog-emulator-host stopped (log: {log_path})")

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
