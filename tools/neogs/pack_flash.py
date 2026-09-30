#!/usr/bin/env python3
"""Pack the NeoGS 512 KB flash image from its parts and check it.

The shipped image data/rom/neogs/full_ngs.rom (NeoGS flash v1.11) is exactly:
  #00000  loader_ngs.rom   (the loader, 2,476 bytes)
  #10000  neogs.rom        (main ROM v1.11, 32 KB)
  #70000  bootFPGA.crc     (FPGA boot program + MegaLZ-packed configuration)
  #FF everywhere else, and an 8-byte trailer at the end of each 64 KB block
  that holds a part: 6 ASCII bytes + a little-endian date word (bits 14:9
  year - 2000, 8:5 month, 4:0 day, bit 15 "stable"). Loader command #08
  reads the trailers, so they are protocol, not decoration.

The parts come from the NedoPC `ngs` sources (z80/loader_ngs, z80/main_rom,
z80/bootFPGA00; build_full_rom.bat does the same packing with Win32 tools).
They are kept as binaries in tools/neogs/parts/ (neogs-tdd.md §4.1).

Usage:
  pack_flash.py            rebuild and compare with the shipped image (exit 1 on mismatch)
  pack_flash.py --write    rebuild and overwrite the shipped image
"""

import argparse
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PARTS = Path(__file__).resolve().parent / "parts"
IMAGE = ROOT / "data/rom/neogs/full_ngs.rom"
EXPECTED_SHA256 = "f8087ecde8eb4ed08a90cfde409b24df8cca266e65b2f6053d234ff374d3b18e"

SIZE = 512 * 1024
BLOCK = 64 * 1024

# (offset, part file, trailer name, (day, month, year), stable)
LAYOUT = [
    (0x00000, "loader_ngs.rom", "LOADER", (7, 2, 2026), True),
    (0x10000, "neogs.rom", "ROM   ", (7, 2, 2026), True),
    (0x70000, "bootFPGA.crc", "FPGA  ", (19, 1, 2011), True),
]


def trailer(name: str, date, stable: bool) -> bytes:
    day, month, year = date
    word = ((year - 2000) << 9) | (month << 5) | day | (0x8000 if stable else 0)
    return name.encode("ascii") + struct.pack("<H", word)


def build() -> bytes:
    image = bytearray(b"\xff" * SIZE)
    for offset, part, name, date, stable in LAYOUT:
        data = (PARTS / part).read_bytes()
        if len(data) > BLOCK - 8:
            raise SystemExit(f"{part}: {len(data)} bytes does not fit its 64 KB block")
        image[offset:offset + len(data)] = data
        end = offset + BLOCK - 8
        image[end:end + 8] = trailer(name, date, stable)
    return bytes(image)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true", help="overwrite the shipped image")
    args = ap.parse_args()

    image = build()
    digest = hashlib.sha256(image).hexdigest()
    if args.write:
        IMAGE.parent.mkdir(parents=True, exist_ok=True)
        IMAGE.write_bytes(image)
        print(f"wrote {IMAGE} ({digest})")
        return 0

    shipped = IMAGE.read_bytes()
    if shipped != image:
        first = next(i for i in range(min(len(shipped), len(image))) if shipped[i] != image[i]) \
            if len(shipped) == len(image) else None
        print(f"MISMATCH: packed image differs from {IMAGE}" + (f" (first at #{first:05X})" if first is not None else ""))
        return 1
    if digest != EXPECTED_SHA256:
        print(f"MISMATCH: packed image SHA-256 {digest}, expected {EXPECTED_SHA256}")
        return 1
    print(f"OK: {IMAGE.name} = packed parts, SHA-256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
