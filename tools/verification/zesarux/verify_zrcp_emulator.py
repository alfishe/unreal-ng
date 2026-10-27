#!/usr/bin/env python3
"""ZRCP verification against a REAL emulator instance (any host running the production module).

Drives the production ZEsarUX-impersonating module wired into the emulator:

  DeZog-shaped ZRCP client  ->  zrcp::Server  ->  DezogDebugAdapter  ->  Emulator

Every command the dispatcher (core/automation/zesarux/src/zrcpserver.cpp, Server::handleCommand) handles is
exercised with value assertions: DeZog's exact init/disconnect sequences, registers, memory, disassembly, step /
step-over, run (banner, stop text, interrupt), breakpoints with conditions and pass counts, watchpoints,
cpu-history, extended-stack, misc queries and the error text on bad input. Conditional breakpoints are checked end
to end on a loop that increments B: the condition the server passes to the core (translateToCore) and the ones the
core refuses (they stay in the server's evaluator) must both stop exactly when the condition first holds. With the
WebAPI of the host reachable (always with --launch) the core breakpoint list is cross-checked: the translated
condition, the match / hit counters, and that disable / enable / clear reach the core.

The state captured at the start (registers + touched memory) is restored at the end.

Result kinds: PASS, FAIL (exit code 1), KNOWN (a known server divergence from ZEsarUX, reported but not failing
unless --strict), XPASS (a KNOWN check that now holds: the divergence was fixed, update the script).

Usage:
    # Launch unreal-qt offscreen on free ports of its own, create a machine, run, stop it
    python3 tools/verification/zesarux/verify_zrcp_emulator.py --launch

    # A given binary / port base (WebAPI = base, CLI +100, MCP +200, GDB +300, DZRP +400, ZRCP +500) / model
    python3 tools/verification/zesarux/verify_zrcp_emulator.py \\
        --launch cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt --port-base 18600 --model 48K

    # Against a running host (start an emulator in it first). ZRCP port: UNREAL_ZRCP_PORT, else 10000.
    # --webapi-port enables the core breakpoint cross-checks
    python3 tools/verification/zesarux/verify_zrcp_emulator.py --port 10000 --webapi-port 8090
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from zrcp_client import (  # noqa: E402
    PROMPT, RUN_BANNER, ZRCPClient, is_error, parse_disasm_line, parse_history_entry,
    parse_registers,
)

# Loop program (mirrors core/tests/automation/dezog/dezogtestfixture.h)
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
PROGRAM_PCS = {PROGRAM_START, PROGRAM_LOOP, PROGRAM_STORE, PROGRAM_JP}

# Counter program for the conditional breakpoints. At the k-th arrival at 8003h (k = 0, 1, ...): B = k,
# (9000h) = k & FFh (77h at k = 0), F = the flags of INC B giving k (the preset F at k = 0)
#   8000: F3           DI
#   8001: 06 00        LD   B,0
#   8003: 04           INC  B
#   8004: 78           LD   A,B
#   8005: 32 00 90     LD   (9000h),A
#   8008: 3A 02 90     LD   A,(9002h)
#   800B: 18 F6        JR   8003h
COUNTER = bytes([0xF3, 0x06, 0x00, 0x04, 0x78, 0x32, 0x00, 0x90, 0x3A, 0x02, 0x90, 0x18, 0xF6])
COUNTER_PCS = [0x8000, 0x8001, 0x8003, 0x8004, 0x8005, 0x8008, 0x800B]
COUNTER_BP = 0x8003
COUNTER_PREFIXES = ["DI", "LD B", "INC B", "LD A,B", "LD (", "LD A,(", "JR"]

# Call program (step-over / step-out shapes)
#   8000: F3           DI
#   8001: CD 10 80     CALL 8010h
#   8004: 18 FB        JR   8001h
#   8010: 00 00        NOP; NOP
#   8012: C9           RET
CALLER = {0x8000: bytes([0xF3, 0xCD, 0x10, 0x80, 0x18, 0xFB]), 0x8010: bytes([0x00, 0x00, 0xC9])}

# Block copy program (step-over runs the whole LDIR)
#   8000: 21 00 91     LD   HL,9100h
#   8003: 11 00 92     LD   DE,9200h
#   8006: 01 10 00     LD   BC,16
#   8009: ED B0        LDIR
#   800B: 00           NOP
BLOCK = bytes([0x21, 0x00, 0x91, 0x11, 0x00, 0x92, 0x01, 0x10, 0x00, 0xED, 0xB0, 0x00])

STACK_TOP = 0xFF00
SERVER_VERSION = "12.1"
SERVER_NAME = "Unreal-NG"
FIRED = "Breakpoint fired: "
UNKNOWN = "Unknown command"
NOT_IN_STEP = "Error. You must first enter cpu-step mode"
NO_PARAM = "ERROR. No parameter set"

# Memory the script writes: saved first, restored at the end
SAVED_RANGES = [(0x8000, 0x100), (0x9000, 0x300), (0xFE00, 0x200)]

DEFAULT_BINARY = "cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt"
PORT_OFFSETS = (("UNREAL_WEBAPI_PORT", 0), ("UNREAL_CLI_PORT", 100), ("UNREAL_MCP_PORT", 200),
                ("UNREAL_GDB_PORT", 300), ("UNREAL_DEZOG_PORT", 400), ("UNREAL_ZRCP_PORT", 500))


def find_project_root() -> Path:
    current = Path(__file__).resolve().parent
    for _ in range(10):
        if (current / "CMakeLists.txt").exists() and (current / "core").is_dir():
            return current
        if current.parent == current:
            break
        current = current.parent
    raise RuntimeError("Could not find project root")


def port_free(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.bind(("127.0.0.1", port))
        except OSError:
            return False
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.2)
        return s.connect_ex(("127.0.0.1", port)) != 0


def find_port_base(start: int) -> int:
    for base in range(start, start + 2000, 7):
        if all(port_free(base + offset) for _, offset in PORT_OFFSETS):
            return base
    raise SystemExit("no free port base found")


def wait_for_port(port: int, host: str = "127.0.0.1", timeout: float = 30.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(0.5)
            if s.connect_ex((host, port)) == 0:
                return True
        time.sleep(0.25)
    return False


class WebApi:
    """Just enough of the host's REST API: machine creation, mute, the core breakpoint list"""

    def __init__(self, host: str, port: int):
        self.base = f"http://{host}:{port}/api/v1"
        self.emulator_id = None

    def request(self, method: str, path: str, body=None, timeout: float = 5.0):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            text = resp.read().decode()
        return json.loads(text) if text else {}

    def emulators(self):
        return self.request("GET", "/emulator").get("emulators", [])

    def pick_emulator(self) -> str:
        listing = self.emulators()
        if not listing:
            raise RuntimeError("the host has no emulator instance")
        self.emulator_id = listing[0]["id"]
        return self.emulator_id

    def emulator(self) -> dict:
        for entry in self.emulators():
            if entry["id"] == self.emulator_id:
                return entry
        return {}

    def adapter_breakpoints(self, addr=None):
        """The core breakpoints the debugger adapters own (owner 'dezog'), optionally at one address"""
        reply = self.request("GET", f"/emulator/{self.emulator_id}/breakpoints?all=true")
        bps = [bp for bp in reply.get("breakpoints", []) if bp.get("owner") == "dezog"]
        if addr is not None:
            bps = [bp for bp in bps if bp.get("address") == addr]
        return bps


class Host:
    """unreal-qt offscreen on ports of its own, one machine, audio muted"""

    def __init__(self, binary: Path, port_base: int, model: str, log_path: Path):
        self.binary = binary
        self.port_base = port_base
        self.model = model
        self.log_path = log_path
        self.process = None
        self.webapi = WebApi("127.0.0.1", port_base)
        self.zrcp_port = port_base + 500

    def start(self, timeout: float):
        env = dict(os.environ)
        for name, offset in PORT_OFFSETS:
            env[name] = str(self.port_base + offset)
        env.setdefault("UNREAL_STREAMDECK_SUPERVISOR", "off")
        env.setdefault("UNREAL_QT_VIEW_SETTINGS", "off")
        env.setdefault("UNREAL_RECENT_FILES", "off")
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        print(f"Launching {self.binary} (ports {self.port_base}+, ZRCP {self.zrcp_port}, log {self.log_path})")
        with open(self.log_path, "w") as log:
            self.process = subprocess.Popen([str(self.binary), "-platform", "offscreen"], env=env,
                                            stdout=log, stderr=subprocess.STDOUT)
        deadline = time.time() + timeout
        while True:
            if self.process.poll() is not None:
                raise SystemExit(f"{self.binary} exited with {self.process.returncode}")
            try:
                self.webapi.emulators()
                break
            except (OSError, urllib.error.URLError, ValueError):
                if time.time() > deadline:
                    raise SystemExit("the launched host does not answer on the WebAPI")
                time.sleep(0.5)
        time.sleep(1.0)  # an instance the host creates by itself shows up first
        if not self.webapi.emulators():
            self.webapi.request("POST", "/emulator/start", {"model": self.model}, timeout=20.0)
        for _ in range(40):
            if self.webapi.emulators():
                break
            time.sleep(0.25)
        emulator_id = self.webapi.pick_emulator()
        # Silent: a verification run makes no sound on the host
        try:
            self.webapi.request("PUT", f"/emulator/{emulator_id}/audio/mixer/master", {"muted": True})
        except (OSError, urllib.error.URLError, ValueError) as e:
            print(f"  (mute failed: {e})")
        if not wait_for_port(self.zrcp_port, timeout=timeout):
            raise SystemExit(f"ZRCP port {self.zrcp_port} did not open")
        print(f"  emulator {emulator_id} ({self.model}) ready")

    def stop(self):
        if self.process is None:
            return
        self.process.terminate()
        try:
            self.process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
        print(f"Host stopped (log: {self.log_path})")


class Skip(Exception):
    """A step that does not apply to this machine"""


class EmulatorVerifier:
    def __init__(self, host: str, port: int, webapi=None, timeout: float = 15.0, strict: bool = False):
        self.client = ZRCPClient(host, port, timeout=timeout)
        self.host = host
        self.port = port
        self.webapi = webapi
        self.strict = strict
        self.results = []
        self.passed = 0
        self.failed = 0
        self.known = 0
        self.xpassed = 0
        self.skipped = 0
        self.machine = ""
        self.banked = False
        self.saved_regs = {}
        self.saved_memory = {}
        self.coverage = set()

    # --- bookkeeping ---

    def check(self, name: str, ok: bool, detail: str = ""):
        if ok:
            self.passed += 1
            self.results.append((name, "PASS", ""))
        else:
            self.failed += 1
            self.results.append((name, "FAIL", detail))
        return ok

    def known_divergence(self, name: str, holds: bool, detail: str):
        """A ZEsarUX behaviour the server does not have yet: `holds` = the ZEsarUX behaviour was observed"""
        if holds:
            self.xpassed += 1
            self.results.append((name, "XPASS", "now matches ZEsarUX: turn this into a plain check"))
        elif self.strict:
            self.failed += 1
            self.results.append((name, "FAIL", detail))
        else:
            self.known += 1
            self.results.append((name, "KNOWN", detail))

    def run_step(self, name, func):
        try:
            ok = func()
            if ok is None:
                ok = True
            self.check(name, bool(ok))
        except Skip as e:
            self.skipped += 1
            self.results.append((name, "SKIP", str(e)))
        except Exception as e:  # noqa: BLE001
            self.check(name, False, f"{type(e).__name__}: {e}")
            self.resync()

    def resync(self):
        """After a failed step: interrupt a run still going, drain every pending answer, re-synchronize"""
        sock = self.client.sock
        if sock is None:
            return
        try:
            self.client.send_blank()
            deadline = time.time() + 10
            sock.settimeout(0.7)
            while time.time() < deadline:
                try:
                    if not sock.recv(65536):
                        break
                except socket.timeout:
                    break
            self.client._buffer = b""
            self.sync()
            # A failed step may leave breakpoints behind: DeZog's whole-table clear
            for bp_id in range(1, 101):
                self.client.command(f"disable-breakpoint {bp_id}")
                self.client.command(f"set-breakpoint {bp_id} 0")
            self.client.command("clear-membreakpoints")
            self.client.command("set-debug-settings 0")
        except Exception as e:  # noqa: BLE001
            print(f"  (resync failed: {e})")

    # --- protocol helpers ---

    def cmd(self, line: str, timeout=None) -> str:
        word = line.split(" ", 1)[0]
        self.coverage.add(word if word else "<blank>")
        return self.client.command(line, timeout)

    def ok(self, line: str):
        reply = self.cmd(line)
        assert reply == "", f"{line!r} -> {reply!r}"

    def expect(self, line: str, expected: str):
        reply = self.cmd(line)
        assert reply == expected, f"{line!r} -> {reply!r}, expected {expected!r}"

    def regs(self) -> dict:
        return parse_registers(self.cmd("get-registers"))

    def set_regs(self, **values):
        for name, value in values.items():
            self.ok(f"set-register {name.replace('_', chr(39))}={value}")

    def read_mem(self, addr: int, size: int) -> bytes:
        return bytes.fromhex(self.cmd(f"read-memory {addr} {size}"))

    def write_mem(self, addr: int, data: bytes):
        self.ok(f"write-memory-raw {addr} {data.hex().upper()}")

    def run_to_stop(self, timeout: float = 15.0):
        """run until the emulator stops by itself: (stop line or '', registers)"""
        self.coverage.add("run")
        banner, rest = self.client.run(timeout)
        assert banner == RUN_BANNER, f"run banner {banner!r}"
        lines = rest.split("\n")
        fired = lines[0] if lines[0].startswith(FIRED) else ""
        disasm = lines[-1]
        regs = self.regs()
        assert parse_disasm_line(disasm)[0] == regs["PC"], f"stop line {disasm!r} != PC {regs['PC']:04X}"
        return fired, regs

    def run_interrupted(self, delay: float = 0.25):
        """run, then DeZog's pause (a bare newline): (whole stop output, registers)"""
        self.coverage.add("run")
        self.client.send_line("run")
        banner = self.client.read_line()
        assert banner == RUN_BANNER, f"run banner {banner!r}"
        time.sleep(delay)
        self.client.send_blank()
        rest = self.client.read_until_prompt(timeout=10.0)
        # When the run stopped by itself before the blank line arrived, the server answers that line as an empty
        # command: a second prompt, possibly in the same chunk. Cut it off and re-synchronize
        rest = rest.split(PROMPT)[0].rstrip("\n")
        self.sync()
        return rest, self.regs()

    def sync(self):
        """Reads past stray prompts until a get-version answer arrives"""
        self.client.send_line("get-version")
        for _ in range(4):
            reply = self.client.read_until_prompt(timeout=5.0)
            if reply.split(PROMPT)[-1].strip() == SERVER_VERSION:
                return
        raise AssertionError("lost the prompt framing")

    def bp(self, bp_id: int, condition: str):
        """DeZog's breakpoint creation sequence (zesaruxremote.ts setBreakpointZesarux)"""
        self.ok(f"set-breakpoint {bp_id} {condition}")
        self.ok(f"set-breakpointaction {bp_id}")
        self.ok(f"enable-breakpoint {bp_id}")

    def clear_bp(self, bp_id: int):
        """DeZog's removal idiom"""
        self.ok(f"disable-breakpoint {bp_id}")
        self.ok(f"set-breakpoint {bp_id} 0")

    def core_bps(self, addr=None):
        return self.webapi.adapter_breakpoints(addr) if self.webapi else None

    def install(self, blocks: dict, pc: int = PROGRAM_START, **regs):
        for addr, data in blocks.items():
            self.write_mem(addr, data)
        self.set_regs(PC=pc, SP=STACK_TOP, **regs)

    def install_program(self):
        self.install({PROGRAM_START: PROGRAM, WATCH_TARGET: b"\x00"})

    def install_counter(self):
        # (9000h) = 77h, (9001h) = 80h (high byte for PEEKW), (9002h) = 5Ah (read by the loop); C = 2; F = 0
        self.install({PROGRAM_START: COUNTER, 0x9000: bytes([0x77, 0x80, 0x5A])}, C=2, F=0, A=0)

    def save_state(self):
        self.saved_regs = self.regs()
        for base, size in SAVED_RANGES:
            self.saved_memory[base] = self.read_mem(base, size)

    def restore_state(self):
        for base, data in self.saved_memory.items():
            self.write_mem(base, data)
        r = self.saved_regs
        if not r:
            return
        for name, key in (("AF", "AF"), ("BC", "BC"), ("DE", "DE"), ("HL", "HL"), ("IX", "IX"), ("IY", "IY"),
                          ("AF'", "AF2"), ("BC'", "BC2"), ("DE'", "DE2"), ("HL'", "HL2"), ("I", "I"),
                          ("R", "R"), ("SP", "SP"), ("PC", "PC")):
            self.ok(f"set-register {name}={r[key]}")
        self.ok(f"set-register IM={r['im']}")

    # --- session and init ---

    def step_before_cpu_step(self):
        # A fresh session refuses execution until enter-cpu-step (ZEsarUX remote.c text)
        if not self.client.connect():
            return False
        assert self.client.banner[0] == (f"Welcome to {SERVER_NAME} - ZEsarUX ZRCP {SERVER_VERSION} "
                                         f"compatible debug server"), self.client.banner
        self.expect("cpu-step", NOT_IN_STEP)
        self.expect("cpu-step-over", NOT_IN_STEP)
        self.expect("run", NOT_IN_STEP)  # no "Running until" banner either
        self.ok("quit")
        closed = self.client.socket_closed()
        self.client.disconnect()
        return closed

    def step_connect(self):
        if not self.client.connect():
            return False
        return len(self.client.banner) == 1 and self.client.banner[0].startswith("Welcome to ")

    def step_init_sequence(self):
        # The exact command stream DeZog's ZesaruxRemote sends on connect,
        # order included (zesaruxremote.ts connectToRemote + initSequence).
        # Fails iff the real extension would fail to attach.
        self.ok("close-all-menus")
        self.expect("about", f"{SERVER_NAME} ZRCP debug server (ZEsarUX {SERVER_VERSION} wire compatible)")
        self.expect("get-version", SERVER_VERSION)
        self.ok("set-debug-settings 0")
        # Log-only answers complete with an empty result (DeZog strips the
        # "log> " line before its prompt check and forwards it to the UI)
        self.client.logs.clear()
        self.ok("hard-reset-cpu")
        assert len(self.client.logs) == 1 and "hard-reset-cpu" in self.client.logs[0], self.client.logs
        self.ok("enter-cpu-step")
        self.ok("enter-cpu-step")  # idempotent
        self.client.logs.clear()
        self.ok("load /tmp/game.z80")
        assert len(self.client.logs) == 1 and "load request ignored" in self.client.logs[0], self.client.logs
        self.client.logs.clear()
        self.ok("smartload /tmp/game.z80")
        assert len(self.client.logs) == 1 and "load request ignored" in self.client.logs[0], self.client.logs
        self.machine = self.cmd("get-current-machine")
        assert self.machine in ("ZX Spectrum 128K", "ZX Spectrum 48K"), self.machine
        self.banked = "128K" in self.machine
        self.ok("clear-membreakpoints")
        self.ok("enable-breakpoints")
        for bp_id in range(1, 101):  # DeZog clears its whole 100-slot table
            self.ok(f"disable-breakpoint {bp_id}")
        self.ok("cpu-code-coverage enabled no")
        self.ok("cpu-code-coverage get")
        self.ok("extended-stack enabled yes")
        self.ok("cpu-history enabled yes")
        self.ok("cpu-history set-max-size 8192")
        self.ok("cpu-history clear")
        self.ok("cpu-history started yes")
        self.ok("cpu-history ignrephalt yes")
        self.ok("cpu-history ignrepldxr yes")
        self.expect("cpu-history is-enabled", "1")
        self.expect("cpu-history is-started", "1")
        if self.webapi:
            # enter-cpu-step pauses the machine (ZEsarUX parity)
            deadline = time.time() + 3
            while not self.webapi.emulator().get("is_paused") and time.time() < deadline:
                time.sleep(0.05)
            state = self.webapi.emulator()
            assert state.get("is_paused") is True, state
        return True

    # --- registers ---

    def step_registers_line(self):
        regs = self.regs()
        self.save_state()
        ok = (all(c in "SZ5H3PNC-" for c in regs["flags"] + regs["flags2"])
              and len(regs["flags"]) == 8
              and regs["memptr"] == 0
              and regs["im"] in (0, 1, 2)
              and regs["iff"] == "--"
              and len(regs["mmu"]) == 8
              and regs["mmu"][4:] == regs["mmu"][:4])  # groups 5-8 repeat
        if self.banked:
            ok = ok and regs["mmu"][0] >= 0x8000 and regs["mmu"][1] == 5 and regs["mmu"][2] == 2
        else:
            ok = ok and regs["mmu"][:4] == [0x8000, 5, 2, 0]
        # Flag letters follow F bit by bit (S Z 5 H 3 P N C)
        line = self.cmd("get-registers")
        expected_flags = "".join(c if regs["AF"] & (0x80 >> i) else "-" for i, c in enumerate("SZ5H3PNC"))
        return ok and parse_registers(line)["flags"] == expected_flags

    def step_set_register_pairs(self):
        # DeZog sends decimal; every pair of the name table, read back from get-registers
        values = {"PC": 0x1234, "SP": 0xFE80, "AF": 0x1F2E, "BC": 0x3D4C, "DE": 0x5B6A, "HL": 0x7988,
                  "IX": 0x97A6, "IY": 0xB5C4, "AF'": 0xD3E2, "BC'": 0xF101, "DE'": 0x1223, "HL'": 0x3445}
        for name, value in values.items():
            self.ok(f"set-register {name}={value}")
        regs = self.regs()
        bad = [n for n, v in values.items() if regs[n.replace("'", "2")] != v]
        assert not bad, f"pairs not set: {bad} ({regs})"
        return True

    def step_set_register_bytes(self):
        self.set_regs(BC=0, DE=0, HL=0, IX=0, IY=0, AF=0)
        self.set_regs(A=0x11, F=0xFF, B=0x22, C=0x33, D=0x44, E=0x55, H=0x66, L=0x77,
                      IXH=0x88, IXL=0x99, IYH=0xAA, IYL=0xBB)
        self.set_regs(**{"A_": 0x12, "F_": 0x41, "B_": 0x34, "C_": 0x56, "D_": 0x78, "E_": 0x9A,
                         "H_": 0xBC, "L_": 0xDE})
        self.set_regs(I=0x3F, R=0x55, IM=2)
        r = self.regs()
        expected = {"AF": 0x11FF, "BC": 0x2233, "DE": 0x4455, "HL": 0x6677, "IX": 0x8899, "IY": 0xAABB,
                    "AF2": 0x1241, "BC2": 0x3456, "DE2": 0x789A, "HL2": 0xBCDE, "I": 0x3F, "R": 0x55}
        bad = {k: f"{r[k]:04X}" for k, v in expected.items() if r[k] != v}
        assert not bad, f"bytes not set: {bad}"
        assert r["im"] == 2, r["im"]
        assert r["flags"] == "SZ5H3PNC" and r["flags2"] == "-Z-----C", (r["flags"], r["flags2"])
        self.ok(f"set-register IM={self.saved_regs.get('im', 1)}")
        return True

    def step_set_register_formats(self):
        # ZEsarUX parse_string_to_number: decimal, 0x / # prefix, h suffix; names case-insensitive
        for text, value in (("4660", 0x1234), ("0x2345", 0x2345), ("#3456", 0x3456), ("4567h", 0x4567),
                            ("4567H", 0x4567)):
            self.ok(f"set-register HL={text}")
            assert self.regs()["HL"] == value, text
        self.ok("set-register hl=22136")
        assert self.regs()["HL"] == 0x5678
        self.ok("set-register HL=74565")  # 0x12345: masked to 16 bits
        assert self.regs()["HL"] == 0x2345
        self.ok("set-register IFF1=0")  # IFF is not modeled: acknowledged and ignored
        self.ok("set-register IFF2=1")
        self.expect("set-register PC", "Error. Unknown parameter")
        self.expect("set-register PC=zz", "Error. Unknown parameter")
        self.expect("set-register XY=1", "Error. Unknown register XY")
        return True

    # --- memory ---

    def step_memory_roundtrip(self):
        self.write_mem(0x8100, bytes([0xDE, 0xAD, 0xBE, 0xEF]))
        assert self.read_mem(0x8100, 4) == bytes([0xDE, 0xAD, 0xBE, 0xEF])
        # Hex is accepted in any case and split over several words; the address in every number form
        self.ok("write-memory-raw 0x8104 ca fe")
        self.ok("write-memory-raw 8106h 12 34 56")
        self.ok("write-memory-raw #8109 78")
        assert self.read_mem(0x8100, 10).hex() == "deadbeefcafe12345678"
        self.expect("read-memory 8104h 2", "CAFE")  # uppercase on the wire
        self.expect("read-memory #8106 3", "123456")
        self.expect("read-memory 0x8109", "78")  # length defaults to 1
        self.expect("read-memory 33024 0", "")
        self.expect("read-memory 33024 -3", "")
        return True

    def step_memory_errors(self):
        self.expect("read-memory", NO_PARAM)
        self.expect("read-memory xyz 4", NO_PARAM)
        self.expect("write-memory-raw", NO_PARAM)
        self.expect("write-memory-raw 33024", NO_PARAM)
        self.expect("write-memory-raw xyz 00", NO_PARAM)
        self.expect("write-memory-raw 33024 ABC", "Error. Invalid data")
        self.expect("write-memory-raw 33024 ZZ", "Error. Invalid data")
        # Nothing was written by the refused requests
        return self.read_mem(0x8100, 2) == bytes([0xDE, 0xAD])

    def step_memory_full_64k(self):
        # DeZog's disassembly view fetches the whole space in one request and
        # asserts the answer is exactly len*2 hex chars (fetch64kMemory)
        full = bytes.fromhex(self.cmd("read-memory 0 65536"))
        return (len(full) == 65536
                and full == self.read_mem(0, 32768) + self.read_mem(32768, 32768)
                and self.cmd("read-memory 0 65537") == "Error. Invalid length"
                and self.read_mem(0xFFFF, 2) == full[0xFFFF:] + full[:1])

    # --- disassembly ---

    def step_disassemble(self):
        self.install_counter()
        lines = self.cmd(f"disassemble {PROGRAM_START} {len(COUNTER_PCS)}").split("\n")
        assert len(lines) == len(COUNTER_PCS), lines
        zone = self.regs()["mmu"][2]
        for line, pc, prefix in zip(lines, COUNTER_PCS, COUNTER_PREFIXES):
            addr, digit, mnemonic = parse_disasm_line(line)
            assert addr == pc and mnemonic.startswith(prefix), f"{line!r}: expected {pc:04X} {prefix}"
            assert mnemonic == mnemonic.upper(), line
            assert digit == (zone & 0xF), f"zone digit {digit} != bank {zone}"
        # Default address = PC, default count 1; count < 1 gives one line; every number form
        assert self.cmd("disassemble") == lines[0]
        assert self.cmd("disassemble 32768 0") == lines[0]
        assert self.cmd("disassemble 8001h") == lines[1]
        assert self.cmd("disassemble 0x8003 1") == lines[2]
        rom = parse_disasm_line(self.cmd("disassemble 0"))
        assert rom[0] == 0 and rom[1] == (self.regs()["mmu"][0] & 1), rom
        self.expect("disassemble zz", "Error. Invalid address")
        return True

    # --- stepping ---

    def step_cpu_step(self):
        self.install_counter()
        seen = []
        for _ in range(3):
            addr, _, mnemonic = parse_disasm_line(self.cmd("cpu-step"))
            seen.append((addr, mnemonic))
        r = self.regs()
        # DI, LD B,0, INC B executed: the step output names the NEXT instruction
        return ([a for a, _ in seen] == [0x8001, 0x8003, 0x8004]
                and seen[2][1].startswith("LD A,B")
                and r["PC"] == 0x8004 and (r["BC"] >> 8) == 1)

    def step_cpu_step_with_registers(self):
        # set-debug-settings bit 0: registers line (TSTATES suffix) + disassembly line
        self.install_counter()
        self.ok("set-debug-settings 1")
        out = self.cmd("cpu-step")
        self.ok("set-debug-settings 0")
        lines = out.split("\n")
        assert len(lines) == 2 and lines[0].endswith(" TSTATES: 0"), lines
        regs = parse_registers(lines[0])
        assert regs["PC"] == 0x8001 and parse_disasm_line(lines[1])[0] == 0x8001, lines
        self.expect("set-debug-settings", NO_PARAM)
        self.expect("set-debug-settings abc", NO_PARAM)
        return len(self.cmd("cpu-step").split("\n")) == 1  # back to one line

    def step_cpu_step_into_call(self):
        self.install(CALLER, pc=0x8001)
        addr, _, mnemonic = parse_disasm_line(self.cmd("cpu-step"))
        r = self.regs()
        return (addr == 0x8010 and mnemonic.startswith("NOP") and r["PC"] == 0x8010
                and r["SP"] == STACK_TOP - 2 and self.read_mem(STACK_TOP - 2, 2) == bytes([0x04, 0x80]))

    def step_cpu_step_over_call(self):
        # CALL: temporary breakpoint after it, exactly ONE line (no banner, no break echo)
        self.install(CALLER, pc=0x8001)
        out = self.cmd("cpu-step-over")
        addr, _, mnemonic = parse_disasm_line(out)
        r = self.regs()
        return (addr == 0x8004 and mnemonic.startswith("JR") and r["PC"] == 0x8004 and r["SP"] == STACK_TOP
                and "\n" not in out)

    def step_cpu_step_over_plain(self):
        self.install(CALLER, pc=0x8010)
        addr, _, _ = parse_disasm_line(self.cmd("cpu-step-over"))  # NOP: a single step
        r = self.regs()
        return addr == 0x8011 and r["PC"] == 0x8011

    def step_cpu_step_over_ldir(self):
        pattern = bytes(range(0x30, 0x40))
        self.install({PROGRAM_START: BLOCK, 0x9100: pattern, 0x9200: bytes(16)})
        for _ in range(3):
            self.cmd("cpu-step")
        assert self.regs()["PC"] == 0x8009
        addr, _, mnemonic = parse_disasm_line(self.cmd("cpu-step-over"))
        r = self.regs()
        return (addr == 0x800B and mnemonic.startswith("NOP") and r["BC"] == 0
                and r["HL"] == 0x9110 and r["DE"] == 0x9210 and self.read_mem(0x9200, 16) == pattern)

    def step_cpu_step_over_rst(self):
        # RST 38h runs the 48K ROM's IM 1 handler (PUSH AF; PUSH HL ... EI; RET); it needs the ROM's IY (5C3Ah).
        # It ends with EI: DI after it
        handler = self.read_mem(0x0038, 2)
        if handler != bytes([0xF5, 0xE5]):
            raise Skip(f"the ROM at 0038h is not the 48K BASIC one ({handler.hex()})")
        self.install({PROGRAM_START: bytes([0xFF, 0xF3, 0x18, 0xFE])}, IY=0x5C3A)
        addr, _, mnemonic = parse_disasm_line(self.cmd("cpu-step-over"))
        r = self.regs()
        self.cmd("cpu-step")  # DI
        return addr == 0x8001 and mnemonic.startswith("DI") and r["PC"] == 0x8001 and r["SP"] == STACK_TOP

    def step_cpu_step_onto_breakpoint(self):
        # ZEsarUX cpu-step always executes one instruction, a breakpoint at PC or not
        self.install_counter()
        self.bp(12, "PC=8003h")
        try:
            pcs = [parse_disasm_line(self.cmd("cpu-step"))[0] for _ in range(3)]
        finally:
            self.clear_bp(12)
        r = self.regs()
        assert pcs == [0x8001, 0x8003, 0x8004], f"step PCs {[f'{pc:04X}' for pc in pcs]}: the step at the " \
                                                 f"breakpoint address did not execute (B={r['BC'] >> 8})"
        return r["BC"] >> 8 == 1

    def step_cpu_step_after_stop_and_browse(self):
        # DeZog after every stop: registers, then the history spot (cpu-history get 0), then the user steps
        self.install_counter()
        self.bp(13, "PC=8005h")
        try:
            fired, _ = self.run_to_stop()
            assert fired == "Breakpoint fired: PC=8005h", fired
            assert parse_history_entry(self.cmd("cpu-history get 0"))["PC"] == 0x8004
            addr = parse_disasm_line(self.cmd("cpu-step"))[0]
        finally:
            self.clear_bp(13)
        assert addr == 0x8008, f"cpu-step after the stop + history read stayed at {addr:04X}"
        return self.read_mem(0x9000, 1) == b"\x01"

    def step_exit_cpu_step(self):
        self.ok("exit-cpu-step")
        self.expect("run", NOT_IN_STEP)
        self.expect("cpu-step", NOT_IN_STEP)
        self.ok("enter-cpu-step")
        self.install_counter()
        return parse_disasm_line(self.cmd("cpu-step"))[0] == 0x8001

    # --- run and breakpoints ---

    def step_run_breakpoint(self):
        self.install_program()
        self.bp(1, "PC=08006h")
        fired, regs = self.run_to_stop()
        self.clear_bp(1)
        return fired == "Breakpoint fired: PC=08006h" and regs["PC"] == PROGRAM_JP

    def step_run_interrupt(self):
        # No breakpoint: the blank line stops MANUALLY, no "Breakpoint fired", one disassembly line at PC
        self.install_program()
        rest, regs = self.run_interrupted()
        lines = rest.split("\n")
        return (len(lines) == 1 and parse_disasm_line(lines[0])[0] == regs["PC"]
                and regs["PC"] in PROGRAM_PCS and self.read_mem(WATCH_TARGET, 1) == b"\x01")

    def step_run_condition_false(self):
        # A is preset to 1 and the loop only ever loads A=1: the condition
        # never holds, so the breakpoint must silently auto-resume. The blank
        # line then stops MANUALLY: no "Breakpoint fired" in the output.
        self.install_program()
        self.set_regs(A=1)
        self.bp(2, "PC=08001h and (A<>1)")
        rest, regs = self.run_interrupted()
        self.clear_bp(2)
        return FIRED not in rest and rest != "" and regs["PC"] in PROGRAM_PCS

    def step_run_condition_true(self):
        self.install_program()
        self.bp(3, "PC=08001h and (A=1)")
        fired, regs = self.run_to_stop()
        self.clear_bp(3)
        return fired == "Breakpoint fired: PC=08001h and (A=1)" and regs["PC"] == PROGRAM_LOOP

    def step_pass_count(self):
        # Hit 1 at entry (PC=8001h after DI), stops on the 3rd: (9000h) was written twice
        self.install_program()
        self.ok("set-breakpoint 4 PC=08001h")
        self.ok("set-breakpointaction 4")
        self.ok("set-breakpointpasscount 4 3")
        self.ok("enable-breakpoint 4")
        self.write_mem(WATCH_TARGET, b"\x00")
        fired, regs = self.run_to_stop()
        self.clear_bp(4)
        return fired == "Breakpoint fired: PC=08001h" and regs["PC"] == PROGRAM_LOOP

    def step_pass_count_once(self):
        # ZEsarUX debug.c fires a pass-counted breakpoint once (counter == pass count), never again
        self.install_counter()
        self.ok("set-breakpoint 5 PC=8003h")
        self.ok("set-breakpointpasscount 5 3")
        self.ok("enable-breakpoint 5")
        fired, regs = self.run_to_stop()
        assert fired == "Breakpoint fired: PC=8003h" and (regs["BC"] >> 8) == 2, (fired, regs["BC"])
        rest, regs = self.run_interrupted()
        self.clear_bp(5)
        self.known_divergence("pass count fires once (ZEsarUX), the server fires on every later hit",
                              FIRED not in rest,
                              f"2nd run stopped again: {rest.splitlines()[0]!r} B={regs['BC'] >> 8} "
                              f"(zrcpserver.cpp breakpointShouldStop: hitCount < passCount)")
        return True

    def step_pass_count_reset_by_set(self):
        # ZEsarUX remote_set_breakpoint calls debug_set_breakpoint(..., pass_count 0): set-breakpoint drops it
        self.install_counter()
        self.ok("set-breakpointpasscount 6 3")
        self.ok("set-breakpoint 6 PC=8003h")
        self.ok("enable-breakpoint 6")
        fired, regs = self.run_to_stop()
        self.clear_bp(6)
        assert fired == "Breakpoint fired: PC=8003h", fired
        self.known_divergence("set-breakpoint resets the pass count (ZEsarUX)", (regs["BC"] >> 8) == 0,
                              f"stopped at B={regs['BC'] >> 8}, expected 0 (zrcpserver.cpp handleSetBreakpoint "
                              f"keeps entry.passCount)")
        return True

    def step_breakpoint_errors(self):
        self.expect("set-breakpoint", "Error. No parameters set")
        self.expect("set-breakpoint abc PC=8000h", "Error. Index out of range")
        # Unknown ids are tolerated (DeZog's 100 disables on init)
        self.ok("disable-breakpoint 99")
        self.ok("disable-breakpoint")
        self.ok("enable-breakpoint")
        self.ok("set-breakpointpasscount")
        self.ok("set-breakpointpasscount 7")
        # A condition without an address can not fire: logged, acknowledged
        self.client.logs.clear()
        self.ok("set-breakpoint 7 A=1")
        assert len(self.client.logs) == 1 and "can not fire" in self.client.logs[0], self.client.logs
        self.ok("set-breakpoint 7 0")
        # ZEsarUX: index 1..100 only ("Error. Index out of range" for 0, 101)
        replies = [self.cmd("set-breakpoint 0 PC=8000h"), self.cmd("set-breakpoint 101 PC=8000h")]
        self.ok("set-breakpoint 0 0")
        self.ok("set-breakpoint 101 0")
        self.known_divergence("set-breakpoint index range 1..100 (ZEsarUX)",
                              replies == ["Error. Index out of range"] * 2,
                              f"replies {replies} (zrcpserver.cpp handleSetBreakpoint has no range check)")
        return True

    def step_breakpoint_action(self):
        # Actions are acknowledged and ignored: the breakpoint still stops
        self.install_counter()
        self.ok("set-breakpoint 8 PC=8003h AND B=2")
        self.ok("set-breakpointaction 8 printe hello")
        self.ok("enable-breakpoint 8")
        fired, regs = self.run_to_stop()
        self.clear_bp(8)
        return fired == "Breakpoint fired: PC=8003h AND B=2" and (regs["BC"] >> 8) == 2

    def step_step_over_condition(self):
        # DeZog's step-over: "SP>=<sp>" without a PC literal, resolved to the instruction after the CALL
        self.install(CALLER, pc=0x8001)
        self.bp(9, f"SP>={STACK_TOP}")
        fired, regs = self.run_to_stop()
        self.clear_bp(9)
        return fired == f"Breakpoint fired: SP>={STACK_TOP}" and regs["PC"] == 0x8004 and regs["SP"] == STACK_TOP

    def step_step_out_condition(self):
        # DeZog's step-out: "PC=PEEKW(SP-2) AND SP>=<bpSp>" from inside the subroutine
        self.install(CALLER, pc=0x8001)
        self.cmd("cpu-step")
        assert self.regs()["PC"] == 0x8010
        condition = f"PC=PEEKW(SP-2) AND SP>={STACK_TOP}"
        self.bp(10, condition)
        fired, regs = self.run_to_stop()
        self.clear_bp(10)
        return fired == FIRED + condition and regs["PC"] == 0x8004 and regs["SP"] == STACK_TOP

    # --- conditional breakpoints (core translation + server evaluator) ---

    def counter_stop(self, bp_id: int, condition: str, expected_b: int, core: str = None, expected_mem=None,
                     check_counts: bool = True):
        """Runs the counter loop to `condition`; asserts the stop and, with the WebAPI, the core breakpoint.
        core: the condition the core breakpoint shows (its canonical form of the translation: outer brackets
        dropped, numbers below 10 decimal), '' = none (the server evaluates), None = no check"""
        self.install_counter()
        self.bp(bp_id, condition)
        try:
            fired, regs = self.run_to_stop()
            b = regs["BC"] >> 8
            assert fired == FIRED + condition, f"{fired!r}"
            assert regs["PC"] == COUNTER_BP and b == expected_b, f"PC={regs['PC']:04X} B={b}, expected {expected_b}"
            if expected_mem is not None:
                mem = self.read_mem(0x9000, 1)[0]
                assert mem == expected_mem, f"(9000h)={mem:02X}, expected {expected_mem:02X}"
            if self.webapi and core is not None:
                bps = self.core_bps(COUNTER_BP)
                assert len(bps) == 1, f"core breakpoints at 8003h: {bps}"
                got = bps[0].get("condition", "")
                assert got == core, f"core condition {got!r}, expected {core!r}"
                if check_counts:
                    # Each arrival at 8003h is a match; with the condition in the core only the last one stopped
                    arrivals = (expected_b if expected_b else 256) + 1
                    if core:
                        assert bps[0].get("match_count") == arrivals and bps[0].get("hit_count") == 1, bps[0]
                    else:
                        assert bps[0].get("hit_count") == arrivals, bps[0]
        finally:
            self.clear_bp(bp_id)
        return True

    def step_cond_register(self):
        # The task's headline case: stops with B == 5, not before (the first stop is the one reported)
        return self.counter_stop(20, "PC=8003h AND B=5", 5, "B == 5")

    def step_cond_lowercase(self):
        return self.counter_stop(20, "pc=8003h and b=4", 4, "B == 4")

    def step_cond_peek(self):
        return self.counter_stop(21, "PC=8003h AND PEEK(9000h)=4", 4, "M(#9000) == 4", expected_mem=4)

    def step_cond_peekw(self):
        return self.counter_stop(22, "PC=8003h AND PEEKW(9000h)=8006h", 6, "W(#9000) == #8006", expected_mem=6)

    def step_cond_not(self):
        return self.counter_stop(23, "PC=8003h AND NOT B<6", 6, "!(B < 6)")

    def step_cond_not_equal(self):
        return self.counter_stop(24, "PC=8003h AND B<>0 AND B<>1 AND B!=2", 3,
                                 "((B != 0) && (B != 1)) && (B != 2)")

    def step_cond_or_arith(self):
        return self.counter_stop(25, "PC=8003h AND (B>3 OR C=9) AND B*2+1=11", 5,
                                 "((B > 3) || (C == 9)) && (((B * 2) + 1) == #B)")

    def step_cond_const_division(self):
        return self.counter_stop(26, "PC=8003h AND B/2=4", 8, "(B / 2) == 4")

    def step_cond_flag_h(self):
        # H flag (bit 4) of INC B: set when B's low nibble wraps, first at B = 16
        return self.counter_stop(27, "PC=8003h AND (F/16)-(F/32)*2=1", 16,
                                 "((F / #10) - ((F / #20) * 2)) == 1", check_counts=True)

    def step_cond_flag_z(self):
        # Z flag (bit 6): B wraps to 0 after 256 increments; (9000h) = 00h then (77h on the first arrival)
        return self.counter_stop(28, "PC=8003h AND (F/64)-(F/128)*2=1", 0,
                                 "((F / #40) - ((F / #80) * 2)) == 1", expected_mem=0x00)

    def ram_bank_at_counter(self):
        """The bank the server's evaluator sees as RAM at 8003h: the MMU word on banked machines; on 48K the
        evaluator reads DeZog's 48K slot model ({0, 1}: bank 1 at 4000h-FFFFh), not the MMU get-registers prints"""
        return self.regs()["mmu"][2] if self.banked else 1

    def step_cond_ram_matches_mmu(self):
        # On 48K get-registers prints MMU=8000 0005 0002 0000 (the ZEsarUX 48K layout: bank 2 at 8000h) and the
        # disassembly zone digit says 2, but RAM in a condition is 1 there (the DeZog 48K slot model)
        if self.banked:
            raise Skip("banked machine: RAM and MMU come from the same slots")
        mmu = self.regs()["mmu"][2]
        self.install_counter()
        self.bp(36, f"PC=8003h AND B=2 AND RAM={mmu}")
        rest, regs = self.run_interrupted(1.0)
        self.clear_bp(36)
        assert rest.startswith(FIRED) and regs["BC"] >> 8 == 2, \
            (f"RAM={mmu} (the MMU bank get-registers reports at 8000h) never holds: zesaruxcondition.cpp "
             f"bankAtBreakpoint reads getSlots() = {{0, 1}} (dezogdebugadapter.cpp getSlots, ZX48K), while "
             f"zrcpserver.cpp mmuWordForSlot prints the ZEsarUX 48K layout ROM0, 5, 2, 0")
        return True

    def step_cond_refused_bank(self):
        # RAM (the bank at the breakpoint address) has no core operand: the core stops on every arrival,
        # the server's evaluator decides
        bank = self.ram_bank_at_counter()
        return self.counter_stop(29, f"PC=8003h AND B=6 AND RAM={bank}", 6, "")

    def step_cond_refused_bank_false(self):
        # A RAM bank that is not at 8000h: never stops (silent resumes), the blank line stops MANUALLY
        bank = self.ram_bank_at_counter()
        self.install_counter()
        self.bp(30, f"PC=8003h AND RAM={bank + 1}")
        rest, regs = self.run_interrupted()
        hits = self.core_bps(COUNTER_BP)
        self.clear_bp(30)
        assert FIRED not in rest, rest
        if hits is not None:
            assert len(hits) == 1 and "condition" not in hits[0] and hits[0]["hit_count"] > 10, hits
        return True

    def step_cond_refused_division(self):
        # A division by a register stays in the server (C = 2: 8/2 = 4 first)
        return self.counter_stop(31, "PC=8003h AND B/C=4", 8, "")

    def step_cond_unknown_name(self):
        # An unknown name: the evaluator fails to parse and stops conservatively on the first arrival
        return self.counter_stop(32, "PC=8003h AND XYZ=1", 0, "", check_counts=False)

    def step_cond_zesarux_flag_names(self):
        # ZEsarUX expression_parser.c knows FS FZ FP FV FH FN FC; expected: the first stop at B = 16
        # The server lexes FH as the hex number Fh (15 = 1 is false): it never stops, the blank line ends the run
        self.install_counter()
        self.bp(33, "PC=8003h AND FH=1")
        rest, regs = self.run_interrupted(1.0)
        self.clear_bp(33)
        b = regs["BC"] >> 8
        stopped = rest.startswith(FIRED) and regs["PC"] == COUNTER_BP
        self.known_divergence("ZEsarUX flag names (FH=1)", stopped and b == 16,
                              f"{'stopped at B=%d' % b if stopped else 'never stopped'}, expected B=16: "
                              f"zesaruxcondition.cpp has no flag names (FH lexes as the number Fh)")
        return True

    def step_cond_bitwise(self):
        # ZEsarUX has & | ^ (docs/features/conditional-breakpoints.md lists them as taken too); expected B = 16
        self.install_counter()
        self.bp(34, "PC=8003h AND (F & 10h)<>0")
        fired, regs = self.run_to_stop()
        self.clear_bp(34)
        b = regs["BC"] >> 8
        self.known_divergence("bitwise & in a condition", b == 16,
                              f"stopped at B={b}, expected 16: the evaluator and translator have no & | ^ %")
        return fired.startswith(FIRED)

    def step_cond_pc_not_first(self):
        # ZEsarUX evaluates the whole text; the server cuts the PC literal out anywhere: "B=5 AND " remains
        self.install_counter()
        self.bp(35, "B=5 AND PC=8003h")
        fired, regs = self.run_to_stop()
        self.clear_bp(35)
        b = regs["BC"] >> 8
        self.known_divergence("PC literal not first ('B=5 AND PC=8003h')", b == 5,
                              f"stopped at B={b}, expected 5: zesaruxcondition.cpp extractPcLiteral leaves "
                              f"'B=5 AND ' (trimLeadingAnd strips a leading AND only), the parse fails")
        return regs["PC"] == COUNTER_BP

    def step_cond_two_same_address(self):
        # Two breakpoints at 8003h: the one whose condition holds reports
        self.install_counter()
        self.bp(40, "PC=8003h AND B=9")
        self.bp(41, "PC=8003h AND B=4")
        try:
            if self.webapi:
                conditions = sorted(bp.get("condition", "") for bp in self.core_bps(COUNTER_BP))
                assert conditions == ["B == 4", "B == 9"], conditions
            fired1, regs1 = self.run_to_stop()
            fired2, regs2 = self.run_to_stop()
        finally:
            self.clear_bp(40)
            self.clear_bp(41)
        assert fired1 == "Breakpoint fired: PC=8003h AND B=4" and regs1["BC"] >> 8 == 4, (fired1, regs1["BC"])
        assert fired2 == "Breakpoint fired: PC=8003h AND B=9" and regs2["BC"] >> 8 == 9, (fired2, regs2["BC"])
        return True

    def step_cond_two_mixed(self):
        # One refused (server evaluates, the core stops every time) and one translated at the same address
        bank = self.ram_bank_at_counter()
        self.install_counter()
        refused = f"PC=8003h AND B=3 AND RAM={bank}"
        self.bp(42, refused)
        self.bp(43, "PC=8003h AND B=7")
        try:
            fired1, regs1 = self.run_to_stop()
            fired2, regs2 = self.run_to_stop()
        finally:
            self.clear_bp(42)
            self.clear_bp(43)
        assert fired1 == FIRED + refused and regs1["BC"] >> 8 == 3, (fired1, regs1["BC"])
        assert fired2 == "Breakpoint fired: PC=8003h AND B=7" and regs2["BC"] >> 8 == 7, (fired2, regs2["BC"])
        return True

    def step_cond_two_refused_clear_one(self):
        # Two entries without a core condition at one address: each has its own core breakpoint, clearing one
        # leaves the other stopping
        bank = self.ram_bank_at_counter()
        self.install_counter()
        first = f"PC=8003h AND B=3 AND RAM={bank}"
        second = f"PC=8003h AND B=6 AND RAM={bank}"
        self.bp(49, first)
        self.bp(50, second)
        try:
            shared = None
            if self.webapi:
                ids = [bp["id"] for bp in self.core_bps(COUNTER_BP)]
                shared = len(ids) == 1
            self.clear_bp(49)
            rest, regs = self.run_interrupted(1.0)
        finally:
            self.clear_bp(49)
            self.clear_bp(50)
        assert rest.split("\n")[0] == FIRED + second and regs["BC"] >> 8 == 6, \
            (f"{rest.splitlines()[0]!r}: clearing 49 removed 50's core breakpoint too" +
             (" (both got the same core breakpoint: the core returns the existing id for a plain breakpoint at "
              "an address already taken, breakpointmanager.cpp AddMemoryBreakpoint)" if shared else ""))
        return True

    def step_step_over_onto_user_breakpoint(self):
        # cpu-step-over a CALL whose return address holds a user breakpoint: the temporary breakpoint must not
        # take the user's with it
        self.install(CALLER, pc=0x8001)
        self.bp(51, "PC=8004h")
        try:
            addr = parse_disasm_line(self.cmd("cpu-step-over"))[0]
            assert addr == 0x8004, f"{addr:04X}"
            core = len(self.core_bps(0x8004)) if self.webapi else None
            rest, regs = self.run_interrupted(1.0)
        finally:
            self.clear_bp(51)
        assert rest.split("\n")[0] == "Breakpoint fired: PC=8004h", \
            (f"after the step-over the user breakpoint at 8004h no longer stops ({rest.splitlines()[0]!r}, "
             f"core breakpoints at 8004h: {core}): zrcpserver.cpp handleCpuStepOver removes the temporary "
             f"breakpoint's id, the core had returned the user's (plain breakpoints share an id)")
        return regs["PC"] == 0x8004

    def step_cond_two_both_true(self):
        # Both hold on the same arrival: one stop, one of them reports
        self.install_counter()
        self.bp(44, "PC=8003h AND B>=2")
        self.bp(45, "PC=8003h AND B=2")
        try:
            fired, regs = self.run_to_stop()
        finally:
            self.clear_bp(44)
            self.clear_bp(45)
        # Which one reports is unspecified (the server's table is an unordered_map)
        assert fired in ("Breakpoint fired: PC=8003h AND B>=2", "Breakpoint fired: PC=8003h AND B=2") \
            and regs["BC"] >> 8 == 2, (fired, regs["BC"] >> 8)
        return True

    def step_cond_pass_count(self):
        # The condition holds at B = 2, 3, 4: the third one stops
        self.install_counter()
        self.ok("set-breakpoint 46 PC=8003h AND B>=2")
        self.ok("set-breakpointaction 46")
        self.ok("set-breakpointpasscount 46 3")
        self.ok("enable-breakpoint 46")
        fired, regs = self.run_to_stop()
        self.clear_bp(46)
        return fired == "Breakpoint fired: PC=8003h AND B>=2" and regs["BC"] >> 8 == 4

    def step_cond_redefine(self):
        # set-breakpoint on a live id replaces its core breakpoint (one entry, the new condition)
        self.install_counter()
        self.bp(47, "PC=8003h AND B=9")
        self.bp(47, "PC=8003h AND B=3")
        if self.webapi:
            bps = self.core_bps(COUNTER_BP)
            assert [bp.get("condition") for bp in bps] == ["B == 3"], bps
        fired, regs = self.run_to_stop()
        self.clear_bp(47)
        return fired == "Breakpoint fired: PC=8003h AND B=3" and regs["BC"] >> 8 == 3

    def step_cond_disable_enable_clear(self):
        """disable / enable / disable-breakpoints / enable-breakpoints / clear reach the core"""
        condition = "PC=8003h AND B=5"

        def core_count():
            return len(self.core_bps(COUNTER_BP)) if self.webapi else None

        def expect_core(count, what):
            got = core_count()
            assert got in (count, None), f"after {what}: {got} core breakpoints at 8003h, expected {count}"

        self.install_counter()
        self.bp(48, condition)
        expect_core(1, "set-breakpoint")
        self.ok("disable-breakpoint 48")
        expect_core(0, "disable-breakpoint")
        rest, _ = self.run_interrupted()
        assert FIRED not in rest, f"disabled breakpoint fired: {rest!r}"

        self.install_counter()
        self.ok("enable-breakpoint 48")
        expect_core(1, "enable-breakpoint")
        fired, regs = self.run_to_stop()
        assert fired == FIRED + condition and regs["BC"] >> 8 == 5, (fired, regs["BC"])

        self.ok("disable-breakpoints")
        expect_core(0, "disable-breakpoints")
        self.install_counter()
        rest, _ = self.run_interrupted()
        assert FIRED not in rest, f"breakpoint fired after disable-breakpoints: {rest!r}"
        self.ok("enable-breakpoints")
        expect_core(1, "enable-breakpoints")
        self.install_counter()
        fired, regs = self.run_to_stop()
        assert fired == FIRED + condition and regs["BC"] >> 8 == 5, (fired, regs["BC"])

        # Clear (DeZog's idiom) and re-enable: nothing comes back, no stops
        self.clear_bp(48)
        self.ok("enable-breakpoint 48")
        expect_core(0, "clear + enable-breakpoint")
        self.install_counter()
        rest, regs = self.run_interrupted()
        assert FIRED not in rest, f"cleared breakpoint fired: {rest!r}"
        self.ok("disable-breakpoint 48")
        return regs["PC"] in COUNTER_PCS

    # --- watchpoints ---

    def step_watchpoint_write(self):
        self.install_program()
        self.ok("set-membreakpoint 9000h 2 1")
        fired, _ = self.run_to_stop()
        self.ok("set-membreakpoint 9000h 0 1")  # DeZog removal
        self.ok("clear-membreakpoints")
        return (fired == "Breakpoint fired: Memory Breakpoint Write Address: 9000H"
                and self.read_mem(WATCH_TARGET, 1) == b"\x01")

    def step_watchpoint_read(self):
        self.install_counter()
        self.ok("set-membreakpoint 9002h 1 1")
        if self.webapi:
            bps = self.core_bps(0x9002)
            assert len(bps) == 1 and bps[0].get("read") and not bps[0].get("write"), bps
        fired, regs = self.run_to_stop()
        self.ok("set-membreakpoint 9002h 0 1")
        assert fired == "Breakpoint fired: Memory Breakpoint Read Address: 9002H", fired
        # The first read is in the first pass: B = 1
        return regs["BC"] >> 8 == 1 and regs["PC"] in (0x8008, 0x800B)

    def step_watchpoint_read_write(self):
        self.install_counter()
        self.ok("set-membreakpoint 9002h 3 1")
        fired, _ = self.run_to_stop()
        self.ok("set-membreakpoint 9002h 0 1")
        return fired == "Breakpoint fired: Memory Breakpoint Read Address: 9002H"

    def step_watchpoint_range(self):
        # 8FFFh, 2 bytes covers 9000h; the access address is reported
        self.install_counter()
        self.ok("set-membreakpoint 8FFFh 2 2")
        fired, regs = self.run_to_stop()
        self.ok("set-membreakpoint 8FFFh 0 2")
        return (fired == "Breakpoint fired: Memory Breakpoint Write Address: 9000H" and regs["BC"] >> 8 == 1
                and self.read_mem(0x9000, 1) == b"\x01")

    def step_watchpoint_no_match_and_remove(self):
        self.install_counter()
        self.ok("set-membreakpoint 9001h 2 1")  # never written by the loop
        rest, _ = self.run_interrupted()
        assert FIRED not in rest, rest
        self.ok("set-membreakpoint 9001h 0 1")
        # Removal by address + size; then clear-membreakpoints removes the rest
        self.ok("set-membreakpoint 9000h 2 1")
        self.ok("set-membreakpoint 9002h 1 1")
        self.ok("set-membreakpoint 9000h 0 1")
        self.install_counter()
        fired, _ = self.run_to_stop()
        assert fired == "Breakpoint fired: Memory Breakpoint Read Address: 9002H", fired
        self.ok("clear-membreakpoints")
        if self.webapi:
            assert self.core_bps(0x9000) == [] and self.core_bps(0x9002) == []
        self.install_counter()
        rest, _ = self.run_interrupted()
        return FIRED not in rest

    def step_watchpoint_errors(self):
        self.expect("set-membreakpoint", NO_PARAM)
        self.expect("set-membreakpoint 9000h", NO_PARAM)
        self.expect("set-membreakpoint zz 2", "Error. Invalid parameter")
        self.expect("set-membreakpoint 9000h x", "Error. Invalid parameter")
        self.expect("set-membreakpoint 9000h 4 1", "Error. Invalid type")
        self.ok("set-membreakpoint 9000h 0 1")  # removing a missing one is fine
        return True

    # --- reverse debugging and stack ---

    def step_history_recent(self):
        # Index 0 = the most recent instruction (registers before it), ZEsarUX "get" semantics
        self.install_counter()
        for _ in range(3):
            self.cmd("cpu-step")
        entries = [parse_history_entry(self.cmd(f"cpu-history get {i}")) for i in range(3)]
        pcs = [e["PC"] for e in entries]
        assert pcs == [0x8003, 0x8001, 0x8000], [f"{pc:04X}" for pc in pcs]
        assert entries[0]["BC"] >> 8 == 0, f"B before INC B = {entries[0]['BC'] >> 8}"
        assert entries[0]["pc_opcodes"] == self.read_mem(0x8003, 4).hex()
        sp_word = self.read_mem(entries[0]["SP"], 2)
        assert entries[0]["sp_content"] == f"{sp_word[1]:02x}{sp_word[0]:02x}", entries[0]["sp_content"]
        return True

    def step_history_reverse_debug(self):
        # The headline ZRCP feature: DeZog's ZesaruxCpuHistory browsing.
        self.install_program()
        self.bp(11, "PC=08006h")
        for _ in range(4):  # loop iterations of recorded history: 10 entries stay inside this program
            fired, _ = self.run_to_stop()
            assert fired == "Breakpoint fired: PC=08006h", fired
        self.clear_bp(11)

        size = int(self.cmd("cpu-history get-size"))
        assert size > 3, f"history size {size}"
        self.expect("cpu-history get-max-size", "8192")

        t0 = time.time()
        entries = []
        for index in range(10):
            line = self.cmd(f"cpu-history get {index}")
            assert not is_error(line), line
            entry = parse_history_entry(line)
            assert entry["PC"] in PROGRAM_PCS, f"index {index}: pc {entry['PC']:04X}"
            # (PC) = the 4 opcode bytes at the entry PC (lowercase on the wire)
            mem = self.read_mem(entry["PC"], 4).hex()
            assert entry["pc_opcodes"] == mem, f"index {index}: {entry['pc_opcodes']} != {mem}"
            assert len(entry["mmu"]) == 8 and entry["mmu"][4:] == entry["mmu"][:4]
            entries.append(entry)
        # The loop order backwards from the stop at 8006h: 8003h, 8001h, 8006h, ...
        assert [e["PC"] for e in entries[:3]] == [PROGRAM_STORE, PROGRAM_LOOP, PROGRAM_JP], entries[:3]
        deep = self.cmd(f"cpu-history get {min(1000, size - 1)}")
        assert not is_error(deep), deep
        # Re-visiting index 0 must be stable (cache path)
        again = parse_history_entry(self.cmd("cpu-history get 0"))
        out_of_range = self.cmd(f"cpu-history get {size + 1000}")
        print(f"  history: size={size}, {(time.time() - t0) * 100:.1f} ms/10 entries")
        return again["PC"] == entries[0]["PC"] and out_of_range == "ERROR: index out of range"

    def step_history_settings(self):
        self.ok("cpu-history set-max-size 4096")
        self.expect("cpu-history get-max-size", "4096")
        self.ok("cpu-history set-max-size -5")  # refused silently
        self.expect("cpu-history get-max-size", "4096")
        self.ok("cpu-history set-max-size 8192")
        self.ok("cpu-history started no")
        self.expect("cpu-history is-started", "0")
        self.ok("cpu-history started yes")
        self.ok("cpu-history enabled no")
        self.expect("cpu-history is-enabled", "0")
        self.ok("cpu-history enabled yes")
        self.expect("cpu-history is-enabled", "1")
        self.ok("cpu-history enabled maybe")  # not a yes/no word: unchanged
        self.expect("cpu-history is-enabled", "1")
        self.ok("cpu-history ignrephalt no")
        self.ok("cpu-history ignrepldxr no")
        self.expect("cpu-history get", "ERROR: index out of range")
        self.expect("cpu-history get -1", "ERROR: index out of range")
        self.expect("cpu-history get x", "ERROR: index out of range")
        self.expect("cpu-history", "Error. Unknown parameter")
        self.expect("cpu-history bogus", "Error. Unknown parameter")
        return True

    def step_extended_stack(self):
        self.install_program()
        # Opcode anchors: CALL at 0x9000, RST 8 at 0x9004
        self.write_mem(0x9000, bytes([0xCD, 0x00, 0x00, 0x00, 0xCF]))
        # Stack values 0x9003 / 0x9005 / 0x8007 read upward from SP
        self.write_mem(STACK_TOP, bytes([0x03, 0x90, 0x05, 0x90, 0x07, 0x80]))
        self.ok("extended-stack enabled yes")
        self.expect("extended-stack get 3", "9003H call\n9005H rst\n8007H push")
        self.expect(f"extended-stack get 2 {STACK_TOP + 2}", "9005H rst\n8007H push")  # explicit start
        self.expect("extended-stack get 1 0FF04h", "8007H push")
        self.expect("extended-stack get 0", "")
        self.expect("extended-stack get", NO_PARAM)
        self.expect("extended-stack get -1", NO_PARAM)
        self.ok("extended-stack clear")
        self.expect("extended-stack", "Error. Unknown parameter")
        self.expect("extended-stack bogus", "Error. Unknown parameter")
        self.ok("extended-stack enabled no")
        self.expect("extended-stack get 1", "Error. It's not enabled")
        self.ok("extended-stack enabled yes")
        return True

    # --- misc ---

    def step_code_coverage(self):
        # Coverage is not recorded (documented): every form acknowledges with nothing
        for line in ("cpu-code-coverage enabled yes", "cpu-code-coverage enabled get", "cpu-code-coverage get",
                     "cpu-code-coverage clear", "cpu-code-coverage enabled no", "cpu-code-coverage"):
            self.ok(line)
        return True

    def step_misc_queries(self):
        self.expect("get-tstates-partial", "0")  # no t-states counter in the Emulator API (documented)
        self.ok("reset-tstates-partial")
        self.expect("get-cpu-frequency", "3500000")
        mmu = self.regs()["mmu"][:4]
        expected = "".join((f"RO{w & 1} " if w >= 0x8000 else f"RA{w} ") for w in mmu)
        self.expect("get-memory-pages", expected)
        self.expect("get-current-machine", self.machine)
        self.ok("close-all-menus")
        self.ok("")  # a blank line is acknowledged with the prompt
        return True

    def step_unknown_and_unsupported(self):
        # Unknown commands, and ZEsarUX commands the server does not have (DeZog sends the first four:
        # write-memory, snapshot-save / snapshot-load for "-state save/restore", load-binary)
        for line in ("write-memory 36864 1", "snapshot-save /nonexistent/x.zsf", "snapshot-load /nonexistent/x.zsf",
                     "load-binary \"/nonexistent/x.bin\" 32768 0", "hexdump 32768 16", "get-breakpoints",
                     "tbblue-get-register 0", "no-such-command", "RUN"):
            self.expect(line, UNKNOWN)
        # ZEsarUX cpu-history sub-commands DeZog does not send
        for sub in ("get-extended 0", "find-pc 32768", "get-pc 0 1", "restore 0"):
            self.expect(f"cpu-history {sub}", "Error. Unknown parameter")
        return True

    def step_dezog_flow(self):
        # A slice of a real session: registers, disassembly window and stack
        # fetches in the sizes DeZog's remote disassembler requests them.
        regs = self.regs()
        pc, sp = regs["PC"], regs["SP"]
        assert len(self.read_mem(pc, 0x40)) == 0x40        # disassembly window
        assert len(self.read_mem(sp, 0x40)) == 0x40        # stack fetch
        assert len(self.read_mem(0x0000, 0x8000)) == 0x8000  # big disasm dump
        return True

    def step_restore_and_quit(self):
        self.restore_state()
        restored = self.regs()
        for key in ("PC", "SP", "AF", "BC", "DE", "HL", "IX", "IY", "AF2", "BC2", "DE2", "HL2", "I", "im"):
            assert restored[key] == self.saved_regs[key], f"{key} not restored"
        for base, data in self.saved_memory.items():
            assert self.read_mem(base, len(data)) == data, f"memory at {base:04X} not restored"
        # Leave a breakpoint and a watchpoint behind: the session teardown removes them from the core
        self.ok("set-breakpoint 90 PC=0FFF0h AND B=1")
        self.ok("enable-breakpoint 90")
        self.ok("set-membreakpoint 0FFF0h 2 1")
        if self.webapi:
            assert len(self.core_bps()) == 2, self.core_bps()
        # The exact disconnect sequence DeZog sends (all must succeed)
        self.ok("")
        self.ok("cpu-history enabled no")
        self.ok("cpu-code-coverage enabled no")
        self.ok("extended-stack enabled no")
        self.ok("clear-membreakpoints")
        self.ok("disable-breakpoints")
        self.ok("exit-cpu-step")
        self.ok("set-membreakpoint 0FFF0h 2 1")
        self.ok("enable-breakpoint 90")
        self.coverage.add("quit")
        assert self.client.command("quit") == ""
        closed = self.client.socket_closed()
        self.client.disconnect()
        if self.webapi:
            deadline = time.time() + 3
            while self.core_bps() and time.time() < deadline:
                time.sleep(0.05)
            assert self.core_bps() == [], f"left in the core after quit: {self.core_bps()}"
            while self.webapi.emulator().get("is_paused") and time.time() < deadline:
                time.sleep(0.05)
            assert self.webapi.emulator().get("is_paused") is False, "the machine stays paused after quit"
        # The server survives and accepts a fresh session
        if not self.client.connect():
            return False
        version = self.client.command("get-version")
        self.client.command("quit")
        self.client.disconnect()
        return closed and version == SERVER_VERSION

    def step_user_breakpoint_survives(self):
        # A breakpoint the user set (WebAPI before the session: owner 'interactive') at the address of a ZRCP
        # breakpoint must survive the client's set + clear there
        if not self.webapi:
            raise Skip("needs the WebAPI")
        eid = self.webapi.emulator_id
        user_addr = 0x8020
        added = self.webapi.request("POST", f"/emulator/{eid}/breakpoints", {"address": user_addr, "type": "execution"})
        user_id = added["id"]

        def user_bps():
            reply = self.webapi.request("GET", f"/emulator/{eid}/breakpoints?all=true")
            return [bp for bp in reply["breakpoints"] if bp["id"] == user_id and bp.get("owner") != "dezog"]

        try:
            assert user_bps(), "the user breakpoint was not created"
            assert self.client.connect()
            self.ok("enter-cpu-step")
            self.bp(14, f"PC={user_addr:04X}h")
            self.clear_bp(14)
            survived = bool(user_bps())
            self.ok("quit")
            self.client.disconnect()
        finally:
            if user_bps():
                self.webapi.request("DELETE", f"/emulator/{eid}/breakpoints/{user_id}")
        assert survived, (f"the user's breakpoint {user_id} at {user_addr:04X}h is gone after the ZRCP client set and "
                          f"cleared one there: the core returned the user's id for the plain breakpoint "
                          f"(breakpointmanager.cpp AddMemoryBreakpoint), the server removed it "
                          f"(zrcpserver.cpp uninstallBreakpoint)")
        return True

    def run_all(self) -> bool:
        steps = [
            ("Fresh session: execution refused before enter-cpu-step", self.step_before_cpu_step),
            ("Connect (welcome banner + prompt)", self.step_connect),
            ("DeZog init sequence (incl. 100 disables)", self.step_init_sequence),
            ("get-registers line shape (MMU/flags/order)", self.step_registers_line),
            ("set-register: every pair", self.step_set_register_pairs),
            ("set-register: 8-bit halves, alternates, I/R/IM, flags", self.step_set_register_bytes),
            ("set-register: number forms, IFF ack, errors", self.step_set_register_formats),
            ("write/read-memory round-trip + number forms", self.step_memory_roundtrip),
            ("read/write-memory errors", self.step_memory_errors),
            ("full 64K read-memory (DeZog fetch64kMemory)", self.step_memory_full_64k),
            ("disassemble: lengths, zone digit, defaults, errors", self.step_disassemble),
            ("cpu-step advances PC and registers", self.step_cpu_step),
            ("cpu-step with registers (set-debug-settings 1)", self.step_cpu_step_with_registers),
            ("cpu-step into CALL", self.step_cpu_step_into_call),
            ("cpu-step-over CALL (temp bp, 1 line)", self.step_cpu_step_over_call),
            ("cpu-step-over plain instruction", self.step_cpu_step_over_plain),
            ("cpu-step-over LDIR (whole block)", self.step_cpu_step_over_ldir),
            ("cpu-step-over RST 38h (ROM handler)", self.step_cpu_step_over_rst),
            ("cpu-step onto a breakpoint address, then over it", self.step_cpu_step_onto_breakpoint),
            ("cpu-step after a stop + cpu-history get (DeZog)", self.step_cpu_step_after_stop_and_browse),
            ("exit-cpu-step refuses run / step", self.step_exit_cpu_step),
            ("run -> breakpoint fired + echo", self.step_run_breakpoint),
            ("run + blank line -> manual stop", self.step_run_interrupt),
            ("run, condition false -> silent resume + interrupt", self.step_run_condition_false),
            ("run, condition true -> stop with condition", self.step_run_condition_true),
            ("set-breakpointpasscount skips hits", self.step_pass_count),
            ("pass count: later hits (ZEsarUX fires once)", self.step_pass_count_once),
            ("pass count set before set-breakpoint", self.step_pass_count_reset_by_set),
            ("breakpoint command errors", self.step_breakpoint_errors),
            ("set-breakpointaction ack (actions ignored)", self.step_breakpoint_action),
            ("step-over condition 'SP>=n' (no PC literal)", self.step_step_over_condition),
            ("step-out condition 'PC=PEEKW(SP-2) AND SP>=n'", self.step_step_out_condition),
            ("cond PC=8003h AND B=5 -> B == 5 (core)", self.step_cond_register),
            ("cond lowercase pc= / and", self.step_cond_lowercase),
            ("cond PEEK (core)", self.step_cond_peek),
            ("cond PEEKW (core)", self.step_cond_peekw),
            ("cond NOT (core)", self.step_cond_not),
            ("cond <> and != (core)", self.step_cond_not_equal),
            ("cond OR, * and + (core)", self.step_cond_or_arith),
            ("cond division by a constant (core)", self.step_cond_const_division),
            ("cond H flag from F (core)", self.step_cond_flag_h),
            ("cond Z flag from F, 256 passes (core)", self.step_cond_flag_z),
            ("cond RAM=<bank> refused -> server evaluator", self.step_cond_refused_bank),
            ("cond RAM=<other bank> never stops", self.step_cond_refused_bank_false),
            ("cond RAM=<MMU bank> on 48K", self.step_cond_ram_matches_mmu),
            ("cond B/C refused -> server evaluator", self.step_cond_refused_division),
            ("cond unknown name -> conservative stop", self.step_cond_unknown_name),
            ("cond ZEsarUX flag name FH", self.step_cond_zesarux_flag_names),
            ("cond bitwise &", self.step_cond_bitwise),
            ("cond PC literal not first", self.step_cond_pc_not_first),
            ("two at one address: the holding one reports", self.step_cond_two_same_address),
            ("two at one address: refused + translated", self.step_cond_two_mixed),
            ("two at one address: both hold -> one stop", self.step_cond_two_both_true),
            ("two refused at one address: clear one", self.step_cond_two_refused_clear_one),
            ("step-over onto a user breakpoint keeps it", self.step_step_over_onto_user_breakpoint),
            ("condition + pass count", self.step_cond_pass_count),
            ("set-breakpoint redefines the core breakpoint", self.step_cond_redefine),
            ("disable/enable/clear reach the core", self.step_cond_disable_enable_clear),
            ("set-membreakpoint write fires", self.step_watchpoint_write),
            ("set-membreakpoint read fires", self.step_watchpoint_read),
            ("set-membreakpoint read+write fires on read", self.step_watchpoint_read_write),
            ("set-membreakpoint range covers the address", self.step_watchpoint_range),
            ("watchpoint no match / remove / clear", self.step_watchpoint_no_match_and_remove),
            ("set-membreakpoint errors", self.step_watchpoint_errors),
            ("cpu-history get 0 = most recent", self.step_history_recent),
            ("cpu-history reverse debugging", self.step_history_reverse_debug),
            ("cpu-history settings + errors", self.step_history_settings),
            ("extended-stack classification, start, errors", self.step_extended_stack),
            ("cpu-code-coverage forms", self.step_code_coverage),
            ("tstates / frequency / memory-pages / machine", self.step_misc_queries),
            ("unknown + unsupported commands", self.step_unknown_and_unsupported),
            ("DeZog flow (disasm and stack dumps)", self.step_dezog_flow),
            ("Restore state + quit sequence + reconnect", self.step_restore_and_quit),
            ("a user breakpoint survives the client's set + clear", self.step_user_breakpoint_survives),
        ]
        if not self.webapi:
            print("  (no WebAPI: the core breakpoint cross-checks are skipped; --launch or --webapi-port)")
        for name, func in steps:
            self.run_step(name, func)
        self.print_results()
        return self.failed == 0

    def print_results(self):
        print("\n" + "=" * 72)
        print("ZRCP Real-Emulator Verification Results")
        print("=" * 72)
        for name, status, msg in self.results:
            line = f"  [{status:5}] {name}" + (f": {msg}" if msg else "")
            print(line)
        print("-" * 72)
        print(f"  Total: {self.passed} passed, {self.failed} failed, {self.known} known divergences, "
              f"{self.xpassed} xpassed, {self.skipped} skipped")
        print(f"  Commands sent: {', '.join(sorted(self.coverage))}")
        print("=" * 72)


def main():
    parser = argparse.ArgumentParser(description="ZRCP verification against a real emulator",
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=int(os.environ.get("UNREAL_ZRCP_PORT", 10000)),
                        help="ZRCP server port of a running host")
    parser.add_argument("--webapi-port", type=int, default=0,
                        help="WebAPI port of a running host: enables the core breakpoint cross-checks")
    parser.add_argument("--launch", nargs="?", const=DEFAULT_BINARY, metavar="UNREAL_QT",
                        help=f"start this unreal-qt offscreen on free ports (default {DEFAULT_BINARY})")
    parser.add_argument("--port-base", type=int, default=0,
                        help="WebAPI port of the launched host (CLI +100, MCP +200, GDB +300, DZRP +400, "
                             "ZRCP +500); default: the first free base from 18600")
    parser.add_argument("--model", default="PENTAGON", help="model of the launched machine")
    parser.add_argument("--startup-timeout", type=float, default=40.0)
    parser.add_argument("--strict", action="store_true", help="KNOWN divergences count as failures")
    args = parser.parse_args()

    host = None
    webapi = None
    port = args.port
    if args.launch:
        root = find_project_root()
        binary = Path(args.launch)
        if not binary.is_absolute():
            binary = root / binary
        if not binary.exists():
            print(f"ERROR: {binary} not found")
            return 2
        base = args.port_base or find_port_base(18600)
        for _, offset in PORT_OFFSETS:
            if not port_free(base + offset):
                print(f"ERROR: port {base + offset} is taken: pick another --port-base")
                return 2
        log_path = root / "scratch" / "verification-zesarux" / f"unreal-qt-{base}.log"
        host = Host(binary, base, args.model, log_path)
        try:
            host.start(args.startup_timeout)
        except BaseException:
            host.stop()
            raise
        webapi = host.webapi
        port = host.zrcp_port
    elif args.webapi_port:
        webapi = WebApi(args.host, args.webapi_port)
        webapi.pick_emulator()

    try:
        verifier = EmulatorVerifier(args.host, port, webapi, strict=args.strict)
        success = verifier.run_all()
    finally:
        if host is not None:
            host.stop()

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
