#!/usr/bin/env python3
"""Extract the FT81x ROM image (fonts 16-34) from a Bridgetek EVE emulator DLL.

The VDAC2 card's FT812 draws CMD_TEXT / CMD_NUMBER and the TEXT8X8 / TEXTVGA
bitmap formats from its ROM fonts (FT812 behavior spec §1, vdac2-tdd.md §5.5).
The only available source of that ROM is the Bridgetek emulator library
`bt8xxemu.dll`, which carries the FT81x ROM uncompressed:

  - Bridgetek EVE Emulator: https://github.com/Bridgetek/EVE_Emulator (bin/)
  - TS-Labs Unreal ships one too: https://github.com/tslabs/zx-evo-unreal
    (Unreal/cfg/bt8xxemu.dll, bt8xxemu_x64.dll)

What this script does:
  1. finds every copy of a ROM font table in the DLL by its first metric block
     (font 16: 128 widths of 8, format L1, stride 1, 8x8, data pointer inside
     the ROM area);
  2. maps the copy to chip addresses: the table sits at 0x201EE0, the address
     ROM_FONTROOT (0x2FFFFC) holds [FT81X Programmers Guide §5.5.1];
  3. validates each copy against the spec: 19 metric blocks (fonts 16-34),
     fonts 16-25 L1 and 26-34 L4, stride = ceil(width * bits / 8), every data
     pointer and glyph range inside 0x1E0000-0x2FFFFF, a non-zero advance for
     'A', and real glyphs ('A',
     'B', '0' not blank) in fonts 16, 25 and 34;
  4. writes the first valid copy as the 1152 KB image of 0x1E0000-0x2FFFFF,
     with ROM_FONTROOT set to 0x201EE0, and prints its SHA-1.

Other chips' ROMs (BT81x) in the same DLL fail step 3 and are reported.

The output is a third-party ROM: keep it local, do not commit it. unreal-ng
loads it from [VDAC2] RomImage, default rom/ft81x.rom
(vdac2-integration-design.md §10).

Known result (2026-10-01): the Bridgetek DLL of EVE Emulator 5.1.26 and the
TS-Labs DLL give the same 0x200000-0x2FFFFF (SHA-1 84a16c5c...); they differ
only in font 34's glyphs for codes 0x00-0x1F, which no font covers.

Usage:
  extract-ft81x-rom.py DLL [-o OUT] [--copy N] [--list]
"""

import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path

ROM_BASE = 0x1E0000
ROM_END = 0x300000                 # exclusive
ROM_SIZE = ROM_END - ROM_BASE      # 1152 KB
FONT_TABLE = 0x201EE0              # value of ROM_FONTROOT
FONT_ROOT = 0x2FFFFC
METRIC_SIZE = 148
FIRST_FONT, LAST_FONT = 16, 34
FORMAT_BITS = {1: 1, 2: 4, 3: 8, 17: 2}   # L1, L4, L8, L2
FIRST_BLOCK = bytes([8]) * 128 + struct.pack("<IIII", 1, 1, 8, 8)


def metric(image: bytes, font: int):
    at = FONT_TABLE - ROM_BASE + (font - FIRST_FONT) * METRIC_SIZE
    widths = image[at:at + 128]
    fmt, stride, width, height, ptr = struct.unpack_from("<IIIII", image, at + 128)
    return widths, fmt, stride, width, height, ptr


def glyph_has_pixels(image: bytes, font: int, char: str) -> bool:
    _, fmt, stride, _, height, ptr = metric(image, font)
    code = ord(char) - (0x80 if font in (17, 19) else 0)
    start = ptr - ROM_BASE + code * stride * height
    return any(image[start:start + stride * height])


def validate(image: bytes) -> list:
    problems = []
    for font in range(FIRST_FONT, LAST_FONT + 1):
        widths, fmt, stride, width, height, ptr = metric(image, font)
        expected = 1 if font <= 25 else 2
        if fmt != expected:
            problems.append(f"font {font}: format {fmt}, expected {expected}")
            continue
        bits = FORMAT_BITS[fmt]
        if stride != (width * bits + 7) // 8:
            problems.append(f"font {font}: stride {stride} for width {width}")
        if not 1 <= height <= 255 or not 1 <= width <= 255:
            problems.append(f"font {font}: size {width}x{height}")
        if not ROM_BASE <= ptr or ptr + 128 * stride * height > ROM_END:
            problems.append(f"font {font}: glyphs 0x{ptr:06X}+ outside the ROM")
        # The widths are pen advances and may exceed the bitmap width ('@',
        # 'W' in fonts 20, 25, 34), so they bound nothing; a zero advance for
        # a printable letter would mean a misaligned table
        if widths[ord("A") - (0x80 if font in (17, 19) else 0)] == 0 and font not in (17, 19):
            problems.append(f"font {font}: zero advance for 'A'")
    if problems:
        return problems
    for font in (16, 25, 34):
        for char in "AB0":
            if not glyph_has_pixels(image, font, char):
                problems.append(f"font {font}: glyph '{char}' is blank")
    return problems


def candidates(dll: bytes):
    for match in re.finditer(re.escape(FIRST_BLOCK), dll):
        at = match.start()
        ptr = struct.unpack_from("<I", dll, at + 144)[0]
        if not ROM_BASE <= ptr < ROM_END:
            continue                         # FT80x copies: their ROM is elsewhere
        start = at - (FONT_TABLE - ROM_BASE)
        if start < 0 or start + ROM_SIZE > len(dll):
            continue
        image = bytearray(dll[start:start + ROM_SIZE])
        struct.pack_into("<I", image, FONT_ROOT - ROM_BASE, FONT_TABLE)
        yield at, bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("dll", type=Path, help="bt8xxemu.dll (Bridgetek or TS-Labs Unreal)")
    parser.add_argument("-o", "--out", type=Path, default=Path("ft81x.rom"), help="output image (default ft81x.rom)")
    parser.add_argument("--copy", type=int, default=None, help="take this valid copy (0 = first)")
    parser.add_argument("--list", action="store_true", help="only list the copies found")
    args = parser.parse_args()

    dll = args.dll.read_bytes()
    valid = []
    for at, image in candidates(dll):
        problems = validate(image)
        status = "valid" if not problems else "rejected: " + problems[0]
        print(f"table at DLL offset 0x{at:X}: {status}")
        if not problems:
            valid.append(image)
    if not valid:
        print("no valid FT81x ROM font table found", file=sys.stderr)
        return 1
    if args.list:
        return 0

    index = args.copy if args.copy is not None else 0
    if not 0 <= index < len(valid):
        print(f"--copy {index}: only {len(valid)} valid copies", file=sys.stderr)
        return 1
    image = valid[index]
    if len(valid) > 1 and args.copy is None:
        same = all(v[0x20000:] == image[0x20000:] for v in valid)
        print(f"{len(valid)} valid copies; 0x200000-0x2FFFFF {'identical' if same else 'DIFFERENT'}, taking the first")

    args.out.write_bytes(image)
    print(f"wrote {args.out} ({len(image)} bytes)")
    print(f"SHA-1 image          {hashlib.sha1(image).hexdigest()}")
    print(f"SHA-1 0x200000-end   {hashlib.sha1(image[0x20000:]).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
