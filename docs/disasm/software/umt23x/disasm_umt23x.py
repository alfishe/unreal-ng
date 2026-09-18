#!/usr/bin/env python3
"""Extract UMT v2.3x (universal memory tester) from UMT23X.tap and build a
fully annotated disassembly (educational RE companion).

The tape holds ONE MegaLZ-packed code block. A built-in literal Z80-subset
interpreter executes the on-tape stage-1 relocator + MegaLZ depacker exactly
as a real machine would (validated byte-identical against an emulator capture
halted at the JP 0x6000 entry), so this script regenerates everything from the
tape alone - no emulator needed.

Inputs:
  testdata/memory/UMT23X.tap          the source tape (repo-relative)
  - or - umt-payload-c000.bin          a previously extracted payload block
  testdata/memory/UMT23X.sna           optional: mid-run emulator snapshot;
                                       when present the depacked image is
                                       cross-checked against it (see below)

Outputs (next to this script):
  umt-unpacked-6000.bin                24 KiB image for Z80 0x6000..0xBFFF
                                       (program 0x6000-0x84F9, zero gap,
                                       resident depacker 0xBF00-0xBFE5)
  umt23x-z80dasm.asm                   machine disassembly (z80dasm reference)
  umt23x-z80dasm.asm.sym               z80dasm symbol dictionary

See memory-addressing.md in this folder for the per-model port/bit analysis
and README.md for the package overview.

Usage: disasm_umt23x.py [TAP_PATH | BIN_PATH] [OUT_DIR]
Location-aware: with no arguments the docs folder regenerates itself.
Requires z80dasm on PATH.
"""
import os
import re
import struct
import subprocess
import sys
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(HERE))))
TAP_DEFAULT = os.path.join(REPO, "testdata/memory/UMT23X.tap")
SNA_DEFAULT = os.path.join(REPO, "testdata/memory/UMT23X.sna")

ORG = 0x6000
IMAGE_LEN = 0x6000            # 24 KiB window the depacker output covers
PROG_END = 0x84FA             # first byte past the program proper
DEPACK_AT = 0xBF00            # resident MegaLZ depacker after unpacking

MODEL_TABLE = 0x6F40
MODEL_ENTRY_LEN = 27          # [class byte][5 LE routine words][16-char name]
MODEL_COUNT = 15


# ---------------------------------------------------------------------------
# TAP handling
# ---------------------------------------------------------------------------

def parse_tap(data):
    """Yield (header, body) for every header/data pair in a .tap stream."""
    pos, pending = 0, None
    while pos + 2 <= len(data):
        length = data[pos] | data[pos + 1] << 8
        if length == 0 or pos + 2 + length > len(data):
            break
        block = data[pos + 2:pos + 2 + length]
        if block[0] == 0x00 and length == 19:
            body = block[1:]
            pending = {
                "type": body[0],
                "name": body[1:11].decode("ascii", "replace"),
                "length": body[11] | body[12] << 8,
                "start": body[13] | body[14] << 8,
            }
        elif block[0] == 0xFF and pending is not None:
            yield pending, block[1:-1]
            pending = None
        pos += 2 + length


def extract_payload(tap_path):
    """The tester is the single type-0 block: a tiny BASIC loader that
    RANDOMIZE USRs a 21-byte copy stub (LD SP,0BF00; LD HL,(PROG); LD DE,4Dh;
    ADD HL,DE; LD DE,0C000h; PUSH DE; LD BC,1458h; LDIR; RET), which moves
    the 5208-byte payload to 0xC000 and jumps to it. We locate the stage-1
    relocator signature (DI; LD (0000),SP; LD DE,0BF00) and take the rest."""
    sig = bytes.fromhex("f3ed7300001100bf")
    for header, data in parse_tap(open(tap_path, "rb").read()):
        if header["type"] == 0:
            idx = data.find(sig)
            if idx >= 0:
                payload = data[idx:idx + 0x1458]   # trailing 0Dh is the line end
                assert len(payload) == 0x1458, len(payload)
                return payload
    sys.exit("payload block not found in " + tap_path)


# ---------------------------------------------------------------------------
# Literal Z80-subset interpreter for the on-tape MegaLZ depacker.
# Validated byte-identical against the emulator-halted capture. The tricky
# parts are real Z80 semantics, not the bitstream format: ADD HL,ss and
# RLA/RLCA/RRCA touch ONLY carry, POP/LD/EX touch nothing, so the depacker
# legitimately tests Z from a CP several instructions back; and EXX swaps
# BC/DE/HL only while A/F are shared between banks.
# ---------------------------------------------------------------------------

def depack(payload, verify=None):
    """Run the stage-1 relocator + MegaLZ depacker over the raw TAP payload.

    payload: the block loaded at Z80 0xC000
    verify:  optional ground-truth image for 0x6000..0xBFFF; the first
             output byte that disagrees raises (self-test hook)
    returns: unpacked image for Z80 0x6000..0xBFFF (24 KiB)
    """
    mem = bytearray(65536)
    mem[0xC000:0xC000 + len(payload)] = payload

    R = {"a": 0, "f": 0, "b": 0, "c": 0, "d": 0, "e": 0, "h": 0, "l": 0}
    A = dict(R)
    sp = hl = pc = iy = ix = 0

    S, Z, H, PV, N, C = 0x80, 0x40, 0x10, 0x04, 0x02, 0x01

    def getf():
        f = R["f"]
        return {"S": f & S, "Z": f & Z, "H": f & H, "PV": f & PV,
                "N": f & N, "C": f & C}

    def setf(s=None, z=None, h=None, pv=None, n=None, c=None):
        f = R["f"]
        for mask, val in ((S, s), (Z, z), (H, h), (PV, pv), (N, n), (C, c)):
            if val is None:
                continue
            if val:
                f |= mask
            else:
                f &= ~mask & 0xFF
        R["f"] = f

    def hl_():
        return R["h"] << 8 | R["l"]

    def set_hl(v):
        R["h"], R["l"] = (v >> 8) & 0xFF, v & 0xFF

    def de_():
        return R["d"] << 8 | R["e"]

    def set_de(v):
        R["d"], R["e"] = (v >> 8) & 0xFF, v & 0xFF

    # stage-1 relocator at 0xC000: LDIR payload[0x25:0x103] -> 0xBF00,
    # then INC DE/INC DE, PUSH DE, POP IX, LDIR 6 more bytes -> 0xBFE0
    # (IX = 0xBFE0, the end-marker pop table), RET through PUSHed 0xBF00.
    src, dst = 0xC025, 0xBF00
    for _ in range(0xDE):
        mem[dst] = mem[src]
        dst += 1
        src += 1
    ix = dst + 2                     # INC DE x2, PUSH DE, POP IX: IX=BFE0
    dst2 = ix                        # second LDIR fills BFE0.., DE walks on
    src += 6                         # ADD HL,BC: HL = C103 + 6 = C109
    for _ in range(6):
        mem[dst2] = mem[src]
        dst2 += 1
        src += 1
    pc = 0xBF00                       # RET through the PUSHed BF00

    # registers as left by the stage-1 tail right before its RET -> BF00
    set_de(0xD457)
    set_hl(0xD457)
    R["b"], R["c"] = 0x13, 0x49       # BC = 1349 (LDDR count)
    sp = 0xFF00                       # stage-0's stack above the payload

    class DepackError(AssertionError):
        pass

    def wr(addr, val, where):
        mem[addr] = val
        if verify is not None and 0x6000 <= addr < 0xC000 \
                and mem[addr] != verify[addr - 0x6000]:
            raise DepackError(
                f"output mismatch at 0x{addr:04X}: wrote 0x{val:02X}, "
                f"expected 0x{verify[addr - 0x6000]:02X} (pc=0x{where:04X})")

    runs = 0
    while True:
        op = mem[pc]
        npc = pc + 1
        if op == 0x13:                                   # INC DE
            set_de((de_() + 1) & 0xFFFF)
        elif op == 0xEB:                                 # EX DE,HL
            d, h = de_(), hl_()
            set_de(h)
            set_hl(d)
        elif op == 0xF9:                                 # LD SP,HL
            sp = hl_()
        elif op == 0x11:                                 # LD DE,nn
            set_de(mem[npc] | mem[npc + 1] << 8)
            npc += 2
        elif op == 0xD9:                                 # EXX (BC/DE/HL only)
            for k in "bcdehl":
                R[k], A[k] = A[k], R[k]
        elif op == 0x16:                                 # LD D,n
            R["d"] = mem[npc]
            npc += 1
        elif op == 0x01:                                 # LD BC,nn
            R["b"], R["c"] = mem[npc + 1], mem[npc]
            npc += 2
        elif op == 0xE1:                                 # POP HL
            set_hl(mem[sp] | mem[sp + 1] << 8)
            sp = (sp + 2) & 0xFFFF
        elif op == 0xD1:                                 # POP DE
            set_de(mem[sp] | mem[sp + 1] << 8)
            sp = (sp + 2) & 0xFFFF
        elif op == 0xC1:                                 # POP BC
            R["c"], R["b"] = mem[sp], mem[sp + 1]
            sp = (sp + 2) & 0xFFFF
        elif op == 0xF1:                                 # POP AF
            R["a"], R["f"] = mem[sp + 1], mem[sp]
            sp = (sp + 2) & 0xFFFF
        elif op == 0x3B:                                 # DEC SP
            sp = (sp - 1) & 0xFFFF
        elif op == 0x12:                                 # LD (DE),A
            wr(de_(), R["a"], pc)
        elif op == 0x29:                                 # ADD HL,HL
            v = hl_() << 1
            set_hl(v & 0xFFFF)
            setf(c=1 if v & 0x10000 else 0)
        elif op == 0x10:                                 # DJNZ d
            R["b"] = (R["b"] - 1) & 0xFF
            if R["b"]:
                npc = pc + 2 + (mem[npc] - 256 if mem[npc] > 127 else mem[npc])
            else:
                npc += 1
        elif op == 0x41:                                 # LD B,C
            R["b"] = R["c"]
        elif op == 0x18:                                 # JR d
            d = mem[npc] - 256 if mem[npc] > 127 else mem[npc]
            npc = pc + 2 + d
        elif op in (0x38, 0x30, 0x28, 0x20):             # JR cc,d
            cond = {0x38: getf()["C"], 0x30: 0 if getf()["C"] else 1,
                    0x28: getf()["Z"], 0x20: 0 if getf()["Z"] else 1}[op]
            d = mem[npc] - 256 if mem[npc] > 127 else mem[npc]
            npc += 1
            if cond:
                npc = pc + 2 + d
        elif op == 0x1E:                                 # LD E,n
            R["e"] = mem[npc]
            npc += 1
        elif op == 0x3E:                                 # LD A,n
            R["a"] = mem[npc]
            npc += 1
        elif op == 0x17:                                 # RLA
            cin = getf()["C"]
            cout = R["a"] >> 7
            R["a"] = ((R["a"] << 1) | (1 if cin else 0)) & 0xFF
            setf(c=cout)
        elif op == 0x07:                                 # RLCA
            cout = R["a"] >> 7
            R["a"] = ((R["a"] << 1) | cout) & 0xFF
            setf(c=cout)
        elif op == 0x0F:                                 # RRCA
            cout = R["a"] & 1
            R["a"] = (R["a"] >> 1) | (cout << 7)
            setf(c=cout)
        elif op == 0xFE:                                 # CP n
            r = (R["a"] - mem[npc]) & 0xFF
            v = R["a"] ^ mem[npc] ^ r
            setf(s=r & 0x80, z=1 if r == 0 else 0, h=v & 0x10,
                 pv=1 if (R["a"] ^ mem[npc]) & (R["a"] ^ r) & 0x80 else 0,
                 n=1, c=1 if R["a"] < mem[npc] else 0)
            npc += 1
        elif op in (0x83, 0x82, 0x84):                   # ADD A,E/D/H
            srcb = {0x83: R["e"], 0x82: R["d"], 0x84: R["h"]}[op]
            r = (R["a"] + srcb) & 0xFF
            setf(s=r & 0x80, z=1 if r == 0 else 0,
                 h=(R["a"] ^ srcb ^ r) & 0x10,
                 pv=1 if (R["a"] ^ r) & (srcb ^ r) & 0x80 else 0, n=0,
                 c=1 if R["a"] + srcb > 0xFF else 0)
            R["a"] = r
        elif op == 0xC6:                                 # ADD A,n
            srcb = mem[npc]
            npc += 1
            r = (R["a"] + srcb) & 0xFF
            setf(s=r & 0x80, z=1 if r == 0 else 0,
                 h=(R["a"] ^ srcb ^ r) & 0x10,
                 pv=1 if (R["a"] ^ r) & (srcb ^ r) & 0x80 else 0, n=0,
                 c=1 if R["a"] + srcb > 0xFF else 0)
            R["a"] = r
        elif op == 0xCE:                                 # ADC A,n
            srcb = mem[npc]
            npc += 1
            cin = 1 if getf()["C"] else 0
            r = (R["a"] + srcb + cin) & 0xFF
            setf(s=r & 0x80, z=1 if r == 0 else 0,
                 h=(R["a"] ^ srcb ^ r) & 0x10,
                 pv=1 if (R["a"] ^ r) & (srcb ^ r) & 0x80 else 0, n=0,
                 c=1 if R["a"] + srcb + cin > 0xFF else 0)
            R["a"] = r
        elif op == 0x8F:                                 # ADC A,A
            cin = 1 if getf()["C"] else 0
            r = (R["a"] * 2 + cin) & 0xFF
            setf(s=r & 0x80, z=1 if r == 0 else 0,
                 h=(R["a"] ^ R["a"] ^ r) & 0x10,
                 pv=1 if (R["a"] ^ r) & 0x80 and cin else 0, n=0,
                 c=1 if R["a"] * 2 + cin > 0xFF else 0)
            R["a"] = r
        elif op == 0x9F:                                 # SBC A,A
            cin = 1 if getf()["C"] else 0
            r = (-cin) & 0xFF
            R["a"] = r
            setf(s=r & 0x80, z=1 if r == 0 else 0, h=cin, pv=0, n=1, c=cin)
        elif op == 0x92:                                 # SUB D
            srcb = R["d"]
            r = (R["a"] - srcb) & 0xFF
            setf(s=r & 0x80, z=1 if r == 0 else 0,
                 h=(R["a"] ^ srcb ^ r) & 0x10,
                 pv=1 if (R["a"] ^ srcb) & (R["a"] ^ r) & 0x80 else 0, n=1,
                 c=1 if R["a"] < srcb else 0)
            R["a"] = r
        elif op == 0xD6:                                 # SUB n
            srcb = mem[npc]
            npc += 1
            r = (R["a"] - srcb) & 0xFF
            setf(s=r & 0x80, z=1 if r == 0 else 0,
                 h=(R["a"] ^ srcb ^ r) & 0x10,
                 pv=1 if (R["a"] ^ srcb) & (R["a"] ^ r) & 0x80 else 0, n=1,
                 c=1 if R["a"] < srcb else 0)
            R["a"] = r
        elif op == 0xA9:                                 # XOR C
            R["a"] ^= R["c"]
            setf(s=R["a"] & 0x80, z=1 if R["a"] == 0 else 0,
                 h=0, pv=0, n=0, c=0)
        elif op == 0xBF:                                 # CP A
            setf(s=0, z=1, h=0, pv=0, n=1, c=0)
        elif op == 0x5F:                                 # LD E,A
            R["e"] = R["a"]
        elif op == 0x4F:                                 # LD C,A
            R["c"] = R["a"]
        elif op == 0x3C:                                 # INC A
            R["a"] = (R["a"] + 1) & 0xFF
            setf(s=R["a"] & 0x80, z=1 if R["a"] == 0 else 0,
                 h=1 if R["a"] & 0x0F == 0 else 0,
                 pv=1 if R["a"] == 0x80 else 0, n=0)
        elif op == 0x3D:                                 # DEC A
            R["a"] = (R["a"] - 1) & 0xFF
            setf(s=R["a"] & 0x80, z=1 if R["a"] == 0 else 0,
                 h=1 if R["a"] & 0x0F == 0x0F else 0,
                 pv=1 if R["a"] == 0x7F else 0, n=1)
        elif op == 0x26:                                 # LD H,n
            R["h"] = mem[npc]
            npc += 1
        elif op == 0x67:                                 # LD H,A
            R["h"] = R["a"]
        elif op == 0x6F:                                 # LD L,A
            R["l"] = R["a"]
        elif op == 0x19:                                 # ADD HL,DE
            v = hl_() + de_()
            set_hl(v & 0xFFFF)
            setf(c=1 if v & 0x10000 else 0)
        elif op == 0x23:                                 # INC HL
            set_hl((hl_() + 1) & 0xFFFF)
        elif op == 0x7E:                                 # LD A,(HL)
            R["a"] = mem[hl_()]
        elif op == 0x73:                                 # LD (HL),E
            wr(hl_(), R["e"], pc)
        elif op == 0x72:                                 # LD (HL),D
            wr(hl_(), R["d"], pc)
        elif op == 0x48:                                 # LD C,B
            R["c"] = R["b"]
        elif op == 0x47:                                 # LD B,A
            R["b"] = R["a"]
        elif op == 0x3F:                                 # CCF
            cin = getf()["C"]
            setf(h=cin, n=0, c=0 if cin else 1)
        elif op == 0xCB:
            sub = mem[npc]
            npc += 1
            if sub == 0x0A:                              # RRC D
                cout = R["d"] & 1
                R["d"] = (R["d"] >> 1) | (cout << 7)
                setf(s=R["d"] & 0x80, z=1 if R["d"] == 0 else 0, h=0,
                     pv=1 if bin(R["d"]).count("1") % 2 == 0 else 0,
                     n=0, c=cout)
            elif sub == 0x7F:                            # BIT 7,A
                setf(z=1 if not R["a"] & 0x80 else 0, h=1, n=0)
            else:
                raise ValueError(f"CB {sub:02X} at {pc:04X}")
        elif op == 0xED:
            sub = mem[npc]
            npc += 1
            if sub == 0xB8:                              # LDDR
                while True:
                    wr(de_(), mem[hl_()], pc)
                    set_de((de_() - 1) & 0xFFFF)
                    set_hl((hl_() - 1) & 0xFFFF)
                    bc = (R["b"] << 8 | R["c"]) - 1
                    R["b"], R["c"] = bc >> 8, bc & 0xFF
                    if bc == 0:
                        break
            elif sub == 0xB0:                            # LDIR
                while True:
                    wr(de_(), mem[hl_()], pc)
                    set_de((de_() + 1) & 0xFFFF)
                    set_hl((hl_() + 1) & 0xFFFF)
                    bc = (R["b"] << 8 | R["c"]) - 1
                    R["b"], R["c"] = bc >> 8, bc & 0xFF
                    if bc == 0:
                        break
            elif sub == 0xA0:                            # LDI
                wr(de_(), mem[hl_()], pc)
                set_de((de_() + 1) & 0xFFFF)
                set_hl((hl_() + 1) & 0xFFFF)
                bc = (R["b"] << 8 | R["c"]) - 1
                R["b"], R["c"] = bc >> 8, bc & 0xFF
            else:
                raise ValueError(f"ED {sub:02X} at {pc:04X}")
        elif op == 0xDD and mem[npc] == 0xF9:            # LD SP,IX
            sp = ix
            npc += 1
        elif op == 0xC3:                                 # JP nn
            target = mem[npc] | mem[npc + 1] << 8
            if target == ORG:
                break                                    # unpacked entry reached
            npc = target
        elif op == 0xF3:                                 # DI
            pass
        elif op == 0x21:                                 # LD HL,nn
            set_hl(mem[npc] | mem[npc + 1] << 8)
            npc += 2
        elif op == 0x31:                                 # LD SP,nn
            sp = mem[npc] | mem[npc + 1] << 8
            npc += 2
        else:
            raise ValueError(f"opcode {op:02X} at {pc:04X}")

        pc = npc
        runs += 1
        if runs > 50_000_000:
            raise RuntimeError("runaway depacker")

    return bytes(mem[0x6000:0xC000])


# ---------------------------------------------------------------------------
# Runtime cross-check against the UMT23X.sna emulator snapshot.
#
# The SNA is a MID-RUN capture (PC=6798h, program long unpacked and busy
# testing RAM). Everything the depacker wrote must still be there, except
# where the running program itself has since mutated memory. Those spots
# were located by diffing the snapshot against the depacked image: four
# regions, 259 bytes in total - a test-state variable, two text-VM bytes,
# one whole 256-byte runtime-generated block, and stack scratch below the
# resident depacker. Bytes outside these regions must match exactly, which
# turns the snapshot into a machine-state certificate for both the
# interpreter above and megalz-unpack.py in this folder.
# ---------------------------------------------------------------------------

SNA_MUTATED_REGIONS = [
    (0x6532, 0x6532, "test-state variable (RESET_TEST_STATE domain)"),
    (0x6BD1, 0x6BD3, "text VM variables"),
    (0x7700, 0x77FF, "runtime-generated 256-byte block (self-modifying)"),
    (0xBEFC, 0xBEFF, "stack scratch below the resident depacker"),
]


def load_sna_window(sna_path):
    """Extract the 0x6000-0xBFFF window from a 48K/128K .sna snapshot.

    SNA layout: 27-byte header, then bank5 (0x4000-0x7FFF), bank2
    (0x8000-0xBFFF), bank0 (0x0000-0x3FFF). The window we need spans
    the top half of bank5 and all of bank2 - both are standard RAM in
    every model, so the paging state in the 128K extension is irrelevant
    here. Returns bytes for 0x6000..0xBFFF, or None if the file is too
    short to hold the 48K portion.
    """
    data = open(sna_path, "rb").read()
    if len(data) < 27 + 3 * 16384:
        return None
    window = bytearray(0x6000)
    window[0x0000:0x2000] = data[27 + 0x2000:27 + 0x4000]       # 6000-7FFF
    window[0x2000:0x6000] = data[27 + 16384:27 + 16384 + 0x4000]  # 8000-BFFF
    return bytes(window)


def verify_against_sna(image, sna_path):
    """Diff the depacked image vs the snapshot, ignoring the known
    runtime-mutated regions. Returns a list of complaint strings (empty
    list = the snapshot corroborates the unpack byte for byte)."""
    data = open(sna_path, "rb").read()
    snap = load_sna_window(sna_path)
    if snap is None:
        return [f"{sna_path}: not a 48K/128K SNA (too short)"]

    ext_pc = None
    off48 = 27 + 3 * 16384
    if len(data) >= off48 + 4:               # 128K extension: PC word first
        ext_pc = struct.unpack_from("<H", data, off48)[0]

    mutated = set()
    for lo, hi, _why in SNA_MUTATED_REGIONS:
        mutated.update(range(lo, hi + 1))

    bad = []
    for addr in range(0x6000, 0xC000):
        if addr in mutated:
            continue
        if image[addr - 0x6000] != snap[addr - 0x6000]:
            bad.append(f"0x{addr:04X}: depacked 0x{image[addr - 0x6000]:02X}, "
                       f"snapshot 0x{snap[addr - 0x6000]:02X}")
            if len(bad) >= 8:
                bad.append("...")
                break
    if not bad:
        where = f" (mid-run capture, PC=0x{ext_pc:04X})" if ext_pc else ""
        print(f"sna check:    OK - UMT23X.sna{where} corroborates the "
              f"depacked image outside the 4 known runtime-mutated regions")
    return bad


# ---------------------------------------------------------------------------
# Symbol table: addr -> (label, [comment lines]) - comments transfer into
# the listing at the label definition site.
# ---------------------------------------------------------------------------

SYMS = OrderedDict([
    (0x6000, ("ENTRY", [
        "DI; LD A,3Fh; LD I,A; IM 1; LD SP,6000h. Init sequence calls",
        "BUILD_SCREEN_ADDR_TABLE, INIT_CHECKSUM_TABLE, CLEAR_ATTRS, INIT_TEXT;",
        "the main loop then repeatedly calls RESET_TEST_STATE, CLEAR_ATTRS,",
        "HELP_PAGE and the key dispatcher at 66F4h. Exit (key 0) restores",
        "#7FFD <= 10h (plain ROM0/SCR) and reboots via JP 0000."])),
    (0x6037, ("RESET_TEST_STATE", [
        "Clears per-run state before each test pass."])),
    (0x6044, ("PAGE_TESTABLE", [
        "Feeds the current page number (PAGE_NUM) through the model's w0",
        "MAP_PAGE routine and probes the page for writability."])),
    (0x6052, ("TEST_PAGE_PATTERNS", [
        "Pattern test driver for one page: 00/55/FF/AA writes verified by",
        "VERIFY16, walking SWEEP_PTR/SCREEN_PTR to lay results on screen."])),
    (0x60A6, ("PAGE_NUM", [
        "DATA byte - current 16K page number fed to the model's MAP routine."])),
    (0x60A7, ("SCREEN_PTR", [
        "DATA word - screen address for the current test column."])),
    (0x60A9, ("SWEEP_PTR", ["DATA word - pattern sweep work pointer."])),
    (0x60AB, ("SWEEP_PTR2", ["DATA word - second pattern sweep pointer",
                             "(patterns 55/AA are taken from here)."])),
    (0x60AD, ("TABLE_WORD", [
        "HL in = MODEL_TABLE + model*27 + slot*2 - 1; skips model entries",
        "(27 = 0x1B bytes each) and returns the routine word in HL."])),
    (0x60BF, ("CALL_W0", [
        "JP (HL) into the selected model's w0 = MAP_PAGE routine:",
        "map page PAGE_NUM into window 0xC000 using the clone's ports."])),
    (0x60C6, ("MAP_TSCONF", [
        "Pentagon Evo / TSConf: OUT (#13AF),page - raw page number, 256",
        "pages of 16K (4 MiB). Window at #C000. No other bits involved."])),
    (0x60CF, ("MAP_SPRINTER", [
        "Sprinter: OUT (#00E2),page - raw page number (public docs for",
        "this port are scarce; taken as UMT-observed). 256 pages, 4 MiB."])),
    (0x60D8, ("MAP_ATM71", [
        "ATM turbo 7.1: paging needs the ProfROM service - OUT (#FD77),0ABh",
        "(service mode), PUSH 2A53h marker, JP 3D2Fh (ProfROM hook, returns",
        "in service mode); then OUT (#FFF7),((~page)&3Fh)|40h - the page",
        "number is COMPLEMENTED and bit6 marks a RAM page - and finally",
        "OUT (#FF77),0ABh to leave service mode. Every page switch on",
        "ATM-1 pays this firmware round-trip."])),
    (0x60EA, ("ATM71_MODEIN", ["OUT (#FD77),0ABh - enter ProfROM monitor"])),
    (0x60F6, ("ATM71_MODEOUT", ["OUT (#FF77),0ABh - leave ProfROM monitor"])),
    (0x60FE, ("MAP_KAY", [
        "KAY (all sizes): #1FFD bit4=page bit3, bit7=page bit4, bit6=page",
        "bit6 (SMC operand at 6136h toggles bit6 for 1024K models);",
        "#7FFD = (page AND 7) OR 10h, bit7 = page bit5 (SMC at 6141h).",
        "Shares the Scorpion-style high bits in #1FFD."])),
    (0x6145, ("MAP_SCORPION", [
        "Scorpion 1024/256: #1FFD bit4=page bit3, bit6=page bit4,",
        "bit7=page bit5; #7FFD = (page AND 7) OR 10h. The unreal-ng",
        "portdecoder_scorpion256 implements exactly this mapping."])),
    (0x6169, ("MAP_PENTAGON", [
        "Pentagon 1024/512/128K: #7FFD bits 0-2 = page bits 0-2, bit4=1",
        "(screen in bank 7 area convention), bit6=page bit3, bit7=page",
        "bit4, bit5=page bit5 (dual-use of the 48K lock bit - see",
        "memory-addressing.md). Pentagon 128K/512 just ignore the high bits."])),
    (0x6186, ("MAP_PROFI", [
        "Profi 1024/512: #DFFD = page >> 3 (top bits), #7FFD low 3 bits",
        "are set as in MAP_PENTAGON minus the forced bit4/bit5 extras."])),
    (0x61A2, ("MAP_GMX", [
        "GMX 2048: #DFFD = page >> 4, #1FFD bit4 = page bit3, #7FFD =",
        "(page AND 7) OR 10h. Extension of the Profi scheme by one bit."])),
    (0x61BD, ("MAP_ATM45", [
        "ATM 4.50 (1024/512): #FDFD = page >> 3, #7FFD = (page AND 7) OR 10h.",
        "Note #FDFD here vs #DFFD on Profi - adjacent clone conventions."])),
    (0x61D2, ("VERIFY16", [
        "Unrolled 16x CP (HL) against A with early-out; reports the first",
        "mismatching offset in the screen cell."])),
    (0x629E, ("FILL_BY_PUSH", [
        "SP=dest trick: fills a 16K page by PUSHing BC pairs - fastest",
        "possible RAM fill, restores SP afterwards."])),
    (0x62F1, ("CALL_W2", ["JP (HL) into the model's w2 = pattern test slot"])),
    (0x62F8, ("PAT_128K", ["w2 ladder: LD B,8 (8 pages) -> PAGE_SWEEP"])),
    (0x62FC, ("PAT_256K", ["w2 ladder: LD B,10h (16 pages) -> PAGE_SWEEP"])),
    (0x6300, ("PAT_512K", ["w2 ladder: LD B,20h (32 pages) -> PAGE_SWEEP"])),
    (0x6304, ("PAT_1M", ["w2 ladder: LD B,40h (64 pages) -> PAGE_SWEEP"])),
    (0x6309, ("SCREEN_ROW_SET", [
        "LD (SCREEN_PTR),HL then the per-page pattern loop body shared by",
        "every PAT_* entry; HL selects the screen row for this sweep."])),
    (0x6318, ("PAT_2M", [
        "w2 for 2048K models: sweeps screen rows 5880h and 5888h, 40h",
        "pages each (128 pages total)."])),
    (0x6327, ("PAT_4M", [
        "w2 for TSConf/Sprinter: sweeps rows 5880h/5888h/5890h/5898h,",
        "40h pages each (256 pages total, tail-called via JP)."])),
    (0x634D, ("CALL_W1", ["JP (HL) into the model's w1 = mark pages slot"])),
    (0x6354, ("MARK_4M_TSCONF", [
        "w1 for TSConf: B=0 (256 DJNZ iterations); for every page: map it,",
        "write the page number at 0xC000, remember it; verify pass reads",
        "back and collects failures (helpers 63B0/63C3/63D5/63DE)."])),
    (0x6378, ("MARK_1M", ["w1 ladder: LD B,40h (64 pages) mark sweep"])),
    (0x637C, ("MARK_2M", ["w1 ladder: LD B,80h (128 pages) mark sweep"])),
    (0x6380, ("MARK_256K", ["w1 ladder: LD B,10h (16 pages) mark sweep"])),
    (0x6384, ("MARK_512K", ["w1 ladder: LD B,20h (32 pages) mark sweep"])),
    (0x6388, ("MARK_128K", ["w1 ladder: LD B,8 (8 pages) mark sweep"])),
    (0x638C, ("MARK_4M_SPRINTER", [
        "w1 for Sprinter: same 256-page sweep but the step helper 63B0",
        "skips page 40h exactly and pages 50h-5Fh (ROM/VRAM shadows)."])),
    (0x63B0, ("STEP_SPRINTER", [
        "Sprinter mark step: page++; page==40h -> skip once; 50h-5Fh ->",
        "skip (ROM/VRAM); otherwise write page number to 0xC000."])),
    (0x63C3, ("VERIFY_STEP_SPRINTER", [
        "Sprinter verify step with the same 40h/50h-5Fh exclusions."])),
    (0x63D5, ("STEP_LINEAR", [
        "Linear mark step: page++; write page number to 0xC000."])),
    (0x63DE, ("VERIFY_STEP_LINEAR", [
        "Linear verify step: read 0xC000, must equal page; failures are",
        "recorded through the list pointer at 63FBh."])),
    (0x63FE, ("CALL_W3", ["JP (HL) into the model's w3 = checksum slot"])),
    (0x6416, ("SELECT_ROW", [
        "Maps a page number to its screen table row: 40h->5898h,",
        "80h->5890h, C0h->5888h, else 5880h (two 64-page screen halves)."])),
    (0x643C, ("SUM_2M", ["w3 ladder: LD B,80h (128 pages) checksum sweep"])),
    (0x6440, ("SUM_4M", ["w3 ladder: LD B,0 (256 pages) checksum sweep"])),
    (0x6444, ("SUM_256K", ["w3 ladder: LD B,10h (16 pages) checksum sweep"])),
    (0x646D, ("SUM_1M", ["w3 ladder: LD B,40h (64 pages) checksum sweep"])),
    (0x6471, ("SUM_512K", ["w3 ladder: LD B,20h (32 pages) checksum sweep"])),
    (0x6475, ("SUM_128K", ["w3 ladder: LD B,8 (8 pages) checksum sweep"])),
    (0x64A3, ("CHECKSUM_WRITE", [
        "Checksum pass: A = table[L] + table[L+1Fh] stored to table[L+37h]",
        "and to (DE) - a chained triangular sum over the seeded table at",
        "7700h (seed = ROM[0..0FEh] copied at init; 5C00h area is saved",
        "and restored around the run). SMC counter at 64BAh."])),
    (0x64DC, ("CHECKSUM_VERIFY", [
        "Recomputes the chained sums and flags the first difference."])),
    (0x6533, ("CALL_W4", ["JP (HL) into the model's w4 = rotate test slot"])),
    (0x653A, ("ROT_128K", ["w4 ladder: LD B,8 (8 pages) rotate sweep"])),
    (0x6583, ("ROT_256K", ["w4 ladder: LD B,10h (16 pages) rotate sweep"])),
    (0x6587, ("ROT_512K", ["w4 ladder: LD B,20h (32 pages) rotate sweep"])),
    (0x658B, ("ROT_1M", ["w4 ladder: LD B,40h (64 pages) rotate sweep"])),
    (0x658F, ("ROT_2M", ["w4 ladder: LD B,80h (128 pages) rotate sweep"])),
    (0x6593, ("ROT_4M", ["w4 ladder: LD B,0 (256 pages) rotate sweep"])),
    (0x65A2, ("ROTATE_PAGE", [
        "RR (HL) carry-chains: rotates the whole 16K page one bit right",
        "three times; a stuck bit or cross-coupled cell breaks the carry",
        "chain and the shifted-out pattern, which the verify sweep catches."])),
    (0x66D3, ("SEED_TABLE_ONCE", [
        "One-shot (SMC latch) initialization guard for the checksum table."])),
    (0x670F, ("INIT_CHECKSUM_TABLE", [
        "LDIR ROM[0000..00FEh] -> table at 7700h (0x100 bytes); the ROM",
        "image becomes the checksum chain seed."])),
    (0x6819, ("CLEAR_ATTRS", [
        "Clears the attribute area of the results screen."])),
    (0x6853, ("INIT_TEXT", [
        "Sets TEXT_BASE (6BD5h) and TEXT_LIST (6BD7h) -> TEXT_STREAM,",
        "pointing the text VM at the built-in descriptor data."])),
    (0x68A0, ("HELP_PAGE", [
        "Draws the help/status page through the text VM."])),
    (0x68D5, ("SELECT_MODEL", [
        "Menu key handler: digits 1-9 select models 0-8, X selects 9 (GMX),",
        "on the second page digits 1-5 select models 10-14 (+10 logic).",
        "Stores the index at MODEL_INDEX (6F3Fh) - the ONLY place a model",
        "is chosen. UMT never auto-detects the clone: the only port read",
        "in the whole program is IN A,(0FEh) for the keyboard."])),
    (0x6B87, ("BUILD_SCREEN_ADDR_TABLE", [
        "Fills 5B00h with screen addresses for the results grid."])),
    (0x6BD1, ("ATTR_CUR", ["DATA byte - text VM current attribute byte"])),
    (0x6BD2, ("ATTR_TMP", ["DATA byte - text VM scratch attribute"])),
    (0x6BD3, ("TEXT_CURSOR", ["DATA word - text VM screen cursor"])),
    (0x6BD5, ("TEXT_BASE", ["DATA word - text VM screen base"])),
    (0x6BD7, ("TEXT_LIST", ["DATA word - text VM descriptor stream ptr"])),
    (0x6BD9, ("TEXT_STREAM", [
        "DATA - screen descriptor stream: attribute pairs (e.g. 0707h),",
        "ASCII runs, 0Dh = newline, 00h = end, FFh = page marker. Holds",
        "the version line, both model menu pages, blank filler lines,",
        "memory-size box templates (256K/512K/1024/2048/4096), the '*'",
        "fill string and the test description lines."])),
    (0x6F2E, ("NAME_BUFFER", [
        "DATA - 16-byte buffer with the selected model's name, copied",
        "from MODEL_TABLE for the status line."])),
    (0x6F3F, ("MODEL_INDEX", [
        "DATA byte - selected model 0-14 (index into MODEL_TABLE)."])),
    (0x6F40, ("MODEL_TABLE", [
        "DATA - 15 entries x 27 bytes: [class byte][w0..w4 LE words]",
        "[16-char name]. w0 maps a page (ports/bits per clone), w1 marks",
        "pages, w2 runs pattern tests, w3 checksum tests, w4 rotate tests.",
        "Class byte: 4=256K, 5=512K, 6=1024K, 7=2048K, 8=4096K models;",
        "0Ah for the plain 128K entry (standard #7FFD-only paging)."])),
    (0x70DA, ("VERSION_STRING", ["DATA - 'UMTv2.3x', zero, attr pair"])),
    (0x7920, ("FONT", [
        "DATA - 8x220 glyph font (codes 00h-DBh) used for both text",
        "pages; Russian text is encoded in this custom code page."])),
    (0x8000, ("TEXT_EN", ["DATA - English help text page"])),
    (0xBF00, ("DEPACK_ENTRY", [
        "Resident MegaLZ depacker (stays here after unpacking). LDDR",
        "self-copy rewind, then SP-based bit reader: ADD HL,HL with",
        "DJNZ/POP refill; el0 is an UNCONDITIONAL literal; carry=1 ->",
        "literal; matches copy from DE + 0FF00h|A. End marker 0Fh runs",
        "the LD SP,IX pop epilogue writing the final 6 bytes from",
        "END_TABLE, then LD HL,2758h; EXX; LD SP,6000h; DI; JP ENTRY."])),
    (0xBFE0, ("END_TABLE", [
        "DATA - six bytes ('...)',0,0) emitted by the depacker epilogue",
        "through LD SP,IX + three POP DE pairs (IX was set by stage-1)."])),
])

# data ranges inside the image: (start, end-exclusive, kind, note).
# kinds: vars | text | modeltable | font | bytes | words | zeros
DATA_RANGES = [
    (0x60A6, 0x60AD, "vars", "test driver variables (PAGE_NUM/SCREEN_PTR/SWEEP_PTR)"),
    (0x6BD1, 0x6BD9, "vars", "text VM variables"),
    (0x6BD9, 0x6F2E, "text", "TEXT_STREAM - screen descriptors (menus/boxes/help)"),
    (0x6F2E, 0x6F3E, "text", "NAME_BUFFER - selected model name (16 bytes)"),
    (0x6F3E, 0x6F40, "bytes", "spare byte + MODEL_INDEX"),
    (MODEL_TABLE, MODEL_TABLE + MODEL_COUNT * MODEL_ENTRY_LEN,
     "modeltable", "MODEL_TABLE - 15 x 27-byte model descriptors"),
    (0x70D5, 0x70DA, "bytes", "table end marker: 03 03 '0' '0' 00"),
    (0x70DA, 0x70E3, "text", "VERSION_STRING 'UMTv2.3x' + terminator + attr"),
    (0x70E3, 0x7920, "text", "Russian help texts (custom FONT code page)"),
    (0x7920, 0x8000, "font", "FONT - 220 glyphs x 8 bytes"),
    (0x8000, PROG_END, "text", "English help text page"),
    (PROG_END, DEPACK_AT, "zeros", "unused RAM (zero in the reference capture)"),
    (0xBFE0, 0xBFE6, "words", "END_TABLE - depacker end-marker bytes"),
    (0xBFE6, ORG + IMAGE_LEN, "zeros", "unused RAM (zero in the reference capture)"),
]

WORD_VARS = {0x60A7, 0x60A9, 0x6BD3, 0x6BD5, 0x6BD7}


def autolabels(syms, image):
    """Add R_xxxx labels for absolute CALL/JP targets inside code areas."""
    code_spans = [(0x6000, 0x6BD1), (DEPACK_AT, 0xBFE0)]
    for base, end in code_spans:
        data = image[base - ORG:end - ORG]
        for i in range(len(data) - 2):
            if data[i] in (0xCD, 0xC3):
                t = data[i + 1] | data[i + 2] << 8
                if base <= t < end and t not in syms:
                    syms[t] = (f"R_{t:04X}", [])
    return syms


# ---------------------------------------------------------------------------
# data renderers
# ---------------------------------------------------------------------------

def esc(byte):
    if 0x20 <= byte < 0x7F:
        return chr(byte)
    return {0x0D: "\\r", 0x00: "\\0"}.get(byte, f"\\x{byte:02X}")


def dump_vars(out, image, s, e, syms):
    a = s
    while a < e:
        if a in syms:
            name, _ = syms[a]
            out.append(name + ":")
        if a in WORD_VARS:
            w = image[a - ORG] | image[a - ORG + 1] << 8
            out.append(f"\tdefw 0x{w:04X}")
            a += 2
        else:
            out.append(f"\tdefb 0x{image[a - ORG]:02X}")
            a += 1


def dump_text(out, image, s, e, syms):
    if s in syms:
        out.append(syms[s][0] + ":")
    for a in range(s, e, 16):
        chunk = image[a - ORG:min(a + 16, e) - ORG]
        hexs = ",".join(f"0x{b:02X}" for b in chunk)
        txt = "".join(esc(b) for b in chunk)
        out.append(f"\tdefb {hexs:<47s} ; '{txt}'")


def dump_bytes(out, image, s, e, syms):
    if s in syms:
        out.append(syms[s][0] + ":")
    for a in range(s, e, 8):
        chunk = image[a - ORG:min(a + 8, e) - ORG]
        hexs = ",".join(f"0x{b:02X}" for b in chunk)
        out.append(f"\tdefb {hexs}")


def dump_words(out, image, s, e, syms):
    if s in syms:
        out.append(syms[s][0] + ":")
    for a in range(s, e, 2):
        w = image[a - ORG] | image[a - ORG + 1] << 8
        out.append(f"\tdefw 0x{w:04X}")


def dump_font(out, image, s, e, syms):
    out.append(syms[s][0] + ":" if s in syms else f"DATA_{s:04X}:")
    for g in range(s, e, 8):
        chunk = image[g - ORG:g - ORG + 8]
        hexs = ",".join(f"0x{b:02X}" for b in chunk)
        out.append(f"\tdefb {hexs:<35s} ; glyph 0x{g - s:02X}")


def dump_modeltable(out, image, s, e, syms):
    symname = {a: n for a, (n, _) in syms.items()}
    out.append(syms[s][0] + ":" if s in syms else f"DATA_{s:04X}:")
    for k in range(MODEL_COUNT):
        base = s + k * MODEL_ENTRY_LEN
        cls = image[base - ORG]
        words = [image[base + 1 + 2 * i - ORG] | image[base + 2 + 2 * i - ORG] << 8
                 for i in range(5)]
        name = image[base + 11 - ORG:base + 27 - ORG]
        nametxt = "".join(esc(b) for b in name)
        out.append(f"MTAB_{k:02d}:"
                   f"\t\t ; [{k:2d}] {nametxt.strip() or '?'}")
        out.append(f"\tdefb 0x{cls:02X}"
                   f"\t\t ; class byte (4=256K..8=4096K, 0Ah=128K)")
        labels = ["w0 map-page", "w1 mark", "w2 pattern",
                  "w3 checksum", "w4 rotate"]
        for w, lab in zip(words, labels):
            ref = symname.get(w, f"0x{w:04X}")
            out.append(f"\tdefw {ref:<16s} ; {lab}")
        out.append(f'\tdefb "{nametxt}"')


def dump_zeros(out, image, s, e, syms):
    out.append(f"; {e - s} bytes of zero fill omitted "
               f"(0x{s:04X}-0x{e - 1:04X})")


DUMPERS = {"vars": dump_vars, "text": dump_text, "bytes": dump_bytes,
           "words": dump_words, "font": dump_font,
           "modeltable": dump_modeltable, "zeros": dump_zeros}


# ---------------------------------------------------------------------------
# z80dasm driving and listing assembly
# ---------------------------------------------------------------------------

LINE_HINT = re.compile(r";([0-9a-fA-F]{4})\b")
EQU_ECHO = re.compile(r"^[A-Za-z_][A-Za-z_0-9]*:\s*equ")
IS_AUTO = re.compile(r"^[MDR]_[0-9A-F]{4}$")


def run_z80dasm(image, s, e, symfile, segfile):
    open(segfile, "wb").write(image[s - ORG:e - ORG])
    raw = segfile + ".raw"
    subprocess.run(["z80dasm", "-l", "-a", "-t", "-g", hex(s),
                    "-S", symfile, "-o", raw, segfile], check=True,
                   stderr=subprocess.DEVNULL)
    lines = open(raw).read().splitlines()
    os.remove(raw)
    os.remove(segfile)
    k = 0
    while k < len(lines) and not lines[k].strip().lower().startswith("org"):
        k += 1
    k += 1
    while k < len(lines) and (EQU_ECHO.match(lines[k]) or not lines[k].strip()):
        k += 1
    lines = [ln for ln in lines[k:] if not EQU_ECHO.match(ln)]
    # z80dasm echoes labels (and their symfile comments) for symbols defined
    # past the segment end - cut everything after the last address hint
    last = max((i for i, ln in enumerate(lines) if LINE_HINT.search(ln)),
               default=len(lines) - 1)
    return lines[:last + 1]


def build_listing(image, syms, outpath, header):
    symfile = outpath + ".sym"
    with open(symfile, "w") as f:
        # no comments here: annotations are injected by this script at the
        # definition sites (z80dasm would echo symfile comments itself)
        for addr, name in sorted((a, n) for a, (n, _) in syms.items()):
            f.write(f"{name}: equ 0x{addr:x}\n")

    annotations = {a: c for a, (n, c) in syms.items() if c}
    symname = {a: n for a, (n, c) in syms.items()}

    out = ["; " + "=" * 74]
    for h in header:
        out.append("; " + h)
    out.append("; " + "=" * 74)
    out.append("")
    out.append(f"\torg 0x{ORG:04X}")
    out.append("")
    out.append("; " + "-" * 74)
    out.append("; symbol table - named anchors (auto labels appear inline)")
    out.append("; " + "-" * 74)
    for a, (n, c) in sorted(syms.items()):
        if not IS_AUTO.match(n):
            out.append(f"{n:20s}: equ 0x{a:04X}")
    out.append("")

    ranges = sorted((r[0], r[1], r[2], r[3]) for r in DATA_RANGES)
    items = [(r[0], "data", r) for r in ranges]
    # code segments are the complement of data ranges inside each code area
    # (ranges are clipped to the area before subtracting)
    for base, end in ((0x6000, 0x6BD1), (DEPACK_AT, 0xBFE0)):
        cur = base
        for s, e, _, _ in ranges:
            s2, e2 = max(s, base), min(e, end)
            if s2 >= end:
                break
            if s2 >= e2:            # range does not intersect this area
                continue
            if s2 > cur:
                items.append((cur, "code", (cur, s2)))
            cur = max(cur, e2)
            if cur >= end:
                break
        if cur < end:
            items.append((cur, "code", (cur, end)))
    items.sort(key=lambda t: t[0])

    tmp = 0
    for addr, kind, payload in items:
        if kind == "code":
            s, e = payload
            segfile = os.path.join(HERE, f".seg{tmp}.bin")
            tmp += 1
            for line in run_z80dasm(image, s, e, symfile, segfile):
                hit = next((a for a, n in sorted(symname.items())
                            if line.startswith(n + ":")), None)
                if hit is not None and hit in annotations:
                    out.append("; " + "-" * 74)
                    for c in annotations[hit]:
                        out.append("; " + c)
                    out.append("; " + "-" * 74)
                out.append(line)
            out.append("")
        else:
            s, e, k, note = payload
            if s in annotations:
                out.append("; " + "-" * 74)
                for c in annotations[s]:
                    out.append("; " + c)
                out.append("; " + "-" * 74)
            out.append("; " + "-" * 74)
            out.append(f"; DATA 0x{s:04X}-0x{e - 1:04X}  {note}")
            out.append("; " + "-" * 74)
            DUMPERS[k](out, image, s, e, syms)
            out.append("")

    open(outpath, "w").write("\n".join(out) + "\n")
    print(f"disasm: {outpath} ({len(out)} lines)")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else TAP_DEFAULT
    outdir = sys.argv[2] if len(sys.argv) > 2 else HERE
    os.makedirs(outdir, exist_ok=True)
    binpath = os.path.join(outdir, "umt-unpacked-6000.bin")

    if src.lower().endswith(".bin"):
        payload = open(src, "rb").read()
        print(f"payload: {src} ({len(payload)} bytes @0xC000)")
    else:
        payload = extract_payload(src)
        pl = os.path.join(outdir, "umt-payload-c000.bin")
        open(pl, "wb").write(payload)
        print(f"extracted payload: {len(payload)} bytes @0xC000 -> {pl}")

    image = depack(payload)
    open(binpath, "wb").write(image)
    print(f"depacked: {len(image)} bytes @0x6000 -> {binpath}")

    if os.path.isfile(SNA_DEFAULT):
        bad = verify_against_sna(image, SNA_DEFAULT)
        if bad:
            print("sna check:    MISMATCH vs " + SNA_DEFAULT, file=sys.stderr)
            for line in bad:
                print("  " + line, file=sys.stderr)
            return 1

    syms = autolabels(dict(SYMS), image)
    syms = OrderedDict(sorted(syms.items()))
    build_listing(
        image, syms, os.path.join(outdir, "umt23x-z80dasm.asm"),
        [
            "UMT v2.3x - universal memory tester for ZX Spectrum clones",
            f"binary: umt-unpacked-6000.bin  org 0x{ORG:04X}"
            f"  program len 0x{PROG_END - ORG:04X}",
            "source: testdata/memory/UMT23X.tap (single MegaLZ-packed block,",
            "        depacked by the built-in interpreter in this script)",
            "",
            "Machine-generated z80dasm reference with hand-curated symbols.",
            "The analysis article lives in memory-addressing.md; package",
            "overview in README.md (same folder).",
            "",
            "Tests RAM of 15 models: Pentagon1024/512/128K, Scorpion1024/256,",
            "KAY1024/2048, Profi1024/512, ATM4.5/7.1, PentEvo(TSConf),",
            "Sprinter(4096K), GMX(2048), Spectrum 128K. Four methodologies",
            "per model: mark (page# uniqueness), pattern (00/55/FF/AA),",
            "checksum (ROM-seeded chained sums), rotate (RR carry chains).",
            "",
            "READING ORDER",
            "  1. ENTRY                       init + main loop",
            "  2. MODEL_TABLE + TABLE_WORD    per-model routine dispatch",
            "  3. MAP_* (60C6h-61D2h)         the ports/bits per clone",
            "  4. MARK_/PAT_/SUM_/ROT_        the four test methodologies",
            "  5. SELECT_MODEL + text VM      UI (no clone auto-detection!)",
            "  6. DEPACK_ENTRY (0xBF00h)      resident MegaLZ depacker",
        ])


if __name__ == "__main__":
    main()
