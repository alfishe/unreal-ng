#!/usr/bin/env python3
"""Decode a Sprinter Sp2000 port-decoder (DCP) table page into readable rows.

Background (docs/inprogress/2026-09-28-sprinter/hardware-reference.md §4.1):
every external port access reads one byte from RAM page #40, the "DCP code"
of the device that answers.  The page is 16 KB = 4 maps (CNF port) x 4 KB;
inside a map the byte index is built from bus signals:

    bit 11 PN5 (#7FFD bit 5)   bit 10 /DOS (0 = TR-DOS ROM active)
    bit 9  /WR (1 = IN, 0 = OUT)
    bit 8 A15, 7 A14, 6 A6, 5 A5, 4 A13, 3 A7, 2 A2, 1 A1, 0 A0

Worked example: OUT (#BC),A with A = #21 puts #21BC on the address bus:
A15=0 A14=0 A6=0 A5=1 A13=1 A7=1 A2=1 A1=0 A0=0 -> index bits 8-0 =
0 0011 1100 = #03C (+ #000 for "write, DOS on, PN5 = 0").  In map 0 the BIOS
stores code #2B there: "select the primary IDE channel".  With A = #01, A13 = 0,
the index is #02C and the code #2A: the secondary channel.

Sources the tool reads (any one per table):
  --rom FILE       a Sprinter BIOS ROM (3.04 layout): the table the BIOS
                   writes at start-up is unpacked from ROM page 8 exactly as
                   the BIOS does it (the writer at page 8 #0CA1: a bit-flag
                   stream at #1400, then map 3 = 4 copies of map 0's first 1 KB)
  --page FILE      a 16 KB dump of page #40 (e.g. BIOS-TT doc/DCP_PAGE.bin)
  --records FILE --constants FILE
                   BIOS-TT bios/exp/DCP.ASM records (value, fixed-bit mask,
                   code) expanded the way DCP_INIT does it (page cleared to 0,
                   records applied in order); the code names come from
                   Shared_Includes constants/SP2000.inc (MODULE ACEX)

Output: per map, one row per code and decoded cube, e.g.
    #2B  write  DOS:any  PN5:any  00 1x xxxx 1 01x x100   (= #21BC)
or, with --compare, the entries that differ between two tables.

Usage:
  dcp-table.py --rom data/rom/sprinter/sp2k-3.04.rom [--map 0] [--dump out.bin]
  dcp-table.py --rom ROM --compare-page DCP_PAGE.bin
  dcp-table.py --rom ROM --compare-records DCP.ASM --constants SP2000.inc
  dcp-table.py --selftest
"""

import argparse
import re
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
DEFAULT_ROM = ROOT / "data/rom/sprinter/sp2k-3.04.rom"
PAGE_SIZE = 0x4000
MAP_SIZE = 0x1000

# Index bit -> signal, highest first (bits 11..0 inside one map)
SIGNALS = ["PN5", "/DOS", "/WR", "A15", "A14", "A6", "A5", "A13", "A7", "A2", "A1", "A0"]
# Address-bus position of each of the 9 address bits (index bits 8..0)
ADDR_BITS = {8: 15, 7: 14, 6: 6, 5: 5, 4: 13, 3: 7, 2: 2, 1: 1, 0: 0}

# The BIOS 3.04 writer (ROM page 8): LD HL,stream / LD DE,#C000 / LD B,8 / LD C,(HL) / INC HL / RLC C
WRITER_PATTERN = re.compile(rb"\x21(..)\x11\x00\xC0\x06\x08\x4E\x23\xCB\x01", re.S)

# Code names for the readable output (hardware-reference.md §4.3)
CODE_NAMES = {
    0x10: "WD1793 cmd/status", 0x11: "WD1793 track", 0x12: "WD1793 sector", 0x13: "WD1793 data",
    0x14: "Beta system reg", 0x15: "joystick + WD1793 state", 0x16: "FDD 720K", 0x17: "FDD 1.44M",
    0x1B: "ISA control", 0x1C: "CMOS data read", 0x1D: "CMOS address", 0x1E: "CMOS data write",
    0x20: "IDE data", 0x21: "IDE reg 1", 0x22: "IDE reg 2", 0x23: "IDE reg 3", 0x24: "IDE reg 4",
    0x25: "IDE reg 5", 0x26: "IDE reg 6", 0x27: "IDE reg 7", 0x28: "IDE alt status/ctrl",
    0x29: "IDE drive address", 0x2A: "IDE secondary", 0x2B: "IDE primary", 0x2C: "320 lines",
    0x2D: "312 lines", 0x2E: "PLD reload", 0x40: "keyboard #FE", 0x52: "AY read", 0x58: "Kempston mouse",
    0x88: "Covox", 0x89: "Covox-Blaster ctrl", 0x8F: "ROM/fast RAM page", 0x90: "AY register",
    0x91: "AY data", 0xC0: "#1FFD", 0xC1: "#7FFD", 0xC2: "border #FE", 0xC3: "ALL_MODE",
    0xC4: "PORT_Y/RGADR", 0xC5: "RGMOD", 0xC6: "CNF/SYS", 0xC7: "SCALE", 0xCB: "HOLD",
    0xE8: "window 0 page", 0xE9: "window 1 page", 0xEA: "window 2 page", 0xF0: "window 3 page",
}


def tableFromRom(rom: bytes) -> bytes:
    if len(rom) != 0x40000:
        raise ValueError(f"expected a 256 KB Sprinter BIOS image, got {len(rom)} bytes")
    p8 = rom[8 * PAGE_SIZE:9 * PAGE_SIZE]
    m = WRITER_PATTERN.search(p8)
    if not m:
        raise ValueError("the DCP table writer was not found in ROM page 8 (not a 3.0x-style BIOS?)")
    hl = m.group(1)[0] | (m.group(1)[1] << 8)
    out = bytearray()
    while len(out) < PAGE_SIZE:                 # DE runs #C000..#FFFF
        flags = p8[hl]
        hl += 1
        for _ in range(8):
            if flags & 0x80:
                out.append(p8[hl])
                hl += 1
            else:
                out.append(0)
            flags = (flags << 1) & 0xFF
    out[0x3000:0x3400] = out[0:0x400]           # LDIR #C000 -> #F000, #400 bytes
    for k in range(3):                          # LDIR #F000 -> #F400, #C00 bytes (overlapping copy)
        out[0x3400 + k * 0x400:0x3800 + k * 0x400] = out[0:0x400]
    return bytes(out)


def parseConstants(path: Path) -> dict:
    text = path.read_bytes().decode("cp866", "replace").splitlines()
    names, inside, major = {}, False, ""
    for ln in text:
        code = ln.split(";")[0]
        if re.match(r"\s*MODULE\s+ACEX\b", code):
            inside = True
            continue
        if inside and re.match(r"\s*ENDMODULE\b", code):
            break
        if not inside:
            continue
        m = re.match(r"^(\.?[A-Za-z_][\w.]*):?\s*(?:EQU\s+(\S+))?", code)
        if not m or not m.group(1):
            continue
        label, value = m.group(1), m.group(2)
        if label.startswith("."):
            label = major + label
        else:
            major = label
        if value:
            names[label] = value
    resolved = {}

    def value(name, depth=0):
        v = names[name]
        if v.startswith("#"):
            return int(v[1:], 16)
        if v.startswith("%"):
            return int(v[1:].replace("'", ""), 2)
        if v.isdigit():
            return int(v)
        return value(v if v in names else "" + v, depth + 1)

    for n in names:
        try:
            resolved[n] = value(n)
        except (KeyError, ValueError, RecursionError):
            pass
    return resolved


def tableFromRecords(asm: Path, constants: Path) -> bytes:
    consts = parseConstants(constants)
    words, page = [], bytearray(PAGE_SIZE)
    for ln in asm.read_bytes().decode("cp866", "replace").splitlines():
        code = ln.split(";")[0].strip()
        m = re.match(r"DW\s+%([01']+)$", code, re.I)
        if m:
            words.append(int(m.group(1).replace("'", ""), 2))
            continue
        if re.match(r"DW\s+0\s*,\s*0\s*,\s*0", code, re.I):      # end marker
            break
        m = re.match(r"DB\s+(\S+)$", code, re.I)
        if m and len(words) == 2:
            tok = m.group(1)
            val = int(tok[1:], 16) if tok.startswith("#") else consts[tok.split("ACEX.", 1)[-1]]
            base, mask = words
            free = [b for b in range(14) if not (mask >> b) & 1]
            for k in range(1 << len(free)):
                idx = base & mask
                for i, b in enumerate(free):
                    if (k >> i) & 1:
                        idx |= 1 << b
                page[idx] = val
            words = []
        elif m:
            words = []
    return bytes(page)


def cubes(points: set, nbits: int = 12) -> list:
    """Cover a set of indexes with cubes (value, mask of fixed bits), greedy."""
    remaining = set(points)
    out = []
    full = (1 << nbits) - 1
    while remaining:
        seed = min(remaining)
        value, mask = seed, full
        for b in range(nbits):                  # widen bit by bit while the cube stays inside points
            trial = mask & ~(1 << b)
            free = [i for i in range(nbits) if not (trial >> i) & 1]
            ok = True
            for k in range(1 << len(free)):
                idx = value & trial
                for i, fb in enumerate(free):
                    if (k >> i) & 1:
                        idx |= 1 << fb
                if idx not in points:
                    ok = False
                    break
            if ok:
                mask = trial
        free = [i for i in range(nbits) if not (mask >> i) & 1]
        for k in range(1 << len(free)):
            idx = value & mask
            for i, fb in enumerate(free):
                if (k >> i) & 1:
                    idx |= 1 << fb
            remaining.discard(idx)
        out.append((value & mask, mask))
    return out


def describe(value: int, mask: int) -> str:
    def bit(b):
        return "x" if not (mask >> b) & 1 else str((value >> b) & 1)
    rw = {"1": "read ", "0": "write", "x": "r/w  "}[bit(9)]
    dos = {"0": "DOS:on ", "1": "DOS:off", "x": "DOS:any"}[bit(10)]
    pn5 = {"0": "PN5:0  ", "1": "PN5:1  ", "x": "PN5:any"}[bit(11)]
    addr = ["x"] * 16
    for ib, ab in ADDR_BITS.items():
        addr[15 - ab] = bit(ib)
    pattern = "".join(addr)
    pattern = " ".join(pattern[i:i + 4] for i in range(0, 16, 4))
    example = 0
    for ib, ab in ADDR_BITS.items():
        if (mask >> ib) & 1 and (value >> ib) & 1:
            example |= 1 << ab
    return f"{rw}  {dos}  {pn5}  {pattern}  (e.g. #{example:04X})"


def printMap(table: bytes, mapNo: int) -> None:
    base = mapNo * MAP_SIZE
    byCode = {}
    for i in range(MAP_SIZE):
        c = table[base + i]
        if c:
            byCode.setdefault(c, set()).add(i)
    print(f"map {mapNo} (CNF bits = {mapNo}, page #40 offset #{base:04X}): {len(byCode)} codes")
    for c in sorted(byCode):
        for v, m in cubes(byCode[c]):
            print(f"  #{c:02X} {CODE_NAMES.get(c, ''):24s} {describe(v, m)}")


def compare(a: bytes, b: bytes, nameA: str, nameB: str) -> int:
    diff = {}
    for i in range(PAGE_SIZE):
        if a[i] != b[i]:
            diff.setdefault((i >> 12, a[i], b[i]), set()).add(i & 0xFFF)
    count = sum(len(s) for s in diff.values())
    print(f"{nameA} vs {nameB}: {count} bytes differ")
    for (mapNo, ca, cb), pts in sorted(diff.items()):
        for v, m in cubes(pts):
            print(f"  map {mapNo}: #{ca:02X} -> #{cb:02X}  {describe(v, m)}")
    return count


def selftest() -> int:
    rom = DEFAULT_ROM.read_bytes()
    assert zlib.crc32(rom) & 0xFFFFFFFF == 0x1729CB5C, "data/rom/sprinter/sp2k-3.04.rom is not BIOS 3.04"
    t = tableFromRom(rom)
    # the worked example of the docstring: OUT (#BC),A with A = #21 -> code #2B (primary IDE channel)
    assert t[0x03C] == 0x2B, f"#21BC write = #{t[0x03C]:02X}"
    assert t[0x02C] == 0x2A, f"#01BC write = #{t[0x02C]:02X}"
    # port #7FFD: A15=0 A14=1 A6=1 A5=1 A13=1 A7=1 A2=1 A1=0 A0=1 -> index #0FD (write, DOS on, PN5 = 0),
    # +#200 for a read: both are the #7FFD cell, code #C1; with PN5 = 1 and DOS off (+#C00) it is hidden
    assert t[0x0FD] == t[0x2FD] == 0xC1, f"#7FFD: #{t[0x0FD]:02X}/#{t[0x2FD]:02X}"
    assert t[0xCFD] == 0x00, f"#7FFD after the 48K lock: #{t[0xCFD]:02X}"
    # map 3 = map 0's first KB four times
    assert all(t[0x3000 + k * 0x400:0x3400 + k * 0x400] == t[0:0x400] for k in range(4))
    print(f"selftest OK: 3.04 table CRC32 {zlib.crc32(t) & 0xFFFFFFFF:08x}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rom", type=Path)
    ap.add_argument("--page", type=Path)
    ap.add_argument("--records", type=Path)
    ap.add_argument("--constants", type=Path)
    ap.add_argument("--map", type=int, choices=range(4), action="append")
    ap.add_argument("--dump", type=Path, help="write the 16 KB table")
    ap.add_argument("--compare-page", type=Path)
    ap.add_argument("--compare-records", type=Path)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if args.rom:
        table, name = tableFromRom(args.rom.read_bytes()), args.rom.name
    elif args.page:
        table, name = args.page.read_bytes()[:PAGE_SIZE], args.page.name
    elif args.records and args.constants:
        table, name = tableFromRecords(args.records, args.constants), args.records.name
    else:
        table, name = tableFromRom(DEFAULT_ROM.read_bytes()), DEFAULT_ROM.name
    if args.dump:
        args.dump.write_bytes(table)
    if args.compare_page:
        compare(table, args.compare_page.read_bytes()[:PAGE_SIZE], name, args.compare_page.name)
        return 0
    if args.compare_records:
        if not args.constants:
            ap.error("--compare-records needs --constants")
        compare(table, tableFromRecords(args.compare_records, args.constants), name, args.compare_records.name)
        return 0
    for mapNo in (args.map or range(4)):
        printMap(table, mapNo)
    return 0


if __name__ == "__main__":
    sys.exit(main())
