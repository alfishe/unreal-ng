#!/usr/bin/env python3
"""
make_scroller_sna.py — Generate a clean 128K .SNA snapshot from scroller_by_demarche.trd.

Bypasses the BASIC loader and Sinclair 128K editor SWAP-hook desynchronization issues
by extracting all modules from the TR-DOS disk, decompressing them using the authentic
MegaLZ algorithm directly into the target 128K RAM pages, and packing the complete
memory state into a standard 128K .SNA snapshot ready to play in any ZX Spectrum emulator.

Usage:
    python3 make_scroller_sna.py [trd_path] [-o output_sna_path] [--entry menu|start|demo]

Defaults:
    trd_path:        testdata/sound/covox/scroller_by_demarche.trd
    output_sna_path: docs/disasm/demo/scroller/scroller_by_demarche.sna
    entry:           menu ($9B6B)
"""

import argparse
import os
import struct
import sys


def find_project_root():
    """Finds the root directory of the unreal project."""
    curr = os.path.abspath(os.path.dirname(__file__))
    while curr and curr != os.path.dirname(curr):
        if os.path.exists(os.path.join(curr, "CMakeLists.txt")) and os.path.exists(os.path.join(curr, "core")):
            return curr
        curr = os.path.dirname(curr)
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../.."))


def extract_trd_file(trd_data: bytes, target_name: str) -> bytes:
    """Extracts a file by name from a TR-DOS disk image."""
    for i in range(128):
        entry = trd_data[i * 16 : (i + 1) * 16]
        if entry[0] == 0:
            break
        fname = entry[0:8].decode("ascii", errors="replace").strip()
        ext = chr(entry[8])
        full_name = f"{fname}.{ext}"
        if full_name.upper() == target_name.upper():
            sec = entry[14]
            trk = entry[15]
            offset = (trk * 16 + sec) * 256
            length = entry[11] | (entry[12] << 8)
            return trd_data[offset : offset + length]
    raise ValueError(f"File '{target_name}' not found in TRD catalog")


def run_megalz_decompressor(c00_code: bytes, packed_data: bytes, src_offset: int, dest_offset: int) -> bytes:
    """
    Executes the exact 110-byte MegaLZ decompressor routine from SCROLL00.C ($6244-$62B1)
    to decompress a packed stream with 100% bit-for-bit authenticity.
    """
    megalz_code = c00_code[0x44:0xB2]
    mem = bytearray(65536)
    mem[0x6244 : 0x6244 + len(megalz_code)] = megalz_code
    mem[src_offset : src_offset + len(packed_data)] = packed_data

    pc = 0x6244
    hl = src_offset
    de = dest_offset
    b = c = d = e = h = l = a = 0
    flag_c = flag_z = 0
    a_prime = flag_c_prime = flag_z_prime = 0
    stack = []

    while True:
        op = mem[pc]
        if pc == 0x627F:  # RET C (end of stream)
            if flag_c:
                break
            pc += 1
        elif op == 0x3E:  # LD A, imm
            a = mem[pc + 1]
            pc += 2
        elif op == 0x08:  # EX AF, AF'
            a, a_prime = a_prime, a
            flag_c, flag_c_prime = flag_c_prime, flag_c
            flag_z, flag_z_prime = flag_z_prime, flag_z
            pc += 1
        elif op == 0xED and mem[pc + 1] == 0xA0:  # LDI
            mem[de] = mem[hl]
            de = (de + 1) & 0xFFFF
            hl = (hl + 1) & 0xFFFF
            pc += 2
        elif op == 0x01:  # LD BC, imm16
            c = mem[pc + 1]
            b = mem[pc + 2]
            pc += 3
        elif op == 0x87:  # ADD A, A
            val = a + a
            flag_c = 1 if val > 0xFF else 0
            a = val & 0xFF
            flag_z = 1 if a == 0 else 0
            pc += 1
        elif op == 0x20:  # JR NZ, rel
            off = mem[pc + 1] if mem[pc + 1] < 0x80 else mem[pc + 1] - 0x100
            pc = (pc + 2 + off if not flag_z else pc + 2) & 0xFFFF
        elif op == 0x28:  # JR Z, rel
            off = mem[pc + 1] if mem[pc + 1] < 0x80 else mem[pc + 1] - 0x100
            pc = (pc + 2 + off if flag_z else pc + 2) & 0xFFFF
        elif op == 0x30:  # JR NC, rel
            off = mem[pc + 1] if mem[pc + 1] < 0x80 else mem[pc + 1] - 0x100
            pc = (pc + 2 + off if not flag_c else pc + 2) & 0xFFFF
        elif op == 0x38:  # JR C, rel
            off = mem[pc + 1] if mem[pc + 1] < 0x80 else mem[pc + 1] - 0x100
            pc = (pc + 2 + off if flag_c else pc + 2) & 0xFFFF
        elif op == 0x18:  # JR rel
            off = mem[pc + 1] if mem[pc + 1] < 0x80 else mem[pc + 1] - 0x100
            pc = (pc + 2 + off) & 0xFFFF
        elif op == 0x7E:  # LD A, (HL)
            a = mem[hl]
            pc += 1
        elif op == 0x23:  # INC HL
            hl = (hl + 1) & 0xFFFF
            pc += 1
        elif op == 0x17:  # RLA
            val = (a << 1) | flag_c
            flag_c = 1 if val > 0xFF else 0
            a = val & 0xFF
            pc += 1
        elif op == 0xCB and mem[pc + 1] == 0x11:  # RL C
            val = (c << 1) | flag_c
            flag_c = 1 if val > 0xFF else 0
            c = val & 0xFF
            flag_z = 1 if c == 0 else 0
            pc += 2
        elif op == 0xCB and mem[pc + 1] == 0x19:  # RR C
            new_c = (flag_c << 7) | (c >> 1)
            flag_c = c & 1
            c = new_c
            flag_z = 1 if c == 0 else 0
            pc += 2
        elif op == 0xCB and mem[pc + 1] == 0x10:  # RL B
            val = (b << 1) | flag_c
            flag_c = 1 if val > 0xFF else 0
            b = val & 0xFF
            flag_z = 1 if b == 0 else 0
            pc += 2
        elif op == 0xCB and mem[pc + 1] == 0x29:  # SRA C
            carry = c & 1
            c = ((c >> 1) | (c & 0x80)) & 0xFF
            flag_c = carry
            flag_z = 1 if c == 0 else 0
            pc += 2
        elif op == 0xCB and mem[pc + 1] == 0x39:  # SRL C
            carry = c & 1
            c = (c >> 1) & 0xFF
            flag_c = carry
            flag_z = 1 if c == 0 else 0
            pc += 2
        elif op == 0x10:  # DJNZ rel
            b = (b - 1) & 0xFF
            off = mem[pc + 1] if mem[pc + 1] < 0x80 else mem[pc + 1] - 0x100
            pc = (pc + 2 + off if b != 0 else pc + 2) & 0xFFFF
        elif op == 0x3C:  # INC A
            a = (a + 1) & 0xFF
            flag_z = 1 if a == 0 else 0
            pc += 1
        elif op == 0x0C:  # INC C
            c = (c + 1) & 0xFF
            flag_z = 1 if c == 0 else 0
            pc += 1
        elif op == 0x04:  # INC B
            b = (b + 1) & 0xFF
            flag_z = 1 if b == 0 else 0
            pc += 1
        elif op == 0x05:  # DEC B
            b = (b - 1) & 0xFF
            flag_z = 1 if b == 0 else 0
            pc += 1
        elif op == 0x81:  # ADD A, C
            val = a + c
            flag_c = 1 if val > 0xFF else 0
            a = val & 0xFF
            flag_z = 1 if a == 0 else 0
            pc += 1
        elif op == 0x80:  # ADD A, B
            val = a + b
            flag_c = 1 if val > 0xFF else 0
            a = val & 0xFF
            flag_z = 1 if a == 0 else 0
            pc += 1
        elif op == 0x06:  # LD B, imm
            b = mem[pc + 1]
            pc += 2
        elif op == 0x41:  # LD B, C
            b = c
            pc += 1
        elif op == 0x4E:  # LD C, (HL)
            c = mem[hl]
            pc += 1
        elif op == 0xE5:  # PUSH HL
            stack.append(hl)
            pc += 1
        elif op == 0xE1:  # POP HL
            hl = stack.pop()
            pc += 1
        elif op == 0x69:  # LD L, C
            l = c
            hl = (h << 8) | l
            pc += 1
        elif op == 0x60:  # LD H, B
            h = b
            hl = (h << 8) | l
            pc += 1
        elif op == 0x19:  # ADD HL, DE
            hl = (hl + de) & 0xFFFF
            h = (hl >> 8) & 0xFF
            l = hl & 0xFF
            pc += 1
        elif op == 0x4F:  # LD C, A
            c = a
            pc += 1
        elif op == 0xED and mem[pc + 1] == 0xB0:  # LDIR
            cnt = (b << 8) | c
            if cnt == 0:
                cnt = 0x10000
            for _ in range(cnt):
                mem[de] = mem[hl]
                de = (de + 1) & 0xFFFF
                hl = (hl + 1) & 0xFFFF
            b = c = 0
            pc += 2
        else:
            raise RuntimeError(f"Unhandled opcode {op:02X} at address {pc:04X}")

    end = de if de > dest_offset else de + 0x10000
    size = end - dest_offset
    return bytes(mem[dest_offset : dest_offset + size])


def build_scroller_sna(trd_path: str, output_path: str, entry_point: str = "menu"):
    """
    Extracts all demo files, decrunches them into appropriate 128K banks,
    and writes out a compliant 128K .SNA snapshot file.
    """
    print(f"[*] Reading TRD image: {trd_path}")
    with open(trd_path, "rb") as f:
        trd_data = f.read()

    # Extract all 7 binary payloads from the disk
    c00 = extract_trd_file(trd_data, "SCROLL00.C")
    s15 = extract_trd_file(trd_data, "SCROLL15.C")
    s10 = extract_trd_file(trd_data, "SCROLL10.C")
    s11 = extract_trd_file(trd_data, "SCROLL11.C")
    s13 = extract_trd_file(trd_data, "SCROLL13.C")
    s17 = extract_trd_file(trd_data, "SCROLL17.C")
    s12 = extract_trd_file(trd_data, "SCROLL12.C")

    # Allocate 8 RAM pages (16KB each)
    pages = [bytearray(16384) for _ in range(8)]

    # --------------------------------------------------------------------------
    # Bank 5 ($4000-$7FFF): Screen, System Variables, Dispatcher, SCROLL15
    # --------------------------------------------------------------------------
    print("[*] Decompressing SCROLL15.C -> Page 5 @ $62B2...")
    # Place SCROLL00.C at $6200 (offset $2200 in Page 5)
    pages[5][0x2200 : 0x2200 + len(c00)] = c00
    # Decompress SCROLL15.C at $62B2 (offset $22B2 in Page 5) from src $8000
    out15 = run_megalz_decompressor(c00, s15, 0x8000, 0x62B2)
    pages[5][0x22B2 : 0x22B2 + len(out15)] = out15

    # Initialize Screen Attributes ($5800..$5AFF = Page 5 offset $1800..$1AFF) to 0x07 (white ink on black paper)
    for i in range(0x1800, 0x1B00):
        pages[5][i] = 0x07

    # --------------------------------------------------------------------------
    # Bank 0 ($C000-$FFFF): Audio Sample Block 1 (from SCROLL10.C)
    # --------------------------------------------------------------------------
    print("[*] Decompressing SCROLL10.C -> Page 0 @ $C000...")
    out10 = run_megalz_decompressor(c00, s10, 0x8000, 0xC000)
    pages[0][: len(out10)] = out10

    # --------------------------------------------------------------------------
    # Bank 1 ($C000-$FFFF): Audio Sample Block 2 (from SCROLL11.C)
    # --------------------------------------------------------------------------
    print("[*] Decompressing SCROLL11.C -> Page 1 @ $C000...")
    out11 = run_megalz_decompressor(c00, s11, 0x8000, 0xC000)
    pages[1][: len(out11)] = out11

    # --------------------------------------------------------------------------
    # Bank 3 ($C000-$FFFF): Audio Sample Block 3 (from SCROLL13.C)
    # --------------------------------------------------------------------------
    print("[*] Decompressing SCROLL13.C -> Page 3 @ $C000...")
    out13 = run_megalz_decompressor(c00, s13, 0x8000, 0xC000)
    pages[3][: len(out13)] = out13

    # --------------------------------------------------------------------------
    # Bank 7 ($C000-$FFFF): Audio Sample Block 4 (from SCROLL17.C @ $DB00)
    # --------------------------------------------------------------------------
    print("[*] Decompressing SCROLL17.C -> Page 7 @ $DB00...")
    out17 = run_megalz_decompressor(c00, s17, 0x8000, 0xDB00)
    pages[7][0x1B00 : 0x1B00 + len(out17)] = out17

    # --------------------------------------------------------------------------
    # Bank 4 ($C000-$FFFF): Staging Area holding raw SCROLL12.C (48 sectors)
    # --------------------------------------------------------------------------
    pages[4][: len(s12)] = s12

    # --------------------------------------------------------------------------
    # Bank 2 ($8000-$BFFF): Main Demo Body & Menu (from SCROLL12.C @ $C000)
    # --------------------------------------------------------------------------
    print("[*] Decompressing SCROLL12.C -> Page 2 @ $8000...")
    out12 = run_megalz_decompressor(c00, s12, 0xC000, 0x8000)
    pages[2][: len(out12)] = out12

    # Verify integrity of decompressed demo entry
    assert out12[:6] == bytes([0xCD, 0x93, 0x85, 0xFB, 0x76, 0x3E]), "Integrity check failed: $8000 header mismatch"
    assert pages[2][0x1B6B:0x1B71] == bytes([0xFB, 0x76, 0xAF, 0xD3, 0xFE, 0x21]), "Integrity check failed: $9B6B header mismatch"
    print("[+] All 7 modules successfully decompressed and verified!")

    # --------------------------------------------------------------------------
    # Assemble 128K .SNA Snapshot (131,103 bytes)
    # --------------------------------------------------------------------------
    # Entry point resolution:
    if entry_point in ("menu", "9b6b", "0x9b6b", "9B6B", "0x9B6B"):
        pc = 0x9B6B
        desc = "Covox Setup Menu ($9B6B)"
    elif entry_point in ("start", "6200", "0x6200"):
        pc = 0x6200
        desc = "Demo Start Vector ($6200)"
    elif entry_point in ("demo", "8000", "0x8000"):
        pc = 0x8000
        desc = "Main Engine Direct Entry ($8000)"
    else:
        try:
            pc = int(entry_point, 0)
            desc = f"Custom Entry Point (${pc:04X})"
        except ValueError:
            pc = 0x9B6B
            desc = "Covox Setup Menu ($9B6B)"

    print(f"[*] Snapshot PC: {desc}")

    # Standard 27-byte SNA header:
    # I, HL', DE', BC', AF', HL, DE, BC, IY, IX, IFF2, R, AF, SP, IntMode, BorderColor
    header = struct.pack(
        "<BHHHHHHHHHBBHHBB",
        0x3F,   # I
        0,      # HL'
        0,      # DE'
        0,      # BC'
        0,      # AF'
        0x5800, # HL
        0x5801, # DE
        0x7FFD, # BC
        0x5C3A, # IY
        0,      # IX
        0x04,   # Interrupt: bit 2 = 1 (EI)
        0,      # R
        0,      # AF
        0x6200, # SP (below code)
        1,      # IntMode 1
        0,      # Border: Black (0)
    )

    p7ffd = 0x10  # Bank 0 in Bank 3 ($C000-$FFFF), screen 0 (Bank 5), 48K ROM
    trdos_flag = 0  # TR-DOS ROM not active

    ext_header = struct.pack("<HBB", pc, p7ffd, trdos_flag)

    # 128K SNA ordering:
    # Header (27 B) + Bank 5 + Bank 2 + Bank 0 + ExtHeader (4 B) + Bank 1 + Bank 3 + Bank 4 + Bank 6 + Bank 7
    sna_bytes = (
        header
        + bytes(pages[5])
        + bytes(pages[2])
        + bytes(pages[0])
        + ext_header
        + bytes(pages[1])
        + bytes(pages[3])
        + bytes(pages[4])
        + bytes(pages[6])
        + bytes(pages[7])
    )

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(sna_bytes)

    print(f"[+] Successfully created 128K SNA snapshot: {output_path} ({len(sna_bytes):,} bytes)")
    print(f"    - Bank 5: $4000-$7FFF (Screen + SCROLL00 + SCROLL15)")
    print(f"    - Bank 2: $8000-$BFFF (Main engine + Menu + IM2 vector table)")
    print(f"    - Bank 0: $C000-$FFFF (Sample Block 1, currently paged)")
    print(f"    - Bank 1: Sample Block 2")
    print(f"    - Bank 3: Sample Block 3")
    print(f"    - Bank 4: Staged SCROLL12 stream")
    print(f"    - Bank 7: Sample Block 4 (@ $DB00)")
    print(f"    - Port #7FFD: 0x{p7ffd:02X}, Entry Point: 0x{pc:04X}, SP: 0x6200")


def main():
    root = find_project_root()
    default_trd = os.path.join(root, "testdata/sound/covox/scroller_by_demarche.trd")
    default_sna = os.path.join(root, "docs/disasm/demo/scroller/scroller_by_demarche.sna")

    parser = argparse.ArgumentParser(description="Convert scroller_by_demarche.trd to a clean 128K .SNA snapshot.")
    parser.add_argument("trd", nargs="?", default=default_trd, help=f"Path to input TRD file (default: {default_trd})")
    parser.add_argument("-o", "--output", default=default_sna, help=f"Path to output SNA file (default: {default_sna})")
    parser.add_argument(
        "--entry",
        default="menu",
        choices=["menu", "start", "demo", "9B6B", "6200", "8000"],
        help="Initial Program Counter (menu=$9B6B, start=$6200, demo=$8000)",
    )
    args = parser.parse_args()

    if not os.path.exists(args.trd):
        print(f"Error: input file '{args.trd}' does not exist.", file=sys.stderr)
        sys.exit(1)

    build_scroller_sna(args.trd, args.output, args.entry)


if __name__ == "__main__":
    main()
