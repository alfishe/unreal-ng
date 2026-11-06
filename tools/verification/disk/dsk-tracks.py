#!/usr/bin/env python3
"""Read a DSK / Extended DSK image without the emulator: per track the sector IDs, size codes, stored lengths and
ST1 / ST2, with the tracks that are not a plain format flagged. For a track whose sectors store more than their field
(a uPD765 read with a large N running on over the fields after it, e.g. CPDRead protection tracks), the ID and data
marks found inside each long dump show where the sectors really lie on the disk: the distance between them, the real
field length and GAP#3, which is what the loader rebuilds.

    python3 tools/verification/disk/dsk-tracks.py game.dsk               # summary, odd tracks flagged
    python3 tools/verification/disk/dsk-tracks.py game.dsk --track 39    # one track in detail
    python3 tools/verification/disk/dsk-tracks.py game.dsk --track 39 --side 1 --hex 6

Only reads the file; run it with python3 -I on images from outside the repository.
"""
import argparse
import struct
import sys

NOMINAL_TRACK = 6250  # MFM bytes per revolution at 250 kbit/s, 300 rpm
SECTOR_OVERHEAD = 62  # sync, A1 x3, ID, CRC, GAP#2, sync, A1 x3, DAM, data CRC (IBM layout without GAP#3)


def cpdread_slots(data):
    """CPDRead (v3.24) writes a 128-byte sector into a 256-byte slot of the file, its data then zero padding; the
    Extended DSK format has the sector data back to back. True for an image with that creator."""
    return data.startswith(b'EXTENDED CPC DSK File') and data[0x22:0x30].startswith(b'CPDRead')


def tracks(data):
    """Yields (cylinder, side, header bytes, [(c, h, r, n, st1, st2, stored bytes)])"""
    extended = data.startswith(b'EXTENDED CPC DSK File')
    if not extended and not data.startswith(b'MV - CPC'):
        raise SystemExit('not a DSK image')
    slots = cpdread_slots(data)
    count, sides = data[0x30], data[0x31]
    standard_size = struct.unpack('<H', data[0x32:0x34])[0]
    offset = 0x100
    for index in range(count * sides):
        size = data[0x34 + index] * 256 if extended else standard_size
        if size == 0:
            continue
        header = data[offset:offset + 0x100]
        if header[:10] != b'Track-Info':
            raise SystemExit(f'track {index}: no Track-Info at {offset:#x}')
        sectors, src = [], offset + 0x100
        for i in range(header[0x15]):
            c, h, r, n, st1, st2 = header[0x18 + 8 * i:0x1e + 8 * i]
            stored = struct.unpack('<H', header[0x1e + 8 * i:0x20 + 8 * i])[0] if extended else \
                (0x1800 if n >= 6 else 128 << n)
            # A CPDRead slot past the track block (a sector without a data field) holds nothing
            dump = data[src:src + stored] if src + stored <= offset + size or not slots else data[src:offset + size]
            sectors.append((c, h, r, n, st1, st2, dump))
            src += 256 if slots and stored == 128 else stored
        yield index // sides, index % sides, header, sectors
        offset += size


def marks_in(dump):
    """(offset of the mark byte, 'ID c h r n' / 'DAM' / 'DDAM') found after an A1 A1 A1 sync inside a dump"""
    found = []
    at = dump.find(b'\xa1\xa1\xa1')
    while 0 <= at < len(dump) - 3:
        mark = dump[at + 3]
        if mark == 0xFE and at + 8 <= len(dump):
            found.append((at + 3, 'ID %d %d %d %d' % tuple(dump[at + 4:at + 8])))
        elif mark in (0xFB, 0xF8):
            found.append((at + 3, 'DAM' if mark == 0xFB else 'DDAM'))
        at = dump.find(b'\xa1\xa1\xa1', at + 3)
    return found


def odd(sectors):
    """Why a track is not a plain format, or ''"""
    why = []
    stored = sum(len(s[6]) for s in sectors)
    if stored > NOMINAL_TRACK:
        why.append(f'stores {stored} bytes')
    if any(s[3] >= 6 for s in sectors):
        why.append('N >= 6')
    if any(len(s[6]) > (128 << min(s[3], 7)) for s in sectors):
        why.append('copies (weak)')
    if any(s[4] or s[5] for s in sectors):
        why.append('ST1/ST2 set')
    if len({s[0] for s in sectors}) > 1 or len({s[3] for s in sectors}) > 1:
        why.append('mixed C/N')
    return ', '.join(why)


def layout(sectors):
    """Distance data start -> next ID found in the long dumps, as {distance: [sector indexes]}"""
    seen = {}
    for i in range(len(sectors) - 1):
        c, h, r, n = sectors[i + 1][:4]
        at = sectors[i][6].find(b'\xa1\xa1\xa1\xfe' + bytes((c, h, r, n)))
        if at >= 0:
            seen.setdefault(at + 3, []).append(i)
    return seen


def detail(cylinder, side, header, sectors, hex_rows):
    print(f'cylinder {cylinder} side {side}: {len(sectors)} sectors, track-info N {header[0x14]} GAP#3 {header[0x16]} '
          f'filler {header[0x17]:#04x}')
    for i, (c, h, r, n, st1, st2, dump) in enumerate(sectors):
        print(f'  {i:2} C {c:3} H {h:3} R {r:3} N {n:2}  st1 {st1:#04x} st2 {st2:#04x}  stored {len(dump):5}')
        if hex_rows:
            for row in range(0, min(len(dump), hex_rows * 16), 16):
                print(f'       {row:5}  {dump[row:row + 16].hex(" ")}')
        if len(dump) > 128 << (n & 3) or len(dump) > 512:
            for offset, what in marks_in(dump):
                print(f'       @{offset:5} {what}')
    seen = layout(sectors)
    if not seen:
        return
    if len(seen) > 1:
        print(f'  next-ID distances disagree: {seen} (the loader cuts the long fields instead)')
        return
    distance = next(iter(seen))
    witness = sectors[seen[distance][0]][6]
    sync = distance - 3 - 12
    gap_start = sync
    while gap_start > 0 and witness[gap_start - 1] == 0x4E:
        gap_start -= 1
    field, gap3 = gap_start - 2, sync - gap_start
    pitch = field + gap3 + SECTOR_OVERHEAD
    print(f'  on the disk: {field}-byte fields, GAP#3 {gap3}, sectors {pitch} bytes apart, '
          f'{len(sectors)} x {pitch} = {len(sectors) * pitch} of {NOMINAL_TRACK} bytes')
    if len(sectors) * pitch > NOMINAL_TRACK:
        print('  longer than one revolution: the dumps do not start at their data fields, so the image is not '
              'stored the way this script reads it; the loader keeps the dumped bytes on a longer track')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('image')
    parser.add_argument('--track', type=int, help='show this cylinder in detail')
    parser.add_argument('--side', type=int, default=0)
    parser.add_argument('--hex', type=int, default=0, metavar='ROWS', help='hex rows of each dump in the detail view')
    parser.add_argument('--all', action='store_true', help='list every track, not only the odd ones')
    args = parser.parse_args()

    with open(args.image, 'rb') as f:
        data = f.read()
    if cpdread_slots(data):
        print('CPDRead image: 128-byte sectors read from 256-byte slots')
    shown = 0
    for cylinder, side, header, sectors in tracks(data):
        if args.track is not None:
            if (cylinder, side) == (args.track, args.side):
                detail(cylinder, side, header, sectors, args.hex)
                return 0
            continue
        why = odd(sectors)
        if why or args.all:
            ids = ' '.join(str(s[2]) for s in sectors)
            print(f'{cylinder:3}/{side}  {len(sectors):2} sectors  N {",".join(str(n) for n in sorted({s[3] for s in sectors}))}'
                  f'  R {ids}  {why}')
            shown += 1
    if args.track is not None:
        print(f'cylinder {args.track} side {args.side} is not in the image')
        return 1
    if not shown:
        print('every track is a plain format')
    return 0


if __name__ == '__main__':
    sys.exit(main())
