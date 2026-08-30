#!/usr/bin/env python3
"""ZRCP verification against a REAL emulator instance (any host running the production module).

Drives the production ZEsarUX-impersonating module wired into the emulator:

  DeZog-shaped ZRCP client  ->  zrcp::Server  ->  DezogDebugAdapter  ->  Emulator

It replays DeZog's exact init/disconnect sequences, installs a tiny Z80 loop
in RAM and checks registers, breakpoints (with conditions and pass counts),
watchpoints, step/step-over, interruptable run, reverse debugging via
cpu-history, extended-stack and the misc queries end-to-end. The state
captured at the start (registers + touched memory) is restored at the end.

Usage:
    # Against a running host on the default port 10000. Any host works as long
    # as it exposes a live emulator instance: unreal-qt (start an emulator from
    # the UI first - the module reports machine UNKNOWN until then), or the
    # standalone automation binary.
    python3 tools/verification/zesarux/verify_zrcp_emulator.py

    # Custom host / port (ZRCP port: UNREAL_ZRCP_PORT env var, else 10000)
    python3 tools/verification/zesarux/verify_zrcp_emulator.py --host 192.168.1.10 --port 10010
"""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from zrcp_client import (  # noqa: E402
    RUN_BANNER, ZRCPClient, is_error, parse_disasm_line, parse_history_entry,
    parse_registers,
)

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
PROGRAM_PCS = {PROGRAM_START, PROGRAM_LOOP, PROGRAM_STORE, PROGRAM_JP}

SERVER_VERSION = "12.1"


class EmulatorVerifier:
    def __init__(self, host: str, port: int, timeout: float = 15.0):
        self.client = ZRCPClient(host, port, timeout=timeout)
        self.results = []
        self.passed = 0
        self.failed = 0
        self.machine = ""
        self.saved_regs = {}
        self.saved_memory = {}

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

    def cmd(self, line: str, timeout=None) -> str:
        return self.client.command(line, timeout)

    def read_mem(self, addr: int, size: int) -> bytes:
        return bytes.fromhex(self.cmd(f"read-memory {addr} {size}"))

    def write_mem(self, addr: int, data: bytes):
        assert self.cmd(f"write-memory-raw {addr} {data.hex().upper()}") == ""

    def install_program(self):
        self.write_mem(PROGRAM_START, PROGRAM)
        self.write_mem(WATCH_TARGET, b"\x00")
        assert self.cmd(f"set-register PC={PROGRAM_START}") == ""
        assert self.cmd("set-register SP=65280") == ""

    def save_state(self):
        self.saved_regs = parse_registers(self.cmd("get-registers"))
        for base in (0x8000, 0x9000):
            self.saved_memory[base] = self.read_mem(base, 0x100)

    def restore_state(self):
        for base, data in self.saved_memory.items():
            self.write_mem(base, data)
        for name, value in (("PC", self.saved_regs.get("PC")),
                            ("SP", self.saved_regs.get("SP"))):
            if value is not None:
                assert self.cmd(f"set-register {name}={value}") == ""

    # --- steps ---

    def step_connect(self):
        if not self.client.connect():
            return False
        return any("welcome" in line.lower() for line in self.client.banner)

    def step_init_sequence(self):
        # The exact command stream DeZog's ZesaruxRemote sends on connect,
        # order included (zesaruxremote.ts connectToRemote + initSequence).
        # Fails iff the real extension would fail to attach.
        assert self.cmd("close-all-menus") == ""
        about = self.cmd("about")
        assert about != "" and not is_error(about), about
        assert self.cmd("get-version") == SERVER_VERSION, self.cmd("get-version")
        assert self.cmd("set-debug-settings 0") == ""
        # Log-only answers complete with an empty result (DeZog strips the
        # "log> " line before its prompt check and forwards it to the UI)
        self.client.logs.clear()
        assert self.cmd("hard-reset-cpu") == ""
        assert len(self.client.logs) == 1 and "hard-reset-cpu" in self.client.logs[0], self.client.logs
        assert self.cmd("enter-cpu-step") == ""
        self.client.logs.clear()
        assert self.cmd("load /tmp/game.z80") == ""
        assert len(self.client.logs) == 1 and "load request ignored" in self.client.logs[0], self.client.logs
        self.client.logs.clear()
        assert self.cmd("smartload /tmp/game.z80") == ""
        assert len(self.client.logs) == 1 and "load request ignored" in self.client.logs[0], self.client.logs
        self.machine = self.cmd("get-current-machine")
        assert not is_error(self.machine) and ("128k" in self.machine.lower()
                                               or "48k" in self.machine.lower()), self.machine
        assert self.cmd("clear-membreakpoints") == ""
        assert self.cmd("enable-breakpoints") == ""
        for bp_id in range(1, 101):  # DeZog clears its whole 100-slot table
            assert self.cmd(f"disable-breakpoint {bp_id}") == "", bp_id
        assert self.cmd("cpu-code-coverage enabled no") == ""
        assert self.cmd("cpu-code-coverage get") == ""
        assert self.cmd("extended-stack enabled yes") == ""
        assert self.cmd("cpu-history enabled yes") == ""
        assert self.cmd("cpu-history set-max-size 8192") == ""
        assert self.cmd("cpu-history clear") == ""
        assert self.cmd("cpu-history started yes") == ""
        assert self.cmd("cpu-history ignrephalt yes") == ""
        assert self.cmd("cpu-history ignrepldxr yes") == ""
        assert self.cmd("cpu-history is-enabled") == "1"
        assert self.cmd("cpu-history is-started") == "1"
        return True

    def step_registers_line(self):
        regs = parse_registers(self.cmd("get-registers"))
        self.save_state()
        banked = "128k" in self.machine.lower()
        ok = (all(c in "SZ5H3PNC-" for c in regs["flags"] + regs["flags2"])
              and len(regs["flags"]) == 8
              and regs["memptr"] == 0
              and regs["im"] in (0, 1, 2)
              and regs["iff"] == "--"
              and len(regs["mmu"]) == 8
              and regs["mmu"][4:] == regs["mmu"][:4])  # groups 5-8 repeat
        if banked:
            ok = ok and regs["mmu"][0] >= 0x8000 and regs["mmu"][1] == 5 and regs["mmu"][2] == 2
        return ok

    def step_set_register(self):
        # DeZog sends decimal values; AF', IFF1 must ack (IFF is ignored)
        assert self.cmd("set-register PC=4660") == ""     # 0x1234
        assert self.cmd("set-register HL=57005") == ""    # 0xDEAD
        assert self.cmd("set-register A=1") == ""
        assert self.cmd("set-register AF'=13107") == ""   # 0x3333
        assert self.cmd("set-register IFF1=0") == ""
        regs = parse_registers(self.cmd("get-registers"))
        return (regs["PC"] == 0x1234 and regs["HL"] == 0xDEAD
                and (regs["AF"] >> 8) == 1 and regs["AF2"] == 0x3333)

    def step_memory_roundtrip(self):
        self.write_mem(0x8100, bytes([0xDE, 0xAD, 0xBE, 0xEF]))
        return self.read_mem(0x8100, 4) == bytes([0xDE, 0xAD, 0xBE, 0xEF])

    def step_memory_full_64k(self):
        # DeZog's disassembly view fetches the whole space in one request and
        # asserts the answer is exactly len*2 hex chars (fetch64kMemory)
        full = bytes.fromhex(self.cmd("read-memory 0 65536"))
        return (len(full) == 65536
                and full == self.read_mem(0, 32768) + self.read_mem(32768, 32768)
                and self.cmd("read-memory 0 65537") == "Error. Invalid length"
                and self.read_mem(0xFFFF, 2) == full[0xFFFF:] + full[:1])

    def step_disassemble(self):
        self.install_program()
        out = self.cmd("disassemble 32768 3")
        lines = out.split("\n")
        if len(lines) != 3:
            return False
        addr0, _, mnemonic0 = parse_disasm_line(lines[0])
        addr1, _, mnemonic1 = parse_disasm_line(lines[1])
        return (addr0 == PROGRAM_START and mnemonic0.startswith("DI")
                and addr1 == PROGRAM_LOOP and mnemonic1.startswith("LD"))

    def step_cpu_step(self):
        self.install_program()
        out = self.cmd("cpu-step")
        addr, _, mnemonic = parse_disasm_line(out)
        regs = parse_registers(self.cmd("get-registers"))
        return addr == PROGRAM_LOOP and mnemonic.startswith("LD") and regs["PC"] == PROGRAM_LOOP

    def step_cpu_step_over_call(self):
        # 8000: CD 06 80 (CALL 8006h); 8003: 00 (NOP); 8006: C9 (RET)
        self.write_mem(PROGRAM_START, bytes([0xCD, 0x06, 0x80, 0x00, 0x00, 0x00, 0xC9]))
        assert self.cmd(f"set-register PC={PROGRAM_START}") == ""
        out = self.cmd("cpu-step-over")  # exactly ONE line: no banner, no break echo
        addr, _, mnemonic = parse_disasm_line(out)
        return addr == 0x8003 and mnemonic.startswith("NOP")

    def step_run_breakpoint(self):
        self.install_program()
        assert self.cmd("set-breakpointaction 1") == ""
        assert self.cmd("set-breakpoint 1 PC=08006h") == ""
        assert self.cmd("enable-breakpoint 1") == ""
        banner, rest = self.client.run()
        lines = rest.split("\n")
        regs = parse_registers(self.cmd("get-registers"))
        # DeZog's remove idiom: disable + plain numeric condition (never fires)
        assert self.cmd("disable-breakpoint 1") == ""
        assert self.cmd("set-breakpoint 1 0") == ""
        return (banner == RUN_BANNER
                and lines[0] == "Breakpoint fired: PC=08006h"
                and parse_disasm_line(lines[1])[0] == PROGRAM_JP
                and regs["PC"] == PROGRAM_JP)

    def step_run_condition_false(self):
        # A is preset to 1 and the loop only ever loads A=1: the condition
        # never holds, so the breakpoint must silently auto-resume. The blank
        # line then stops MANUALLY: no "Breakpoint fired" in the output.
        self.install_program()
        assert self.cmd("set-register A=1") == ""
        assert self.cmd("set-breakpoint 2 PC=08001h and (A<>1)") == ""
        assert self.cmd("enable-breakpoint 2") == ""
        self.client.send_line("run")
        assert self.client.read_line() == RUN_BANNER
        time.sleep(0.1)
        self.client.send_blank()
        rest = self.client.read_until_prompt(timeout=10.0)
        regs = parse_registers(self.cmd("get-registers"))
        assert self.cmd("disable-breakpoint 2") == ""
        assert self.cmd("set-breakpoint 2 0") == ""
        return ("Breakpoint fired" not in rest and rest != ""
                and regs["PC"] in PROGRAM_PCS)

    def step_run_condition_true(self):
        self.install_program()
        assert self.cmd("set-breakpoint 3 PC=08001h and (A=1)") == ""
        assert self.cmd("enable-breakpoint 3") == ""
        _, rest = self.client.run()
        return rest.split("\n")[0] == "Breakpoint fired: PC=08001h and (A=1)"

    def step_pass_count(self):
        self.install_program()
        assert self.cmd("set-breakpoint 4 PC=08001h") == ""
        assert self.cmd("set-breakpointpasscount 4 3") == ""
        assert self.cmd("enable-breakpoint 4") == ""
        _, rest = self.client.run()
        regs = parse_registers(self.cmd("get-registers"))
        assert self.cmd("disable-breakpoint 4") == ""
        assert self.cmd("set-breakpoint 4 0") == ""
        return rest.split("\n")[0] == "Breakpoint fired: PC=08001h" and regs["PC"] == PROGRAM_LOOP

    def step_watchpoint(self):
        self.install_program()
        assert self.cmd("set-membreakpoint 9000h 2 1") == ""
        _, rest = self.client.run()
        lines = rest.split("\n")
        assert self.cmd("set-membreakpoint 9000h 0 1") == ""  # DeZog removal
        assert self.cmd("clear-membreakpoints") == ""
        return (lines[0] == "Breakpoint fired: Memory Breakpoint Write Address: 9000H"
                and self.read_mem(WATCH_TARGET, 1) == b"\x01")

    def step_history_reverse_debug(self):
        # The headline ZRCP feature: DeZog's ZesaruxCpuHistory browsing.
        self.install_program()
        assert self.cmd("set-breakpoint 5 PC=08006h") == ""
        assert self.cmd("enable-breakpoint 5") == ""
        for _ in range(3):  # a few loop iterations of recorded history
            _, rest = self.client.run()
            assert rest.split("\n")[0] == "Breakpoint fired: PC=08006h"
        assert self.cmd("disable-breakpoint 5") == ""
        assert self.cmd("set-breakpoint 5 0") == ""

        size = int(self.cmd("cpu-history get-size"))
        assert size > 3, f"history size {size}"
        assert self.cmd("cpu-history get-max-size") == "8192"

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
        deep = self.cmd(f"cpu-history get {min(1000, size - 1)}")
        assert not is_error(deep), deep
        # Re-visiting index 0 must be stable (cache path)
        again = parse_history_entry(self.cmd("cpu-history get 0"))
        out_of_range = self.cmd(f"cpu-history get {size + 1000}")
        print(f"  history: size={size}, {(time.time() - t0) * 100:.1f} ms/10 entries")
        return again["PC"] == entries[0]["PC"] and is_error(out_of_range)

    def step_extended_stack(self):
        self.install_program()
        # Opcode anchors: CALL at 0x9000, RST 8 at 0x9004
        self.write_mem(0x9000, bytes([0xCD, 0x00, 0x00, 0x00, 0xCF]))
        # Stack values 0x9003 / 0x9005 / 0x8007 read upward from SP
        self.write_mem(0xFF00, bytes([0x03, 0x90, 0x05, 0x90, 0x07, 0x80]))
        assert self.cmd("extended-stack enabled yes") == ""
        out = self.cmd("extended-stack get 3")
        assert self.cmd("extended-stack enabled no") == ""
        return (out == "9003H call\n9005H rst\n8007H push"
                and is_error(self.cmd("extended-stack get 1")))

    def step_misc_queries(self):
        tstates = int(self.cmd("get-tstates-partial"))
        assert self.cmd("reset-tstates-partial") == ""
        after = int(self.cmd("get-tstates-partial"))
        frequency = int(self.cmd("get-cpu-frequency"))
        pages = self.cmd("get-memory-pages")
        parts = pages.split(" ")
        ok_pages = (len(parts) == 5 and parts[4] == ""
                    and all(p.startswith("R") for p in parts[:4]))
        return 0 <= after <= tstates and frequency > 0 and ok_pages

    def step_dezog_flow(self):
        # A slice of a real session: registers, disassembly window and stack
        # fetches in the sizes DeZog's remote disassembler requests them.
        regs = parse_registers(self.cmd("get-registers"))
        pc, sp = regs["PC"], regs["SP"]
        assert len(self.read_mem(pc, 0x40)) == 0x40        # disassembly window
        assert len(self.read_mem(sp, 0x40)) == 0x40        # stack fetch
        assert len(self.read_mem(0x0000, 0x8000)) == 0x8000  # big disasm dump
        # Step output with registers (set-debug-settings bit 0), like DeZog's
        # step rendering: regs line (TSTATES suffix) + disassembly line
        assert self.cmd("set-debug-settings 1") == ""
        out = self.cmd("cpu-step")
        assert self.cmd("set-debug-settings 0") == ""
        lines = out.split("\n")
        return (len(lines) == 2 and lines[0].startswith("PC=")
                and lines[0].endswith(" TSTATES: 0") and len(lines[1]) > 7)

    def step_restore_and_quit(self):
        self.restore_state()
        # The exact disconnect sequence DeZog sends (all must succeed)
        assert self.cmd("") == ""
        assert self.cmd("cpu-history enabled no") == ""
        assert self.cmd("cpu-code-coverage enabled no") == ""
        assert self.cmd("extended-stack enabled no") == ""
        assert self.cmd("clear-membreakpoints") == ""
        assert self.cmd("disable-breakpoints") == ""
        assert self.cmd("exit-cpu-step") == ""
        assert self.cmd("quit") == ""
        closed = self.client.socket_closed()
        self.client.disconnect()
        # The server survives and accepts a fresh session
        if not self.client.connect():
            return False
        version = self.client.command("get-version")
        self.client.command("quit")
        self.client.disconnect()
        return closed and version == SERVER_VERSION

    def run(self) -> bool:
        steps = [
            ("Connect (welcome banner + prompt)", self.step_connect),
            ("DeZog init sequence (incl. 100 disables)", self.step_init_sequence),
            ("get-registers line shape (MMU/flags/order)", self.step_registers_line),
            ("set-register PC/HL/A/AF'/IFF1", self.step_set_register),
            ("write/read-memory round-trip", self.step_memory_roundtrip),
            ("full 64K read-memory (DeZog fetch64kMemory)", self.step_memory_full_64k),
            ("disassemble <addr> <n>", self.step_disassemble),
            ("cpu-step advances PC", self.step_cpu_step),
            ("cpu-step-over CALL (temp bp, 1 line)", self.step_cpu_step_over_call),
            ("run -> breakpoint fired + echo", self.step_run_breakpoint),
            ("run, condition false -> silent resume + interrupt", self.step_run_condition_false),
            ("run, condition true -> stop with condition", self.step_run_condition_true),
            ("set-breakpointpasscount skips hits", self.step_pass_count),
            ("set-membreakpoint fires on write", self.step_watchpoint),
            ("cpu-history reverse debugging", self.step_history_reverse_debug),
            ("extended-stack classification", self.step_extended_stack),
            ("tstates / frequency / memory-pages", self.step_misc_queries),
            ("DeZog flow (disasm dumps + regs step output)", self.step_dezog_flow),
            ("Restore state + quit sequence + reconnect", self.step_restore_and_quit),
        ]
        for name, func in steps:
            self.run_step(name, func)
        self.print_results()
        return self.failed == 0

    def print_results(self):
        print("\n" + "=" * 60)
        print("ZRCP Real-Emulator Verification Results")
        print("=" * 60)
        for name, status, msg in self.results:
            line = f"  [{status}]   {name}" if status == "PASS" else f"  [{status}]   {name}: {msg}"
            print(line)
        print("-" * 60)
        print(f"  Total: {self.passed} passed, {self.failed} failed")
        print("=" * 60)


def main():
    parser = argparse.ArgumentParser(description="ZRCP verification against a real emulator")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=10000, help="ZRCP server port")
    args = parser.parse_args()

    verifier = EmulatorVerifier(args.host, args.port)
    success = verifier.run()

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
