#!/usr/bin/env python3
"""Cut a small test image out of the 2 GB PQ-DOS hard disk image (Karabas Pro, 2023-09).

The image is an MBR disk with one FAT16 partition (starts at sector 2048, 64 sectors per cluster). Only about 50
clusters belong to what PQ-DOS and DOS Navigator need: the root files, DN and DOS. This keeps those, deletes the
other root entries (their FAT chains are zeroed in both FATs and their clusters cleared) and cuts the file after the
highest cluster kept: 2.9 MB instead of 2 GB, and the partition table, boot sector and FAT stay as they were (the
partition still claims 1936 MB; nothing is ever read beyond the cut, and the guest sees the free space the real
disk had). Nothing is moved, so the result stays the original's own layout.

usage: prune.py <pqdos_image> <out.img> [name,name,..]   (names to delete from the root; default: the games, demos, ..)
"""
import struct
import sys

from fat16 import Fat16, entries

PARTITION_LBA = 2048
DEFAULT_DROP = 'DEMOS,GAMES,KART,UTILS,ADJ,DEMO,DOS_OLD,SECOND,SYSTEM~1'


def tree(fat, cluster):
    """Every cluster of a directory and of everything under it"""
    out = list(fat.chain(cluster))
    for _, name, attr, first, _size in entries(fat.readDirectory(cluster)):
        if name in ('.', '..'):
            continue
        out += tree(fat, first) if attr & 0x10 else fat.chain(first)
    return out


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    source, target = sys.argv[1], sys.argv[2]
    drop = set((sys.argv[3] if len(sys.argv) == 4 else DEFAULT_DROP).split(','))
    fat = Fat16(source, PARTITION_LBA)
    keep, gone = set(), set()
    image = bytearray(open(source, 'rb').read(fat.clusterOffset(2 + 2400) + fat.clusterBytes))
    for offset, name, attr, first, _size in entries(fat.readRoot()):
        if attr & 0x08 or first == 0:
            continue                                        # the volume label, empty files
        clusters = tree(fat, first) if attr & 0x10 else fat.chain(first)
        if name in drop:
            gone.update(clusters)
            image[fat.base + fat.rootSector * 512 + offset] = 0xE5
        else:
            keep.update(clusters)
    highest = max(keep)
    for k in range(fat.fats):
        fatStart = fat.base + (fat.reserved + k * fat.fatSectors) * 512
        for cluster in gone:
            struct.pack_into('<H', image, fatStart + 2 * cluster, 0)
    for cluster in gone:
        start = fat.clusterOffset(cluster)
        if start + fat.clusterBytes <= len(image):
            image[start:start + fat.clusterBytes] = bytes(fat.clusterBytes)
    end = fat.clusterOffset(highest) + fat.clusterBytes
    open(target, 'wb').write(bytes(image[:end]))
    print(f'kept {len(keep)} clusters (highest {highest}), dropped {len(gone)}; {end} bytes = {end / 1048576:.2f} MB')


if __name__ == '__main__':
    main()
