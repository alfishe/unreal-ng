#!/usr/bin/env python3
"""
patch_scroller_trd.py — Patch "Scroller by Demarche" (1996) TRD BASIC Loader

Fixes the port #7FFD vs BANK_M ($5B5C) desynchronization bug in SCROLLER.B:
  Original Line 80:
    80 RANDOMIZE USR VAL "25094": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE
  
  Patched Line 80 (Dual-Mode):
    80 RANDOMIZE USR VAL "25094": POKE VAL "23388",VAL "20": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE

Under the Sinclair 128K editor environment:
  POKE VAL "23388", VAL "20" updates system variable BANK_M ($5B5C).
  When the statement finishes at the colon (:), the 128K editor's $5B00 SWAP routine
  reads BANK_M (0x14) and toggles bit 4 across ROM flips while keeping RAM Page 4
  mapped in bits 0-2, allowing TR-DOS to load SCROLL12 cleanly into Page 4.

Under authentic 48K BASIC / TR-DOS boot:
  OUT VAL "32765", VAL "20" directly switches port #7FFD to Page 4 on the hardware bus.

This allows the resulting TRD to boot cleanly across all emulators and configurations.
"""

import argparse
import os
import sys

def patch_trd(input_path: str, output_path: str) -> None:
    if not os.path.exists(input_path):
        print(f"Error: Input TRD '{input_path}' does not exist.", file=sys.stderr)
        sys.exit(1)

    with open(input_path, "rb") as f:
        data = bytearray(f.read())

    # TRD format: 16-byte catalog entries at track 0, sector 0
    # SCROLLER.B is entry 0
    file_name = data[0:8].rstrip()
    file_ext = chr(data[8])
    if file_name != b"SCROLLER" or file_ext != "B":
        print(f"Error: Expected file 0 to be SCROLLER.B, found {file_name}.{file_ext}", file=sys.stderr)
        sys.exit(1)

    bas_len = data[9] | (data[10] << 8)
    start_sec = data[14]
    start_trk = data[15]
    bas_offset = (start_trk * 16 + start_sec) * 256

    bas = data[bas_offset : bas_offset + bas_len]

    # Find Line 80 in tokenized BASIC
    pos = 0
    l80_pos = -1
    l80_len = -1
    while pos < len(bas):
        lineno = (bas[pos] << 8) | bas[pos + 1]
        length = bas[pos + 2] | (bas[pos + 3] << 8)
        if lineno == 80:
            l80_pos = pos
            l80_len = length
            break
        pos += 4 + length

    if l80_pos == -1:
        print("Error: Could not locate Line 80 in SCROLLER.B", file=sys.stderr)
        sys.exit(1)

    line_body = bas[l80_pos + 4 : l80_pos + 4 + l80_len]

    target = b'\xdf\xb0"32765",\xb0"20":'
    replacement = b'\xf4\xb0"23388",\xb0"20":\xdf\xb0"32765",\xb0"20":'

    if target not in line_body:
        print("Error: Target OUT sequence not found in Line 80.", file=sys.stderr)
        sys.exit(1)

    new_line_body = line_body.replace(target, replacement)
    new_l80_len = len(new_line_body)
    new_l80 = bytes([0, 80, new_l80_len & 0xFF, (new_l80_len >> 8) & 0xFF]) + new_line_body

    new_bas = bas[:l80_pos] + new_l80 + bas[l80_pos + 4 + l80_len :]
    new_bas_len = len(new_bas)

    # Ensure it fits within the allocated 2 sectors (512 bytes)
    if new_bas_len > 512:
        print("Error: Patched BASIC program exceeds 2 sectors (512 bytes).", file=sys.stderr)
        sys.exit(1)

    # Update BASIC program data on disk
    data[bas_offset : bas_offset + new_bas_len] = new_bas

    # Update catalog entry 0 length (both param1 and param2 must equal new_bas_len)
    data[9] = new_bas_len & 0xFF
    data[10] = (new_bas_len >> 8) & 0xFF
    data[11] = new_bas_len & 0xFF
    data[12] = (new_bas_len >> 8) & 0xFF

    out_dir = os.path.dirname(os.path.abspath(output_path))
    os.makedirs(out_dir, exist_ok=True)
    with open(output_path, "wb") as f_out:
        f_out.write(data)

    # Also export raw binary token stream (.bin)
    bin_path = os.path.join(out_dir, "scroller_fixed.bin")
    with open(bin_path, "wb") as f_bin:
        f_bin.write(new_bas)

    # Also export Hobeta file ($B)
    sec_count = (new_bas_len + 255) // 256
    hobeta_hdr = bytearray(17)
    hobeta_hdr[0:8] = b"SCROLLER"
    hobeta_hdr[8] = ord("B")
    hobeta_hdr[9] = new_bas_len & 0xFF
    hobeta_hdr[10] = (new_bas_len >> 8) & 0xFF
    hobeta_hdr[11] = new_bas_len & 0xFF
    hobeta_hdr[12] = (new_bas_len >> 8) & 0xFF
    hobeta_hdr[13] = 0
    hobeta_hdr[14] = sec_count & 0xFF
    csum = 0
    for i in range(15):
        csum = (csum + hobeta_hdr[i] * 257 + i) & 0xFFFF
    hobeta_hdr[15] = csum & 0xFF
    hobeta_hdr[16] = (csum >> 8) & 0xFF

    hobeta_path = os.path.join(out_dir, "scroller_fixed.$B")
    with open(hobeta_path, "wb") as f_hob:
        f_hob.write(hobeta_hdr + new_bas)

    print(f"Successfully patched TRD: {output_path}")
    print(f"  SCROLLER.B length: {bas_len} -> {new_bas_len} bytes")
    print(f"  Exported binary: {bin_path} ({len(new_bas)} bytes)")
    print(f"  Exported Hobeta: {hobeta_path} ({len(hobeta_hdr) + len(new_bas)} bytes)")
    print("  Line 80 fix applied: POKE VAL \"23388\",VAL \"20\": OUT VAL \"32765\",VAL \"20\":")

def main():
    parser = argparse.ArgumentParser(description="Patch 'Scroller by Demarche' TRD loader for 128K Sinclair compatibility.")
    parser.add_argument("-i", "--input", default="testdata/sound/covox/scroller_by_demarche.trd",
                        help="Input TRD file path (default: testdata/sound/covox/scroller_by_demarche.trd)")
    parser.add_argument("-o", "--output", default="docs/disasm/demo/scroller/scroller_fixed.trd",
                        help="Output patched TRD file path (default: docs/disasm/demo/scroller/scroller_fixed.trd)")
    args = parser.parse_args()
    patch_trd(args.input, args.output)

if __name__ == "__main__":
    main()
