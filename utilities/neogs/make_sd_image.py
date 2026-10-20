#!/usr/bin/env python3
"""Build deterministic SD card images (raw, 512-byte sectors) for NeoGS tests.

The images follow the Microsoft FAT specification (fatgen103), because the
NeoGS loader identifies the FAT type from the MBR partition type or, on a card
without a partition table, from the cluster count (loader_ngs.a80 WC_FAT):

  FAT12 < 4085 clusters <= FAT16 < 65525 clusters <= FAT32

Everything that would vary between runs (timestamps, volume serial) is fixed,
so the same inputs always give byte-identical images.

Usage:
  make_sd_image.py OUT.img --fat 16|32 [--size-mb N] [--no-mbr]
                   [--file NAME.EXT=path ...]

The tests do not use this script: they build the same layout at run time
(core/tests/_helpers/fatimagebuilder.h). This script is for making cards by
hand, e.g. for [NGS] SDCardImage.
"""

import argparse
import struct
import sys
from pathlib import Path

SECTOR = 512
PART_START = 2048          # 1 MiB alignment, as modern partitioning tools do
FIXED_SERIAL = 0x4E47530A  # "NGS\n"
# 2026-09-27 12:00:00 in FAT date/time encoding
FAT_DATE = ((2026 - 1980) << 9) | (9 << 5) | 27
FAT_TIME = (12 << 11) | (0 << 5) | 0


def short_name(name: str) -> bytes:
    """8.3 name, upper case, space padded."""
    base, _, ext = name.upper().partition(".")
    if not base or len(base) > 8 or len(ext) > 3:
        raise ValueError(f"not an 8.3 name: {name}")
    return base.ljust(8).encode("ascii") + ext.ljust(3).encode("ascii")


def dir_entry(name11: bytes, attr: int, cluster: int, size: int) -> bytes:
    return struct.pack(
        "<11sBBBHHHHHHHI",
        name11, attr, 0, 0,
        FAT_TIME, FAT_DATE, FAT_DATE,
        (cluster >> 16) & 0xFFFF,
        FAT_TIME, FAT_DATE,
        cluster & 0xFFFF, size,
    )


def layout(total_sectors: int, fat: int):
    """Pick cluster size and FAT size so the cluster count lands in range."""
    reserved = 1 if fat == 16 else 32
    root_entries = 512 if fat == 16 else 0
    root_sectors = (root_entries * 32 + SECTOR - 1) // SECTOR
    # The largest cluster that keeps the count in the type's range, as
    # formatting tools do (some Z80 FAT code mishandles 1 sector per cluster)
    for spc in (64, 32, 16, 8, 4, 2, 1):
        # iterate FAT size to a fixed point
        fat_sectors = 1
        while True:
            data = total_sectors - reserved - 2 * fat_sectors - root_sectors
            clusters = data // spc
            entry_bytes = 2 if fat == 16 else 4
            need = ((clusters + 2) * entry_bytes + SECTOR - 1) // SECTOR
            if need <= fat_sectors:
                break
            fat_sectors = need
        lo, hi = (4085, 65524) if fat == 16 else (65525, 0x0FFFFFF5)
        if lo <= clusters <= hi:
            return reserved, root_entries, root_sectors, spc, fat_sectors, clusters
    raise ValueError(f"size {total_sectors} sectors cannot hold a valid FAT{fat}")


def build_volume(total_sectors: int, fat: int, hidden: int, files):
    reserved, root_entries, root_sectors, spc, fat_sectors, clusters = layout(total_sectors, fat)
    vol = bytearray(total_sectors * SECTOR)
    cluster_bytes = spc * SECTOR
    fat_start = reserved
    root_start = reserved + 2 * fat_sectors            # FAT16 fixed root
    data_start = root_start + root_sectors

    def cluster_offset(c):
        return (data_start + (c - 2) * spc) * SECTOR

    fat_table = [0] * (clusters + 2)
    eoc = 0xFFFF if fat == 16 else 0x0FFFFFFF
    fat_table[0] = (0xFFF8 if fat == 16 else 0x0FFFFFF8)
    fat_table[1] = eoc
    next_free = 2

    def alloc(nbytes):
        nonlocal next_free
        count = max(1, (nbytes + cluster_bytes - 1) // cluster_bytes)
        first = next_free
        for i in range(count):
            c = first + i
            fat_table[c] = c + 1 if i < count - 1 else eoc
        next_free += count
        if next_free > clusters + 2:
            raise ValueError("image too small for the files")
        return first

    root_cluster = 0
    if fat == 32:
        root_cluster = alloc(cluster_bytes)

    # volume label entry, then the files
    entries = [dir_entry(b"NEOGS TEST ", 0x08, 0, 0)]
    for name, data in files:
        first = alloc(len(data))
        off = cluster_offset(first)
        vol[off:off + len(data)] = data
        entries.append(dir_entry(short_name(name), 0x20, first, len(data)))
    root = b"".join(entries)
    if fat == 16:
        if len(entries) > root_entries:
            raise ValueError("too many files for the root directory")
        off = root_start * SECTOR
    else:
        if len(root) > cluster_bytes:
            raise ValueError("root directory needs more than one cluster")
        off = cluster_offset(root_cluster)
    vol[off:off + len(root)] = root

    # boot sector (BPB)
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x58\x90" if fat == 32 else b"\xEB\x3C\x90"
    bs[3:11] = b"UNREALNG"
    struct.pack_into("<HBHBHHBHHHII", bs, 11,
                     SECTOR, spc, reserved, 2, root_entries,
                     total_sectors if total_sectors < 0x10000 else 0,
                     0xF8,
                     fat_sectors if fat == 16 else 0,
                     63, 255, hidden,
                     0 if total_sectors < 0x10000 else total_sectors)
    if fat == 16:
        struct.pack_into("<BBBI11s8s", bs, 36, 0x80, 0, 0x29, FIXED_SERIAL,
                         b"NEOGS TEST ", b"FAT16   ")
    else:
        struct.pack_into("<IHHIHH12s", bs, 36, fat_sectors, 0, 0, root_cluster, 1, 6, bytes(12))
        struct.pack_into("<BBBI11s8s", bs, 64, 0x80, 0, 0x29, FIXED_SERIAL,
                         b"NEOGS TEST ", b"FAT32   ")
    bs[510:512] = b"\x55\xAA"
    vol[0:SECTOR] = bs

    if fat == 32:
        # FSInfo at sector 1, backup boot sector at 6 (+ its FSInfo at 7)
        fsi = bytearray(SECTOR)
        struct.pack_into("<I", fsi, 0, 0x41615252)
        struct.pack_into("<I", fsi, 484, 0x61417272)
        struct.pack_into("<II", fsi, 488, clusters + 2 - next_free, next_free)
        struct.pack_into("<I", fsi, 508, 0xAA550000)
        vol[SECTOR:2 * SECTOR] = fsi
        vol[6 * SECTOR:7 * SECTOR] = bs
        vol[7 * SECTOR:8 * SECTOR] = fsi

    fmt = "<H" if fat == 16 else "<I"
    raw = b"".join(struct.pack(fmt, v) for v in fat_table)
    for copy in range(2):
        off = (fat_start + copy * fat_sectors) * SECTOR
        vol[off:off + len(raw)] = raw
    return vol, spc, clusters


def build_image(size_mb: int, fat: int, mbr: bool, files):
    total = size_mb * 1024 * 1024 // SECTOR
    if not mbr:
        vol, spc, clusters = build_volume(total, fat, 0, files)
        return vol, spc, clusters
    part_sectors = total - PART_START
    vol, spc, clusters = build_volume(part_sectors, fat, PART_START, files)
    img = bytearray(PART_START * SECTOR) + vol
    # FAT16 is typed #06 at any size: strictly, volumes under 32 MB are #04,
    # but real cards carry #06 and Neo Player Light accepts only 1/6/B/C/E
    ptype = 0x0C if fat == 32 else 0x06
    entry = struct.pack("<B3sB3sII", 0x00, b"\xFE\xFF\xFF", ptype, b"\xFE\xFF\xFF",
                        PART_START, part_sectors)
    img[446:446 + 16] = entry
    struct.pack_into("<I", img, 440, FIXED_SERIAL)
    img[510:512] = b"\x55\xAA"
    return img, spc, clusters


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?", help="output image")
    ap.add_argument("--fat", type=int, choices=(16, 32))
    ap.add_argument("--size-mb", type=int)
    ap.add_argument("--no-mbr", action="store_true")
    ap.add_argument("--file", action="append", default=[], metavar="NAME=PATH")
    args = ap.parse_args()

    if not (args.out and args.fat and args.size_mb):
        ap.error("OUT, --fat and --size-mb are required")
    files = []
    for spec in args.file:
        name, _, path = spec.partition("=")
        files.append((name, Path(path).read_bytes()))
    img, spc, clusters = build_image(args.size_mb, args.fat, not args.no_mbr, files)
    Path(args.out).write_bytes(img)
    print(f"{args.out}: FAT{args.fat}, {spc} sectors/cluster, {clusters} clusters")


if __name__ == "__main__":
    sys.exit(main())
