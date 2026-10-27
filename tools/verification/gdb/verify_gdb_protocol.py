#!/usr/bin/env python3
"""GDB RSP verification against a live unreal-ng GDB server: every packet the dispatcher handles
(core/automation/gdb/src/gdbserver.cpp), with assertions on the values.

A small Z80 loop is installed at 0x8000 and drives breakpoint, watchpoint and condition hits deterministically:

    8000 F3        DI
    8001 AF        XOR A
    8002 21 00 00  LD HL,0
    8005 3C        INC A           <- LOOP: arrival k has A = HL = k, (9000h) = k
    8006 23        INC HL
    8007 32 00 90  LD (9000h),A    write watch target
    800A 47        LD B,A
    800B 3A 01 90  LD A,(9001h)    read watch target
    800E 78        LD A,B
    800F C3 05 80  JP LOOP

The registers, the program / data memory and the paging latch are saved first and restored at the end.

Usage:
    # Start unreal-qt offscreen on free ports of its own, create a muted PENTAGON, verify, stop it
    python3 tools/verification/gdb/verify_gdb_protocol.py \\
        --launch cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt

    # Against a running host (one emulator instance; the WebAPI port enables the cross-checks and the
    # recovery of a run the server can not interrupt)
    python3 tools/verification/gdb/verify_gdb_protocol.py --port 2000 --webapi-port 8090

    -v prints every packet; -k <text> runs the groups whose name contains the text.
"""

import argparse
import sys
import time
import traceback
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rsp_client import (  # noqa: E402
    REG_AF, REG_BC, REG_DE, REG_HL, REG_SP, REG_PC, REG_IX, REG_IY, REG_AF2, REG_HL2, REG_IR, REG_IFF, REG_IM,
    REG_P7FFD, REG_P1FFD, REG_PFE, REG_SIZES, G_REGISTERS, AgentExpr, Host, RSPClient, RSPError, WebAPI,
    le_hex, parse_le_hex, parse_stop, reg8_equals, reg_equals,
)

PROGRAM = bytes.fromhex("F3AF2100003C23320090473A019078C30580")
START, LOOP, STORE, LOAD, JUMP = 0x8000, 0x8005, 0x8007, 0x800B, 0x800F
WRITE_TARGET, READ_TARGET = 0x9000, 0x9001
SCRATCH = 0x8100
# A slow loop for the reverse group (rounds of ~106k T-states, 1.5 frames): arrival k at 8204 has HL = k
#   8200 DI / LD HL,0 / 8204 INC HL / LD BC,1000h / 8208 DEC BC / LD A,B / OR C / JR NZ,8208 / 820D LD (9000h),HL
#   8210 JP 8204
RPROGRAM = bytes.fromhex("F3210000230100100B78B120FB220090C30482")
RSTART, RLOOP, RJUMP = 0x8200, 0x8204, 0x8210
STOP_TIMEOUT = 5.0     # a hit comes within microseconds of emulated time; this bounds a missing one
QUIET = 1.5            # how long "no stop" is watched
REGISTER_NAMES = ["af", "bc", "de", "hl", "sp", "pc", "ix", "iy", "af'", "bc'", "de'", "hl'", "ir", "iff", "im",
                  "p7ffd", "p1ffd", "pfe"]


class Failure(Exception):
    pass


def expect(condition, message):
    if not condition:
        raise Failure(message)


class Verifier:
    def __init__(self, args, api: WebAPI = None):
        self.args = args
        self.api = api
        self.results = []   # (group, check, ok, detail)
        self.group = ""
        self.c: RSPClient = None
        self.saved = {}
        self.features = {}
        self.ttd_recording = False

    # --- reporting ---

    def check(self, name, ok, detail=""):
        self.results.append((self.group, name, bool(ok), "" if ok else detail))
        mark = "ok  " if ok else "FAIL"
        print(f"  {mark} {name}" + ("" if ok else f": {detail}"))
        return bool(ok)

    def equal(self, name, actual, expected):
        return self.check(name, actual == expected, f"expected {expected!r}, got {actual!r}")

    def client(self, no_ack=True) -> RSPClient:
        c = RSPClient(self.args.host, self.args.port, verbose=self.args.verbose).connect()
        c.request("qSupported:multiprocess+;swbreak+;hwbreak+;qRelocInsn+;vContSupported+;no-resumed+")
        if no_ack:
            if c.request("QStartNoAckMode") == "OK":
                c.no_ack = True
        return c

    # --- machine helpers ---

    def paused(self):
        if not self.api:
            return None
        info = self.api.info()
        return info.get("is_paused") if isinstance(info, dict) else None

    def wait_stop(self, timeout=STOP_TIMEOUT):
        return self.c.read_packet(timeout)

    def run_to_stop(self, packet="c", timeout=STOP_TIMEOUT):
        self.c.send(packet)
        return self.wait_stop(timeout)

    def recover(self, client=None):
        """Stops a run the server does not stop itself (WebAPI pause), then flushes the line. The pending 'c' reply
        comes when the core pauses; a 0x03 the session could not read while it waited is answered after the next
        byte (gdbserver.cpp receivePacket): a '+' wakes it"""
        c = client or self.c
        if self.api:
            self.api.pause()
        replies = c.drain(0.5)
        c.send_raw(b"+")
        replies += c.drain(0.5)
        return replies

    def restart(self):
        """PC at the program start: the counters start again at 0"""
        expect(self.c.set_reg(REG_PC, START) == "OK", "P pc")

    def clear_gdb_breakpoints(self):
        for addr in (LOOP, STORE, LOAD, JUMP, START):
            self.c.request(f"z0,{addr:x},1")
        for kind in (2, 3, 4):
            self.c.request(f"z{kind},{WRITE_TARGET:x},2")

    def breakpoints(self):
        if not self.api:
            return None
        status, body = self.api.call("GET", "/api/v1/emulator/{id}/breakpoints?all=true")
        return body.get("breakpoints", []) if isinstance(body, dict) else []

    def gdb_breakpoints_at(self, addr):
        return [b for b in (self.breakpoints() or []) if b.get("address") == addr and b.get("owner") == "gdb"]

    # --- groups ---

    def g_ack_mode(self):
        """Framing in ack mode: '+' for a good packet, '-' for a bad checksum, QStartNoAckMode"""
        c = RSPClient(self.args.host, self.args.port, verbose=self.args.verbose).connect()
        try:
            r = c.request("qSupported:swbreak+;hwbreak+")   # raises unless the server sent '+'
            self.check("'+' after a valid packet", r is not None and "PacketSize" in r, repr(r))
            c.send_raw(b"$g#00")
            ack = c.read_ack(1.0)
            self.equal("'-' after a bad checksum", ack, "-")
            self.check("no reply to a bad checksum", c.read_packet(0.5) is None, "a reply came")
            self.check("the retransmitted packet is answered", c.request("qC") == "QC01", "")
            self.equal("QStartNoAckMode", c.request("QStartNoAckMode"), "OK")
            c.no_ack = True
            c.acks.clear()
            c.send("qC")
            r = c.read_packet()
            self.equal("no-ack mode: reply without '+'", (r, c.acks), ("QC01", []))
        finally:
            c.close()

    def g_handshake(self):
        c = self.c
        r = c.request("qSupported:multiprocess+;swbreak+;hwbreak+;qRelocInsn+;fork-events+;vfork-events+;"
                      "exec-events+;vContSupported+;QThreadEvents+;no-resumed+")
        feats = {}
        for item in (r or "").split(";"):
            if not item:
                continue
            if "=" in item:
                k, v = item.split("=", 1)
                feats[k] = v
            else:
                feats[item[:-1]] = item[-1]
        self.features = feats
        self.equal("PacketSize", feats.get("PacketSize"), "4000")
        for name in ("QStartNoAckMode", "qXfer:features:read", "qXfer:osdata:read", "qXfer:threads:read",
                     "swbreak", "hwbreak", "vContSupported", "ConditionalBreakpoints"):
            self.equal(f"qSupported {name}+", feats.get(name), "+")
        self.equal("qSupported multiprocess-", feats.get("multiprocess"), "-")
        self.check("qSupported ReverseStep+ / ReverseContinue+ (TTD hooks present)",
                   feats.get("ReverseStep") == "+" and feats.get("ReverseContinue") == "+", repr(r))
        self.equal("vMustReplyEmpty -> empty packet", c.request("vMustReplyEmpty"), "")

    def g_unsupported(self):
        """RSP: a packet the stub does not support gets the empty packet $#00, not silence"""
        for packet in ("qFooBar", "QFooBar", "vFooBar", "bx", "Z5,8000,1", "z5,8000,1", "!", "vCont;t",
                       "qXfer:features:read:other.xml:0,100", "qXfer:foo:read::0,100"):
            r = self.c.request(packet, timeout=1.0)
            self.check(f"{packet} -> empty reply", r == "", "no reply (timeout)" if r is None else repr(r))

    def g_stop_and_threads(self):
        c = self.c
        sig, pairs = parse_stop(c.request("?"))
        self.check("? -> T05 with thread", sig == 5 and "thread" in pairs, f"{sig} {pairs}")
        self.equal("qC", c.request("qC"), "QC01")
        self.equal("qfThreadInfo", c.request("qfThreadInfo"), "m01")
        self.equal("qsThreadInfo", c.request("qsThreadInfo"), "l")
        self.equal("qAttached", c.request("qAttached"), "1")
        r = c.request("qThreadExtraInfo,01")
        self.equal("qThreadExtraInfo (hex text)", bytes.fromhex(r or "").decode(errors="replace"), "Z80 CPU")
        for p in ("Hg0", "Hg1", "Hc-1", "Hc1"):
            self.equal(p, c.request(p), "OK")
        self.equal("T01 (thread alive)", c.request("T01"), "OK")
        r = c.request("qXfer:threads:read::0,fff")
        ok = bool(r) and r[0] == "l"
        if ok:
            root = ET.fromstring(r[1:])
            threads = root.findall("thread")
            ok = len(threads) == 1 and threads[0].get("id") == "01"
        self.check("qXfer:threads:read -> one thread id 01", ok, repr(r))
        self.equal("qSymbol::", c.request("qSymbol::"), "OK")
        for p in ("qOffsets", "qTStatus", "qTfV", "qTsV", "qTfP", "qTsP", "qTBuffer:0,10", "qTV:1",
                  "qL1200000000000000000", "qP0000001f0000000000000001",
                  "qXfer:exec-file:read::0,fff", "qXfer:auxv:read::0,fff", "qXfer:libraries:read::0,fff",
                  "qXfer:memory-map:read::0,fff", "qXfer:spu:read::0,fff", "qXfer:siginfo:read::0,fff"):
            self.equal(f"{p.split(':')[0] if p.startswith('qT') else p} -> empty", c.request(p, timeout=1.0), "")

    def g_target_xml(self):
        c = self.c
        r = c.request("qXfer:features:read:target.xml:0,3fff")
        expect(r and r[0] == "l", f"target.xml reply {r!r:.80}")
        xml = r[1:]
        root = ET.fromstring(xml)
        self.equal("architecture", root.findtext("architecture"), "z80")
        regs = root.findall(".//reg")
        self.equal("register names in order", [x.get("name") for x in regs], REGISTER_NAMES)
        self.equal("register sizes", [int(x.get("bitsize")) for x in regs], [s * 8 for s in REG_SIZES])
        self.equal("sp/pc types", (regs[4].get("type"), regs[5].get("type")), ("data_ptr", "code_ptr"))
        # A read at an offset continues the document (gdb reads in chunks when it is longer than the packet)
        part = c.request("qXfer:features:read:target.xml:10,20")
        self.check("qXfer offset/length honoured (10,20 -> xml[0x10:0x30])",
                   part is not None and part[1:] == xml[0x10:0x30] and part[0] == "m",
                   f"got {len(part or '') - 1} bytes starting {(part or '')[1:20]!r}")
        end = c.request(f"qXfer:features:read:target.xml:{len(xml):x},100")
        self.check("qXfer read past the end -> 'l'", end == "l", f"{(end or '')[:40]!r}")
        r = c.request("qXfer:osdata:read:processes:0,fff")
        ok = bool(r) and r[0] == "l"
        detail = repr(r)
        if ok:
            items = ET.fromstring(r[1:]).findall("item")
            cols = [{col.get("name"): col.text for col in item.findall("column")} for item in items]
            ok = len(cols) >= 1 and cols[0].get("pid") == "1" and cols[0].get("state") in ("stopped", "running")
            if ok and self.api and self.api.emulator_id:
                ok = any(col.get("command") == self.api.emulator_id for col in cols)
            detail = repr(cols)
        self.check("qXfer:osdata:read:processes (pid 1, this instance)", ok, detail)

    def g_registers(self):
        c = self.c
        g = c.request("g")
        self.equal("g length (15 registers, 28 bytes)", len(g or ""), 56)
        values = c.read_registers()
        singles = [c.reg(n) for n in range(G_REGISTERS)]
        self.equal("p n == g value for registers 0..14", singles, values)
        for n in (REG_P7FFD, REG_P1FFD, REG_PFE):
            r = c.request(f"p{n:x}")
            self.check(f"p{n:x} ({REGISTER_NAMES[n]}) one byte", r is not None and len(r) == 2, repr(r))
        self.equal("p12 (no register 18) -> E02", c.request("p12"), "E02")
        self.equal("pZZ -> E01", c.request("pZZ"), "E01")
        # P round trip on every 16-bit register (PC and SP too: the machine is paused)
        original = values
        bad = []
        for n in range(12):
            value = (0x1357 + n * 0x1111) & 0xFFFF
            if c.set_reg(n, value) != "OK" or c.reg(n) != value:
                bad.append(REGISTER_NAMES[n])
        self.check("P/p round trip af..hl'", not bad, f"failed: {bad}")
        if self.api:
            regs = self.api.call("GET", "/api/v1/emulator/{id}/registers")[1]
            self.equal("WebAPI sees the P writes (hl, ix)", (regs["main"]["hl"], regs["index"]["ix"]),
                       ((0x1357 + 3 * 0x1111) & 0xFFFF, (0x1357 + 6 * 0x1111) & 0xFFFF))
        self.equal("P iff=3 -> 3", (c.set_reg(REG_IFF, 3), c.reg(REG_IFF)), ("OK", 3))
        self.equal("P iff=2 -> 2", (c.set_reg(REG_IFF, 2), c.reg(REG_IFF)), ("OK", 2))
        self.equal("P iff=0 -> 0", (c.set_reg(REG_IFF, 0), c.reg(REG_IFF)), ("OK", 0))
        self.equal("P im=2 -> 2", (c.set_reg(REG_IM, 2), c.reg(REG_IM)), ("OK", 2))
        self.equal("P im=1 -> 1", (c.set_reg(REG_IM, 1), c.reg(REG_IM)), ("OK", 1))
        # ir: I high byte, R low byte with its bit 7 kept apart (only bits 0..6 count)
        self.equal("P ir=12ff -> 12ff", (c.set_reg(REG_IR, 0x12FF), c.reg(REG_IR)), ("OK", 0x12FF))
        c.set_reg(REG_PC, LOOP)
        c.request("s")
        self.equal("ir after one M1 from R=ff: bits 0..6 wrap, bit 7 stays (1280)", c.reg(REG_IR), 0x1280)
        c.set_reg(REG_IR, 0x3405)
        c.set_reg(REG_PC, LOOP)
        c.request("s")
        self.equal("ir after one M1 from R=05 (3406)", c.reg(REG_IR), 0x3406)
        self.equal("P with a short value -> E01", c.request("P3=12"), "E01")
        self.equal("P without '=' -> E01", c.request("P3"), "E01")
        self.equal("P to register 0x99 -> E02", c.request("P99=00"), "E02")
        self.equal("PZZ=0000 -> E01", c.request("PZZ=0000"), "E01")
        # G round trip
        target = list(original)
        target[REG_HL], target[REG_DE], target[REG_IX], target[REG_HL2] = 0xBEEF, 0xCAFE, 0x1234, 0x4321
        target[REG_IFF], target[REG_IM] = 0, 1
        target[REG_PC], target[REG_SP] = START, 0xFF00
        packet = "G" + "".join(le_hex(v, REG_SIZES[i]) for i, v in enumerate(target))
        self.equal("G", c.request(packet), "OK")
        self.equal("g after G", c.read_registers(), target)
        self.equal("G too short -> E01", c.request("G0011"), "E01")
        self.equal("G with bad hex -> E01", c.request("G" + "zz" * 28), "E01")
        # Paging pseudo-registers: the border latch and the 7FFD RAM page bits through the port decoder
        border = c.reg(REG_PFE)
        self.equal("P pfe=05 -> 05", (c.set_reg(REG_PFE, 0x05), c.reg(REG_PFE)), ("OK", 0x05))
        c.set_reg(REG_PFE, border)
        p7ffd = c.reg(REG_P7FFD)
        if p7ffd & 0x20:
            self.check("P p7ffd (skipped: paging locked by bit 5)", True)
        else:
            value = (p7ffd & ~7) | 1
            self.equal("P p7ffd RAM page 1 -> p7ffd", (c.set_reg(REG_P7FFD, value), c.reg(REG_P7FFD)), ("OK", value))
            _, text = c.monitor("bankinfo")
            self.check("bankinfo shows page 01 at C000", "C000-FFFF: RAM page 01" in text, text)
            c.set_reg(REG_P7FFD, p7ffd)
            self.equal("p7ffd restored", c.reg(REG_P7FFD), p7ffd)

    def g_memory(self):
        c = self.c
        data = bytes(range(0x10, 0x20))
        self.equal("M 16 bytes", c.write_mem(SCRATCH, data), "OK")
        self.equal("m reads them back", c.read_mem(SCRATCH, 16), data)
        r = c.request(f"m{SCRATCH:x},0")
        self.check("m length 0 -> empty packet", r == "", "no reply (an empty result is sent as no packet)"
                   if r is None else repr(r))
        wrap = c.read_mem(0xFFFF, 2)
        self.equal("m ffff,2 wraps to 0000", wrap, c.read_mem(0xFFFF, 1) + c.read_mem(0x0000, 1))
        self.equal("m without ',' -> E01", c.request("m8000"), "E01")
        self.equal("mZZ,1 -> E01", c.request("mZZ,1"), "E01")
        self.equal("m8000,ZZ -> E01", c.request("m8000,ZZ"), "E01")
        self.equal("M length mismatch -> E01", c.request(f"M{SCRATCH:x},2:00"), "E01")
        self.equal("M without ':' -> E01", c.request(f"M{SCRATCH:x},1"), "E01")
        # Physical view 0x01PPAAAA: the page mapped at 8000 is RAM page 2 on a 128K / Pentagon
        _, text = c.monitor("bankinfo")
        if "8000-BFFF: RAM page 02" in text:
            self.equal("m 0x0102xxxx == m 8xxx (page 2 at 8000)", c.read_mem(0x01020100, 16), data)
            self.equal("M 0x0102xxxx", c.write_mem(0x01020110, b"\xab\xcd"), "OK")
            self.equal("... visible at 8110", c.read_mem(0x8110, 2), b"\xab\xcd")
        else:
            self.check("physical view (skipped: page 2 not at 8000)", True, text)
        r = c.request("m01ff0000,1")
        self.check("m of a page the machine lacks -> error", r is not None and r.startswith("E"), repr(r))
        # X: binary data, escaped ('}' + byte ^ 0x20 for # $ } *), as gdb sends after an "X addr,0:" probe
        self.equal("X probe (length 0)", c.request(f"X{SCRATCH:x},0:"), "OK")
        payload = bytes([0x12, 0x34, 0x23, 0x7D, 0x24, 0x2A, 0x00, 0xFF])
        r = c.write_mem_binary(SCRATCH + 0x20, payload)
        got = c.read_mem(SCRATCH + 0x20, len(payload))
        self.check("X binary write (escaped bytes) round trip", r == "OK" and got == payload,
                   f"X -> {r!r}, memory {got.hex()} (sent {payload.hex()})")
        # writes need a paused machine
        if self.api:
            self.api.resume()
            time.sleep(0.1)
            self.equal("M while running -> E0D", c.write_mem(SCRATCH, b"\x00"), "E0D")
            self.equal("P while running -> E0D", c.set_reg(REG_HL, 0), "E0D")
            self.equal("G while running -> E0D", c.request("G" + "00" * 28), "E0D")
            self.check("m while running still reads", len(c.read_mem(SCRATCH, 4)) == 4)
            self.api.pause()
            time.sleep(0.1)

    def g_debug_mode(self):
        """A host as it starts (debug mode off): gdb's breakpoints have to stop it"""
        if not self.api:
            self.check("debug mode (skipped: no WebAPI)", True)
            return
        status, body = self.api.call("GET", "/api/v1/emulator/{id}/debugmode")
        enabled = isinstance(body, dict) and body.get("enabled")
        if not enabled:
            self.restart()
            self.c.request(f"Z0,{LOOP:x},1")
            r = self.run_to_stop("c", 2.0)
            ok = r is not None and self.c.reg(REG_PC) == LOOP
            self.check("Z0 stops the core with debug mode off (as unreal-qt starts)", ok,
                       "no stop: the server does not turn the debug mode / breakpoints feature on "
                       "(DezogDebugAdapter::ensureDebugEnabled does)")
            if r is None:
                self.recover()
            self.c.request(f"z0,{LOOP:x},1")
        # Execution breakpoints need the debug mode; memory watchpoints the 'breakpoints' feature as well
        self.api.changing("PUT", "/api/v1/emulator/{id}/debugmode", {"enabled": True})
        status, body = self.api.call("GET", "/api/v1/emulator/{id}/feature/breakpoints")
        if isinstance(body, dict) and not body.get("enabled"):
            self.restart()
            self.c.request(f"Z2,{WRITE_TARGET:x},1")
            r = self.run_to_stop("c", 2.0)
            self.check("Z2 stops the core with the 'breakpoints' feature off (as unreal-qt starts)",
                       r is not None and self.c.reg(REG_PC) in (STORE, 0x800A),
                       "no stop: the server does not turn the 'breakpoints' feature on")
            if r is None:
                self.recover()
            self.c.request(f"z2,{WRITE_TARGET:x},1")
        self.api.changing("PUT", "/api/v1/emulator/{id}/feature/breakpoints", {"enabled": True})
        self.check("debug mode and the breakpoints feature on for the following groups", True)

    def g_breakpoints(self):
        c = self.c
        self.restart()
        self.equal("Z0 at LOOP", c.request(f"Z0,{LOOP:x},1"), "OK")
        if self.api:
            bps = self.gdb_breakpoints_at(LOOP)
            self.check("core lists the gdb breakpoint (owner gdb, execute)",
                       len(bps) == 1 and bps[0].get("execute") is True, repr(self.breakpoints()))
        for k in range(3):
            r = self.run_to_stop("c")
            sig, pairs = parse_stop(r)
            self.check(f"c -> T05 swbreak (arrival {k})", sig == 5 and "swbreak" in pairs, repr(r))
            if r is None:
                self.recover()
                return
            self.equal(f"  stopped at LOOP with HL = A = {k}",
                       (c.reg(REG_PC), c.reg(REG_HL), c.reg(REG_AF) >> 8), (LOOP, k, k))
        # single steps from the breakpoint
        r = c.request("s")
        sig, pairs = parse_stop(r)
        self.check("s -> T05 (no breakpoint key)", sig == 5 and "swbreak" not in pairs, repr(r))
        self.equal("  s: INC A -> pc 8006, A = 3", (c.reg(REG_PC), c.reg(REG_AF) >> 8), (0x8006, 3))
        c.request("S05")
        self.equal("S05: INC HL -> pc 8007", (c.reg(REG_PC), c.reg(REG_HL)), (STORE, 3))
        c.request("vCont;s:1")
        self.equal("vCont;s:1: LD (9000h),A -> pc 800A and (9000h) = 3",
                   (c.reg(REG_PC), c.read_mem(WRITE_TARGET, 1)[0]), (0x800A, 3))
        r = c.request("vCont?")
        actions = set((r or "").split(";")[1:])
        self.check("vCont? -> c C s S", (r or "").startswith("vCont") and {"c", "C", "s", "S"} <= actions, repr(r))
        r = self.run_to_stop("vCont;c")
        self.check("vCont;c -> breakpoint, HL = 3", parse_stop(r)[0] == 5 and c.reg(REG_HL) == 3, repr(r))
        r = self.run_to_stop("vCont;c:01")
        self.check("vCont;c:01 -> breakpoint, HL = 4", parse_stop(r)[0] == 5 and c.reg(REG_HL) == 4, repr(r))
        r = self.run_to_stop("C05")
        self.check("C05 -> breakpoint, HL = 5", parse_stop(r)[0] == 5 and c.reg(REG_HL) == 5, repr(r))
        r = self.run_to_stop("vCont;S05:01", 1.5)
        self.check("vCont;S05 (step with signal, advertised by vCont?) -> stop at 8006",
                   r is not None and parse_stop(r)[0] == 5 and c.reg(REG_PC) == 0x8006,
                   "no reply" if r is None else repr(r))
        r = self.run_to_stop("vCont;C05", 1.5)
        self.check("vCont;C05 (continue with signal, advertised) -> breakpoint",
                   r is not None and parse_stop(r)[0] == 5 and c.reg(REG_PC) == LOOP,
                   "no reply" if r is None else repr(r))
        if r is None:
            pass   # nothing was started
        self.equal("z0 at LOOP", c.request(f"z0,{LOOP:x},1"), "OK")
        if self.api:
            self.equal("core list has no gdb breakpoint at LOOP", self.gdb_breakpoints_at(LOOP), [])
        # Z1: a hardware breakpoint reports hwbreak (advertised hwbreak+)
        self.restart()
        self.equal("Z1 at STORE", c.request(f"Z1,{STORE:x},1"), "OK")
        r = self.run_to_stop("c")
        sig, pairs = parse_stop(r)
        self.check("Z1 hit -> T05 with hwbreak", sig == 5 and "hwbreak" in pairs, repr(r))
        self.equal("  at STORE", c.reg(REG_PC), STORE)
        if r is None:
            self.recover()
        self.equal("z1", c.request(f"z1,{STORE:x},1"), "OK")
        # malformed
        self.equal("Z0 without address -> E01", c.request("Z0"), "E01")
        self.equal("Z0,zz,1 -> E01", c.request("Z0,zz,1"), "E01")
        self.equal("z0 without address -> E01", c.request("z0"), "E01")

    def watch_case(self, kind, addr, key, length=1):
        c = self.c
        self.restart()
        self.equal(f"Z{kind},{addr:x},{length}", c.request(f"Z{kind},{addr:x},{length}"), "OK")
        r = self.run_to_stop("c", 2.0)
        sig, pairs = parse_stop(r)
        stopped = r is not None
        self.check(f"Z{kind} {addr:04x} hit -> T05 {key}:{addr:x}",
                   sig == 5 and key in pairs and int(pairs[key] or "0", 16) == addr,
                   "no stop within 2 s" if not stopped else repr(r))
        if stopped:
            pc = c.reg(REG_PC)
            near = (STORE, 0x800A) if addr == WRITE_TARGET else (LOAD, 0x800E)
            self.check(f"  stopped at the accessing instruction or after it ({near[0]:04x}/{near[1]:04x})",
                       pc in near, f"pc {pc:04x}")
        else:
            self.recover()
        self.equal(f"z{kind},{addr:x},{length}", c.request(f"z{kind},{addr:x},{length}"), "OK")

    def g_watchpoints(self):
        self.watch_case(2, WRITE_TARGET, "watch")
        self.watch_case(3, READ_TARGET, "rwatch")
        self.watch_case(4, READ_TARGET, "awatch")
        self.watch_case(4, WRITE_TARGET, "awatch")
        self.watch_case(2, WRITE_TARGET, "watch", length=2)

    def g_foreign_breakpoints(self):
        """gdb's z packets and watchpoints leave the breakpoints of other clients alone"""
        if not self.api:
            self.check("foreign breakpoints (skipped: no WebAPI)", True)
            return
        c = self.c

        def user_bp(addr, kind="execution"):
            status, body = self.api.add_breakpoint({"type": kind, "address": addr, "note": "verify_gdb_protocol"})
            self.api.release()
            return body.get("id") if isinstance(body, dict) else None

        def user_ids():
            return {b.get("id") for b in (self.api.breakpoints() or [])}

        bp = user_bp(0x8180)
        self.check("WebAPI user breakpoint added", bp is not None, "")
        c.request("z0,8180,1")
        self.check("z0 at an address gdb never set keeps the user's breakpoint", bp in user_ids(),
                   "removed (gdbserver.cpp handleRemoveBreakpoint: RemoveBreakpointByAddress)")
        bp2 = user_bp(0x8181)
        c.request("Z2,8181,1")
        c.request("z2,8181,1")
        self.check("Z2/z2 at a user's breakpoint address keep the user's breakpoint", bp2 in user_ids(),
                   "removed (RemoveBreakpointByAddress)")
        # a user's execute breakpoint where gdb sets a write watch: the watch must still fire
        bp3 = user_bp(WRITE_TARGET)
        self.restart()
        c.request(f"Z2,{WRITE_TARGET:x},1")
        r = self.run_to_stop("c", 2.0)
        self.check("Z2 at an address holding a user's execute breakpoint still stops on the write",
                   r is not None and c.reg(REG_PC) in (STORE, 0x800A),
                   "no stop: AddMemWriteBreakpoint returns the existing breakpoint's id (same key)")
        if r is None:
            self.recover()
        c.request(f"z2,{WRITE_TARGET:x},1")
        for b in (bp, bp2, bp3):
            if b is not None:
                self.api.remove_breakpoint(b)
        self.api.call("DELETE", "/api/v1/emulator/{id}/control/session")

    def g_interrupt(self):
        c = self.c
        # paused: a lone 0x03 (what gdb sends on Ctrl-C) is answered T02
        c.interrupt()
        r = c.read_packet(1.0)
        self.check("0x03 alone -> T02", r is not None and parse_stop(r)[0] == 2,
                   "no reply until another byte arrives (receivePacket checks the flag before feeding the byte)")
        if r is None:
            c.send_raw(b"+")
            c.drain(0.5)
        # 0x03 directly followed by a packet: both answered
        c.send_raw(b"\x03" + c.frame(b"qC"))
        replies = c.drain(1.0)
        self.check("0x03 + qC in one write -> T02 and QC01", {"T02thread:01;", "QC01"} <= set(replies),
                   f"replies {replies} (the byte after 0x03 is dropped, so the packet is lost)")
        if "QC01" not in replies:
            c.drain(0.3)
        if not self.api:
            self.check("interrupt while running (skipped: no WebAPI to recover)", True)
            return
        # running: c without breakpoints, then 0x03
        self.clear_gdb_breakpoints()
        self.restart()
        c.send("c")
        time.sleep(0.3)
        self.equal("c without breakpoints: still running", self.paused(), False)
        c.interrupt()
        r = c.read_packet(2.0)
        self.check("0x03 while running -> T02 and the core pauses", r is not None and parse_stop(r)[0] == 2,
                   "no reply: the session thread waits in handleContinue and does not read the socket")
        if r is None:
            self.api.pause()
            late = c.drain(0.5)
            self.check("  after an external pause the pending c is answered (T05 or T02)",
                       len(late) >= 1 and parse_stop(late[0])[0] in (2, 5), repr(late))
            self.check("  external pause is not reported as a breakpoint (swbreak)",
                       late and "swbreak" not in late[0], repr(late))
            c.send_raw(b"+")
            c.drain(0.5)

    def g_conditions(self):
        c = self.c
        if self.features.get("ConditionalBreakpoints") != "+":
            self.check("conditions (skipped: ConditionalBreakpoints not advertised)", False, "")
            return

        def z0(cond_list):
            return c.request(f"Z0,{LOOP:x},1;{cond_list}")

        def hl_stop(expected, name):
            r = self.run_to_stop("c", 3.0)
            ok = r is not None and parse_stop(r)[0] == 5 and c.reg(REG_PC) == LOOP and c.reg(REG_HL) == expected
            self.check(name, ok, "no stop" if r is None else f"{r} pc={c.reg(REG_PC):04x} hl={c.reg(REG_HL)}")
            if r is None:
                self.recover()
            return ok

        def condition_text():
            bps = self.gdb_breakpoints_at(LOOP)
            return [b.get("condition") for b in bps]

        self.clear_gdb_breakpoints()
        self.restart()
        self.equal("Z0;X (HL == 3)", z0(AgentExpr.condition_list(reg_equals(REG_HL, 3))), "OK")
        if self.api:
            texts = condition_text()
            self.check("core breakpoint carries the condition (HL, 3)",
                       len(texts) == 1 and texts[0] and "HL" in texts[0] and "3" in texts[0], repr(texts))
        hl_stop(3, "stops only when HL == 3 (A = 3 too)")
        self.equal("  A at the stop", c.reg(REG_AF) >> 8, 3)
        self.equal("Z0 again at the same address (HL == 7) replaces it",
                   z0(AgentExpr.condition_list(reg_equals(REG_HL, 7))), "OK")
        if self.api:
            texts = condition_text()
            self.check("  one gdb breakpoint, its condition with 7",
                       len(texts) == 1 and "7" in (texts[0] or ""), repr(texts))
        hl_stop(7, "  next stop at HL == 7 (not 4)")
        z0(AgentExpr.condition_list(reg8_equals(REG_AF, True, 9)))
        hl_stop(9, "$a == 9 as gdb compiles it (af >> 8, zero_ext 8)")
        mem = AgentExpr().const(WRITE_TARGET).op("ref8").const(12).op("equal").op("end").build()
        z0(AgentExpr.condition_list(mem))
        hl_stop(12, "memory condition: byte at 9000 == 12 (ref8)")
        z0(AgentExpr.condition_list(reg_equals(REG_HL, 14), reg_equals(REG_HL, 16)))
        hl_stop(14, "two X expressions (OR): HL == 14 ...")
        hl_stop(16, "  ... then HL == 16")
        # gdb's code for (hl == 18 || hl == 20): if_goto paths
        e = (AgentExpr().reg(REG_HL).const(18).op("equal").if_goto("true")
             .reg(REG_HL).const(20).op("equal").if_goto("true")
             .const(0).goto("end").label("true").const(1).label("end").op("end").build())
        z0(AgentExpr.condition_list(e))
        hl_stop(18, "if_goto paths (HL == 18 || HL == 20): 18 ...")
        hl_stop(20, "  ... then 20")
        # signed compare and arithmetic: (hl - 21) < 1 && hl > 21  ->  hl == 22 ... built as less_signed
        e = AgentExpr().const(22).reg(REG_HL).op("less_signed").op("log_not") \
            .reg(REG_HL).const(22).op("less_signed").op("log_not").op("bit_and").op("end").build()
        z0(AgentExpr.condition_list(e))
        hl_stop(22, "less_signed / log_not / bit_and: HL == 22")
        # false forever: never stops within a bounded run
        z0(AgentExpr.condition_list(AgentExpr().reg(REG_HL).const(0x10000).op("equal").op("end").build()))
        r = self.run_to_stop("c", QUIET)
        self.check(f"HL == 0x10000 (never true): no stop within {QUIET} s", r is None, repr(r))
        if r is None and self.api:
            self.check("  the core is running meanwhile", self.paused() is False, "")
            self.recover()
        # untranslatable: the breakpoint is set without a condition, the next arrival stops
        for name, code in (("backward jump (loop)", AgentExpr().const(0).goto(0).op("end").build()),
                           ("trace_quick", AgentExpr().reg(REG_HL).raw(b"\x0d\x02").const(1).op("end").build()),
                           ("register p1ffd (no core operand)", reg_equals(REG_P1FFD, 1)),
                           ("division by a register", AgentExpr().const(10).reg(REG_HL).op("div_signed")
                            .op("end").build())):
            self.restart()
            self.equal(f"Z0 with {name} -> OK", z0(AgentExpr.condition_list(code)), "OK")
            if self.api:
                self.equal(f"  core breakpoint without a condition", condition_text(), [None])
            hl_stop(0, f"  {name}: stops unconditionally (gdb decides)")
        self.restart()
        self.equal("Z0 with a malformed list (;Xzz) -> OK", c.request(f"Z0,{LOOP:x},1;Xzz,00"), "OK")
        hl_stop(0, "  malformed list: unconditional")
        # a plain Z0 replaces a conditional one
        z0(AgentExpr.condition_list(AgentExpr().reg(REG_HL).const(0x10000).op("equal").op("end").build()))
        self.equal("plain Z0 over a conditional one", c.request(f"Z0,{LOOP:x},1"), "OK")
        self.restart()
        hl_stop(0, "  stops at the first arrival")
        # z0 removes a conditional breakpoint
        z0(AgentExpr.condition_list(reg_equals(REG_HL, 3)))
        self.equal("z0 removes the conditional breakpoint", c.request(f"z0,{LOOP:x},1"), "OK")
        if self.api:
            self.equal("  core list empty at LOOP", self.gdb_breakpoints_at(LOOP), [])
            self.restart()
            r = self.run_to_stop("c", QUIET)
            self.check("  no stop after z0", r is None, repr(r))
            if r is None:
                self.recover()

    def g_monitor(self):
        c = self.c
        r, text = c.monitor("help")
        self.check("monitor help lists the commands",
                   all(w in text for w in ("monitor model", "monitor status", "monitor reset", "monitor instances",
                                           "monitor frame", "monitor bankinfo", "monitor regions", "monitor mem",
                                           "monitor load", "ttd status", "bport")), text[:200])
        _, text = c.monitor("model")
        self.check("monitor model", text.startswith("Model:"), text)
        _, text = c.monitor("status")
        self.check("monitor status: paused + instance", "Status: paused" in text and "Instance:" in text and
                   (not self.api or self.api.emulator_id in text), text)
        _, text = c.monitor("instances")
        self.check("monitor instances", text.startswith("Instances: ") and
                   (not self.api or self.api.emulator_id in text), text)
        c.set_reg(REG_PC, LOOP)
        _, text = c.monitor("frame")
        self.check("monitor frame: T-state and PC 0x8005", "T-state:" in text and "PC: 0x8005" in text, text)
        _, text = c.monitor("bankinfo")
        self.check("monitor bankinfo: four banks", all(b in text for b in ("0000-3FFF", "4000-7FFF", "8000-BFFF",
                                                                          "C000-FFFF")), text)
        _, text = c.monitor("regions")
        self.check("monitor regions: non-empty", len(text.strip()) > 0 and "Unknown" not in text, text[:200])
        _, text = c.monitor("mem cpu:8000 4")
        self.check("monitor mem cpu:8000 4 shows the program bytes", "f3" in text.lower() and "af" in text.lower(),
                   text[:200])
        _, text = c.monitor("load")
        self.check("monitor load (no path) -> error", text.startswith("Error: path required"), text)
        _, text = c.monitor("load /nonexistent/file.xyz")
        self.check("monitor load .xyz -> unsupported type", "unsupported file type" in text, text)
        _, text = c.monitor("load /nonexistent/file.sna")
        self.check("monitor load of a missing snapshot -> error", text.startswith("Error"), text)
        _, text = c.monitor("frobnicate")
        self.check("unknown monitor command", text.startswith("Unknown monitor command: frobnicate"), text)
        _, text = c.monitor("bport")
        self.check("monitor bport (no args) -> unknown/usage", "Unknown monitor command" in text or "Usage" in text,
                   text)
        _, text = c.monitor("bport sideways 1")
        self.check("monitor bport bad form -> usage", text.startswith("Usage: bport"), text)
        _, text = c.monitor("bport out zz")
        self.check("monitor bport out zz -> invalid port", "invalid port" in text, text)
        # ids below 10 read the same in decimal and hex: hold ten others first
        fillers = []
        for port in range(0x10, 0x1A):
            _, t = c.monitor(f"bport in {port:x}")
            try:
                fillers.append(int(t.split("ID ")[1].split()[0]))
            except (IndexError, ValueError):
                pass
        _, text = c.monitor("bport out fe")
        self.check("monitor bport out fe -> set", text.startswith("Port breakpoint set: ID ") and "0xfe (OUT)" in text,
                   text)
        bp_id = None
        try:
            bp_id = int(text.split("ID ")[1].split()[0])
        except (IndexError, ValueError):
            pass
        if self.api and bp_id is not None:
            found = [b for b in self.breakpoints() if b.get("id") == bp_id]
            self.check("  core lists the port breakpoint (out, 0xFE)",
                       len(found) == 1 and found[0].get("type") == "port" and found[0].get("out") is True,
                       repr(found))
        if bp_id is not None:
            _, text = c.monitor(f"bport clear {bp_id}")   # the id exactly as 'set' printed it
            gone = not self.api or not any(b.get("id") == bp_id for b in self.breakpoints())
            self.check(f"monitor bport clear {bp_id} (the printed id) removes it",
                       text.startswith("Port breakpoint removed") and gone,
                       f"{text.strip()!r}: 'set' prints the id in decimal, 'clear' parses hex")
            if self.api and not gone:
                self.api.remove_breakpoint(bp_id)
        if self.api:
            for f in fillers:
                self.api.remove_breakpoint(f)
            self.api.release()
        _, text = c.monitor("bport clear zz")
        self.check("monitor bport clear zz -> invalid id", "invalid breakpoint ID" in text, text)
        _, text = c.monitor("ttd findlast q 8000")
        self.check("monitor ttd findlast q -> access type error",
                   "access type" in text or "TTD not available" in text, text)
        _, text = c.monitor("ttd findlast")
        self.check("monitor ttd findlast (no args) -> usage/error", text.startswith(("Usage", "Unknown TTD", "Error",
                                                                                    "TTD not")), text)
        _, text = c.monitor("ttd seek abc")
        self.check("monitor ttd seek abc -> error", "Error" in text or "TTD not available" in text, text)
        _, text = c.monitor("ttd bogus")
        self.check("monitor ttd bogus -> unknown", "Unknown TTD command" in text or "TTD not available" in text, text)
        if self.api:
            self.api.resume()
            time.sleep(0.1)
            _, text = c.monitor("reset")
            self.check("monitor reset while running -> refused", "must be paused" in text, text)
            _, text = c.monitor("load /nonexistent/file.sna")
            self.check("monitor load while running -> refused", "must be paused" in text, text)
            _, text = c.monitor("ttd start")
            self.check("monitor ttd start while running -> refused", "must be paused" in text, text)
            _, text = c.monitor("status")
            self.check("monitor status: running", "Status: running" in text, text)
            self.api.pause()
            time.sleep(0.1)

    def g_reverse(self):
        """bs / bc over a loop whose rounds span frames (RPROGRAM: 1.5 frames per round), then one within a frame"""
        c = self.c
        if self.api:
            # the groups before may leave the machine running or a WebAPI session claimed
            self.api.pause()
            c.drain(quiet=True)
            self.api.changing("PUT", "/api/v1/emulator/{id}/debugmode", {"enabled": True})
            self.api.changing("PUT", "/api/v1/emulator/{id}/feature/breakpoints", {"enabled": True})
        _, text = c.monitor("ttd status")
        self.check("monitor ttd status", "TTD state:" in text, text)
        if "idle" in text.lower():
            self.equal("bs without a session -> E01", c.request("bs"), "E01")
            self.equal("bc without a session -> E01", c.request("bc"), "E01")
        self.clear_gdb_breakpoints()
        expect(c.write_mem(RSTART, RPROGRAM) == "OK", "M slow program")
        c.set_reg(REG_PC, RSTART)
        c.request(f"Z0,{RLOOP:x},1")
        self.run_to_stop("c")   # arrival 0: HL = 0, history starts here
        _, text = c.monitor("ttd start", timeout=10)
        started = text.startswith("TTD recording started")
        self.check("monitor ttd start (paused)", started, text)
        if not started:
            c.request(f"z0,{RLOOP:x},1")
            return
        self.ttd_recording = True
        for _ in range(3):
            self.run_to_stop("c")
        self.equal("recorded three hits forward over frames (HL = 3)", c.reg(REG_HL), 3)
        _, text = c.monitor("ttd status")
        self.check("ttd status: recording", "TTD state: recording" in text, text)
        r = c.request("bs", timeout=10)
        sig, pairs = parse_stop(r)
        self.check("bs -> T05 (step)", sig == 5 and "replaylog" not in pairs and "swbreak" not in pairs, repr(r))
        self.equal("  bs: back at the JP (8210), HL = 3", (c.reg(REG_PC), c.reg(REG_HL)), (RJUMP, 3))
        r = c.request("bc", timeout=10)
        sig, pairs = parse_stop(r)
        self.check("bc -> T05 swbreak", sig == 5 and "swbreak" in pairs, repr(r))
        self.equal("  bc: the previous hit (8204, HL = 2)", (c.reg(REG_PC), c.reg(REG_HL)), (RLOOP, 2))
        self.equal("P while in the past -> E0D", c.set_reg(REG_HL, 0), "E0D")
        self.equal("M while in the past -> E0D", c.write_mem(SCRATCH, b"\x00"), "E0D")
        self.equal("G while in the past -> E0D", c.request("G" + "00" * 28), "E0D")
        _, text = c.monitor(f"ttd findlast w {WRITE_TARGET:x}")
        self.check("monitor ttd findlast w 9000 -> found, value 2, pc 820d",
                   "Found at frame" in text and "value=0x2" in text and "pc=0x820d" in text, text)
        _, text = c.monitor("ttd findlast w 9876")
        self.check("monitor ttd findlast of a never written byte", "Not found" in text or "Blocked" in text, text)
        r = c.request("bc", timeout=10)
        self.equal("bc again -> previous hit HL = 1", (parse_stop(r)[0], c.reg(REG_PC), c.reg(REG_HL)), (5, RLOOP, 1))
        replies = [c.request("bc", timeout=10) for _ in range(2)]
        sig, pairs = parse_stop(replies[-1])
        self.check("bc past the first recorded hit -> T05 replaylog:begin",
                   sig == 5 and pairs.get("replaylog") == "begin", repr(replies))
        r = c.request("bs", timeout=10)
        self.check("bs at the start of history -> T05", parse_stop(r)[0] == 5, repr(r))
        # forward from the past: replayed hits, then past the end of history into live execution
        hls = []
        for _ in range(5):
            r = self.run_to_stop("c", 10)
            if r is None:
                self.recover()
                break
            hls.append(c.reg(REG_HL))
        self.check("c from the past replays the hits forward and goes live past the end (5 distinct rising HL)",
                   len(hls) == 5 and hls == sorted(hls) and len(set(hls)) == 5,
                   f"HL at the stops {hls}: a c that does not move still answers T05swbreak")
        r = c.write_mem(SCRATCH, b"\x00")
        self.check("the machine is live again after c past the end of history (M -> OK)", r == "OK",
                   f"M -> {r!r}: still in the recorded past (no gdb packet or monitor command returns to live)")
        if r != "OK" and self.api:
            status, body = self.api.call("POST", "/api/v1/emulator/{id}/ttd/resume", {})
            self.api.release()
            self.check("  WebAPI ttd/resume returns it to live", c.write_mem(SCRATCH, b"\x00") == "OK",
                       f"{status} {body}")
        _, text = c.monitor("ttd stop")
        self.check("monitor ttd stop", text.startswith("TTD recording stopped"), text)
        self.ttd_recording = False
        c.request(f"z0,{RLOOP:x},1")
        r = c.write_mem(SCRATCH, b"\x00")
        self.check("writes work after ttd stop (M -> OK)", r == "OK", f"M -> {r!r}")
        if r != "OK" and self.api:
            # the history discarded: the machine stands where it is, live
            status, body = self.api.call("POST", "/api/v1/emulator/{id}/ttd/invalidate", {"reason": "verify_gdb"})
            self.api.release()
            self.check("  recovery: WebAPI ttd/invalidate returns it to live", c.write_mem(SCRATCH, b"\x00") == "OK",
                       f"{status} {body}")
        # Hits inside one frame (the fast loop: 54 T-states a round)
        self.install()
        c.request(f"Z0,{LOOP:x},1")
        self.run_to_stop("c")
        _, text = c.monitor("ttd start", timeout=10)
        if text.startswith("TTD recording started"):
            self.ttd_recording = True
            self.run_to_stop("c")
            self.run_to_stop("c")
            r = c.request("bc", timeout=10)
            self.check("bc between hits inside one frame -> the previous hit (HL 2 -> 1)",
                       parse_stop(r)[1].get("swbreak") == "" and c.reg(REG_HL) == 1,
                       f"{r!r} hl={c.reg(REG_HL)} (TTD reverse-continue finds no hit inside the frame: "
                       f"'matched': false, a core limit, not the server)")
            c.monitor("ttd stop")
            self.ttd_recording = False
            if self.api and c.write_mem(SCRATCH, b"\x00") != "OK":
                self.api.call("POST", "/api/v1/emulator/{id}/ttd/invalidate", {"reason": "verify_gdb"})
                self.api.release()
        c.request(f"z0,{LOOP:x},1")

    def g_run_control(self):
        """c refused by another client's run control is not a breakpoint stop"""
        if not self.api:
            self.check("run control (skipped: no WebAPI)", True)
            return
        c = self.c
        self.clear_gdb_breakpoints()
        self.restart()
        status, body = self.api.call("POST", "/api/v1/emulator/{id}/control/session", {"name": "verify_gdb"})
        session = body.get("session") if isinstance(body, dict) else None   # {"session": "<uuid>", "header": ...}
        try:
            if status >= 300 or not isinstance(session, str):
                self.check("WebAPI control session taken", False, f"{status} {body}")
                return
            r = self.run_to_stop("c", 2.0)
            sig, pairs = parse_stop(r) if r else (None, {})
            pc = c.reg(REG_PC) if r is not None else None
            self.check("c while another client holds run control: not a fake breakpoint stop",
                       r is None or r.startswith("E") or "swbreak" not in pairs,
                       f"reply {r!r}, pc {pc:04x} (nothing ran)" if pc is not None else repr(r))
            if r is None:
                self.recover()
        finally:
            status, body = self.api.call("DELETE", "/api/v1/emulator/{id}/control/session",
                                         headers={"X-Unreal-Session": session} if isinstance(session, str) else None)
            self.api.release()
        self.check("WebAPI control session released", status < 300 and isinstance(body, dict) and
                   body.get("released"), f"{status} {body}")

    def g_attach_detach_kill(self):
        # vAttach on a second connection
        c2 = self.client()
        try:
            self.equal("vAttach;zz -> E01", c2.request("vAttach;zz"), "E01")
            self.equal("vAttach;63 (no such instance) -> E31", c2.request("vAttach;63"), "E31")
            r = c2.request("vAttach;1")
            self.equal("vAttach;1 -> T05thread:01;", r, "T05thread:01;")
            self.check("  instance paused after vAttach", self.paused() in (True, None), "")
            _, text = c2.monitor("status")
            self.check("  monitor status: run control claimed by gdb", "Run-control: claimed" in text, text)
            self.equal("D -> OK", c2.request("D"), "OK")
            closed = False
            try:
                closed = c2.read_packet(1.0) is None and c2.sock.recv(1) == b""
            except (OSError, RSPError):
                closed = True
            self.check("  server closes the connection after D", closed, "")
        finally:
            c2.close()
        if self.api:
            _, text = self.c.monitor("status")
            self.check("run control released after D", "Run-control: unclaimed" in text, text)
        c3 = self.client()
        try:
            r = c3.request("k", timeout=1.0)
            self.check("k -> OK or no reply (kill = detach)", r in ("OK", None), repr(r))
        finally:
            c3.close()
        if self.api:
            info = self.api.info()
            self.check("k never terminates the emulator", isinstance(info, dict) and info.get("id") ==
                       self.api.emulator_id, repr(info)[:200])

    # --- state ---

    def save_state(self):
        c = self.c
        if self.api:
            self.api.pause()
            time.sleep(0.1)
        else:
            c.interrupt()
            c.send_raw(b"+")
            c.drain(0.5)
        self.saved = {
            "regs": c.read_registers(),
            "p7ffd": c.reg(REG_P7FFD),
            "pfe": c.reg(REG_PFE),
            "program": c.read_mem(START, 0x300),
            "data": c.read_mem(WRITE_TARGET, 0x10),
        }

    def install(self):
        c = self.c
        expect(c.write_mem(START, PROGRAM) == "OK", "M program")
        expect(c.write_mem(WRITE_TARGET, b"\x00\x00") == "OK", "M data")
        expect(c.set_reg(REG_SP, 0xFF00) == "OK", "P sp")
        self.restart()
        expect(c.read_mem(START, len(PROGRAM)) == PROGRAM, "program readback")

    def restore_state(self):
        c = self.c
        if self.ttd_recording:
            c.monitor("ttd stop")
        self.clear_gdb_breakpoints()
        if self.api:
            self.api.pause()
        c.drain(0.2)
        s = self.saved
        ok = c.write_mem(START, s["program"]) == "OK" and c.write_mem(WRITE_TARGET, s["data"]) == "OK"
        ok &= c.request("G" + "".join(le_hex(v, REG_SIZES[i]) for i, v in enumerate(s["regs"]))) == "OK"
        ok &= c.set_reg(REG_P7FFD, s["p7ffd"]) == "OK" and c.set_reg(REG_PFE, s["pfe"]) == "OK"
        ok &= c.read_registers() == s["regs"] and c.read_mem(START, 0x300) == s["program"]
        return ok

    # --- run ---

    GROUPS = [
        ("ack mode / framing", "g_ack_mode", False),
        ("handshake", "g_handshake", True),
        ("unsupported packets", "g_unsupported", True),
        ("stop reason / threads / queries", "g_stop_and_threads", True),
        ("qXfer target.xml / osdata", "g_target_xml", True),
        ("registers g/G/p/P", "g_registers", True),
        ("memory m/M/X", "g_memory", True),
        ("debug mode", "g_debug_mode", True),
        ("breakpoints Z0/Z1, c/s/vCont", "g_breakpoints", True),
        ("watchpoints Z2/Z3/Z4", "g_watchpoints", True),
        ("other clients' breakpoints", "g_foreign_breakpoints", True),
        ("interrupt 0x03", "g_interrupt", True),
        ("conditional breakpoints (agent expressions)", "g_conditions", True),
        ("monitor (qRcmd)", "g_monitor", True),
        ("run control", "g_run_control", True),
        ("attach / detach / kill", "g_attach_detach_kill", True),
        ("reverse bs/bc (TTD)", "g_reverse", True),
    ]

    def run(self) -> bool:
        print(f"GDB RSP verification - {self.args.host}:{self.args.port}")
        self.c = self.client()
        self.group = "setup"
        self.save_state()
        self.install()
        if self.api and self.args.only and self.args.only not in "debug mode":
            # the groups need the breakpoints working (the debug mode group checks and does this otherwise)
            self.api.changing("PUT", "/api/v1/emulator/{id}/debugmode", {"enabled": True})
            self.api.changing("PUT", "/api/v1/emulator/{id}/feature/breakpoints", {"enabled": True})
        for title, method, needs_program in self.GROUPS:
            if self.args.only and self.args.only not in title:
                continue
            self.group = title
            print(f"\n[{title}]")
            try:
                if needs_program:
                    self.c.drain(0.05)
                    self.install()
                getattr(self, method)()
            except Exception as error:  # noqa: BLE001
                self.check(f"{title}: no exception", False, f"{type(error).__name__}: {error}")
                if self.args.verbose:
                    traceback.print_exc()
                # the session may be stuck in a run: stop it and reconnect
                try:
                    self.recover()
                except Exception:  # noqa: BLE001
                    pass
                self.c.close()
                self.c = self.client()
        self.group = "teardown"
        print("\n[teardown]")
        self.check("state restored (registers, memory, paging)", self.restore_state(), "")
        self.c.close()
        failed = [r for r in self.results if not r[2]]
        print("\n" + "=" * 70)
        print(f"Results: {len(self.results) - len(failed)}/{len(self.results)} checks passed")
        for group, name, _, detail in failed:
            print(f"  FAIL [{group}] {name}: {detail}")
        return not failed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--launch", metavar="UNREAL_QT", help="start this unreal-qt binary offscreen on free ports")
    parser.add_argument("--model", default="PENTAGON", help="model of the launched instance")
    parser.add_argument("--port-base", type=int, help="WebAPI port of the launched host (GDB = +3); default: free")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2000, help="GDB port of a running host")
    parser.add_argument("--webapi-port", type=int, help="WebAPI port of a running host (cross-checks, recovery)")
    parser.add_argument("-k", dest="only", default="", help="run the groups whose title contains this")
    parser.add_argument("-v", "--verbose", action="store_true", help="print every packet")
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
        ok = Verifier(args, api).run()
    finally:
        if host:
            host.stop()
            print(f"host stopped (log: {host.log_path})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
