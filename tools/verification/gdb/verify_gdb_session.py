#!/usr/bin/env python3
"""A real gdb (17.2, `set architecture z80`) against the unreal-ng GDB server, in batch mode.

Each scenario is one gdb process (`gdb -nx -batch -x <commands>`) on a fresh connection; the Z80 loop of
verify_gdb_protocol.py is installed through the raw protocol before each one. `set debug remote 1` around the
commands that matter shows the packets gdb sends (the Z0 of a conditional breakpoint carries `;X` agent expressions
when the server advertises ConditionalBreakpoints+).

gdb 17.2 aborts in Z80 frame unwinding (trad-frame.h:144 assertion) on `bt`, `stepi`, `step`, `finish` and on any
memory or register write (`set {char}0x9100 = 1`, `set $hl = 0`): the scenarios avoid them.

Usage:
    python3 tools/verification/gdb/verify_gdb_session.py \\
        --launch cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt
    python3 tools/verification/gdb/verify_gdb_session.py --port 2000 --webapi-port 8090   # a running host
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rsp_client import REG_PC, REG_SP, Host, RSPClient, WebAPI, find_project_root  # noqa: E402

PROGRAM = bytes.fromhex("F3AF2100003C23320090473A019078C30580")   # verify_gdb_protocol.py
RPROGRAM = bytes.fromhex("F3210000230100100B78B120FB220090C30482")  # its slow loop at 8200 (1.5 frames a round)
GDB_TIMEOUT = 30.0


class Session:
    def __init__(self, args, api):
        self.args = args
        self.api = api
        self.results = []
        self.work = find_project_root() / "scratch" / "verification-gdb"
        self.work.mkdir(parents=True, exist_ok=True)

    def check(self, name, ok, detail=""):
        self.results.append((name, bool(ok), detail))
        print(f"  {'ok  ' if ok else 'FAIL'} {name}" + ("" if ok else f": {detail}"))
        return ok

    def install(self, pc=0x8000):
        c = RSPClient(self.args.host, self.args.port).connect()
        try:
            c.request("qSupported")
            c.request("QStartNoAckMode")
            c.no_ack = True
            if self.api:
                self.api.pause()
            c.write_mem(0x8000, PROGRAM)
            c.write_mem(0x8200, RPROGRAM)
            c.write_mem(0x9000, b"\x00\x00")
            c.set_reg(REG_PC, pc)
            c.set_reg(REG_SP, 0xFF00)
            for addr in (0x8005, 0x8006, 0x800F, 0x8204):
                c.request(f"z0,{addr:x},1")
            for kind in (2, 3, 4):
                c.request(f"z{kind},9000,2")
        finally:
            c.close()

    def gdb(self, name, body, timeout=GDB_TIMEOUT, pc=0x8000):
        """Runs one gdb session; returns (stdout, stderr = the remote debug log, timed out, crashed)"""
        self.install(pc)
        script = self.work / f"gdb-{name}.gdb"
        script.write_text("set pagination off\nset confirm off\nset width 0\nset architecture z80\n"
                          "set remotetimeout 5\n"
                          f"target remote {self.args.host}:{self.args.port}\n" + body.strip() + "\n")
        process = subprocess.Popen([self.args.gdb, "-nx", "-batch", "-x", str(script)], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        timed_out = False
        try:
            out, err = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            out, err = process.communicate()
            timed_out = True
        (self.work / f"gdb-{name}.out").write_text(out + "\n===== stderr\n" + err)
        crashed = "internal-error" in out + err
        if self.args.verbose:
            print(out)
        return out, err, timed_out, crashed

    @staticmethod
    def values(out):
        """'$1 = 3' lines -> {1: '3'}"""
        return {int(m.group(1)): m.group(2).strip() for m in re.finditer(r"^\$(\d+) = (.*)$", out, re.M)}

    # --- scenarios ---

    def s_basics(self):
        print("\n[connect / registers / memory / conditional breakpoints / detach]")
        out, err, timed_out, crashed = self.gdb("basics", r"""
info registers
x/8xb 0x8000
set debug remote 1
break *0x8005 if $hl == 3
continue
set debug remote 0
p $hl
p/x $pc
p $af >> 8
info breakpoints
delete
break *0x8005 if ($af >> 8) == 7
continue
p $hl
delete
tbreak *0x800f
continue
p/x $pc
monitor status
detach
""")
        self.check("gdb finished (no timeout, no internal error)", not timed_out and not crashed,
                   "timeout" if timed_out else "internal-error")
        self.check("connected (stopped at 0x8000)", "0x00008000 in ?? ()" in out, out[:300])
        regs = dict(re.findall(r"^(\S+)\s+(0x[0-9a-f]+)\s", out, re.M))
        self.check("info registers: the 18 registers of target.xml",
                   all(n in regs for n in ("af", "bc", "de", "hl", "sp", "pc", "ix", "iy", "af'", "bc'", "de'", "hl'",
                                           "ir", "iff", "im", "p7ffd", "p1ffd", "pfe")), repr(sorted(regs)))
        self.check("info registers: pc 0x8000, sp 0xff00", regs.get("pc") == "0x8000" and regs.get("sp") == "0xff00",
                   repr(regs))
        self.check("x/8xb 0x8000 shows the program",
                   "0x8000:\t0xf3\t0xaf\t0x21\t0x00\t0x00\t0x3c\t0x23\t0x32" in out, out[:600])
        z0 = re.findall(r"Sending packet: \$(Z0,8005,[0-9a-f]+[^#]*)#", err)
        self.check("Z0 of 'break *0x8005 if $hl == 3' carries agent expressions (;X)",
                   any(";X" in z for z in z0), repr(z0))
        stops = len(re.findall(r"Packet received: T05swbreak", err))
        self.check("the target evaluated the condition: one stop for HL == 3 (not 4)", stops == 1,
                   f"{stops} breakpoint stops reached gdb")
        v = self.values(out)
        self.check("stopped with $hl == 3, pc 0x8005, A == 3",
                   v.get(1) == "3" and v.get(2) == "0x8005" and v.get(3) == "3", repr(v))
        self.check("info breakpoints shows the condition", "stop only if $hl == 3" in out, "")
        self.check("($af >> 8) == 7 -> $hl == 7", v.get(4) == "7", repr(v))
        self.check("tbreak *0x800f -> pc 0x800f", v.get(5) == "0x800f", repr(v))
        self.check("monitor status (paused)", "Status: paused" in out + err, "")
        self.check("detach", "[Inferior 1 (Remote target) detached]" in out, out[-300:])

    def s_watch(self, command, name):
        print(f"\n[{command}]")
        out, err, timed_out, crashed = self.gdb(name, f"""
{command} *(unsigned char *)0x{0x9000 if command != 'rwatch' else 0x9001:x}
set debug remote 1
continue
set debug remote 0
p/x $pc
delete
detach
""", timeout=15)
        z = re.findall(r"Sending packet: \$(Z[234],[^#]*)#", err)
        self.check(f"{command}: gdb inserts a hardware watchpoint (Z2/Z3/Z4)", bool(z), repr(z))
        replies = re.findall(r"Packet received: (T[^\n]*)", err)
        self.check(f"{command}: gdb reports the watchpoint (no endless resume)",
                   not timed_out and re.search(r"(Hardware (read |access \(read/write\) )?watchpoint 1)", out)
                   and ("Value = " in out or "New value" in out),
                   f"{'timeout after 15 s, ' if timed_out else ''}{len(replies)} stops, first {replies[:2]}")

    def s_reverse(self):
        print("\n[reverse-continue (TTD)]")
        out, err, timed_out, crashed = self.gdb("reverse", r"""
break *0x8204
continue
monitor ttd start
continue
continue
continue
p $hl
set debug remote 1
reverse-continue
set debug remote 0
p $hl
continue
p $hl
monitor ttd stop
delete
detach
""", pc=0x8200)
        self.check("gdb finished", not timed_out and not crashed, "timeout" if timed_out else "internal-error")
        self.check("monitor ttd start", "TTD recording started" in out + err, "")
        self.check("reverse-continue sends bc", "Sending packet: $bc#" in err, "")
        v = self.values(out)
        self.check("reverse-continue -> the previous hit ($hl 3 -> 2), then continue -> 3",
                   v.get(1) == "3" and v.get(2) == "2" and v.get(3) == "3", repr(v))

    def run(self):
        print(f"gdb session verification - {self.args.gdb} -> {self.args.host}:{self.args.port}")
        version = subprocess.run([self.args.gdb, "--version"], capture_output=True, text=True).stdout.splitlines()
        print("  " + (version[0] if version else "gdb ?"))
        self.s_basics()
        self.s_watch("watch", "watch")
        self.s_watch("rwatch", "rwatch")
        self.s_reverse()
        failed = [r for r in self.results if not r[1]]
        print("\n" + "=" * 70)
        print(f"Results: {len(self.results) - len(failed)}/{len(self.results)} checks passed "
              f"(gdb transcripts in {self.work})")
        for name, _, detail in failed:
            print(f"  FAIL {name}: {detail}")
        return not failed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--launch", metavar="UNREAL_QT", help="start this unreal-qt binary offscreen on free ports")
    parser.add_argument("--model", default="PENTAGON")
    parser.add_argument("--port-base", type=int)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2000)
    parser.add_argument("--webapi-port", type=int)
    parser.add_argument("--gdb", default="/opt/homebrew/bin/gdb")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    host = None
    api = None
    if args.launch:
        host = Host(args.launch, args.model, args.port_base)
        print(f"Launching {args.launch} (WebAPI {host.webapi_port}, GDB {host.gdb_port}, log {host.log_path})")
        host.start()
        args.port = host.gdb_port
        api = host.api
    elif args.webapi_port:
        api = WebAPI(args.webapi_port, args.host)
        api.emulator_id = api.first_emulator()
        api.mute()
    try:
        if api:
            # gdb's breakpoints need the debug mode and the breakpoints feature (the server does not turn them on)
            api.changing("PUT", "/api/v1/emulator/{id}/debugmode", {"enabled": True})
            api.changing("PUT", "/api/v1/emulator/{id}/feature/breakpoints", {"enabled": True})
        ok = Session(args, api).run()
    finally:
        if host:
            host.stop()
            print(f"host stopped (log: {host.log_path})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
