#!/usr/bin/env python3
"""
patch_scroller_trd_v2.py — Patch "Scroller by Demarche" with paging lock detection

Adds:
1. Paging lock detection at boot (before any loading)
2. Dual-mode Line 80 fix (POKE + OUT)

If paging is locked (128K menu → 48K mode), shows error message instead of crashing.
"""

import argparse
import os
import sys
from typing import Tuple

# Paging test machine code (60 bytes, ORG $5CD0)
# Writes $DEADBEEF signature to $C000, switches page, checks if still visible
PAGING_TEST_MC = bytes([
    0x21, 0x00, 0xC0,        # LD HL, $C000
    0x36, 0xDE,              # LD (HL), $DE
    0x23, 0x36, 0xAD,        # INC HL: LD (HL), $AD
    0x23, 0x36, 0xBE,        # INC HL: LD (HL), $BE
    0x23, 0x36, 0xEF,        # INC HL: LD (HL), $EF
    0x01, 0xFD, 0x7F,        # LD BC, $7FFD
    0x3E, 0x01,              # LD A, 1
    0xED, 0x79,              # OUT (C), A
    0x3A, 0x00, 0xC0,        # LD A, ($C000)
    0xFE, 0xDE,              # CP $DE
    0x20, 0x1B,              # JR NZ, ok
    0x3A, 0x01, 0xC0,        # LD A, ($C001)
    0xFE, 0xAD,              # CP $AD
    0x20, 0x14,              # JR NZ, ok
    0x3A, 0x02, 0xC0,        # LD A, ($C002)
    0xFE, 0xBE,              # CP $BE
    0x20, 0x0D,              # JR NZ, ok
    0x3A, 0x03, 0xC0,        # LD A, ($C003)
    0xFE, 0xEF,              # CP $EF
    0x20, 0x06,              # JR NZ, ok
    # locked:
    0xAF,                    # XOR A
    0xED, 0x79,              # OUT (C), A (restore page 0)
    0x3E, 0x01,              # LD A, 1 (return locked)
    0xC9,                    # RET
    # ok:
    0xAF,                    # XOR A
    0xED, 0x79,              # OUT (C), A (restore page 0)
    0xAF,                    # XOR A (return 0 = OK)
    0xC9,                    # RET
])

def make_float(n: int) -> bytes:
    """Create Sinclair BASIC 5-byte float for integer n."""
    if n == 0:
        return bytes([0x0E, 0x00, 0x00, 0x00, 0x00, 0x00])
    # For small positive integers, use the short form
    return bytes([0x0E, 0x00, 0x00, n & 0xFF, (n >> 8) & 0xFF, 0x00])

def tokenize_line(lineno: int, text: str) -> bytes:
    """Simple tokenizer for our specific BASIC lines."""
    # Token codes
    tokens = {
        'FOR': 0xEB, 'TO': 0xCC, 'READ': 0xE3, 'POKE': 0xF4,
        'NEXT': 0xF3, 'DATA': 0xE4, 'IF': 0xFA, 'USR': 0xC0,
        'THEN': 0xCB, 'PRINT': 0xF5, 'STOP': 0xE2, 'RANDOMIZE': 0xF9,
        'VAL': 0xB0, 'OUT': 0xDF, 'REM': 0xEA, 'INK': 0xD9,
        'PAPER': 0xDA, 'BORDER': 0xE7, 'CLEAR': 0xFD, 'NOT': 0xC3,
        'PI': 0xA7, 'LOAD': 0xEF, 'CODE': 0xAF,
    }

    result = bytearray()
    i = 0
    while i < len(text):
        # Check for token match
        matched = False
        for tok, code in tokens.items():
            if text[i:].upper().startswith(tok):
                result.append(code)
                i += len(tok)
                matched = True
                break
        if not matched:
            # Check for number
            if text[i].isdigit():
                j = i
                while j < len(text) and text[j].isdigit():
                    j += 1
                num_str = text[i:j]
                num = int(num_str)
                # Add number as text followed by float
                result.extend(num_str.encode())
                result.extend(make_float(num))
                i = j
            elif text[i] == '"':
                # String literal
                result.append(ord('"'))
                i += 1
                while i < len(text) and text[i] != '"':
                    result.append(ord(text[i]))
                    i += 1
                if i < len(text):
                    result.append(ord('"'))
                    i += 1
            else:
                # Single character
                result.append(ord(text[i]))
                i += 1

    result.append(0x0D)  # ENTER

    # Build line header: big-endian line number, little-endian length
    header = bytes([lineno >> 8, lineno & 0xFF, len(result) & 0xFF, (len(result) >> 8) & 0xFF])
    return header + bytes(result)


def build_line_15() -> bytes:
    """Build Line 15: IF USR 23760 THEN PRINT "...": STOP"""
    # 15 IF USR 23760 THEN PRINT "PAGING LOCKED!": PRINT "Use RESET=BASIC": STOP
    body = bytearray()
    body.append(0xFA)  # IF
    body.append(0xC0)  # USR
    body.extend(b'23760')
    body.extend(make_float(23760))
    body.append(0x20)  # space
    body.append(0xCB)  # THEN
    body.append(0x20)  # space
    body.append(0xF5)  # PRINT
    body.append(0x20)  # space
    body.extend(b'"PAGING LOCKED!"')
    body.append(0x3A)  # :
    body.append(0xF5)  # PRINT
    body.append(0x20)  # space
    body.extend(b'"Use RESET=BASIC"')
    body.append(0x3A)  # :
    body.append(0xE2)  # STOP
    body.append(0x0D)  # ENTER

    header = bytes([0, 15, len(body) & 0xFF, (len(body) >> 8) & 0xFF])
    return header + bytes(body)


def build_paging_check_lines() -> bytes:
    """Build Lines 11-12-13 for paging check (compact DATA approach)."""
    lines = bytearray()

    # Line 11: FOR i=0 TO 59: READ d: POKE 23760+i,d: NEXT i
    body11 = bytearray()
    body11.append(0xEB)  # FOR
    body11.append(ord('i'))
    body11.append(ord('='))
    body11.extend(b'0')
    body11.extend(make_float(0))
    body11.append(0xCC)  # TO
    body11.extend(b'59')
    body11.extend(make_float(59))
    body11.append(ord(':'))
    body11.append(0xE3)  # READ
    body11.append(ord('d'))
    body11.append(ord(':'))
    body11.append(0xF4)  # POKE
    body11.extend(b'23760')
    body11.extend(make_float(23760))
    body11.append(ord('+'))
    body11.append(ord('i'))
    body11.append(ord(','))
    body11.append(ord('d'))
    body11.append(ord(':'))
    body11.append(0xF3)  # NEXT
    body11.append(ord('i'))
    body11.append(0x0D)
    header11 = bytes([0, 11, len(body11) & 0xFF, (len(body11) >> 8) & 0xFF])
    lines.extend(header11 + bytes(body11))

    # Line 12: DATA 33,0,192,...
    body12 = bytearray()
    body12.append(0xE4)  # DATA
    for i, b in enumerate(PAGING_TEST_MC):
        if i > 0:
            body12.append(ord(','))
        s = str(b)
        body12.extend(s.encode())
        body12.extend(make_float(b))
    body12.append(0x0D)
    header12 = bytes([0, 12, len(body12) & 0xFF, (len(body12) >> 8) & 0xFF])
    lines.extend(header12 + bytes(body12))

    # Line 13: IF USR 23760 THEN PRINT "PAGING LOCKED!": PRINT "Use RESET=BASIC": STOP
    body13 = bytearray()
    body13.append(0xFA)  # IF
    body13.append(0xC0)  # USR
    body13.extend(b'23760')
    body13.extend(make_float(23760))
    body13.append(0xCB)  # THEN
    body13.append(0xF5)  # PRINT
    body13.extend(b'"PAGING LOCKED!"')
    body13.append(ord(':'))
    body13.append(0xF5)  # PRINT
    body13.extend(b'"Use RESET=BASIC"')
    body13.append(ord(':'))
    body13.append(0xE2)  # STOP
    body13.append(0x0D)
    header13 = bytes([0, 13, len(body13) & 0xFF, (len(body13) >> 8) & 0xFF])
    lines.extend(header13 + bytes(body13))

    return bytes(lines)


def find_free_sectors(data: bytearray) -> Tuple[int, int]:
    """Find first free sector on disk (after existing files)."""
    # Read disk info from sector 8 of track 0
    info_offset = 8 * 256
    first_free_sec = data[info_offset + 0xE1]
    first_free_trk = data[info_offset + 0xE2]
    return first_free_trk, first_free_sec


def add_code_file(data: bytearray, name: str, code: bytes, load_addr: int) -> None:
    """Add a CODE file to the TRD disk."""
    # Find free catalog slot
    catalog_slot = -1
    for i in range(128):
        entry_offset = i * 16
        if data[entry_offset] == 0:  # Empty slot
            catalog_slot = i
            break

    if catalog_slot == -1:
        raise ValueError("No free catalog slots")

    # Find free sectors
    info_offset = 8 * 256
    first_free_sec = data[info_offset + 0xE1]
    first_free_trk = data[info_offset + 0xE2]

    # Calculate number of sectors needed
    num_sectors = (len(code) + 255) // 256

    # Write file data
    file_offset = (first_free_trk * 16 + first_free_sec) * 256
    data[file_offset:file_offset + len(code)] = code

    # Create catalog entry
    entry = bytearray(16)
    name_bytes = name.encode()[:8].ljust(8)
    entry[0:8] = name_bytes
    entry[8] = ord('C')  # Extension 'C' for CODE
    entry[9] = load_addr & 0xFF
    entry[10] = (load_addr >> 8) & 0xFF
    entry[11] = len(code) & 0xFF
    entry[12] = (len(code) >> 8) & 0xFF
    entry[13] = num_sectors
    entry[14] = first_free_sec
    entry[15] = first_free_trk

    # Write catalog entry
    data[catalog_slot * 16 : catalog_slot * 16 + 16] = entry

    # Update disk info
    next_sec = first_free_sec + num_sectors
    next_trk = first_free_trk
    while next_sec >= 16:
        next_sec -= 16
        next_trk += 1
    data[info_offset + 0xE1] = next_sec
    data[info_offset + 0xE2] = next_trk

    # Update file count
    data[info_offset + 0xE4] += 1

    # Update free sectors count
    free_sectors = data[info_offset + 0xE5] | (data[info_offset + 0xE6] << 8)
    free_sectors -= num_sectors
    data[info_offset + 0xE5] = free_sectors & 0xFF
    data[info_offset + 0xE6] = (free_sectors >> 8) & 0xFF


def patch_trd(input_path: str, output_path: str, add_paging_check: bool = True) -> None:
    if not os.path.exists(input_path):
        print(f"Error: Input TRD '{input_path}' does not exist.", file=sys.stderr)
        sys.exit(1)

    with open(input_path, "rb") as f:
        data = bytearray(f.read())

    # Verify SCROLLER.B is entry 0
    file_name = data[0:8].rstrip()
    file_ext = chr(data[8])
    if file_name != b"SCROLLER" or file_ext != "B":
        print(f"Error: Expected file 0 to be SCROLLER.B, found {file_name}.{file_ext}", file=sys.stderr)
        sys.exit(1)

    orig_bas_len = data[9] | (data[10] << 8)
    start_sec = data[14]
    start_trk = data[15]
    bas_offset = (start_trk * 16 + start_sec) * 256

    bas = data[bas_offset : bas_offset + orig_bas_len]

    # Parse original BASIC to extract all lines
    lines = {}
    pos = 0
    while pos < len(bas):
        if pos + 4 > len(bas):
            break
        lineno = (bas[pos] << 8) | bas[pos + 1]
        length = bas[pos + 2] | (bas[pos + 3] << 8)
        if lineno == 0 and length == 0:
            break
        lines[lineno] = bas[pos : pos + 4 + length]
        pos += 4 + length

    print(f"Original BASIC: {len(lines)} lines, {orig_bas_len} bytes")

    # Patch Line 80: add POKE before OUT
    if 80 in lines:
        line80 = lines[80]
        line_body = line80[4:]
        target = b'\xdf\xb0"32765",\xb0"20":'
        replacement = b'\xf4\xb0"23388",\xb0"20":\xdf\xb0"32765",\xb0"20":'

        if target in line_body:
            new_body = line_body.replace(target, replacement)
            new_header = bytes([0, 80, len(new_body) & 0xFF, (len(new_body) >> 8) & 0xFF])
            lines[80] = new_header + new_body
            print("  Applied Line 80 dual-mode fix (POKE + OUT)")
        else:
            print("  Warning: Line 80 pattern not found, may already be patched")

    # Add paging check lines if requested
    if add_paging_check:
        check_lines = build_paging_check_lines()
        # Parse the check lines back into individual lines
        cpos = 0
        while cpos < len(check_lines):
            clineno = (check_lines[cpos] << 8) | check_lines[cpos + 1]
            clength = check_lines[cpos + 2] | (check_lines[cpos + 3] << 8)
            lines[clineno] = check_lines[cpos : cpos + 4 + clength]
            cpos += 4 + clength
        print(f"  Added paging lock detection (Lines 11-13)")

    # Rebuild BASIC program (lines in sorted order)
    new_bas = bytearray()
    for lineno in sorted(lines.keys()):
        new_bas.extend(lines[lineno])

    new_bas_len = len(new_bas)
    print(f"Patched BASIC: {len(lines)} lines, {new_bas_len} bytes")

    # Calculate sectors needed
    new_sectors = (new_bas_len + 255) // 256
    orig_sectors = data[13]  # sectors in catalog entry

    if new_sectors > orig_sectors:
        print(f"  Expanding from {orig_sectors} to {new_sectors} sectors")
        # For now, just expand in place (assumes no overlap with next file)
        # In production, would need to relocate the file

    # Update BASIC program data on disk
    data[bas_offset : bas_offset + new_bas_len] = new_bas

    # Update catalog entry 0
    data[9] = new_bas_len & 0xFF
    data[10] = (new_bas_len >> 8) & 0xFF
    data[11] = new_bas_len & 0xFF
    data[12] = (new_bas_len >> 8) & 0xFF
    data[13] = new_sectors

    out_dir = os.path.dirname(os.path.abspath(output_path))
    os.makedirs(out_dir, exist_ok=True)
    with open(output_path, "wb") as f_out:
        f_out.write(data)

    print(f"\nSuccessfully patched TRD: {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Patch 'Scroller by Demarche' TRD with paging lock detection.")
    parser.add_argument("-i", "--input", default="testdata/sound/covox/scroller_by_demarche.trd",
                        help="Input TRD file path")
    parser.add_argument("-o", "--output", default="docs/disasm/demo/scroller/scroller_fixed_v2.trd",
                        help="Output patched TRD file path")
    parser.add_argument("--no-paging-check", action="store_true",
                        help="Skip adding paging lock detection")
    args = parser.parse_args()
    patch_trd(args.input, args.output, add_paging_check=not args.no_paging_check)


if __name__ == "__main__":
    main()
