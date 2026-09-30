#!/usr/bin/env python3
"""Join RZX recordings into one multi-block recording.

The output keeps the first file's creator block, then every snapshot and input
recording block of each file in order: a snapshot block between input blocks
is what Fuse / Spectaculator write for a multiload or a rollback point, and a
player applies it when the input block before it ends.

Example (a two-part fixture: Eric's first 300 frames, then SkoolKit's state at
frame 300 with the next 300 frames):
  rzxtrim.py eric.rzx part1.rzx --frames 300
  rzxplay-memptr.py --stop 300 eric.rzx tail.rzx      # snapshot at 300 + the rest
  rzxtrim.py tail.rzx part2.rzx --frames 300
  rzxjoin.py joined.rzx part1.rzx part2.rzx
"""
import struct
import sys


def blocks(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'RZX!':
        sys.exit(f'{path}: not an RZX file')
    i = 10
    while i + 5 <= len(data):
        length, = struct.unpack('<I', data[i + 1:i + 5])
        if length < 5 or i + length > len(data):
            sys.exit(f'{path}: bad block length at {i}')
        yield data[i], data[i:i + length]
        i += length


def main():
    if len(sys.argv) < 4:
        sys.exit('usage: rzxjoin.py OUT.rzx IN1.rzx IN2.rzx [...]')
    out = bytearray(b'RZX!\x00\x0d\x00\x00\x00\x00')
    creator_written = False
    counts = []
    for path in sys.argv[2:]:
        kept = 0
        for block_id, raw in blocks(path):
            if block_id == 0x10:
                if not creator_written:
                    out += raw
                    creator_written = True
            elif block_id in (0x30, 0x80):
                out += raw
                kept += 1
        counts.append(kept)
    with open(sys.argv[1], 'wb') as f:
        f.write(out)
    print(f'{sys.argv[1]}: {sum(counts)} snapshot / input blocks from {len(counts)} files, {len(out)} bytes')


if __name__ == '__main__':
    main()
