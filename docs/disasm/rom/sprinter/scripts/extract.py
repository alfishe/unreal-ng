#!/usr/bin/env python3
"""Slice the Sprinter BIOS 3.04 ROM into the pieces the listings cover.

Input:  data/rom/sprinter/sp2k-3.04.rom (262 144 bytes, CRC32 1729cb5c)
Output (next to this script):
  bios304-p8.bin     ROM page 8 (file offset #20000): the BIOS proper ("EXP")
  bios304-p0.bin     ROM page 0 (file offset #00000): disk drivers + packed SETUP
  bios304-setup.bin  SETUP, unpacked from page 0 (Hrust 1.x stream at page
                     offset #115F; it runs at #8000)
  bios304-stub.bin   page 0 #1000-#115E: the SETUP stub and the Hrust
                     depacker; it runs from RAM at #8000 (page 8 copies it)
  bios304-pc.bin     the first 256 bytes of ROM page #C (file offset #30000):
                     the PLD configuration loader that runs at power-on

Run from this directory: python3 extract.py
"""
import os
import sys
import zlib

import hrust

HERE = os.path.dirname(os.path.abspath(__file__))
ROM = os.path.join(HERE, '..', '..', '..', '..', '..', 'data', 'rom', 'sprinter', 'sp2k-3.04.rom')
PAGE = 0x4000
PACKED_START = 0x115F   # page 0 offset of the "HR" header
PACKED_END = 0x320A     # first #FF filler byte after the packed stream


def main():
    rom = open(ROM, 'rb').read()
    crc = zlib.crc32(rom) & 0xFFFFFFFF
    if len(rom) != 0x40000 or crc != 0x1729CB5C:
        sys.exit(f'unexpected ROM: {len(rom)} bytes, CRC32 {crc:08x} (want 262144 bytes, 1729cb5c)')
    p0 = rom[0:PAGE]
    pieces = {
        'bios304-p8.bin': rom[8 * PAGE:9 * PAGE],
        'bios304-p0.bin': p0,
        'bios304-setup.bin': hrust.depack(p0[PACKED_START:PACKED_END]),
        'bios304-pc.bin': rom[0xC * PAGE:0xC * PAGE + 0x100],
        'bios304-stub.bin': p0[0x1000:PACKED_START],
    }
    for name, data in pieces.items():
        with open(os.path.join(HERE, name), 'wb') as f:
            f.write(data)
        print(f'{name}: {len(data)} bytes, CRC32 {zlib.crc32(data) & 0xFFFFFFFF:08x}')


if __name__ == '__main__':
    main()
