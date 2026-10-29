#!/usr/bin/env python3
"""DZRP verification against a REAL emulator instance (unreal-qt / dezog-emulator-host).

Unlike verify_dzrp_protocol.py (which targets the mock dezog-test-server), this
script drives the production DeZog module wired into the emulator:

  DeZog-shaped DZRP client  ->  dzrp::Server  ->  DezogDebugAdapter  ->  Emulator

Every command the server's dispatcher (core/automation/dezog/src/dzrpserver.cpp
handleCommand) handles is sent with the payload DeZog builds
(src/remotes/dzrpbuffer/dzrpbufferremote.ts) and its reply is checked against
the emulator state: registers (every CMD_SET_REGISTER id, R bit 7, the shadow
set, IM), memory (64K wrap, blocks, banks), slots, ports, border, breakpoints
(plain, long / banked addresses, DeZog conditions the core filters on, the
unconditional fallback of the ones it cannot take), DeZog's step-into /
step-over / step-out continue shapes, watchpoints (read / write / range /
banked), pause, state save / restore, history, the ZX Next commands (on a Next
machine; their empty replies elsewhere) and the commands it does not handle.

A small Z80 program in RAM (0x8000) drives the execution checks. The state
captured at the start is restored at the end and the machine resumed.

Usage:
    # Launch unreal-qt offscreen on ports of its own (WebAPI --port-base, DeZog
    # --port-base + 400), create a muted machine, verify, stop it
    python3 tools/verification/dezog/verify_dzrp_emulator.py \
        --launch-qt cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt --model PENTAGON
    python3 tools/verification/dezog/verify_dzrp_emulator.py --launch-qt <unreal-qt> --model NEXT

    # Launch the headless dezog-emulator-host (CMake option BUILD_DEZOG_EMULATOR_HOST=ON)
    python3 tools/verification/dezog/verify_dzrp_emulator.py --launch --port 12010 --model PENTAGON

    # Against an already running host on the default port 12000
    python3 tools/verification/dezog/verify_dzrp_emulator.py

Result lines: PASS, FAIL (counted, exit code 1), KNOWN (a known server issue
confirmed; not counted as a failure), SKIP (not applicable to the machine).
"""

import argparse
import json
import os
import random
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dzrp_client import DZRPClient, DZRPCommand, Reg  # noqa: E402

# --- Z80 programs -------------------------------------------------------------

# Main loop (mirrors core/tests/automation/dezog/dezogtestfixture.h)
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

# Counting loop for the conditional breakpoints: B counts passes (wraps at 256), DE counts them in 16 bits
#   8000: F3           DI
#   8001: 06 00        LD   B,0
#   8003: 11 00 00     LD   DE,0
#   8006: 04           INC  B          <- loop
#   8007: 13           INC  DE         <- COND_BP: B = pass number, DE = passes before this one
#   8008: 78           LD   A,B
#   8009: 32 00 90     LD   (9000h),A
#   800C: 18 F8        JR   8006h
COND_PROGRAM = bytes([0xF3, 0x06, 0x00, 0x11, 0x00, 0x00, 0x04, 0x13, 0x78, 0x32, 0x00, 0x90, 0x18, 0xF8])
COND_BP = 0x8007

# Read watchpoint loop
#   8100: 3A 02 90     LD   A,(9002h)
#   8103: 18 FB        JR   8100h
READ_PROGRAM = bytes([0x3A, 0x02, 0x90, 0x18, 0xFB])
READ_START = 0x8100
READ_TARGET = 0x9002

# Call / return for the step flows
#   8200: CD 10 82     CALL 8210h
#   8203: 00           NOP
#   8204: 18 FE        JR   $
#   8210: 3E 42        LD   A,42h
#   8212: 32 01 90     LD   (9001h),A
#   8215: C9           RET
CALL_START = 0x8200
CALL_AFTER = 0x8203
CALL_SPIN = 0x8204
CALL_SUB = 0x8210
CALL_SUB_STORE = 0x8212
CALL_SUB_RET = 0x8215
CALL_TARGET = 0x9001
CALL_MAIN = bytes([0xCD, 0x10, 0x82, 0x00, 0x18, 0xFE])
CALL_SUBROUTINE = bytes([0x3E, 0x42, 0x32, 0x01, 0x90, 0xC9])

# Four NOPs for the refresh register
#   8300: 00 00 00 00  NOP x4
#   8304: 18 FE        JR   $
NOP_START = 0x8300
NOP_PROGRAM = bytes([0x00, 0x00, 0x00, 0x00, 0x18, 0xFE])

STACK_TOP = 0xFF00

REASON_MANUAL = 1
REASON_BREAKPOINT = 2
REASON_WATCHPOINT_READ = 3
REASON_WATCHPOINT_WRITE = 4

MACHINE_ZX48K = 2
MACHINE_ZX128K = 3
MACHINE_ZXNEXT = 4

# What CMD_GET_SUPPORTED_COMMANDS must advertise (dzrpprotocol.cpp buildSupportedCommandsBitfield)
SUPPORTED_BASE = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 20, 21, 24, 28, 40, 41, 42, 43, 50, 51, 0xE0, 0xE1}
SUPPORTED_NEXT = {11, 16, 17, 18, 19, 25, 26, 27}
# DZRP commands DeZog defines that the dispatcher has no case for (an empty ACK, the session goes on)
UNHANDLED_DZRP = [13, 14, 15, 22, 23]

# 8-bit CMD_SET_REGISTER ids: the 16-bit field and the byte they change
BYTE_REGS = {
    Reg.F: ("af", 0), Reg.A: ("af", 8), Reg.C: ("bc", 0), Reg.B: ("bc", 8),
    Reg.E: ("de", 0), Reg.D: ("de", 8), Reg.L: ("hl", 0), Reg.H: ("hl", 8),
    Reg.IXL: ("ix", 0), Reg.IXH: ("ix", 8), Reg.IYL: ("iy", 0), Reg.IYH: ("iy", 8),
    Reg.F2: ("af2", 0), Reg.A2: ("af2", 8), Reg.C2: ("bc2", 0), Reg.B2: ("bc2", 8),
    Reg.E2: ("de2", 0), Reg.D2: ("de2", 8), Reg.L2: ("hl2", 0), Reg.H2: ("hl2", 8),
}
WORD_REGS = {
    Reg.PC: "pc", Reg.SP: "sp", Reg.AF: "af", Reg.BC: "bc", Reg.DE: "de", Reg.HL: "hl", Reg.IX: "ix",
    Reg.IY: "iy", Reg.AF2: "af2", Reg.BC2: "bc2", Reg.DE2: "de2", Reg.HL2: "hl2",
}
REG_FIELDS = ("pc", "sp", "af", "bc", "de", "hl", "ix", "iy", "af2", "bc2", "de2", "hl2", "r", "i", "im")


class Known(Exception):
    """A known server issue reproduced: reported as KNOWN, not counted as a failure"""


class Skip(Exception):
    """The step does not apply to this machine"""


def find_project_root() -> Path:
    current = Path(__file__).resolve().parent
    for _ in range(10):
        if (current / "CMakeLists.txt").exists() and (current / "core").is_dir():
            return current
        if current.parent == current:
            break
        current = current.parent
    raise RuntimeError("Could not find project root")


def port_open(port: int, host: str = "127.0.0.1") -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.5)
        return s.connect_ex((host, port)) == 0


def wait_for_port(port: int, host: str = "localhost", timeout: float = 30.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if port_open(port, host):
            return True
        time.sleep(0.25)
    return False


def locate_host_binary(build_dir: Path):
    for c in (build_dir / "bin" / "dezog-emulator-host", build_dir / "bin" / "dezog-emulator-host.exe"):
        if c.exists():
            return c
    return None


def http_json(method: str, url: str, body=None, timeout: float = 10.0):
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(url, data=data, method=method, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        text = response.read().decode()
    return json.loads(text) if text else {}


class EmulatorVerifier:
    def __init__(self, host: str, port: int, instance_timeout: float = 20.0):
        self.client = DZRPClient(host, port)
        self.results = []
        self.passed = 0
        self.failed = 0
        self.known = 0
        self.skipped = 0
        self.saved_state = b""
        self.machine = None
        self.slots0 = []
        self.instance_timeout = instance_timeout

    # --- helpers ---

    def run_step(self, name, func):
        try:
            ok = func()
            if ok is None:
                ok = True
            if ok:
                self.passed += 1
                self.results.append((name, "PASS", ""))
            else:
                self.failed += 1
                self.results.append((name, "FAIL", "returned False"))
        except Known as e:
            self.known += 1
            self.results.append((name, "KNOWN", str(e)))
        except Skip as e:
            self.skipped += 1
            self.results.append((name, "SKIP", str(e)))
        except Exception as e:  # noqa: BLE001
            self.failed += 1
            self.results.append((name, "FAIL", f"{type(e).__name__}: {e}"))

    def expect(self, ok, message):
        if not ok:
            raise AssertionError(message)

    def regs(self):
        regs = self.client.cmd_get_registers()
        self.expect("pc" in regs, f"GET_REGISTERS: {regs}")
        return regs

    def set_reg(self, reg, value):
        self.expect(self.client.cmd_set_register(int(reg), value), f"SET_REGISTER {reg.name}")

    def write(self, addr, data):
        self.expect(self.client.cmd_write_mem(addr, bytes(data)), f"WRITE_MEM 0x{addr:04X}")

    def read(self, addr, size):
        data = self.client.cmd_read_mem(addr, size)
        self.expect(len(data) == size, f"READ_MEM 0x{addr:04X}+{size}: {len(data)} bytes")
        return data

    def install_program(self):
        self.write(PROGRAM_START, PROGRAM)
        self.write(WATCH_TARGET, b"\x00")
        self.set_reg(Reg.PC, PROGRAM_START)
        self.set_reg(Reg.SP, STACK_TOP)

    def install_cond_program(self, a=0, bc2=0x0000):
        self.write(PROGRAM_START, COND_PROGRAM)
        self.set_reg(Reg.PC, PROGRAM_START)
        self.set_reg(Reg.SP, STACK_TOP)
        self.set_reg(Reg.A, a)
        self.set_reg(Reg.BC2, bc2)

    def install_call_program(self):
        self.write(CALL_START, CALL_MAIN)
        self.write(CALL_SUB, CALL_SUBROUTINE)
        self.write(CALL_TARGET, b"\x00")
        self.set_reg(Reg.PC, CALL_START)
        self.set_reg(Reg.SP, STACK_TOP)
        self.set_reg(Reg.A, 0)

    def expect_notification(self, reason, address=None, timeout=5.0):
        ntf = self.client.wait_notification(timeout=timeout)
        if ntf is None:
            raise AssertionError("no NTF_PAUSE within %.1fs" % timeout)
        if ntf.reason != reason:
            raise AssertionError(f"NTF reason {ntf.reason} != {reason} (address 0x{ntf.address:04X})")
        if address is not None and ntf.address != address:
            raise AssertionError(f"NTF address 0x{ntf.address:04X} != 0x{address:04X}")
        return ntf

    def expect_no_stop(self, seconds=0.4, what="the machine"):
        """The machine runs `seconds` without a stop; then a CMD_PAUSE gives exactly one MANUAL notification"""
        ntf = self.client.wait_notification(timeout=seconds)
        if ntf is not None:
            regs = self.client.cmd_get_registers()
            raise AssertionError(f"{what}: unexpected stop reason {ntf.reason} at 0x{ntf.address:04X} "
                                 f"(B={regs.get('bc', 0) >> 8}, DE={regs.get('de', 0)})")
        self.expect(self.client.cmd_pause(), "PAUSE")
        self.expect_notification(REASON_MANUAL)
        self.expect(not self.client.has_pending_notification(), "a stray notification after the pause")

    def is_128k(self):
        return self.machine == MACHINE_ZX128K

    def is_next(self):
        return self.machine == MACHINE_ZXNEXT

    def need(self, condition, why):
        if not condition:
            raise Skip(why)

    # --- session ---

    def step_connect(self):
        return self.client.connect()

    def step_init(self):
        # Replicates DeZog's onConnect() (dzrpremote.ts onConnect + dzrpbufferremote.ts sendDzrpCmdInit):
        # CMD_INIT with DZRP_VERSION=[2,0,0] + a program name; error byte 0; DZRP_VERSION[0] == major and
        # DZRP_VERSION[1] <= minor; machine type one of {16K, 48K, 128K, ZXNEXT}
        DZRP_VERSION = (2, 0, 0)
        VALID_MACHINES = (1, 2, 3, 4)
        started = time.time()
        deadline = started + self.instance_timeout
        resp = {}
        while True:
            resp = self.client.cmd_init(version=DZRP_VERSION, name="DeZog")
            self.machine = resp.get("machine")
            major, minor = resp.get("version", (0, 0, 0))[0], resp.get("version", (0, 0, 0))[1]
            version_ok = (DZRP_VERSION[0] == major) and (DZRP_VERSION[1] <= minor)
            if resp.get("error") == 0 and version_ok and self.machine in VALID_MACHINES:
                print(f"  DeZog onConnect OK after {time.time() - started:.1f}s "
                      f"(DZRP {major}.{minor}, machine={self.machine}, program='{resp.get('name', '')}')")
                break
            if time.time() >= deadline:
                print(f"  onConnect would FAIL after {self.instance_timeout:.0f}s "
                      f"(error={resp.get('error')}, version=({major}.{minor}), machine={self.machine})")
                break
            time.sleep(0.5)
        major, minor = resp.get("version", (0, 0, 0))[0], resp.get("version", (0, 0, 0))[1]
        return (resp.get("error") == 0 and DZRP_VERSION[0] == major and DZRP_VERSION[1] <= minor
                and self.machine in VALID_MACHINES and len(resp.get("name", "")) > 0)

    def step_supported_commands(self):
        supported = self.client.cmd_get_supported_commands()
        expected = SUPPORTED_BASE | (SUPPORTED_NEXT if self.is_next() else set())
        self.expect(supported == expected,
                    f"advertised {sorted(supported)}; expected {sorted(expected)} "
                    f"(missing {sorted(expected - supported)}, extra {sorted(supported - expected)})")

    def step_pause(self):
        # Running or already paused (INIT parks the machine): one MANUAL notification either way
        self.expect(self.client.cmd_pause(), "PAUSE")
        self.expect_notification(REASON_MANUAL)
        self.expect(not self.client.has_pending_notification(), "a stray notification")
        # A second pause of a paused machine: one more MANUAL, at the same PC
        pc = self.regs()["pc"]
        self.expect(self.client.cmd_pause(), "PAUSE")
        self.expect_notification(REASON_MANUAL, pc)

    def step_capture_state(self):
        self.saved_state = self.client.cmd_read_state()
        self.slots0 = self.regs()["slots"]
        if self.is_next() and not self.saved_state:
            raise Skip("READ_STATE replies empty on the ZX Next (the core saves no .sna for it: "
                       "dezogdebugadapter.cpp captureSnapshotBytes); the end state is restored by hand")
        return len(self.saved_state) > 0

    # --- registers ---

    def step_registers_and_slots(self):
        slots = self.regs()["slots"]
        if self.is_next():
            self.expect(len(slots) == 8, f"Next slots {slots}")
        elif self.is_128k():
            # ROM bank 8/9, 5 at 0x4000, 2 at 0x8000 (DeZog MemoryModelZx128k)
            self.expect(len(slots) == 4 and slots[0] in (8, 9) and slots[1] == 5 and slots[2] == 2,
                        f"128K slots {slots}")
        else:
            self.expect(slots == [0, 1], f"48K slots {slots}")

    def step_set_register_every_id(self):
        expected = {k: v for k, v in self.regs().items() if k in REG_FIELDS}
        rng = random.Random(0xDE20)

        def compare(after):
            regs = self.regs()
            diff = {k: (f"0x{regs[k]:04X}", f"0x{expected[k]:04X}") for k in REG_FIELDS if regs[k] != expected[k]}
            self.expect(not diff, f"after {after}: (got, expected) {diff}")

        for reg, field in WORD_REGS.items():
            value = rng.randrange(0x10000)
            self.set_reg(reg, value)
            expected[field] = value
            compare(f"{reg.name}=0x{value:04X}")
        for reg, (field, shift) in BYTE_REGS.items():
            value = rng.randrange(0x100)
            self.set_reg(reg, value)
            expected[field] = (expected[field] & ~(0xFF << shift)) | (value << shift)
            compare(f"{reg.name}=0x{value:02X} (merge into {field})")
        for value in (0x3F, 0xA5):
            self.set_reg(Reg.I, value)
            expected["i"] = value
            compare(f"I=0x{value:02X}")
        for value in (0x85, 0x7F, 0x00, 0xFF):
            self.set_reg(Reg.R, value)
            expected["r"] = value
            compare(f"R=0x{value:02X} (bit 7 kept)")
        for value, result in ((0, 0), (1, 1), (2, 2), (5, 2)):
            self.set_reg(Reg.IM, value)
            expected["im"] = result
            compare(f"IM {value}")
        return True

    def step_shadow_set_independent(self):
        # The main and shadow sets are separate registers: setting one leaves the other
        for reg, value in ((Reg.AF, 0x1122), (Reg.BC, 0x3344), (Reg.DE, 0x5566), (Reg.HL, 0x7788),
                           (Reg.AF2, 0x99AA), (Reg.BC2, 0xBBCC), (Reg.DE2, 0xDDEE), (Reg.HL2, 0xFF01)):
            self.set_reg(reg, value)
        regs = self.regs()
        got = (regs["af"], regs["bc"], regs["de"], regs["hl"], regs["af2"], regs["bc2"], regs["de2"], regs["hl2"])
        self.expect(got == (0x1122, 0x3344, 0x5566, 0x7788, 0x99AA, 0xBBCC, 0xDDEE, 0xFF01),
                    "registers " + " ".join(f"{v:04X}" for v in got))

    def step_refresh_register_runs(self):
        # R counts M1 cycles in its low 7 bits and keeps bit 7 (LD R,A): four NOPs from R=0xFE
        self.write(NOP_START, NOP_PROGRAM)
        self.set_reg(Reg.PC, NOP_START)
        self.set_reg(Reg.R, 0xFE)
        self.expect(self.client.cmd_continue_bps(NOP_START + 4), "CONTINUE")
        self.expect_notification(REASON_BREAKPOINT, NOP_START + 4)
        r = self.regs()["r"]
        self.expect(r & 0x80, f"R bit 7 lost: 0x{r:02X}")
        self.expect((r & 0x7F) in (0x02, 0x03), f"R 0x{r:02X}: 0xFE + 4 M1 cycles wraps the low 7 bits to 0x02")

    # --- memory ---

    def step_memory_roundtrip(self):
        data = bytes([0xDE, 0xAD, 0xBE, 0xEF])
        self.write(0x8100, data)
        self.expect(self.read(0x8100, 4) == data, "4-byte round trip")
        block = bytes(random.Random(7).randrange(256) for _ in range(0x1000))
        self.write(0x6000, block)
        self.expect(self.read(0x6000, 0x1000) == block, "4K round trip")

    def step_memory_wrap(self):
        # READ_MEM / WRITE_MEM wrap at 0xFFFF into 0x0000
        self.write(0xFFFE, b"\x5A\xA5")
        head = self.read(0x0000, 2)
        self.expect(self.read(0xFFFE, 4) == b"\x5A\xA5" + head, "read across 0xFFFF")

    def step_read_mem_blocks(self):
        blocks = [(PROGRAM_START, 9), (0x0000, 4), (0xFFFE, 4), (0x5C00, 0x100)]
        expected = b"".join(self.read(a, n) for a, n in blocks)
        got = self.client.cmd_read_mem_blocks(blocks)
        self.expect(got == expected, f"{len(got)} bytes, expected {len(expected)} (the blocks in order)")

    def step_read_full_64k(self):
        self.install_program()
        data = self.client.cmd_read_mem(0x0000, 0xFFFF)
        return len(data) == 0xFFFF and data[PROGRAM_START:PROGRAM_START + len(PROGRAM)] == PROGRAM

    # --- banking (128K) ---

    def restore_slots(self):
        for slot, bank in enumerate(self.slots0):
            self.client.cmd_set_slot(slot, bank)

    def step_banking_128k(self):
        self.need(self.is_128k(), "128K paging only")
        try:
            pattern = bytes(range(0x30, 0x40))
            self.expect(self.client.cmd_write_bank(6, pattern), "WRITE_BANK 6")
            self.expect(self.client.cmd_write_bank(1, bytes(0x200)), "WRITE_BANK 1")
            self.expect(self.client.cmd_set_slot(3, 6), "SET_SLOT 3=6")
            self.expect(self.regs()["slots"][3] == 6, "slot 3 reads back 6")
            self.expect(self.read(0xC000, 16) == pattern, "WRITE_BANK data at 0xC000 through slot 3")
            # WRITE_MEM goes to the bank the slot shows
            p2 = bytes([0xC1, 0xC2, 0xC3, 0xC4])
            self.write(0xC100, p2)
            self.expect(self.client.cmd_set_slot(3, 1), "SET_SLOT 3=1")
            self.expect(self.read(0xC100, 4) == bytes(4), "bank 1 does not see bank 6's write")
            self.expect(self.client.cmd_set_slot(3, 6), "SET_SLOT 3=6")
            self.expect(self.read(0xC100, 4) == p2, "bank 6 keeps WRITE_MEM's bytes")
            # Windows 1 and 2 take any bank too (a debugger-forced mapping)
            for slot, base in ((1, 0x4000), (2, 0x8000)):
                self.expect(self.client.cmd_set_slot(slot, 6), f"SET_SLOT {slot}=6")
                self.expect(self.regs()["slots"][slot] == 6, f"slot {slot} reads back 6")
                self.expect(self.read(base, 16) == pattern, f"bank 6 at 0x{base:04X}")
                self.expect(self.client.cmd_set_slot(slot, self.slots0[slot]), "restore")
            # ROM slot: 8 = ROM0, 9 = ROM1, the aliases 0xFE / 0xFF the same
            self.expect(self.client.cmd_set_slot(0, 8), "SET_SLOT 0=8")
            self.expect(self.regs()["slots"][0] == 8, "slot 0 = 8")
            rom0 = self.read(0x0000, 64)
            self.expect(self.client.cmd_set_slot(0, 9), "SET_SLOT 0=9")
            self.expect(self.regs()["slots"][0] == 9, "slot 0 = 9")
            rom1 = self.read(0x0000, 64)
            self.expect(rom0 != rom1, "ROM0 and ROM1 read the same")
            self.expect(self.client.cmd_set_slot(0, 0xFE), "SET_SLOT 0=0xFE")
            self.expect(self.regs()["slots"][0] == 8 and self.read(0, 64) == rom0, "alias 0xFE = ROM0")
            self.expect(self.client.cmd_set_slot(0, 0xFF), "SET_SLOT 0=0xFF")
            self.expect(self.regs()["slots"][0] == 9 and self.read(0, 64) == rom1, "alias 0xFF = ROM1")
        finally:
            self.restore_slots()
        self.expect(self.regs()["slots"] == self.slots0, "slots restored")

    # 128K .sna: 27-byte header, banks 5, 2 and the paged one, then PC(2) and the #7FFD byte
    SNA128_7FFD = 27 + 3 * 0x4000 + 2

    def step_port_paging_128k(self):
        # CMD_WRITE_PORT reaches the machine's ports: #7FFD pages bank 3 in and ROM1 / ROM0. The state DeZog
        # saves carries the latch; the SOS reset locks it (bit 5), so the check runs on an unlocked copy
        self.need(self.is_128k(), "128K paging only")
        c = self.client
        state = c.cmd_read_state()
        self.need(len(state) in (131103, 147487), f"not a 128K .sna ({len(state)} bytes)")
        latch = state[self.SNA128_7FFD]
        self.expect((latch & 7) == self.slots0[3], f".sna #7FFD 0x{latch:02X} vs slot 3 bank {self.slots0[3]}")
        try:
            if latch & 0x20:
                unlocked = bytearray(state)
                unlocked[self.SNA128_7FFD] = latch & ~0x20
                self.expect(c.cmd_write_state(bytes(unlocked)), "WRITE_STATE (unlocked #7FFD)")
            self.expect(c.cmd_write_port(0x7FFD, 0x10 | 3), "WRITE_PORT 7FFD")
            slots = self.regs()["slots"]
            self.expect(slots[3] == 3, f"#7FFD=0x13: slot 3 = {slots[3]}, expected 3 (WRITE_PORT must reach "
                                       f"the port decoder)")
            self.expect(slots[0] == 9, f"#7FFD=0x13: slot 0 = {slots[0]}, expected 9 (ROM1)")
            self.expect(c.cmd_read_state()[self.SNA128_7FFD] == 0x13, "the saved latch follows")
            self.expect(c.cmd_write_port(0x7FFD, 0x00 | 4), "WRITE_PORT 7FFD")
            slots = self.regs()["slots"]
            self.expect(slots[3] == 4 and slots[0] == 8, f"#7FFD=0x04: slots {slots}, expected bank 4, ROM 8")
        finally:
            c.cmd_write_state(state)
        self.expect(self.regs()["slots"] == self.slots0, f"slots restored: {self.regs()['slots']} vs {self.slots0}")

    def step_ports(self):
        # CMD_READ_PORT: exactly one data byte (cspectremote.ts); CMD_WRITE_PORT: an empty ACK
        value = self.client.cmd_read_port(0xFE)
        self.expect(value is not None, "READ_PORT reply is not one byte")
        self.expect(self.client.cmd_write_port(0xFE, 0x02), "WRITE_PORT: empty ACK")

    def step_ports_reach_device(self):
        # The ports are the machine's: select AY register 1 (#FFFD), write it (#BFFD), read it back (#FFFD).
        # Register 1 (tone A coarse, 4 bits) makes no sound by itself
        self.need(self.machine in (MACHINE_ZX128K, MACHINE_ZXNEXT), "no AY on a 48K")
        c = self.client
        self.expect(c.cmd_write_port(0xFFFD, 0x01) and c.cmd_write_port(0xBFFD, 0x0A), "WRITE_PORT")
        self.expect(c.cmd_write_port(0xFFFD, 0x01), "WRITE_PORT")
        got = c.cmd_read_port(0xFFFD)
        self.expect(got == 0x0A, f"AY register 1 reads 0x{got:02X} after WRITE_PORT #BFFD 0x0A "
                                 f"(READ_PORT / WRITE_PORT must go through the port decoder)")
        c.cmd_write_port(0xBFFD, 0x00)

    def step_border(self):
        # The border lands in the state DeZog saves (.sna header byte 26); 3 bits only
        if self.is_next():
            self.expect(self.client.cmd_set_border(2), "SET_BORDER")
            raise Skip("ACK only: no .sna on the ZX Next to read the border back")
        for color, expected in ((5, 5), (0x0B, 3), (2, 2)):
            self.expect(self.client.cmd_set_border(color), "SET_BORDER")
            state = self.client.cmd_read_state()
            self.expect(len(state) > 27, f"READ_STATE {len(state)} bytes")
            self.expect((state[26] & 7) == expected, f"SET_BORDER {color}: .sna border {state[26]}, expected {expected}")

    def step_next_commands_elsewhere(self):
        # The Next commands are not advertised on other machines; their replies keep the shape, values zero
        self.need(not self.is_next(), "Next machine: real values checked below")
        c = self.client
        self.expect(c.cmd_get_tbblue_reg(0x00) == b"\x00", "GET_TBBLUE_REG: one zero byte")
        self.expect(c.cmd_read_bank_mem(4, 0, 8) == bytes(8), "READ_BANK_MEM: zeros")
        self.expect(c.cmd_write_bank_mem(4, 0, b"\x01") == b"", "WRITE_BANK_MEM: empty")
        self.expect(c.cmd_set_nextregs([(0x07, 0x01)]) == b"", "SET_NEXTREGS: empty")
        self.expect(c.cmd_get_sprites_palette(0) == b"", "GET_SPRITES_PALETTE: empty")
        self.expect(c.cmd_get_sprites_clip() == b"", "GET_SPRITES_CLIP_WINDOW_AND_CONTROL: empty")
        self.expect(c.cmd_get_sprites(0, 2) == b"", "GET_SPRITES: empty")
        self.expect(c.cmd_get_sprite_patterns(0, 1) == b"", "GET_SPRITE_PATTERNS: empty")
        self.expect("pc" in c.cmd_get_registers(), "session alive")

    # --- ZX Next ---

    def step_next_slots_and_tbblue(self):
        self.need(self.is_next(), "ZX Next only")
        c = self.client
        try:
            self.expect(c.cmd_get_tbblue_reg(0x00) == bytes([10]), "NR #00 machine id 10 (ZX Next)")
            for slot in range(8):
                self.expect(c.cmd_get_tbblue_reg(0x50 + slot) == bytes([self.slots0[slot]]),
                            f"NR #{0x50 + slot:02X} = slot {slot} bank {self.slots0[slot]}")
            self.expect(c.cmd_set_slot(7, 20), "SET_SLOT 7=20")
            self.expect(self.regs()["slots"][7] == 20, "slot 7 = 20")
            self.expect(c.cmd_get_tbblue_reg(0x57) == bytes([20]), "NR #57 = 20")
            self.expect(c.cmd_set_nextregs([(0x56, 22), (0x57, 23)]) == b"", "SET_NEXTREGS: empty reply")
            slots = self.regs()["slots"]
            self.expect(slots[6] == 22 and slots[7] == 23, f"SET_NEXTREGS MMU6/7: slots {slots}")
            self.expect(c.cmd_get_tbblue_reg(0x56) == bytes([22]), "NR #56 = 22")
        finally:
            self.restore_slots()
        self.expect(self.regs()["slots"] == self.slots0, "slots restored")

    def step_next_bank_memory(self):
        self.need(self.is_next(), "ZX Next only")
        c = self.client
        try:
            pattern = bytes(range(0x80, 0x90))
            self.expect(c.cmd_write_bank_mem(20, 0x10, pattern) == b"", "WRITE_BANK_MEM: empty reply")
            self.expect(c.cmd_read_bank_mem(20, 0x10, 16) == pattern, "READ_BANK_MEM bank 20 +0x10")
            self.expect(c.cmd_read_bank_mem(20, 0x2010, 16) == pattern, "the offset wraps inside the 8K bank")
            self.expect(c.cmd_set_slot(7, 20), "SET_SLOT 7=20")
            self.expect(self.read(0xE010, 16) == pattern, "bank 20 through slot 7")
        finally:
            self.restore_slots()

    def step_next_rom_bank(self):
        # Slot 0 showing bank 0xFF (the ROM): READ_BANK_MEM 0xFF reads what READ_MEM reads there
        self.need(self.is_next() and self.slots0[0] == 0xFF, "ZX Next with the ROM in slot 0")
        got, mem = self.client.cmd_read_bank_mem(0xFF, 0, 16), self.read(0, 16)
        self.expect(got == mem, f"READ_BANK_MEM 0xFF {got.hex()} != READ_MEM 0x0000 {mem.hex()} "
                                f"(the ROM bank is what slot 0 reads: boot ROM, overlays and the alternate ROM included)")

    def step_next_write_mem_slot7(self):
        # CMD_WRITE_MEM at 0xE000-0xFFFF must land in the bank MMU7 shows (READ_MEM reads it from there)
        self.need(self.is_next(), "ZX Next only")
        c = self.client
        try:
            self.expect(c.cmd_set_slot(6, 0) and c.cmd_set_slot(7, 20), "SET_SLOT 6=0, 7=20")
            p2 = b"\x11\x22\x33\x44"
            self.write(0xE020, p2)
            got = self.read(0xE020, 4)
            self.expect(got == p2 and c.cmd_read_bank_mem(20, 0x20, 4) == p2,
                        f"WRITE_MEM 0xE020 with MMU7=20: READ_MEM gives {got.hex()}, bank 20 "
                        f"{c.cmd_read_bank_mem(20, 0x20, 4).hex()}, bank 1 (MMU6's 16K pair) "
                        f"{c.cmd_read_bank_mem(1, 0x20, 4).hex()} (WRITE_MEM must go through the 8K MMU)")
        finally:
            self.restore_slots()

    def step_next_write_bank_8k(self):
        # DeZog's sendDzrpCmdWriteBank takes an 8K bank ("DZRP is for ZX Next only", loadBinSna writes 2 x 8K
        # per 16K bank): WRITE_BANK 21 + 8K must land in Next 8K bank 21
        self.need(self.is_next(), "ZX Next only")
        c = self.client
        pattern = bytes(random.Random(21).randrange(256) for _ in range(0x2000))
        self.expect(c.cmd_write_bank(21, pattern), "WRITE_BANK error byte")
        got = c.cmd_read_bank_mem(21, 0, 0x2000)
        self.expect(got == pattern,
                    f"WRITE_BANK 21 (8K bank): READ_BANK_MEM 21 = {got[:8].hex()}..., written {pattern[:8].hex()}... "
                    f"(WRITE_BANK takes an 8K bank on the Next)")

    def step_next_palette(self):
        self.need(self.is_next(), "ZX Next only")
        c = self.client
        # NR #43 bits 6-4 select the palette written: 010 sprites first, 110 sprites second; NR #40 index,
        # NR #41 an 8-bit RRRGGGBB (the 9th blue bit = B1 | B0)
        c.cmd_set_nextregs([(0x43, 0x20), (0x40, 5), (0x41, 0xE3)])
        c.cmd_set_nextregs([(0x43, 0x60), (0x40, 7), (0x41, 0x1C)])
        c.cmd_set_nextregs([(0x43, 0x00)])
        first = c.cmd_get_sprites_palette(0)
        second = c.cmd_get_sprites_palette(1)
        self.expect(len(first) == 512 and len(second) == 512, f"palette sizes {len(first)}, {len(second)}")
        self.expect(first[10] == 0xE3 and (first[11] & 1) == 1, f"first palette [5] = {first[10]:02X} {first[11]:02X}")
        self.expect(second[14] == 0x1C and (second[15] & 1) == 0,
                    f"second palette [7] = {second[14]:02X} {second[15]:02X}")

    def step_next_clip_and_control(self):
        self.need(self.is_next(), "ZX Next only")
        c = self.client
        # NR #1C bit 1 resets the sprite clip index; NR #19 takes x1, x2, y1, y2 in turn; NR #15 sprite control
        c.cmd_set_nextregs([(0x1C, 0x02), (0x19, 10), (0x19, 200), (0x19, 20), (0x19, 180), (0x15, 0x03)])
        clip = c.cmd_get_sprites_clip()
        self.expect(list(clip) == [10, 200, 20, 180, 0x03], f"clip + control {list(clip)}")
        c.cmd_set_nextregs([(0x1C, 0x02), (0x19, 0), (0x19, 255), (0x19, 0), (0x19, 191), (0x15, 0x00)])

    # Sprite pattern 2 and the attributes of sprite 3, written by the CPU through ports #303B, #5B and #57
    #   8400: 01 3B 30     LD   BC,303Bh
    #   8403: 3E 02        LD   A,2
    #   8405: ED 79        OUT  (C),A      ; pattern 2 (and sprite 2)
    #   8407: 21 00 85     LD   HL,8500h
    #   840A: 01 5B 00     LD   BC,005Bh   ; B = 0: 256 bytes
    #   840D: ED B3        OTIR
    #   840F: 01 3B 30     LD   BC,303Bh
    #   8412: 3E 03        LD   A,3
    #   8414: ED 79        OUT  (C),A      ; sprite 3
    #   8416: 21 00 86     LD   HL,8600h
    #   8419: 01 57 05     LD   BC,0557h   ; 5 attribute bytes
    #   841C: ED B3        OTIR
    #   841E: 18 FE        JR   $
    SPRITE_PROGRAM = bytes([0x01, 0x3B, 0x30, 0x3E, 0x02, 0xED, 0x79, 0x21, 0x00, 0x85, 0x01, 0x5B, 0x00, 0xED, 0xB3,
                            0x01, 0x3B, 0x30, 0x3E, 0x03, 0xED, 0x79, 0x21, 0x00, 0x86, 0x01, 0x57, 0x05, 0xED, 0xB3,
                            0x18, 0xFE])

    def step_next_sprites(self):
        self.need(self.is_next(), "ZX Next only")
        c = self.client
        pattern = bytes((i * 7 + 3) & 0xFF for i in range(256))
        attrs = [0x11, 0x22, 0x33, 0xC5, 0x00]  # byte 3 bit 6: a fifth byte follows
        self.write(0x8400, self.SPRITE_PROGRAM)
        self.write(0x8500, pattern)
        self.write(0x8600, bytes(attrs))
        self.set_reg(Reg.PC, 0x8400)
        self.set_reg(Reg.SP, STACK_TOP)
        self.expect(c.cmd_continue_bps(0x841E), "CONTINUE")
        self.expect_notification(REASON_BREAKPOINT, 0x841E)
        got = c.cmd_get_sprite_patterns(2, 1)
        self.expect(got == pattern, f"pattern 2: {got[:8].hex()}... (len {len(got)})")
        self.expect(len(c.cmd_get_sprite_patterns(0, 3)) == 768, "3 patterns = 768 bytes")
        self.expect(c.cmd_get_sprite_patterns(1, 2)[256:] == pattern, "patterns 1..2: the second is pattern 2")
        got = c.cmd_get_sprites(3, 1)
        self.expect(list(got) == attrs, f"sprite 3 attributes {list(got)}")
        self.expect(len(c.cmd_get_sprites(0, 4)) == 20 and c.cmd_get_sprites(0, 4)[15:20] == bytes(attrs),
                    "4 sprites = 20 bytes, the fourth is sprite 3")
        # Sprite 3 invisible again
        self.write(0x8600, bytes([0, 0, 0, 0x40, 0]))
        self.set_reg(Reg.PC, 0x840F)
        self.expect(c.cmd_continue_bps(0x841E), "CONTINUE")
        self.expect_notification(REASON_BREAKPOINT, 0x841E)

    def step_next_write_port_sprites(self):
        # The same through CMD_WRITE_PORT (DeZog's port writes)
        self.need(self.is_next(), "ZX Next only")
        c = self.client
        self.expect(c.cmd_write_port(0x303B, 5), "WRITE_PORT #303B")
        for b in (0xA1, 0xA2, 0xA3, 0x00):
            c.cmd_write_port(0x57, b)
        got = c.cmd_get_sprites(5, 1)
        self.expect(list(got[:3]) == [0xA1, 0xA2, 0xA3], f"sprite 5 attributes {list(got)} after WRITE_PORT #57 "
                                                          f"(WRITE_PORT must reach the sprite port)")

    def step_next_banked_breakpoint(self):
        # DeZog's long address on the Next: ((8K bank + 1) << 16) | addr. The program at 0x8001 lives in the bank
        # of MMU4 (slot 4)
        self.need(self.is_next(), "ZX Next only")
        self.install_program()
        bank = self.slots0[4]
        bp = self.client.cmd_add_breakpoint_long(((bank + 1) << 16) | PROGRAM_LOOP)
        self.expect(bp > 0, "ADD_BREAKPOINT long")
        try:
            self.expect(self.client.cmd_continue(), "CONTINUE")
            ntf = self.client.wait_notification(timeout=1.0)
            if ntf is None:
                self.client.cmd_pause()
                self.expect_notification(REASON_MANUAL)
                raise AssertionError(f"breakpoint on 8K bank {bank} (long 0x{((bank + 1) << 16) | PROGRAM_LOOP:06X}) "
                                     f"never hit while that bank is in the slot")
            self.expect(ntf.reason == REASON_BREAKPOINT and ntf.address == PROGRAM_LOOP,
                        f"NTF {ntf.reason} at 0x{ntf.address:04X}")
        finally:
            self.client.cmd_remove_breakpoint(bp)

    # --- breakpoints ---

    def step_breakpoint_hit(self):
        self.install_program()
        self.bp_id = self.client.cmd_add_breakpoint(PROGRAM_JP)
        self.expect(self.bp_id > 0, "ADD_BREAKPOINT id")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect_notification(REASON_BREAKPOINT, PROGRAM_JP)
        return self.regs()["pc"] == PROGRAM_JP

    def step_breakpoint_rehit(self):
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect_notification(REASON_BREAKPOINT, PROGRAM_JP)
        self.expect(self.read(WATCH_TARGET, 1) == b"\x01", "the loop ran between the hits")
        return self.client.cmd_remove_breakpoint(self.bp_id)

    def step_breakpoint_two_ids_remove(self):
        # Two breakpoints: two ids, both stop; the removed one no longer stops
        self.install_program()
        bp_a = self.client.cmd_add_breakpoint(PROGRAM_STORE)
        bp_b = self.client.cmd_add_breakpoint(PROGRAM_JP)
        self.expect(bp_a > 0 and bp_b > 0 and bp_a != bp_b, f"ids {bp_a}, {bp_b}")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect_notification(REASON_BREAKPOINT, PROGRAM_STORE)
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect_notification(REASON_BREAKPOINT, PROGRAM_JP)
        self.expect(self.client.cmd_remove_breakpoint(bp_a), "REMOVE_BREAKPOINT")
        for _ in range(2):
            self.expect(self.client.cmd_continue(), "CONTINUE")
            self.expect_notification(REASON_BREAKPOINT, PROGRAM_JP)
        self.expect(self.client.cmd_remove_breakpoint(bp_b), "REMOVE_BREAKPOINT")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect_no_stop(0.3, "no breakpoint left")
        # Removing an id that does not exist: an empty ACK, the session goes on
        self.expect(self.client.cmd_remove_breakpoint(0xFFF0), "REMOVE_BREAKPOINT unknown id")
        self.expect("pc" in self.client.cmd_get_registers(), "session alive")

    def step_banked_breakpoint_128k(self):
        # DeZog's long address on the 128K: ((bank + 1) << 16) | addr. The program at 0x8000 is in bank 2
        self.need(self.is_128k(), "128K banks")
        self.install_program()
        bp = self.client.cmd_add_breakpoint_long((3 << 16) | PROGRAM_JP)
        self.expect(bp > 0, "ADD_BREAKPOINT bank 2")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        ntf = self.expect_notification(REASON_BREAKPOINT, PROGRAM_JP)
        # NTF_PAUSE carries bank + 1 of the stop address (DeZog adds it as the long address' upper byte)
        self.expect(ntf.bank == 3, f"NTF_PAUSE bank byte {ntf.bank}, want 3 (bank 2)")
        self.expect(self.client.cmd_remove_breakpoint(bp), "REMOVE")
        # The same address in bank 4 (not mapped at 0x8000): no stop
        bp = self.client.cmd_add_breakpoint_long((5 << 16) | PROGRAM_JP)
        self.expect(bp > 0, "ADD_BREAKPOINT bank 4")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        try:
            self.expect_no_stop(0.3, "breakpoint in an unmapped bank")
        finally:
            self.client.cmd_remove_breakpoint(bp)

    def step_rom_breakpoint_via_interrupt(self):
        # EI; loop: JR loop -> IM1 interrupt every frame vectors to ROM 0x0038
        self.write(PROGRAM_START, bytes([0xFB, 0x18, 0xFE]))
        self.set_reg(Reg.PC, PROGRAM_START)
        self.set_reg(Reg.SP, STACK_TOP)
        self.set_reg(Reg.IM, 1)
        bp = self.client.cmd_add_breakpoint(0x0038)
        self.expect(bp > 0, "ADD_BREAKPOINT 0x0038")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect_notification(REASON_BREAKPOINT, 0x0038)
        self.expect(self.regs()["im"] == 1, "IM 1")
        # The interrupt pushed the loop's address
        sp = self.regs()["sp"]
        ret = self.read(sp, 2)
        self.expect(sp == STACK_TOP - 2 and ret[0] | (ret[1] << 8) in (PROGRAM_START + 1, PROGRAM_START + 3),
                    f"SP 0x{sp:04X}, pushed 0x{ret[0] | (ret[1] << 8):04X}")
        self.expect(self.client.cmd_remove_breakpoint(bp), "REMOVE")
        return True

    def step_di_blocks_interrupt(self):
        # DI; loop: no interrupt reaches 0x0038 (the interrupt state DeZog cannot read, seen through its effect)
        self.write(PROGRAM_START, bytes([0xF3, 0x18, 0xFE]))
        self.set_reg(Reg.PC, PROGRAM_START)
        self.set_reg(Reg.SP, STACK_TOP)
        self.set_reg(Reg.IM, 1)
        bp = self.client.cmd_add_breakpoint(0x0038)
        self.expect(bp > 0, "ADD_BREAKPOINT 0x0038")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        try:
            self.expect_no_stop(0.3, "DI loop")
        finally:
            self.client.cmd_remove_breakpoint(bp)
        self.install_program()

    def rom_breakpoint_case(self):
        rom = self.regs()["slots"][0]
        other = 8 if rom == 9 else 9
        self.write(PROGRAM_START, bytes([0xFB, 0x18, 0xFE]))
        self.set_reg(Reg.PC, PROGRAM_START)
        self.set_reg(Reg.SP, STACK_TOP)
        self.set_reg(Reg.IM, 1)
        bp = self.client.cmd_add_breakpoint_long(((rom + 1) << 16) | 0x0038)
        self.expect(bp > 0, f"ADD_BREAKPOINT ROM bank {rom}")
        try:
            self.expect(self.client.cmd_continue(), "CONTINUE")
            ntf = self.client.wait_notification(timeout=1.0)
            if ntf is None:
                self.client.cmd_pause()
                self.expect_notification(REASON_MANUAL)
                raise AssertionError(f"0x0038 on ROM bank {rom} (what GET_REGISTERS reports for slot 0) never "
                                     f"stopped: the slots and the breakpoints must use one ROM bank mapping")
            self.expect(ntf.reason == REASON_BREAKPOINT and ntf.address == 0x0038, f"NTF {ntf}")
        finally:
            self.client.cmd_remove_breakpoint(bp)
        bp = self.client.cmd_add_breakpoint_long(((other + 1) << 16) | 0x0038)
        self.expect(bp > 0, f"ADD_BREAKPOINT ROM bank {other}")
        self.set_reg(Reg.PC, PROGRAM_START)
        self.set_reg(Reg.SP, STACK_TOP)
        self.expect(self.client.cmd_continue(), "CONTINUE")
        try:
            self.expect_no_stop(0.3, f"0x0038 on ROM bank {other} (not mapped)")
        finally:
            self.client.cmd_remove_breakpoint(bp)

    def step_banked_rom_breakpoint(self):
        # A long address on the ROM bank that slot 0 shows stops; the other ROM bank does not. Both #7FFD ROM
        # selections (bit 4), set through the saved state's latch
        self.need(self.is_128k(), "128K ROM banks")
        c = self.client
        state = c.cmd_read_state()
        self.need(len(state) in (131103, 147487), f"not a 128K .sna ({len(state)} bytes)")
        errors = []
        try:
            for rom_bit in (0x00, 0x10):
                variant = bytearray(state)
                variant[self.SNA128_7FFD] = (state[self.SNA128_7FFD] & 0x07) | rom_bit
                self.expect(c.cmd_write_state(bytes(variant)), "WRITE_STATE")
                slot0 = self.regs()["slots"][0]
                try:
                    self.rom_breakpoint_case()
                except AssertionError as e:
                    errors.append(f"#7FFD bit 4 = {rom_bit >> 4} (slot 0 = {slot0}): {e}")
        finally:
            c.cmd_write_state(state)
        self.install_program()
        self.expect(not errors, "; ".join(errors))

    # --- continue / step shapes ---

    def step_step_over_shape(self):
        # From the JP, DeZog "step" places temp bp1 at the loop head
        self.install_program()
        self.set_reg(Reg.PC, PROGRAM_JP)
        self.expect(self.client.cmd_continue_bps(PROGRAM_LOOP, PROGRAM_STORE), "CONTINUE bp1/bp2")
        self.expect_notification(REASON_BREAKPOINT, PROGRAM_LOOP)
        # Temporary breakpoints are gone: a plain continue must not stop again
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect_no_stop(0.2, "temporary breakpoints cleared")

    def step_rapid_step_loop(self):
        # 8001 -> 8003 -> 8006 -> 8001 ... exactly one NTF per CMD_CONTINUE, no strays
        self.install_program()
        self.set_reg(Reg.PC, PROGRAM_LOOP)
        pc = PROGRAM_LOOP
        for i in range(30):
            target = {PROGRAM_LOOP: PROGRAM_STORE, PROGRAM_STORE: PROGRAM_JP, PROGRAM_JP: PROGRAM_LOOP}[pc]
            self.expect(self.client.cmd_continue_with_temp_bps(target), f"step {i}")
            self.expect_notification(REASON_BREAKPOINT, target)
            self.expect(not self.client.has_pending_notification(), f"stray notification at step {i}")
            pc = target
        return True

    def step_step_into_call_and_ret(self):
        # DeZog stepInto (calcStepBp(false)): a CALL gets bp1 = the next instruction, bp2 = the call target;
        # a RET gets bp1 = the next byte, bp2 = the return address on the stack
        self.install_call_program()
        c = self.client
        self.expect(c.cmd_continue_bps(CALL_AFTER, CALL_SUB), "CONTINUE (step into CALL)")
        self.expect_notification(REASON_BREAKPOINT, CALL_SUB)
        regs = self.regs()
        self.expect(regs["pc"] == CALL_SUB and regs["sp"] == STACK_TOP - 2, f"in the subroutine: {regs['pc']:04X}")
        self.expect(self.read(regs["sp"], 2) == bytes([CALL_AFTER & 0xFF, CALL_AFTER >> 8]), "return address pushed")
        self.expect(c.cmd_continue_bps(CALL_SUB_STORE), "CONTINUE (step LD A)")
        self.expect_notification(REASON_BREAKPOINT, CALL_SUB_STORE)
        self.expect(self.regs()["af"] >> 8 == 0x42, "A = 0x42")
        self.expect(c.cmd_continue_bps(CALL_SUB_RET), "CONTINUE (step LD (nn),A)")
        self.expect_notification(REASON_BREAKPOINT, CALL_SUB_RET)
        self.expect(self.read(CALL_TARGET, 1) == b"\x42", "store done")
        self.expect(c.cmd_continue_bps(CALL_SUB_RET + 1, CALL_AFTER), "CONTINUE (step RET)")
        self.expect_notification(REASON_BREAKPOINT, CALL_AFTER)
        self.expect(self.regs()["sp"] == STACK_TOP, "SP back after RET")
        self.expect(not c.has_pending_notification(), "stray notification")

    def step_step_over_call(self):
        # DeZog stepOver (calcStepBp(true)) of a CALL: bp1 = the next instruction only; the subroutine runs
        self.install_call_program()
        self.expect(self.client.cmd_continue_bps(CALL_AFTER), "CONTINUE (step over CALL)")
        self.expect_notification(REASON_BREAKPOINT, CALL_AFTER)
        regs = self.regs()
        self.expect(regs["sp"] == STACK_TOP and regs["af"] >> 8 == 0x42, f"SP {regs['sp']:04X}, A {regs['af'] >> 8:02X}")
        self.expect(self.read(CALL_TARGET, 1) == b"\x42", "the subroutine ran")

    def step_step_out(self):
        # DeZog stepOut: a plain breakpoint-free continue with bp1 = the return address read from the stack
        self.install_call_program()
        self.expect(self.client.cmd_continue_bps(CALL_SUB), "CONTINUE (into the subroutine)")
        self.expect_notification(REASON_BREAKPOINT, CALL_SUB)
        sp = self.regs()["sp"]
        ret = self.read(sp, 2)
        self.expect(self.client.cmd_continue_bps(ret[0] | (ret[1] << 8)), "CONTINUE (step out)")
        self.expect_notification(REASON_BREAKPOINT, CALL_AFTER)

    def step_step_from_user_breakpoint(self):
        # Stopped at a user breakpoint, a step must reach the next instruction (the stop's own breakpoint is
        # passed once, not hit again)
        self.install_call_program()
        bp = self.client.cmd_add_breakpoint(CALL_START)
        self.expect(bp > 0, "ADD_BREAKPOINT")
        try:
            self.set_reg(Reg.PC, CALL_SPIN)
            self.write(CALL_SPIN, bytes([0x18, 0xFA]))  # JR 8200h: back to the CALL
            self.expect(self.client.cmd_continue(), "CONTINUE")
            self.expect_notification(REASON_BREAKPOINT, CALL_START)
            self.expect(self.client.cmd_continue_bps(CALL_AFTER), "CONTINUE (step over)")
            self.expect_notification(REASON_BREAKPOINT, CALL_AFTER)
            # The user breakpoint is still there: the next pass stops on it
            self.expect(self.client.cmd_continue(), "CONTINUE")
            self.expect_notification(REASON_BREAKPOINT, CALL_START)
        finally:
            self.client.cmd_remove_breakpoint(bp)
            self.write(CALL_SPIN, bytes([0x18, 0xFE]))

    def step_temp_bp_on_user_bp(self):
        # A temporary breakpoint at a user breakpoint's address is a breakpoint of its own: clearing the
        # temporaries after the stop leaves the user's breakpoint in place
        self.install_call_program()
        self.write(CALL_SPIN, bytes([0x18, 0xFA]))  # JR 8200h
        bp = self.client.cmd_add_breakpoint(CALL_AFTER)
        self.expect(bp > 0, "ADD_BREAKPOINT")
        try:
            self.expect(self.client.cmd_continue_bps(CALL_AFTER), "CONTINUE (temp bp at the user's)")
            self.expect_notification(REASON_BREAKPOINT, CALL_AFTER)
            self.expect(self.client.cmd_continue(), "CONTINUE")
            ntf = self.client.wait_notification(timeout=0.5)
            if ntf is None:
                self.client.cmd_pause()
                self.expect_notification(REASON_MANUAL)
                raise AssertionError(f"user breakpoint {bp} at 0x{CALL_AFTER:04X} gone after a stop on a "
                                     f"temporary breakpoint at the same address")
            self.expect(ntf.reason == REASON_BREAKPOINT and ntf.address == CALL_AFTER, f"NTF {ntf}")
        finally:
            self.client.cmd_remove_breakpoint(bp)
            self.write(CALL_SPIN, bytes([0x18, 0xFE]))

    def step_two_user_bps_same_address(self):
        # DeZog sends one CMD_ADD_BREAKPOINT per breakpoint, logpoint and assertion: two at one address (a
        # breakpoint and a logpoint) must stay two; removing one must leave the other
        self.install_call_program()
        self.write(CALL_SPIN, bytes([0x18, 0xFA]))  # JR 8200h
        bp_a = self.client.cmd_add_breakpoint(CALL_AFTER)
        bp_b = self.client.cmd_add_breakpoint(CALL_AFTER)
        try:
            self.expect(bp_a > 0 and bp_b > 0, f"ids {bp_a}, {bp_b}")
            self.expect(self.client.cmd_remove_breakpoint(bp_a), "REMOVE_BREAKPOINT a")
            self.expect(self.client.cmd_continue(), "CONTINUE")
            ntf = self.client.wait_notification(timeout=0.5)
            if ntf is None:
                self.client.cmd_pause()
                self.expect_notification(REASON_MANUAL)
                raise AssertionError(f"two breakpoints at 0x{CALL_AFTER:04X} got ids {bp_a} and {bp_b}; "
                                     f"removing {bp_a} removed both")
            self.expect(ntf.reason == REASON_BREAKPOINT and ntf.address == CALL_AFTER, f"NTF {ntf}")
        finally:
            self.client.cmd_remove_breakpoint(bp_a)
            self.client.cmd_remove_breakpoint(bp_b)
            self.write(CALL_SPIN, bytes([0x18, 0xFE]))

    # --- conditional breakpoints ---

    def cond_stops(self, condition, stops, check, a=0, bc2=0x0000):
        """Breakpoint at COND_BP with a DeZog condition: `stops` continues, each must stop there and pass
        check(i, regs); then removal: no more stops"""
        self.install_cond_program(a=a, bc2=bc2)
        bp = self.client.cmd_add_breakpoint(COND_BP, 0, condition)
        self.expect(bp > 0, f"ADD_BREAKPOINT '{condition}'")
        seen = []
        try:
            for i in range(stops):
                self.expect(self.client.cmd_continue(), "CONTINUE")
                ntf = self.client.wait_notification(timeout=3.0)
                if ntf is None:
                    self.client.cmd_pause()
                    self.expect_notification(REASON_MANUAL)
                    raise AssertionError(f"'{condition}': stop {i} missing (seen {seen})")
                self.expect(ntf.reason == REASON_BREAKPOINT and ntf.address == COND_BP,
                            f"'{condition}': NTF {ntf.reason} at 0x{ntf.address:04X}")
                regs = self.regs()
                b, de = regs["bc"] >> 8, regs["de"]
                seen.append((b, de))
                self.expect(check(i, regs), f"'{condition}': stop {i} with B={b} DE={de} (stops {seen})")
        finally:
            self.expect(self.client.cmd_remove_breakpoint(bp), "REMOVE_BREAKPOINT")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect_no_stop(0.25, f"'{condition}' removed")
        return seen

    def cond_never(self, condition, a=0, bc2=0x0000, seconds=0.5):
        """A condition false on every pass: the core never stops (the loop runs thousands of passes)"""
        self.install_cond_program(a=a, bc2=bc2)
        bp = self.client.cmd_add_breakpoint(COND_BP, 0, condition)
        self.expect(bp > 0, f"ADD_BREAKPOINT '{condition}'")
        try:
            self.expect(self.client.cmd_continue(), "CONTINUE")
            self.expect_no_stop(seconds, f"'{condition}' (false on every pass)")
            regs = self.regs()
            self.expect(0x8006 <= regs["pc"] <= 0x800D and regs["de"] != 0,
                        f"the loop ran (PC=0x{regs['pc']:04X}, DE={regs['de']})")
        finally:
            self.client.cmd_remove_breakpoint(bp)

    @staticmethod
    def b_is(value, first_de):
        return lambda i, r: (r["bc"] >> 8) == value and r["de"] == first_de + 256 * i

    def step_cond_b_equals(self):
        # B == 5: stops only at the passes with B = 5 (DE = 4, 260, 516); DeZog would re-check, the core filters
        self.cond_stops("B == 5", 3, self.b_is(5, 4))

    def step_cond_and_lowercase_hex(self):
        # Lower-case register, 0x hex, && with a second register (A = the previous pass's B)
        self.cond_stops("b == 0x05 && A != 0", 2,
                        lambda i, r: (r["bc"] >> 8) == 5 and (r["af"] >> 8) == 4 and r["de"] == 4 + 256 * i)

    def step_cond_and_false(self):
        # B == 1 && A != 0: at B = 1 A is always 0 -> never
        self.cond_never("B == 1 && A != 0")

    def step_cond_shadow(self):
        # A shadow register: B' = 7 (BC' = 0x0700) never changes in the loop
        self.cond_stops("B' == 7 && B == 3", 2, self.b_is(3, 2), bc2=0x0700)
        self.cond_never("B' == 6 && B == 3", bc2=0x0700)

    def step_cond_number_forms(self):
        # $ hex, digit-led h hex, b binary (DeZog's lower-case suffixes), decimal, || and parentheses
        self.cond_stops("B == $0A", 1, self.b_is(10, 9))
        self.cond_stops("B == 0Bh", 1, self.b_is(11, 10))
        self.cond_stops("B == 1100b", 1, self.b_is(12, 11))
        self.cond_stops("(B == 200) || (B == 13)", 2,
                        lambda i, r: (r["bc"] >> 8) == (13, 200)[i] and r["de"] == (12, 199)[i])
        self.cond_stops("(B & 0x0F) == 0x0E && B > 0x10", 2,
                        lambda i, r: (r["bc"] >> 8) == (0x1E, 0x2E)[i])

    def step_cond_word_registers(self):
        # A 16-bit register and PC: DE == 0x0100 (the 257th pass, B wrapped to 1), PC always 0x8007
        self.cond_stops("DE == 0x0100", 1, lambda i, r: r["de"] == 0x100 and (r["bc"] >> 8) == 1)
        self.cond_stops("PC == 0x8007 && B == 2", 1, self.b_is(2, 1))

    def step_cond_fallback_unconditional(self):
        # What the core refuses is set unconditionally (DeZog alone decides): every pass stops
        for condition in ("B / 2 == 2", "LABEL == 1", "B == FFh", "2 ** 3 == 8"):
            self.cond_stops(condition, 4, lambda i, r: (r["bc"] >> 8) == i + 1 and r["de"] == i)

    def step_cond_temp_bp_coexists(self):
        # A step (temp bp) at a conditional breakpoint's address must not take the condition's breakpoint with it
        self.install_cond_program()
        bp = self.client.cmd_add_breakpoint(COND_BP, 0, "B == 5")
        self.expect(bp > 0, "ADD_BREAKPOINT")
        try:
            self.expect(self.client.cmd_continue_bps(COND_BP), "CONTINUE (step to the INC DE)")
            self.expect_notification(REASON_BREAKPOINT, COND_BP)
            self.expect(self.regs()["bc"] >> 8 == 1, "stepped with B = 1")
            self.expect(self.client.cmd_continue(), "CONTINUE")
            self.expect_notification(REASON_BREAKPOINT, COND_BP, timeout=3.0)
            self.expect(self.regs()["bc"] >> 8 == 5, "the conditional breakpoint survives the step and stops at B = 5")
        finally:
            self.client.cmd_remove_breakpoint(bp)

    # --- watchpoints ---

    def step_watchpoint_write(self):
        self.install_program()
        self.expect(self.client.cmd_add_watchpoint(WATCH_TARGET, 1, access=2), "ADD_WATCHPOINT w")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect_notification(REASON_WATCHPOINT_WRITE, WATCH_TARGET)
        self.expect(self.client.cmd_remove_watchpoint(WATCH_TARGET, 1, access=2), "REMOVE_WATCHPOINT")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect_no_stop(0.25, "write watchpoint removed")
        return self.read(WATCH_TARGET, 1) == b"\x01"

    def step_watchpoint_read(self):
        self.write(READ_START, READ_PROGRAM)
        self.write(READ_TARGET, b"\x5A")
        self.set_reg(Reg.PC, READ_START)
        self.expect(self.client.cmd_add_watchpoint(READ_TARGET, 1, access=1), "ADD_WATCHPOINT r")
        try:
            self.expect(self.client.cmd_continue(), "CONTINUE")
            self.expect_notification(REASON_WATCHPOINT_READ, READ_TARGET)
            self.expect(self.client.cmd_continue(), "CONTINUE")
            self.expect_notification(REASON_WATCHPOINT_READ, READ_TARGET)
        finally:
            self.expect(self.client.cmd_remove_watchpoint(READ_TARGET, 1, access=1), "REMOVE_WATCHPOINT")
        # A write watchpoint on a byte the loop only reads: no stop
        self.expect(self.client.cmd_add_watchpoint(READ_TARGET, 1, access=2), "ADD_WATCHPOINT w")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        try:
            self.expect_no_stop(0.25, "write watchpoint on a byte only read")
        finally:
            self.client.cmd_remove_watchpoint(READ_TARGET, 1, access=2)

    def step_watchpoint_range_and_rw(self):
        # A 4-byte range: the write to its third byte stops with that address; 'rw' stops too
        self.install_program()
        self.write(PROGRAM_STORE, bytes([0x32, 0x02, 0x90]))  # LD (9002h),A
        try:
            self.expect(self.client.cmd_add_watchpoint(0x9000, 4, access=2), "ADD_WATCHPOINT 0x9000+4 w")
            self.expect(self.client.cmd_continue(), "CONTINUE")
            self.expect_notification(REASON_WATCHPOINT_WRITE, 0x9002)
            self.expect(self.client.cmd_remove_watchpoint(0x9000, 4, access=2), "REMOVE_WATCHPOINT")
            self.expect(self.client.cmd_add_watchpoint(0x9002, 1, access=3), "ADD_WATCHPOINT rw")
            self.expect(self.client.cmd_continue(), "CONTINUE")
            ntf = self.client.wait_notification(timeout=3.0)
            # The access direction comes from the hit: a write to an 'rw' watchpoint is a WATCHPOINT_WRITE
            self.expect(ntf is not None and ntf.address == 0x9002 and ntf.reason == REASON_WATCHPOINT_WRITE,
                        f"rw watchpoint NTF {ntf}")
            self.expect(self.client.cmd_remove_watchpoint(0x9002, 1, access=3), "REMOVE_WATCHPOINT rw")
        finally:
            self.client.cmd_remove_watchpoint(0x9000, 4, access=2)
            self.client.cmd_remove_watchpoint(0x9002, 1, access=3)
            self.install_program()

    def step_watchpoint_errors_idempotent(self):
        c = self.client
        self.expect(not c.cmd_add_watchpoint(0x9000, 1, access=0), "access 0 must answer error 1")
        self.expect(not c.cmd_add_watchpoint(0x9000, 0, access=2), "size 0 must answer error 1")
        self.expect(c.cmd_add_watchpoint(0x9000, 1, access=2), "add")
        self.expect(c.cmd_add_watchpoint(0x9000, 1, access=2), "the same again: error 0 (idempotent)")
        self.expect(c.cmd_remove_watchpoint(0x9000, 1, access=2), "remove")
        self.expect(c.cmd_remove_watchpoint(0x9000, 1, access=2), "remove again: empty ACK")
        self.install_program()
        self.expect(c.cmd_continue(), "CONTINUE")
        self.expect_no_stop(0.25, "after the idempotent add + remove")

    def step_banked_watchpoint_128k(self):
        self.need(self.is_128k(), "128K banks")
        self.install_program()
        c = self.client
        # 0x9000 is in bank 2 (slot 2): a watchpoint there with the long address stops
        self.expect(c.cmd_add_watchpoint(WATCH_TARGET, 1, bank=3, access=2), "ADD_WATCHPOINT bank 2")
        self.expect(c.cmd_continue(), "CONTINUE")
        try:
            self.expect_notification(REASON_WATCHPOINT_WRITE, WATCH_TARGET)
        finally:
            c.cmd_remove_watchpoint(WATCH_TARGET, 1, bank=3, access=2)
        self.expect(c.cmd_add_watchpoint(WATCH_TARGET, 1, bank=5, access=2), "ADD_WATCHPOINT bank 4")
        self.expect(c.cmd_continue(), "CONTINUE")
        try:
            self.expect_no_stop(0.3, "watchpoint in an unmapped bank")
        finally:
            c.cmd_remove_watchpoint(WATCH_TARGET, 1, bank=5, access=2)

    # --- pause, history, state ---

    def step_pause_running(self):
        self.install_program()
        self.expect(self.client.cmd_continue(), "CONTINUE")
        time.sleep(0.2)
        self.expect(self.client.cmd_pause(), "PAUSE")
        ntf = self.expect_notification(REASON_MANUAL)
        regs = self.regs()
        self.expect(ntf.address == regs["pc"], f"NTF address 0x{ntf.address:04X} != PC 0x{regs['pc']:04X}")
        return PROGRAM_START <= ntf.address <= PROGRAM_JP + 2

    def step_history_info(self):
        info = self.client.cmd_get_history_info()
        return info["available"] and info["recording"]

    def step_history_walk_back(self):
        program_pcs = {PROGRAM_START, PROGRAM_LOOP, PROGRAM_STORE, PROGRAM_JP}
        self.install_program()
        bp = self.client.cmd_add_breakpoint(PROGRAM_JP)
        self.expect(bp > 0, "ADD_BREAKPOINT")
        for _ in range(4):
            self.expect(self.client.cmd_continue(), "CONTINUE")
            self.expect_notification(REASON_BREAKPOINT, PROGRAM_JP)
        self.expect(self.client.cmd_remove_breakpoint(bp), "REMOVE")
        entries = []
        for i in range(10):
            e = self.client.cmd_get_history_entry(i)
            self.expect(e is not None, f"entry {i} missing")
            self.expect(e["pc"] in program_pcs, f"entry {i}: pc {e['pc']:04X} not in program")
            mem = self.client.cmd_read_mem(e["pc"], 4)
            self.expect(e["opcodes"] == mem, f"entry {i}: opcodes {e['opcodes'].hex()} != mem {mem.hex()}")
            entries.append(e)
        # The executed order backwards: JP at index 0 is preceded by the store, then LD A,1
        e0 = self.client.cmd_get_history_entry(0)
        self.expect(e0 is not None and e0["pc"] == entries[0]["pc"], "index 0 revisit")
        return e0["opcodes"] == self.client.cmd_read_mem(e0["pc"], 4)

    def step_history_deep_and_crossframe(self):
        self.expect(self.client.cmd_continue(), "CONTINUE")
        time.sleep(0.4)
        self.expect(self.client.cmd_pause(), "PAUSE")
        self.expect_notification(REASON_MANUAL)
        info = self.client.cmd_get_history_info()
        self.expect(info["available"] and info["recording"], f"history {info}")

        def coherent(e, tag):
            self.expect(e is not None, f"{tag}: entry missing")
            mem = self.client.cmd_read_mem(e["pc"], 4)
            self.expect(e["opcodes"] == mem, f"{tag}: opcodes {e['opcodes'].hex()} != mem {mem.hex()} @0x{e['pc']:04X}")
            self.expect(len(e["slots"]) in (2, 4, 8), f"{tag}: slot count {len(e['slots'])}")
            return e

        # A slow reply must not desynchronise the session: wait long, then judge the time against DeZog's
        # command timeout (cspect remote: 5 s)
        self.client.sock.settimeout(120)
        self.client.COMMAND_TIMEOUT = 120
        slow = []
        try:
            first = []
            for i in range(200):
                t0 = time.time()
                first.append(coherent(self.client.cmd_get_history_entry(i), f"scan[{i}]"))
                if time.time() - t0 > 5:
                    slow.append((i, time.time() - t0))
            for i in (500, 2000, 8000, 50_000):
                t0 = time.time()
                coherent(self.client.cmd_get_history_entry(i), f"deep[{i}]")
                if time.time() - t0 > 5:
                    slow.append((i, time.time() - t0))
        finally:
            self.client.COMMAND_TIMEOUT = DZRPClient.COMMAND_TIMEOUT
            self.client.sock.settimeout(DZRPClient.COMMAND_TIMEOUT)
        self.expect(not slow, "GET_HISTORY_ENTRY over DeZog's 5 s timeout: " +
                    ", ".join(f"index {i} {t:.1f} s" for i, t in slow))
        r0 = self.client.cmd_get_history_entry(0)
        self.expect(r0 is not None and r0["pc"] == first[0]["pc"], "forward revisit changed pc")
        self.expect(self.client.cmd_get_history_entry(50_000_000) is None, "huge index should be out of range")
        # Browsing is read-only (dezogdebugadapter.cpp getHistoryEntry): recording goes on, the history stays
        self.expect(self.client.cmd_get_history_info()["available"], "history still available after a bad index")
        self.expect(self.client.cmd_get_history_entry(100) is not None, "bad index must not wipe the history")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect(self.client.cmd_pause(), "PAUSE")
        self.expect_notification(REASON_MANUAL)
        self.expect(self.client.cmd_get_history_info()["recording"], "resume must restart recording")
        return True

    def step_history_continue_after_browse(self):
        self.install_program()
        bp = self.client.cmd_add_breakpoint(PROGRAM_JP)
        self.expect(bp > 0, "ADD_BREAKPOINT")
        self.expect(self.client.cmd_get_history_entry(3) is not None, "browse")
        self.expect(self.client.cmd_continue(), "CONTINUE")
        self.expect_notification(REASON_BREAKPOINT, PROGRAM_JP)
        self.expect(self.client.cmd_remove_breakpoint(bp), "REMOVE")
        regs = self.regs()
        info = self.client.cmd_get_history_info()
        return regs["pc"] == PROGRAM_JP and info["recording"] and \
            self.read(WATCH_TARGET, 1) == b"\x01" and not self.client.has_pending_notification()

    def step_state_roundtrip(self):
        # READ_STATE / WRITE_STATE bring back memory (incl. a paged-out bank), registers, the shadow set, R, IM
        c = self.client
        self.need(self.saved_state, "no state on this machine (READ_STATE empty)")
        self.write(0x8200, bytes([0xA1, 0xB2]))
        if self.is_128k():
            c.cmd_write_bank(6, bytes([0x66] * 4))
        regs_in = {Reg.PC: 0x8200, Reg.SP: 0xFE00, Reg.HL: 0x1234, Reg.IX: 0x4321, Reg.HL2: 0x5A5A,
                   Reg.AF2: 0xA55A}
        for reg, value in regs_in.items():
            self.set_reg(reg, value)
        self.set_reg(Reg.IM, 2)
        self.set_reg(Reg.I, 0x3B)
        before = self.regs()
        state = c.cmd_read_state()
        self.expect(len(state) > 0, "READ_STATE empty")
        self.write(0x8200, bytes([0x00, 0x00]))
        if self.is_128k():
            c.cmd_write_bank(6, bytes(4))
        for reg in regs_in:
            self.set_reg(reg, 0)
        self.set_reg(Reg.IM, 0)
        self.set_reg(Reg.I, 0)
        self.expect(c.cmd_write_state(state), "WRITE_STATE")
        after = self.regs()
        diff = {k: (after[k], before[k]) for k in ("pc", "sp", "hl", "ix", "hl2", "af2", "im", "i", "slots")
                if after[k] != before[k]}
        self.expect(not diff, f"registers after WRITE_STATE (got, expected): {diff}")
        self.expect(self.read(0x8200, 2) == bytes([0xA1, 0xB2]), "memory after WRITE_STATE")
        if self.is_128k():
            try:
                c.cmd_set_slot(3, 6)
                self.expect(self.read(0xC000, 4) == bytes([0x66] * 4), "bank 6 (paged out) after WRITE_STATE")
            finally:
                self.restore_slots()

    # --- robustness ---

    def step_dezog_real_flow(self):
        # Byte-exact replay of a real CSpect-remote session start (VS Code "stop on start"): breakpoint setup,
        # registers fetch, disassembly and stack reads (incl. the 0x8000-byte dumps), continue + pause + NTF,
        # and enough commands to roll the sequence number past 15
        c = self.client
        bp = c.cmd_add_breakpoint(0x8000, 0, "")
        if bp == 0:
            return False
        regs = self.regs()
        pc, sp = regs["pc"], regs["sp"]
        self.expect(len(c.cmd_read_mem(pc, 0x40)) == 0x40, "disassembly window")
        self.expect(len(c.cmd_read_mem(sp, 0x40)) == 0x40, "stack fetch")
        self.expect(len(c.cmd_read_mem(0x0000, 0x8000)) == 0x8000, "big disasm dump")
        self.expect(c.cmd_continue(), "CONTINUE")
        self.expect(c.cmd_pause(), "PAUSE")
        self.expect_notification(REASON_MANUAL)
        self.expect(not c.has_pending_notification(), "stray notification")
        for _ in range(16):
            self.expect("pc" in c.cmd_get_registers(), "seq rollover")
        return c.cmd_remove_breakpoint(bp)

    def step_unknown_and_unhandled_commands(self):
        # No NAK (DeZog compares the seq byte raw): an empty ACK for an unknown id
        for cmd, payload in [(99, b""), (0xFF, b"\x01\x02")]:
            resp = self.client._send_raw_command(cmd, payload)
            self.expect(not resp.nak and len(resp.payload) == 0, f"command {cmd}: nak={resp.nak} payload={resp.payload!r}")
        # The dezogif / CSpect commands DeZog does not send to this server still answer in their own format
        self.write(PROGRAM_START, PROGRAM)
        expected = {
            13: bytes([PROGRAM[0], PROGRAM[1]]),  # SET_BREAKPOINTS: the original byte at each 3-byte address
            14: b"",                              # RESTORE_MEM: nothing was patched, empty ACK
            15: b"\x01\x02\x03",                  # LOOPBACK: the payload back
            23: b"",                              # INTERRUPT_ON_OFF: empty ACK
        }
        payloads = {13: b"\x00\x80\x00\x01\x80\x00", 14: b"\x00\x80\x00\xF3", 15: b"\x01\x02\x03", 23: b"\x00"}
        for cmd, want in expected.items():
            resp = self.client._send_raw_command(cmd, payloads[cmd])
            self.expect(not resp.nak and resp.payload == want,
                        f"command {cmd}: nak={resp.nak} payload={resp.payload!r}, want {want!r}")
        self.expect(self.client.cmd_read_mem(PROGRAM_START, len(PROGRAM)) == PROGRAM, "RESTORE_MEM left memory alone")
        # EXEC_ASM: error 1 (not supported) + F A C B E D L H
        resp = self.client._send_raw_command(22, b"\x00")
        self.expect(not resp.nak and len(resp.payload) == 9 and resp.payload[0] == 1, f"EXEC_ASM {resp.payload!r}")
        self.expect("pc" in self.client.cmd_get_registers(), "session alive")

    def step_restore_original(self):
        if self.saved_state:
            self.expect(self.client.cmd_write_state(self.saved_state), "WRITE_STATE (original)")
        else:
            self.restore_slots()
        self.expect(self.regs()["slots"] == self.slots0, "original slots")
        return self.client.cmd_continue()

    def step_close(self):
        ok = self.client.cmd_close()
        self.client.disconnect()
        return ok

    def step_reconnect(self):
        # The server takes the next client after CLOSE: a second session's INIT + registers + CLOSE
        self.client = DZRPClient(self.client.host, self.client.port)
        deadline = time.time() + 5
        while not self.client.connect():
            self.expect(time.time() < deadline, "reconnect")
            time.sleep(0.2)
        resp = self.client.cmd_init(version=(2, 0, 0), name="DeZog")
        self.expect(resp.get("error") == 0 and resp.get("machine") == self.machine, f"INIT {resp}")
        self.expect("pc" in self.client.cmd_get_registers(), "GET_REGISTERS")
        ok = self.client.cmd_close()
        self.client.disconnect()
        return ok

    def run(self) -> bool:
        session = [
            ("Connect", self.step_connect),
            ("CMD_INIT (machine + name)", self.step_init),
        ]
        for name, func in session:
            self.run_step(name, func)
        if self.failed:
            self.print_results()
            return False
        steps = [
            ("CMD_GET_SUPPORTED_COMMANDS exact set", self.step_supported_commands),
            ("CMD_PAUSE -> NTF_PAUSE MANUAL (twice)", self.step_pause),
            ("CMD_READ_STATE (capture original)", self.step_capture_state),
            ("CMD_GET_REGISTERS slots layout", self.step_registers_and_slots),
            ("CMD_SET_REGISTER every id (16/8-bit, shadow, I, R bit 7, IM)", self.step_set_register_every_id),
            ("CMD_SET_REGISTER shadow set independent", self.step_shadow_set_independent),
            ("R counts M1 cycles, keeps bit 7", self.step_refresh_register_runs),
            ("CMD_WRITE_MEM/READ_MEM round-trip (4 B, 4 KB)", self.step_memory_roundtrip),
            ("CMD_READ_MEM/WRITE_MEM wrap at 0xFFFF", self.step_memory_wrap),
            ("CMD_READ_MEM_BLOCKS", self.step_read_mem_blocks),
            ("CMD_READ_MEM full 64K", self.step_read_full_64k),
            ("128K: CMD_WRITE_BANK + CMD_SET_SLOT (RAM, ROM, aliases)", self.step_banking_128k),
            ("128K: CMD_WRITE_PORT #7FFD paging", self.step_port_paging_128k),
            ("CMD_READ_PORT/WRITE_PORT reply shapes", self.step_ports),
            ("CMD_WRITE_PORT/READ_PORT reach the device (AY register)", self.step_ports_reach_device),
            ("CMD_SET_BORDER (in the saved state)", self.step_border),
            ("Next commands on another machine: empty / zero replies", self.step_next_commands_elsewhere),
            ("Next: SET_SLOT / SET_NEXTREGS / GET_TBBLUE_REG", self.step_next_slots_and_tbblue),
            ("Next: READ_BANK_MEM / WRITE_BANK_MEM", self.step_next_bank_memory),
            ("Next: READ_BANK_MEM 0xFF = the ROM slot 0 shows", self.step_next_rom_bank),
            ("Next: WRITE_MEM at 0xE000 lands in MMU7's bank", self.step_next_write_mem_slot7),
            ("Next: WRITE_BANK takes an 8K bank (DeZog)", self.step_next_write_bank_8k),
            ("Next: GET_SPRITES_PALETTE", self.step_next_palette),
            ("Next: GET_SPRITES_CLIP_WINDOW_AND_CONTROL", self.step_next_clip_and_control),
            ("Next: GET_SPRITES / GET_SPRITE_PATTERNS (CPU-written)", self.step_next_sprites),
            ("Next: sprite attributes through CMD_WRITE_PORT", self.step_next_write_port_sprites),
            ("Next: breakpoint on a long (8K bank) address", self.step_next_banked_breakpoint),
            ("Execution breakpoint hit", self.step_breakpoint_hit),
            ("Breakpoint re-hit + remove", self.step_breakpoint_rehit),
            ("Two breakpoints, remove one, remove unknown id", self.step_breakpoint_two_ids_remove),
            ("128K: breakpoint on a long (bank) address", self.step_banked_breakpoint_128k),
            ("ROM breakpoint 0x0038 via IM1 interrupt", self.step_rom_breakpoint_via_interrupt),
            ("DI: no interrupt reaches 0x0038", self.step_di_blocks_interrupt),
            ("128K: breakpoint on a long ROM-bank address (both ROMs)", self.step_banked_rom_breakpoint),
            ("CMD_CONTINUE temp BPs (step) + auto-clear", self.step_step_over_shape),
            ("Rapid step loop x30 (one NTF per step)", self.step_rapid_step_loop),
            ("Step into CALL ... RET (bp1 + bp2)", self.step_step_into_call_and_ret),
            ("Step over CALL", self.step_step_over_call),
            ("Step out (bp1 = return address)", self.step_step_out),
            ("Step from a user breakpoint (passed once, kept)", self.step_step_from_user_breakpoint),
            ("Temp BP at a user BP's address keeps the user BP", self.step_temp_bp_on_user_bp),
            ("Two user BPs at one address, remove one", self.step_two_user_bps_same_address),
            ("Condition 'B == 5': stops only when true", self.step_cond_b_equals),
            ("Condition 'b == 0x05 && A != 0'", self.step_cond_and_lowercase_hex),
            ("Condition 'B == 1 && A != 0': never", self.step_cond_and_false),
            ("Condition with a shadow register B'", self.step_cond_shadow),
            ("Condition numbers $ / h / b / decimal, || ()", self.step_cond_number_forms),
            ("Condition DE / PC", self.step_cond_word_registers),
            ("Refused conditions fall back to unconditional", self.step_cond_fallback_unconditional),
            ("Step at a conditional BP keeps the condition", self.step_cond_temp_bp_coexists),
            ("Write watchpoint hit + remove", self.step_watchpoint_write),
            ("Read watchpoint hit; write WP on a read byte silent", self.step_watchpoint_read),
            ("Watchpoint range (4 bytes) + rw", self.step_watchpoint_range_and_rw),
            ("Watchpoint errors (access 0, size 0), idempotent add/remove", self.step_watchpoint_errors_idempotent),
            ("128K: watchpoint on a long (bank) address", self.step_banked_watchpoint_128k),
            ("CMD_PAUSE while running (NTF address = PC)", self.step_pause_running),
            ("History: CMD_GET_HISTORY_INFO (available + recording)", self.step_history_info),
            ("History: walk back 10 entries via TTD", self.step_history_walk_back),
            ("History: deep + cross-frame reverse debugging", self.step_history_deep_and_crossframe),
            ("History: continue after browsing", self.step_history_continue_after_browse),
            ("CMD_READ_STATE/WRITE_STATE round-trip (regs, memory, banks)", self.step_state_roundtrip),
            ("DeZog real-flow replay (bp/regs/disasm/stack/NTF/seq-rollover)", self.step_dezog_real_flow),
            ("Unknown commands -> empty ACK; dezogif commands 13-15, 22, 23", self.step_unknown_and_unhandled_commands),
            ("Restore original state + CONTINUE", self.step_restore_original),
            ("CMD_CLOSE", self.step_close),
            ("Reconnect: second session INIT/GET_REGISTERS/CLOSE", self.step_reconnect),
        ]
        for name, func in steps:
            self.run_step(name, func)
        self.print_results()
        return self.failed == 0

    def print_results(self):
        print("\n" + "=" * 72)
        print(f"DZRP Real-Emulator Verification Results (machine type {self.machine})")
        print("=" * 72)
        for name, status, msg in self.results:
            print(f"  [{status}]   {name}" + (f": {msg}" if msg else ""))
        print("-" * 72)
        print(f"  Total: {self.passed} passed, {self.failed} failed, {self.known} known, {self.skipped} skipped")
        print("=" * 72)


class QtHost:
    """unreal-qt offscreen on ports of its own (WebAPI base, DeZog base + 400), one muted machine"""

    OFFSETS = (("UNREAL_WEBAPI_PORT", 0), ("UNREAL_CLI_PORT", 100), ("UNREAL_MCP_PORT", 200),
               ("UNREAL_GDB_PORT", 300), ("UNREAL_DEZOG_PORT", 400), ("UNREAL_ZRCP_PORT", 500))

    def __init__(self, binary: str, port_base: int, model: str, log_path: Path):
        self.binary = binary
        self.port_base = port_base
        self.model = model
        self.log_path = log_path
        self.process = None
        self.base_url = f"http://127.0.0.1:{port_base}/api/v1"

    @property
    def dezog_port(self):
        return self.port_base + 400

    def start(self):
        for _, offset in self.OFFSETS:
            if port_open(self.port_base + offset):
                raise SystemExit(f"port {self.port_base + offset} is taken: pick another --port-base")
        env = dict(os.environ)
        for name, offset in self.OFFSETS:
            env[name] = str(self.port_base + offset)
        env.setdefault("UNREAL_STREAMDECK_SUPERVISOR", "off")
        env.setdefault("UNREAL_QT_VIEW_SETTINGS", "off")
        env.setdefault("UNREAL_RECENT_FILES", "off")
        self.log = open(self.log_path, "w")
        self.process = subprocess.Popen([self.binary, "-platform", "offscreen"], env=env, stdout=self.log,
                                        stderr=subprocess.STDOUT)
        deadline = time.time() + 40
        while True:
            try:
                http_json("GET", f"{self.base_url}/emulator", timeout=2)
                break
            except Exception:  # noqa: BLE001
                if self.process.poll() is not None:
                    raise SystemExit(f"{self.binary} exited with {self.process.returncode} (log {self.log_path})")
                if time.time() > deadline:
                    raise SystemExit("unreal-qt WebAPI did not answer")
                time.sleep(0.5)
        created = http_json("POST", f"{self.base_url}/emulator/start", {"model": self.model}, timeout=30)
        emulator_id = created.get("id")
        if not emulator_id:
            raise SystemExit(f"could not create a {self.model}: {created}")
        # Silent: a verification run makes no sound on the host
        try:
            http_json("PUT", f"{self.base_url}/emulator/{emulator_id}/audio/mixer/master", {"muted": True})
        except Exception as e:  # noqa: BLE001
            print(f"warning: mute failed: {e}")
        print(f"unreal-qt pid {self.process.pid}: {self.model} {emulator_id}, DeZog port {self.dezog_port} "
              f"(log {self.log_path})")
        if not wait_for_port(self.dezog_port, timeout=20):
            raise SystemExit(f"DeZog port {self.dezog_port} did not open")

    def stop(self):
        if self.process is None:
            return
        self.process.terminate()
        try:
            self.process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
        self.log.close()
        print(f"unreal-qt stopped (log {self.log_path})")


def main():
    parser = argparse.ArgumentParser(description="DZRP verification against a real emulator")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=12000, help="DeZog server port (not with --launch-qt)")
    parser.add_argument("--launch", action="store_true",
                        help="Launch the headless dezog-emulator-host from --build-dir first")
    parser.add_argument("--launch-qt", metavar="UNREAL_QT",
                        help="Launch this unreal-qt binary offscreen on ports of its own (--port-base)")
    parser.add_argument("--port-base", type=int, default=18170,
                        help="--launch-qt: WebAPI port; CLI +100, MCP +200, GDB +300, DeZog +400, ZRCP +500")
    parser.add_argument("--build-dir", default="cmake-build-release")
    parser.add_argument("--model", default="PENTAGON", help="Emulator model for --launch / --launch-qt")
    parser.add_argument("--startup-timeout", type=float, default=40.0,
                        help="seconds to wait for the DZRP port when using --launch")
    parser.add_argument("--instance-timeout", type=float, default=20.0,
                        help="seconds to wait for the host to expose an emulator instance")
    args = parser.parse_args()

    root = find_project_root()
    scratch = root / "scratch" / "verification-dezog"
    scratch.mkdir(parents=True, exist_ok=True)

    process = None
    qt = None
    log_path = None
    port = args.port
    if args.launch_qt:
        qt = QtHost(args.launch_qt, args.port_base, args.model, scratch / f"unreal-qt-{args.model.lower()}.log")
        try:
            qt.start()
        except BaseException:
            qt.stop()
            raise
        port = qt.dezog_port
    elif args.launch:
        binary = locate_host_binary(root / args.build_dir)
        if binary is None:
            print(f"ERROR: dezog-emulator-host not found under {root / args.build_dir}/bin "
                  f"(CMake -DBUILD_DEZOG_EMULATOR_HOST=ON, target dezog-emulator-host)")
            return 2
        log_path = scratch / f"dezog-emulator-host-{args.model.lower()}.log"
        print(f"Launching {binary} port={port} model={args.model} (log: {log_path})")
        with open(log_path, "w") as log:
            process = subprocess.Popen([str(binary), str(port), args.model],
                                       stdout=log, stderr=subprocess.STDOUT, cwd=binary.parent)
        if not wait_for_port(port, args.host, args.startup_timeout):
            print(f"ERROR: DeZog port {port} did not open within {args.startup_timeout}s")
            process.terminate()
            return 2

    try:
        verifier = EmulatorVerifier(args.host, port, args.instance_timeout)
        success = verifier.run()
    finally:
        if process is not None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
            print(f"dezog-emulator-host stopped (log: {log_path})")
        if qt is not None:
            qt.stop()

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
