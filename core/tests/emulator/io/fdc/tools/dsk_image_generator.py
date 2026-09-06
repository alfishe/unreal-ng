#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DSK / EDSK fixture generator (Amstrad CPC / ZX Spectrum +3 disk images)

Generates the two synthetic disk images used by core/tests/loaders/disk/loader_dsk_test.cpp:

  plus3-blank.dsk      Standard ("MV - CPC") layout. A freshly formatted +3DOS disk: 40 tracks, 1 side,
                       9 x 512-byte sectors numbered 1..9, GAP#3 0x2A, filler 0xE5. Track 0 sector 1 carries a
                       valid +3DOS disk specification (16 bytes) whose checksum byte makes the sector sum to 3 mod 256.

  edsk-protected.dsk   Extended ("EXTENDED CPC DSK") layout with one feature per track:
                       track 0  CPC data format, sectors 0xC1..0xC9, GAP#3 0x4E
                       track 1  sector 3 written with a deleted data mark (ST2 bit 6)
                       track 2  sector 2 with a data CRC error (ST1 bit 5 + ST2 bit 5),
                                sector 4 with an ID CRC error (ST1 bit 5 alone)
                       track 3  sector 5 is ID-only (ST1 bit 2, actual data length 0)
                       track 4  sector 6 is weak: two copies stored, differing in 3 bytes
                       track 5  unformatted (size 0 in the track size table)
                       track 6  plain 1..9 track (checks the offsets after the unformatted track)

No copyrighted data is used: every data byte is generated from a deterministic pattern.

Usage:
    python3 dsk_image_generator.py                # writes both files into <repo>/testdata/loaders/dsk/
    python3 dsk_image_generator.py --out-dir DIR  # writes them into DIR

Only the Python standard library is required.
"""

import argparse
import os
import struct
from dataclasses import dataclass, field
from typing import List

STANDARD_SIGNATURE = b"MV - CPCEMU Disk-File\r\nDisk-Info\r\n"
EXTENDED_SIGNATURE = b"EXTENDED CPC DSK File\r\nDisk-Info\r\n"
TRACK_SIGNATURE = b"Track-Info\r\n"
CREATOR = b"dsk_image_gen "  # 14 bytes

DISK_INFO_SIZE = 256
TRACK_INFO_SIZE = 256

# uPD765 status bits
ST1_NO_DATA = 0x04
ST1_DATA_ERROR = 0x20
ST2_DATA_ERROR_IN_DATA = 0x20
ST2_CONTROL_MARK = 0x40

MODE_MFM = 2
RATE_SD_DD = 1


@dataclass
class Sector:
    c: int
    h: int
    r: int
    n: int
    data: bytes           # Bytes stored in the file (may hold several copies, or nothing for ID-only)
    st1: int = 0
    st2: int = 0

    @property
    def size(self) -> int:
        return 128 << self.n


@dataclass
class Track:
    cylinder: int
    side: int
    sectors: List[Sector] = field(default_factory=list)
    gap3: int = 0x2A
    filler: int = 0xE5
    unformatted: bool = False


def pattern(track: int, index: int, size: int) -> bytes:
    """Deterministic, non-copyrighted data pattern that differs per track and sector"""
    return bytes((track * 37 + index * 11 + i) & 0xFF for i in range(size))


def track_info(track: Track, extended: bool) -> bytes:
    info = bytearray(TRACK_INFO_SIZE)
    info[0:len(TRACK_SIGNATURE)] = TRACK_SIGNATURE
    info[0x10] = track.cylinder
    info[0x11] = track.side
    if extended:
        info[0x12] = RATE_SD_DD
        info[0x13] = MODE_MFM
    info[0x14] = track.sectors[0].n if track.sectors else 2
    info[0x15] = len(track.sectors)
    info[0x16] = track.gap3
    info[0x17] = track.filler
    for i, s in enumerate(track.sectors):
        off = 0x18 + 8 * i
        info[off + 0] = s.c
        info[off + 1] = s.h
        info[off + 2] = s.r
        info[off + 3] = s.n
        info[off + 4] = s.st1
        info[off + 5] = s.st2
        if extended:
            struct.pack_into("<H", info, off + 6, len(s.data))
    return bytes(info)


def track_block(track: Track, extended: bool) -> bytes:
    block = bytearray(track_info(track, extended))
    for s in track.sectors:
        if extended:
            block += s.data
        else:
            # Standard layout stores exactly 128 << N bytes per sector
            block += s.data[:s.size].ljust(s.size, bytes([track.filler]))
    if extended:
        # Track blocks are multiples of 256 bytes (the size table stores size / 256)
        pad = (-len(block)) % 256
        block += bytes(pad)
    return bytes(block)


def build_standard(cylinders: int, sides: int, tracks: List[Track]) -> bytes:
    blocks = [track_block(t, extended=False) for t in tracks]
    track_size = len(blocks[0])
    assert all(len(b) == track_size for b in blocks), "standard DSK requires equal track sizes"

    header = bytearray(DISK_INFO_SIZE)
    header[0:len(STANDARD_SIGNATURE)] = STANDARD_SIGNATURE
    header[0x22:0x22 + 14] = CREATOR
    header[0x30] = cylinders
    header[0x31] = sides
    struct.pack_into("<H", header, 0x32, track_size)
    return bytes(header) + b"".join(blocks)


def build_extended(cylinders: int, sides: int, tracks: List[Track]) -> bytes:
    header = bytearray(DISK_INFO_SIZE)
    header[0:len(EXTENDED_SIGNATURE)] = EXTENDED_SIGNATURE
    header[0x22:0x22 + 14] = CREATOR
    header[0x30] = cylinders
    header[0x31] = sides

    body = bytearray()
    for i, t in enumerate(tracks):
        if t.unformatted:
            header[0x34 + i] = 0
            continue
        block = track_block(t, extended=True)
        header[0x34 + i] = len(block) // 256
        body += block
    return bytes(header) + bytes(body)


def plus3_boot_sector() -> bytes:
    """+3DOS disk specification for a standard 180K disk, sector sum == 3 (mod 256)"""
    spec = bytes([
        0x00,  # disk type: standard PCW range DD SS ST
        0x00,  # sidedness: single sided
        0x28,  # tracks per side: 40
        0x09,  # sectors per track
        0x02,  # log2(sector size) - 7: 512 bytes
        0x01,  # reserved tracks
        0x03,  # log2(block size) - 7: 1024 bytes
        0x02,  # directory blocks
        0x2A,  # gap length (read / write)
        0x52,  # gap length (format)
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,  # checksum, patched below
    ])
    sector = bytearray(spec + bytes([0xE5]) * (512 - len(spec)))
    total = sum(sector[:15]) + sum(sector[16:])
    sector[15] = (3 - total) & 0xFF
    assert sum(sector) % 256 == 3
    return bytes(sector)


def make_plus3_blank() -> bytes:
    tracks = []
    for cyl in range(40):
        sectors = []
        for r in range(1, 10):
            data = plus3_boot_sector() if (cyl == 0 and r == 1) else bytes([0xE5]) * 512
            sectors.append(Sector(c=cyl, h=0, r=r, n=2, data=data))
        tracks.append(Track(cylinder=cyl, side=0, sectors=sectors, gap3=0x2A, filler=0xE5))
    return build_standard(40, 1, tracks)


def make_edsk_protected() -> bytes:
    tracks = []

    # Track 0: CPC data format, sector IDs 0xC1..0xC9
    t0 = Track(cylinder=0, side=0, gap3=0x4E, filler=0xE5)
    for i in range(9):
        t0.sectors.append(Sector(c=0, h=0, r=0xC1 + i, n=2, data=pattern(0, i, 512)))
    tracks.append(t0)

    # Track 1: sector 3 has a deleted data mark
    t1 = Track(cylinder=1, side=0)
    for i in range(9):
        s = Sector(c=1, h=0, r=i + 1, n=2, data=pattern(1, i, 512))
        if s.r == 3:
            s.st2 |= ST2_CONTROL_MARK
        t1.sectors.append(s)
    tracks.append(t1)

    # Track 2: sector 2 data CRC error (ST1.DE + ST2.DD), sector 4 ID CRC error (ST1.DE alone)
    t2 = Track(cylinder=2, side=0)
    for i in range(9):
        s = Sector(c=2, h=0, r=i + 1, n=2, data=pattern(2, i, 512))
        if s.r == 2:
            s.st1 |= ST1_DATA_ERROR
            s.st2 |= ST2_DATA_ERROR_IN_DATA
        if s.r == 4:
            s.st1 |= ST1_DATA_ERROR
        t2.sectors.append(s)
    tracks.append(t2)

    # Track 3: sector 5 is ID-only (no data field)
    t3 = Track(cylinder=3, side=0)
    for i in range(9):
        s = Sector(c=3, h=0, r=i + 1, n=2, data=pattern(3, i, 512))
        if s.r == 5:
            s.st1 |= ST1_NO_DATA
            s.data = b""
        t3.sectors.append(s)
    tracks.append(t3)

    # Track 4: sector 6 is weak - two copies that differ at offsets 10, 200 and 511
    t4 = Track(cylinder=4, side=0)
    for i in range(9):
        s = Sector(c=4, h=0, r=i + 1, n=2, data=pattern(4, i, 512))
        if s.r == 6:
            first = bytearray(s.data)
            second = bytearray(s.data)
            for off in (10, 200, 511):
                second[off] ^= 0xFF
            s.data = bytes(first) + bytes(second)
        t4.sectors.append(s)
    tracks.append(t4)

    # Track 5: unformatted
    tracks.append(Track(cylinder=5, side=0, unformatted=True))

    # Track 6: plain track after the unformatted one
    t6 = Track(cylinder=6, side=0)
    for i in range(9):
        t6.sectors.append(Sector(c=6, h=0, r=i + 1, n=2, data=pattern(6, i, 512)))
    tracks.append(t6)

    return build_extended(len(tracks), 1, tracks)


def default_out_dir() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    # core/tests/emulator/io/fdc/tools -> repository root is six levels up
    root = os.path.abspath(os.path.join(here, *([".."] * 6)))
    return os.path.join(root, "testdata", "loaders", "dsk")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate DSK / EDSK test fixtures")
    parser.add_argument("--out-dir", default=default_out_dir(), help="output directory (default: <repo>/testdata/loaders/dsk)")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    outputs = {
        "plus3-blank.dsk": make_plus3_blank(),
        "edsk-protected.dsk": make_edsk_protected(),
    }
    for name, content in outputs.items():
        path = os.path.join(args.out_dir, name)
        with open(path, "wb") as f:
            f.write(content)
        print("wrote %s (%d bytes)" % (path, len(content)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
