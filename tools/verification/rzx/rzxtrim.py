#!/usr/bin/env python3
"""Cut an RZX recording down to a test fixture.

Keeps the creator block, the start snapshot and the first N frames of the
first input block (default: all), and can move the start snapshot out of the
file (an external snapshot descriptor, the image written next to the output).

Examples:
  rzxtrim.py game.rzx short.rzx --frames 5
      the first 5 frames, snapshot embedded as it was
  rzxtrim.py game.rzx ext.rzx --frames 50 --external ext-start.z80
      ext.rzx points at ext-start.z80, written next to it

The output is RZX 0.13, input frames zlib-compressed, repeat frames kept.
"""
import argparse
import os
import struct
import sys
import zlib


def blocks(data):
    if data[:4] != b'RZX!':
        sys.exit('not an RZX file')
    i = 10
    while i + 5 <= len(data):
        block_id = data[i]
        length = struct.unpack('<I', data[i + 1:i + 5])[0]
        if length < 5 or i + length > len(data):
            sys.exit(f'bad block length at {i}')
        yield block_id, data[i:i + length]
        i += length


def snapshot_image(block):
    flags, = struct.unpack('<I', block[5:9])
    extension = block[9:13].rstrip(b'\0').decode('ascii').lower()
    length, = struct.unpack('<I', block[13:17])
    payload = block[17:]
    if flags & 1:
        sys.exit('the start snapshot is already external')
    if flags & 2:
        payload = zlib.decompress(payload)
    if len(payload) != length:
        sys.exit('snapshot length mismatch')
    return extension, payload


def frames_of(block):
    count, = struct.unpack('<I', block[5:9])
    tstates, = struct.unpack('<I', block[10:14])
    flags, = struct.unpack('<I', block[14:18])
    data = block[18:]
    if flags & 1:
        sys.exit('protected input block')
    if flags & 2:
        data = zlib.decompress(data)
    frames = []
    j = 0
    for _ in range(count):
        fetch, ins = struct.unpack('<HH', data[j:j + 4])
        size = 4 + (0 if ins == 0xFFFF else ins)
        frames.append(data[j:j + size])
        j += size
    return tstates, frames


def block(block_id, body):
    return bytes([block_id]) + struct.pack('<I', len(body) + 5) + body


def main():
    parser = argparse.ArgumentParser(description='Cut an RZX recording down to a test fixture.')
    parser.add_argument('infile')
    parser.add_argument('outfile')
    parser.add_argument('--frames', type=int, default=0, help='frames to keep (0: all)')
    parser.add_argument('--external', metavar='NAME',
                        help='write the start snapshot to NAME next to the output and refer to it')
    args = parser.parse_args()

    with open(args.infile, 'rb') as f:
        data = f.read()

    creator = snapshot = None
    tstates = frames = None
    for block_id, raw in blocks(data):
        if block_id == 0x10 and creator is None:
            creator = raw
        elif block_id == 0x30 and snapshot is None:
            snapshot = snapshot_image(raw)
        elif block_id == 0x80 and frames is None:
            tstates, frames = frames_of(raw)
    if snapshot is None or frames is None:
        sys.exit('no start snapshot or no input block')
    if args.frames:
        frames = frames[:args.frames]
        # A repeat at the start would lose what it repeats
        if frames and struct.unpack('<H', frames[0][2:4])[0] == 0xFFFF:
            sys.exit('the kept range starts with a repeat frame')

    out = bytearray(b'RZX!\x00\x0d\x00\x00\x00\x00')
    if creator:
        out += creator
    extension, image = snapshot
    if args.external:
        target = os.path.join(os.path.dirname(os.path.abspath(args.outfile)), args.external)
        with open(target, 'wb') as f:
            f.write(image)
        descriptor = struct.pack('<I', zlib.crc32(image)) + args.external.encode('ascii') + b'\0'
        body = struct.pack('<I', 1) + extension.encode('ascii').ljust(4, b'\0') + \
            struct.pack('<I', len(descriptor)) + descriptor
    else:
        packed = zlib.compress(image, 9)
        body = struct.pack('<I', 2) + extension.encode('ascii').ljust(4, b'\0') + \
            struct.pack('<I', len(image)) + packed
    out += block(0x30, body)
    stream = zlib.compress(b''.join(frames), 9)
    out += block(0x80, struct.pack('<IBII', len(frames), 0, tstates, 2) + stream)

    with open(args.outfile, 'wb') as f:
        f.write(out)
    print(f'{args.outfile}: {len(frames)} frames, {extension} snapshot'
          f'{" external " + args.external if args.external else ""}, {len(out)} bytes')


if __name__ == '__main__':
    main()
