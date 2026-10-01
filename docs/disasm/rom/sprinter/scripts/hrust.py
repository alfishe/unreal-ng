#!/usr/bin/env python3
"""Hrust 1.x depacker (the format the Sprinter BIOS uses for its packed SETUP).

The Sprinter BIOS ROM page 0 holds the SETUP program packed with Hrust 1.x
(a ZX Spectrum LZ packer).  This is a straight port of the depacker in
mhmt by lvd^NedoPC (`mhmt-depack-hrust.c`, `mhmt-depack.c`; the corpus copy is
`emulators/github/mhmt`), with the "ZX header" variant:

    'H' 'R' <unpacked length, 2 bytes> <packed length, 2 bytes>
    <last 6 bytes of the unpacked data, stored raw> <bit stream ...>

The bit stream is read in 16-bit little-endian words, most significant bit
first, and the next word is fetched as soon as the previous one is used up.
The six raw bytes of the header are appended to the output at the end.

Usage: hrust.py <packed.bin> <unpacked.bin>
"""
import sys


class BitReader:
    def __init__(self, data, pos):
        self.data = data
        self.pos = pos
        self.bits = 0
        self.left = 0

    def byte(self):
        b = self.data[self.pos]
        self.pos += 1
        return b

    def word(self):
        lo = self.byte()
        hi = self.byte()
        self.bits = (hi << 8) | lo
        self.left = 16

    def get(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.bits >> 15) & 1)
            self.bits = (self.bits << 1) & 0xFFFF
            self.left -= 1
            if self.left == 0:
                self.word()
        return v


def depack(data):
    if data[0:2] != b'HR':
        raise ValueError('no Hrust ZX header ("HR")')
    unpackedLength = data[2] | (data[3] << 8)
    tail = data[6:12]
    r = BitReader(data, 12)
    r.word()
    out = bytearray([r.byte()])
    expBitLen = 2

    def repeat(disp, length):
        for _ in range(length):
            out.append(out[len(out) + disp])

    while True:
        disp = 0
        length = 0
        skipLen = skipDisp = docopy = False
        dispType = 'plusbyte'
        if r.get(1):
            docopy, length = True, 1
        else:
            kind = r.get(2)
            if kind == 0:                       # %000abc: one byte, disp -8..-1
                disp = -8 | r.get(3)
                length, skipLen, skipDisp = 1, True, True
            elif kind == 1:                     # %001: two bytes or insertion
                length, skipLen = 2, True
                sub = r.get(2)
                if sub == 0:
                    disp = -768
                elif sub == 1:
                    disp = -512
                elif sub == 2:
                    b = r.byte()
                    skipDisp = True
                    if b < 0xE0:
                        disp = -256 | b
                    elif b == 0xFE:             # widen the long displacement
                        length = 0
                        expBitLen += 1
                        if expBitLen > 8:
                            expBitLen = 1
                    else:                       # insertion match, xor 2
                        length = -3
                        b = ((b << 1) & 0xFE) | (b >> 7)
                        b = ((b ^ 2) - 15) & 0xFF
                        disp = -256 | b
                else:
                    dispType = 'abcde'
            elif kind == 2:                     # %010: three bytes
                length, skipLen = 3, True
                dispType = 'common'
            else:                               # %011: variable length
                dispType = 'common'

        stop = False
        if not skipLen and not docopy:
            sel = r.get(2)
            if sel == 0:
                if r.get(1):                    # insertion match, disp -16..-1
                    disp = -16 | r.get(4)
                    length, skipDisp = -3, True
                elif r.get(1):                  # copy 12..42 raw bytes
                    length = ((r.get(4)) + 6) << 1
                    skipDisp = docopy = True
                else:
                    v = r.get(7)
                    if v == 15:
                        stop = True
                    elif v > 15:
                        length = v
                    else:
                        length = (v << 8) + r.byte()
            elif sel == 1:
                length = 4
            elif sel == 2:
                length = 5
            else:
                length = 6
                while True:
                    v = r.get(2)
                    length += v
                    if v != 3 or length >= 15:
                        break
        if stop:
            break

        if not skipDisp and not docopy:
            if dispType == 'common':
                sel = r.get(2)
                if sel == 2:
                    disp = -32 | r.get(5)
                else:
                    if sel == 0:
                        disp, dispType = -512, 'plusbyte'
                    elif sel == 1:
                        disp = -256
                    else:
                        disp = (-1 << expBitLen) | r.get(expBitLen)
                        disp <<= 8
                        dispType = 'plusbyte'
                    b = r.byte()
                    if dispType == 'common':
                        if b < 0xE0:
                            disp = -256 | b
                        else:                   # insertion match, xor 3
                            length = -3
                            b = ((b << 1) & 0xFE) | (b >> 7)
                            b = ((b ^ 3) - 15) & 0xFF
                            disp = -256 | b
                    else:
                        disp += b
            elif dispType == 'plusbyte':
                disp += r.byte()
            else:
                disp = -32 | r.get(5)

        if docopy:
            for _ in range(length):
                out.append(r.byte())
        elif length == -3:
            b = r.byte()
            repeat(disp, 1)
            out.append(b)
            repeat(disp, 1)
        elif length:
            repeat(disp, length)

    out += tail
    if len(out) != unpackedLength:
        raise ValueError(f'unpacked {len(out)} bytes, header says {unpackedLength}')
    return bytes(out)


if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    data = depack(open(src, 'rb').read())
    open(dst, 'wb').write(data)
    print(f'{dst}: {len(data)} bytes')
