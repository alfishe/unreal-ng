#!/usr/bin/env python3
"""LZSA2 raw-block decoder (forward), after expand_block_v2.c of github.com/emmanuel-marty/lzsa.
lzsa2.py <in> <offset> [out]  - decode from a byte offset, report consumed / produced"""
import sys


class Lzsa2:
    def __init__(self, src, pos=0):
        self.src, self.pos, self.nib = src, pos, None

    def byte(self):
        b = self.src[self.pos]
        self.pos += 1
        return b

    def nibble(self):
        # high nibble first, the low one is kept for the next call
        if self.nib is None:
            b = self.byte()
            self.nib = b & 0x0F
            return b >> 4
        n, self.nib = self.nib, None
        return n

    def length(self, base):
        n = base + self.nibble()
        if n - base == 15:
            n += self.byte()
            if n == 257:                      # a 16-bit length follows
                n = self.byte() | self.byte() << 8
            elif n == 256:                    # end of data (only reachable for matches)
                n = 0
        return n

    def decode(self):
        out, off = bytearray(), 0
        while True:
            token = self.byte()
            lit = (token >> 3) & 3
            if lit == 3:
                lit = self.length(3)
            out += self.src[self.pos:self.pos + lit]
            self.pos += lit
            z = token & 0x20
            mode = token & 0xC0
            if mode == 0x00:                  # 5-bit offset
                off = (((self.nibble() << 1) | (z >> 5)) ^ 0x1E) + 1
            elif mode == 0x40:                # 9-bit offset
                off = ((self.byte() | z << 3) ^ 0x0FF) + 1
            elif mode == 0x80:                # 13-bit offset
                n = self.nibble()
                off = ((self.byte() | n << 9 | z << 3) ^ 0x1EFF) + 513
            elif not z:                       # 16-bit offset
                hi = self.byte()
                off = ((hi << 8 | self.byte()) ^ 0xFFFF) + 1
            # else: repeat the previous offset
            mlen = token & 7
            mlen = self.length(9) if mlen == 7 else mlen + 2
            if mlen == 0:
                return out
            start = len(out) - off
            if start < 0:
                raise ValueError(f"match before the output start (src {self.pos:#x}, out {len(out):#x})")
            for i in range(mlen):             # byte by byte: matches may overlap
                out.append(out[start + i])


if __name__ == "__main__":
    data = open(sys.argv[1], "rb").read()
    start = int(sys.argv[2], 0)
    d = Lzsa2(data, start)
    out = d.decode()
    print(f"end of data: consumed {d.pos - start:#x} bytes, produced {len(out):#x}")
    if len(sys.argv) > 3:
        open(sys.argv[3], "wb").write(out)
