#!/usr/bin/env python3
"""Build the CHD test fixtures in testdata/media/chd/ with MAME's chdman.

The source disk (mixed.img, 256 KiB = 64 hunks of 4 KiB) is generated here so every
hunk codec has data it wins on:

    hunks  0-3   zeros                       (self references after the first)
    hunks  4-11  English-like text           (zlib / lzma / huff / zstd)
    hunks 12-23  16-bit stereo sine, LE      (flac, byte order 'L')
    hunks 24-27  16-bit stereo sine, BE      (flac, byte order 'B')
    hunks 28-35  random bytes                (stored as is)
    hunks 36-43  a copy of hunks 4-11        (self references)
    hunks 44-51  bytes from a skewed alphabet (huff)
    hunks 52-63  a counting pattern

chdman then writes one CHD per codec (none, zlib, lzma, huff, flac, zstd), chdman's
default set (lzma, zlib, huff, flac), and a child of the default CHD whose data
differs in two hunks: 16 bytes of hunk 5 and hunk 30 zeroed. Geometry: 8 cylinders, 4 heads, 16 sectors.

Usage: python3 tools/chd/make-test-fixtures.py --chdman <path to chdman>
The output is deterministic for the image; chdman's compressed output depends on
its version (the fixtures were made with chdman 0.289).
"""

import argparse
import math
import random
import struct
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
OUT = REPO_ROOT / "testdata" / "media" / "chd"
HUNK = 4096
CHS = "8,4,16"

WORDS = ("the quick brown fox jumps over a lazy dog while the spectrum loads its tape and "
         "the disk spins at three hundred revolutions per minute so every sector comes by").split()


def text_hunks(rng, count):
    out = bytearray()
    while len(out) < count * HUNK:
        out += (" ".join(rng.choice(WORDS) for _ in range(12)) + ".\r\n").encode()
    return bytes(out[:count * HUNK])


def sine_hunks(count, big_endian, phase):
    frames = count * HUNK // 4
    fmt = ">hh" if big_endian else "<hh"
    out = bytearray()
    for i in range(frames):
        left = int(12000 * math.sin(2 * math.pi * (i + phase) / 97.0))
        right = int(9000 * math.sin(2 * math.pi * (i + phase) / 61.0))
        out += struct.pack(fmt, left, right)
    return bytes(out)


def build_image():
    rng = random.Random(1983)
    text = text_hunks(rng, 8)
    parts = [
        bytes(4 * HUNK),
        text,
        sine_hunks(12, False, 0),
        sine_hunks(4, True, 1000),
        bytes(rng.getrandbits(8) for _ in range(8 * HUNK)),
        text,
        bytes(min(255, int(rng.expovariate(0.35))) for _ in range(8 * HUNK)),
        bytes((i // 3) & 0xFF for i in range(12 * HUNK)),
    ]
    image = b"".join(parts)
    assert len(image) == 64 * HUNK
    return image


def run(chdman, *args):
    subprocess.run([chdman, *args], check=True, stdout=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--chdman", required=True)
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    image = build_image()
    (OUT / "mixed.img").write_bytes(image)
    child = bytearray(image)
    child[5 * HUNK:5 * HUNK + 16] = b"CHILD CHANGED IT"
    child[30 * HUNK:31 * HUNK] = bytes(HUNK)
    child_img = OUT / "child.img"  # removed below: the tests rebuild it from mixed.img
    child_img.write_bytes(bytes(child))

    variants = {"none": "none", "zlib": "zlib", "lzma": "lzma", "huff": "huff", "flac": "flac", "zstd": "zstd",
                "default": "lzma,zlib,huff,flac"}
    for name, codecs in variants.items():
        run(args.chdman, "createhd", "-f", "-i", str(OUT / "mixed.img"), "-o", str(OUT / f"mixed-{name}.chd"),
            "-chs", CHS, "-c", codecs)
    run(args.chdman, "createhd", "-f", "-i", str(child_img), "-o", str(OUT / "child-of-default.chd"),
        "-op", str(OUT / "mixed-default.chd"))
    child_img.unlink()
    print(f"fixtures written to {OUT}")


if __name__ == "__main__":
    main()
