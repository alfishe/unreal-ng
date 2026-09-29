#!/usr/bin/env python3
"""A ZX Spectrum screen (6912 bytes) as text, matched against the 48K ROM font at #3D00.

usage: screen-text.py <screen.scr> <48.rom> <out.txt>
Cells that match no character (or its inverse) print as '?'; trailing spaces are dropped.
"""
import sys


def main():
    if len(sys.argv) != 4:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    scr = open(sys.argv[1], 'rb').read()
    rom = open(sys.argv[2], 'rb').read()
    if len(scr) < 6144 or len(rom) < 0x4000:
        print('screen-text: short screen or ROM file', file=sys.stderr)
        return 2
    font = {}
    for c in range(96):
        glyph = rom[0x3D00 + c * 8:0x3D00 + c * 8 + 8]
        font.setdefault(glyph, chr(32 + c))
        font.setdefault(bytes(~b & 0xFF for b in glyph), chr(32 + c))
    lines = []
    for row in range(24):
        line = ''
        for col in range(32):
            cell = bytes(scr[((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | col]
                         for y in range(row * 8, row * 8 + 8))
            line += font.get(cell, '?')
        lines.append(line.rstrip())
    with open(sys.argv[3], 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
