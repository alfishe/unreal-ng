#!/usr/bin/env python3
"""Turn a ZX Spectrum screen dump into text: screentext.py <screen 6912 bytes> <ROM image with the 48K font>

Each 8x8 cell is matched against the Sinclair font (found in the ROM image by its first two characters);
inverse cells are matched too. Cells that match nothing print as '?'.
"""
import sys

FONT_HEAD = bytes(8) + bytes([0x00, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x00])  # ' ' and '!'


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    scr = open(sys.argv[1], 'rb').read()
    rom = open(sys.argv[2], 'rb').read()
    at = rom.find(FONT_HEAD)
    if at < 0 or len(scr) < 6144:
        return 1
    glyphs = {}
    for i in range(96):
        g = rom[at + i * 8:at + i * 8 + 8]
        ch = '\u00a9' if i == 95 else chr(32 + i)  # the font's last character is the copyright sign
        glyphs.setdefault(g, ch)
        glyphs.setdefault(bytes(255 - b for b in g), ch)
    lines = []
    for row in range(24):
        text = ''
        for col in range(32):
            cell = bytes(scr[((row & 0x18) << 8) | (y << 8) | ((row & 7) << 5) | col] for y in range(8))
            text += glyphs.get(cell, '?')
        lines.append(text.rstrip())
    print('\n'.join(lines).rstrip())
    return 0


if __name__ == '__main__':
    sys.exit(main())
