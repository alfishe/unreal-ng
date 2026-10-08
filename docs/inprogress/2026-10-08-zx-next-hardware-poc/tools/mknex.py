#!/usr/bin/env python3
"""Wrap a flat binary (origin #8000, at most 16K) into a NEX V1.2 file for the ZX Spectrum Next.

  mknex.py program.bin program.nex

Header layout (NEX V1.2, checked against the reference loaders of the Next distribution and jnext):
  0 "Next", 4 "V1.2", 8 RAM required (0 = 768K), 9 number of banks, 10 screen flags (0 = none),
  11 border, 12 SP, 14 PC, 16 extra files, 18..129 one flag per 16K bank 0..111 (1 = in the file),
  130 loading bar (0), 131 bar colour, 132 delay per bank, 133 start delay, 134 preserve NextREGs (0 = reset),
  135..137 core version required, 138 hires colour, 139 entry bank (mapped at #C000), 140 file handle (0 = close).
Banks are stored in the order 5, 2, 0, 1, 3, 4, 6, 7, ...; bank 2 is #8000-#BFFF, so a program at #8000 is bank 2.
"""
import struct
import sys

ORG = 0x8000
SP = 0xBFF0
BANK = 16384


def main():
    src, dst = sys.argv[1], sys.argv[2]
    code = open(src, 'rb').read()
    if len(code) > BANK:
        raise SystemExit(f'{src}: {len(code)} bytes do not fit one 16K bank')
    hdr = bytearray(512)
    hdr[0:4] = b'Next'
    hdr[4:8] = b'V1.2'
    hdr[8] = 0                      # 768K is enough
    hdr[9] = 1                      # one bank in the file
    hdr[10] = 0                     # no loading screen
    hdr[11] = 0                     # border black
    struct.pack_into('<H', hdr, 12, SP)
    struct.pack_into('<H', hdr, 14, ORG)
    struct.pack_into('<H', hdr, 16, 0)
    hdr[18 + 2] = 1                 # bank 2 present
    hdr[134] = 0                    # reset the NextREG state, then run
    hdr[139] = 0                    # entry bank at #C000
    hdr[140] = 0
    hdr[141] = 0
    open(dst, 'wb').write(bytes(hdr) + code + bytes(BANK - len(code)))


if __name__ == '__main__':
    main()
