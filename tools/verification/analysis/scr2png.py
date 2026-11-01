#!/usr/bin/env python3
"""ZX Spectrum .scr (6912 bytes) to PNG, optionally many screens tiled into one sheet: scr2png.py out.png a.scr [b.scr ...]"""

import struct
import sys
import zlib

PALETTE = [(0, 0, 0), (0, 0, 0xD7), (0xD7, 0, 0), (0xD7, 0, 0xD7), (0, 0xD7, 0), (0, 0xD7, 0xD7), (0xD7, 0xD7, 0),
           (0xD7, 0xD7, 0xD7)]
BRIGHT = [(0, 0, 0), (0, 0, 0xFF), (0xFF, 0, 0), (0xFF, 0, 0xFF), (0, 0xFF, 0), (0, 0xFF, 0xFF), (0xFF, 0xFF, 0),
          (0xFF, 0xFF, 0xFF)]


def pixels(scr):
    rows = []
    for y in range(192):
        line = []
        for cx in range(32):
            byte = scr[((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | cx]
            attr = scr[6144 + (y >> 3) * 32 + cx]
            colors = BRIGHT if attr & 0x40 else PALETTE
            ink, paper = colors[attr & 7], colors[(attr >> 3) & 7]
            line += [ink if byte & (0x80 >> b) else paper for b in range(8)]
        rows.append(line)
    return rows


def write_png(path, rows):
    height, width = len(rows), len(rows[0])
    raw = b''.join(b'\0' + bytes(c for px in row for c in px) for row in rows)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def main():
    out, files = sys.argv[1], sys.argv[2:]
    shots = [pixels(open(f, 'rb').read()) for f in files]
    columns = min(4, len(shots))
    blank = [(40, 40, 40)] * 256
    rows = []
    for top in range(0, len(shots), columns):
        group = shots[top:top + columns]
        for y in range(192):
            rows.append([px for i in range(columns) for px in (group[i][y] if i < len(group) else blank) + [(40, 40, 40)] * 4])
        rows += [[(40, 40, 40)] * len(rows[0])] * 4
    write_png(out, rows)


if __name__ == '__main__':
    main()
