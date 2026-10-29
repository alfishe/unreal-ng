#!/usr/bin/env python3
"""LZSA2 raw blocks (forward, with the end-of-data marker): decoder, compressor, in-place check.

The decoder is the one of docs/disasm/software/sprinter/prosiak-wildsound/tools/lzsa2.py (after
expand_block_v2.c of github.com/emmanuel-marty/lzsa); it decodes the same way as the Z80 depacker
of prosiak.exe (#3D56). The compressor is new: lazy matching over hash chains, offsets up to 64 KB,
the repeat-offset form, matches capped at 255 bytes (no 16-bit match lengths), literal runs of any
length (the 16-bit literal length when a run exceeds 255 bytes). Its output is not the same as the
official `lzsa` tool's, but any LZSA2 raw-block decoder gives the input back.

    lzsa2.py <in> <offset> [out]      decode from a byte offset, report consumed / produced
    lzsa2.py -c <in> <out>            compress <in> to a raw block, then decode it again to check
"""
import sys


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def write_file(path, data):
    with open(path, "wb") as f:
        f.write(data)


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

    def decode(self, out=None, dst=0, trace=None):
        """Decode to a new bytearray, or (out, dst given) into `out` at `dst`, which may be the
        same buffer as the source: that is the in-place case, reads and writes interleave as on
        the Z80. Returns the output bytes (or the end address in `out`). `trace` (a list) gets
        (bytes written, source position) after every command."""
        inplace = out is not None
        if not inplace:
            out, dst = bytearray(), 0
        start, off = dst, 0
        while True:
            token = self.byte()
            lit = (token >> 3) & 3
            if lit == 3:
                lit = self.length(3)
            if inplace:
                for _ in range(lit):          # byte by byte, as LDIR: a literal is read just before its slot is written
                    out[dst] = self.byte()
                    dst += 1
            else:
                out += self.src[self.pos:self.pos + lit]
                self.pos += lit
                dst += lit
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
                if trace is not None:
                    trace.append((dst - start, self.pos))
                return dst if inplace else out
            s = dst - off
            if s < start:
                raise ValueError(f"match before the output start (src {self.pos:#x}, out {dst - start:#x})")
            for i in range(mlen):             # byte by byte: matches may overlap
                if inplace:
                    out[dst] = out[s + i]
                else:
                    out.append(out[s + i])
                dst += 1
            if trace is not None:
                trace.append((dst - start, self.pos))


def decode(data, pos=0):
    """Decode one raw block starting at `pos`; returns (output, bytes consumed)."""
    d = Lzsa2(data, pos)
    out = d.decode()
    return bytes(out), d.pos - pos


# ---------------------------------------------------------------------------------------------
# Compressor

MIN_MATCH = 2
MAX_MATCH = 255           # 24 + 231: the longest match without the 16-bit length form
MAX_OFFSET = 65536
CHAIN_DEPTH = 24          # hash-chain candidates looked at per position
GOOD_ENOUGH = 64          # stop searching once a match this long is found


def _offset_cost(off, rep):
    """Bytes the offset takes (nibbles count as a half)."""
    if off == rep:
        return 0.0
    if off <= 32:
        return 0.5
    if off <= 512:
        return 1.0
    if off <= 8704:
        return 1.5
    return 2.0


def _length_cost(mlen):
    if mlen - 2 < 7:
        return 0.0
    if mlen - 2 < 22:
        return 0.5
    return 1.5


def _gain(mlen, off, rep):
    # a match saves mlen literal bytes and costs a token, its offset and its length extension
    return mlen - 1.0 - _offset_cost(off, rep) - _length_cost(mlen)


def _match_len(data, a, b, limit):
    """Length of the common run of data[a:] and data[b:] (b < a), at most `limit`."""
    n = 0
    while n + 16 <= limit and data[a + n:a + n + 16] == data[b + n:b + n + 16]:
        n += 16
    while n < limit and data[a + n] == data[b + n]:
        n += 1
    return n


class _Writer:
    """Byte output with the nibble rule: the first nibble takes a new byte at the current
    position (high half), the next one fills its low half."""

    def __init__(self):
        self.out = bytearray()
        self.nib_at = None

    def byte(self, b):
        self.out.append(b)

    def nibble(self, n):
        if self.nib_at is None:
            self.nib_at = len(self.out)
            self.out.append(n << 4)
        else:
            self.out[self.nib_at] |= n
            self.nib_at = None

    def literal_length(self, n):
        # called after the token when n >= 3
        if n < 18:
            self.nibble(n - 3)
        elif n < 256:
            self.nibble(15)
            self.byte(n - 18)                 # 0..237; 238 would read as end of data
        else:
            self.nibble(15)
            self.byte(239)
            self.byte(n & 0xFF)
            self.byte(n >> 8)

    def match_length(self, mlen):
        n = mlen - 2
        if n < 7:
            return
        if n < 22:
            self.nibble(n - 7)
        else:
            self.nibble(15)
            self.byte(n - 22)                 # 0..231 (mlen 24..255)

    def command(self, literals, mlen, off, rep):
        """One command: literals, then a match (mlen > 0) or the end-of-data marker (mlen == 0)."""
        nlit = len(literals)
        if nlit > 0xFFFF:
            raise ValueError("literal run longer than 65535 bytes")
        lbits = 3 if nlit >= 3 else nlit
        if mlen == 0:
            # end of data: 9-bit offset form (offset byte ignored), length 7 + nibble 15 + byte 232
            self.byte(0x40 | lbits << 3 | 7)
            if nlit >= 3:
                self.literal_length(nlit)
            self.out += literals
            self.byte(0x00)
            self.nibble(15)
            self.byte(232)
            return
        mbits = min(mlen - 2, 7)
        if off == rep:
            xyz, extra = 0xE0, None
        elif off <= 32:
            x = (off - 1) ^ 0x1E
            xyz, extra = 0x00 | (x & 1) << 5, ("n", x >> 1)
        elif off <= 512:
            x = (off - 1) ^ 0xFF
            xyz, extra = 0x40 | (x >> 8) << 5, ("b", x & 0xFF)
        elif off <= 8704:
            x = (off - 513) ^ 0x1EFF
            xyz, extra = 0x80 | ((x >> 8) & 1) << 5, ("nb", x >> 9, x & 0xFF)
        else:
            x = (off - 1) ^ 0xFFFF
            xyz, extra = 0xC0, ("bb", x >> 8, x & 0xFF)
        self.byte(xyz | lbits << 3 | mbits)
        if nlit >= 3:
            self.literal_length(nlit)
        self.out += literals
        if extra is not None:
            if extra[0] == "n":
                self.nibble(extra[1])
            elif extra[0] == "b":
                self.byte(extra[1])
            elif extra[0] == "nb":
                self.nibble(extra[1])
                self.byte(extra[2])
            else:
                self.byte(extra[1])
                self.byte(extra[2])
        self.match_length(mlen)


def compress(data, literal_from=None):
    """LZSA2 raw block of `data` (bytes), ending with the end-of-data marker. From `literal_from`
    on, the data is stored as literals (compress_in_place uses that)."""
    data = bytes(data)
    n = len(data)
    stop = n if literal_from is None else max(0, min(n, literal_from))
    w = _Writer()
    head = {}                                 # 3-byte key -> latest position
    prev = [-1] * n                           # previous position with the same key
    inserted = 0                              # positions < inserted are in the chains

    def insert_upto(p):
        nonlocal inserted
        while inserted < p and inserted + 3 <= n:
            k = data[inserted:inserted + 3]
            prev[inserted] = head.get(k, -1)
            head[k] = inserted
            inserted += 1
        if inserted < p:
            inserted = p

    def best_match(i, rep):
        limit = min(MAX_MATCH, stop - i)
        if limit < MIN_MATCH:
            return 0, 0, 0.0
        best_len, best_off, best_gain = 0, 0, 0.0
        if rep and rep <= i:
            ml = _match_len(data, i, i - rep, limit)
            if ml >= MIN_MATCH:
                best_len, best_off, best_gain = ml, rep, _gain(ml, rep, rep)
        if limit >= 3:
            insert_upto(i)
            cand = head.get(data[i:i + 3], -1)
            depth = 0
            while cand >= 0 and depth < CHAIN_DEPTH:
                off = i - cand
                if off > MAX_OFFSET:
                    break
                # quick reject: a candidate that differs at the current best length cannot be longer
                if not best_len or best_len >= limit or data[cand + best_len] == data[i + best_len]:
                    ml = _match_len(data, i, cand, limit)
                    g = _gain(ml, off, rep)
                    if g > best_gain:
                        best_len, best_off, best_gain = ml, off, g
                        if ml >= GOOD_ENOUGH or ml == limit:
                            break
                cand = prev[cand]
                depth += 1
        elif 1 <= i:
            # two bytes left: a short offset may still pay
            for off in range(1, min(i, 32) + 1):
                if data[i - off:i - off + 2] == data[i:i + 2]:
                    g = _gain(2, off, rep)
                    if g > best_gain:
                        best_len, best_off, best_gain = 2, off, g
                    break
        if best_gain <= 0:
            return 0, 0, 0.0
        return best_len, best_off, best_gain

    i, lit_start, rep = 0, 0, 0
    while i < stop:
        mlen, off, g = best_match(i, rep)
        if mlen == 0:
            i += 1
            continue
        # lazy step: a better match one byte later wins over this one
        if i + 1 < stop:
            mlen2, off2, g2 = best_match(i + 1, rep)
            if g2 > g + 1.0:
                i += 1
                continue
        w.command(data[lit_start:i], mlen, off, rep)
        rep = off
        i += mlen
        lit_start = i
    w.command(data[lit_start:n], 0, 0, rep)
    return bytes(w.out)


def unpack_in_place(stream, ulen, gap):
    """Emulate unpacking in place: the output occupies [0, ulen), the stream ends `gap` bytes after
    the output's end; reads and writes share one buffer. -> the output (corrupt if unsafe)."""
    i0 = ulen + gap - len(stream)
    shift = max(0, -i0)
    mem = bytearray(shift + ulen + gap)
    mem[shift + i0:] = stream
    end = Lzsa2(mem, shift + i0).decode(out=mem, dst=shift)
    return bytes(mem[shift:end])


def compress_in_place(data, gap):
    """A raw block of `data` that unpacks correctly in place with the stream ending `gap` bytes
    after the output's end (prosiak's VM: 32). When the writes would overtake the reads, the data
    from the command where the output leads the input most is stored as literals instead."""
    data = bytes(data)
    tail = None
    for _ in range(8):
        packed = compress(data, tail)
        try:
            if unpack_in_place(packed, len(data), gap) == data:
                return packed
        except (ValueError, IndexError):
            pass
        trace = []
        Lzsa2(packed).decode(trace=trace)
        cut = max(trace, key=lambda t: t[0] - t[1])[0]
        tail = cut if tail is None or cut < tail else max(0, tail - 256)
    return literal_block(data)


def literal_block(data):
    """A raw block that is one literal run and the end marker: always valid, 7 bytes longer than
    the data at most, and safe to unpack in place (the reads stay ahead of the writes)."""
    w = _Writer()
    w.command(bytes(data), 0, 0, 0)
    return bytes(w.out)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "-c":
        src = read_file(sys.argv[2])
        packed = compress(src)
        back, used = decode(packed)
        if back != src or used != len(packed):
            raise SystemExit("compressor self-check failed")
        write_file(sys.argv[3], packed)
        print(f"{len(src)} -> {len(packed)} bytes, decoded back and compared")
    elif len(sys.argv) in (3, 4):
        data = read_file(sys.argv[1])
        start = int(sys.argv[2], 0)
        d = Lzsa2(data, start)
        out = d.decode()
        print(f"end of data: consumed {d.pos - start:#x} bytes, produced {len(out):#x}")
        if len(sys.argv) > 3:
            write_file(sys.argv[3], out)
    else:
        raise SystemExit(__doc__)
