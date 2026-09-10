#!/usr/bin/env python3
"""
Synthetic fixture generator for the MGT / IMG and Hobeta loaders (python3, stdlib only).

Produces:
  testdata/loaders/mgt/synthetic.mgt      80 cyl x 2 sides x 10 x 512, cylinder-major (sides interleaved)
  testdata/loaders/mgt/synthetic.img      same content, side-major (all side 0 tracks, then all side 1)
  testdata/loaders/hobeta/hello.$C        3-sector CODE file with a valid header checksum
  testdata/loaders/hobeta/bad-checksum.$C same file with a corrupted checksum

Every MGT sector is deterministic and self-describing:
  byte 0 = cylinder, byte 1 = (side << 4) | sector number (1..10),
  byte i (i >= 2) = (cylinder * 7 + side * 13 + sector * 3 + i) & 0xFF
so track-order tests can verify that each sector landed on the right (cylinder, side).

Usage:
  python3 core/tests/emulator/io/fdc/tools/mgt_hobeta_generator.py [repo-root]
"""

import os
import sys

CYLINDERS = 80
SIDES = 2
SECTORS = 10
SECTOR_SIZE = 512

HOBETA_HEADER_SIZE = 17
TRDOS_SECTOR_SIZE = 256


def mgt_sector(cylinder, side, sector):
    data = bytearray(SECTOR_SIZE)
    data[0] = cylinder
    data[1] = (side << 4) | sector
    for i in range(2, SECTOR_SIZE):
        data[i] = (cylinder * 7 + side * 13 + sector * 3 + i) & 0xFF
    return bytes(data)


def mgt_track(cylinder, side):
    return b"".join(mgt_sector(cylinder, side, n) for n in range(1, SECTORS + 1))


def build_mgt():
    """Cylinder-major, sides interleaved: c0/s0, c0/s1, c1/s0, ..."""
    return b"".join(mgt_track(c, s) for c in range(CYLINDERS) for s in range(SIDES))


def build_img():
    """Side-major: all side 0 tracks, then all side 1 tracks"""
    return b"".join(mgt_track(c, s) for s in range(SIDES) for c in range(CYLINDERS))


def hobeta_checksum(header15):
    total = 0
    for i, b in enumerate(header15[:15]):
        total += b * 257 + i
    return total & 0xFFFF


def build_hobeta(name, ftype, start, length, sectors, body, reserved=0):
    assert len(body) == sectors * TRDOS_SECTOR_SIZE
    header = bytearray(HOBETA_HEADER_SIZE)
    header[0:8] = name.ljust(8).encode("ascii")[:8]
    header[8] = ord(ftype)
    header[9:11] = start.to_bytes(2, "little")
    header[11:13] = length.to_bytes(2, "little")
    header[13] = reserved
    header[14] = sectors
    header[15:17] = hobeta_checksum(header).to_bytes(2, "little")
    return bytes(header) + body


def hello_body(sectors):
    body = bytearray(sectors * TRDOS_SECTOR_SIZE)
    for i in range(len(body)):
        body[i] = (i * 3 + (i >> 8) * 11 + 0x21) & 0xFF
    return bytes(body)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    print("%-52s %8d bytes" % (os.path.relpath(path), len(data)))


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "..", ".."))
    mgt_dir = os.path.join(root, "testdata", "loaders", "mgt")
    hobeta_dir = os.path.join(root, "testdata", "loaders", "hobeta")

    mgt = build_mgt()
    img = build_img()
    assert len(mgt) == CYLINDERS * SIDES * SECTORS * SECTOR_SIZE == 819200
    assert len(img) == len(mgt)
    assert sorted(mgt) == sorted(img)
    write(os.path.join(mgt_dir, "synthetic.mgt"), mgt)
    write(os.path.join(mgt_dir, "synthetic.img"), img)

    sectors = 3
    length = 700                     # Less than 3 x 256: the last sector is partially used, as TR-DOS does
    good = build_hobeta("hello", "C", 0x8000, length, sectors, hello_body(sectors))
    assert len(good) == HOBETA_HEADER_SIZE + sectors * TRDOS_SECTOR_SIZE
    write(os.path.join(hobeta_dir, "hello.$C"), good)

    bad = bytearray(good)
    bad[15] ^= 0x5A                  # Corrupt the low checksum byte only
    write(os.path.join(hobeta_dir, "bad-checksum.$C"), bytes(bad))


if __name__ == "__main__":
    main()
