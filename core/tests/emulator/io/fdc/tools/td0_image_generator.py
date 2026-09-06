#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Teledisk TD0 fixture generator (python3, standard library only).

Writes the fixtures used by core/tests/loaders/disk/loader_td0_test.cpp:

  testdata/loaders/td0/trdos-sample.td0      "TD" (uncompressed), 80 cylinders x 2 sides, 16 x 256 per track,
                                             TR-DOS volume sector at cylinder 0 / sector 9, every data block
                                             encoding (0 raw, 1 repeated 2-byte pattern, 2 RLE) used
  testdata/loaders/td0/trdos-sample-adv.td0  "td" (advanced compression = LZSS + adaptive Huffman, "LZHUF"),
                                             identical content to trdos-sample.td0
  testdata/loaders/td0/protected-sample.td0  "TD", 4 cylinders x 2 sides with every sector flag the loader
                                             maps: deleted DAM, data CRC error, ID-only (0x20), DOS-unallocated
                                             (0x10), duplicate sector number (0x01), no-ID sector (0x40),
                                             an FM track, an empty track, a stale sector CRC and a comment block

Everything here (CRC-16 with polynomial 0xA097, the LZHUF encoder / decoder, the RLE encoder) is written from
the published algorithm descriptions, independently of the C++ loader, so the fixtures act as a cross-check.

The exact sector contents of trdos-sample.td0 are a function of (cylinder, head, sector) - see
sector_content() - and the C++ test recomputes them with the same rule.

Usage:  python3 td0_image_generator.py [output-directory]     (default: <repo>/testdata/loaders/td0)
"""

import os
import struct
import sys

# region CRC

def crc16_teledisk(data, crc=0):
    """CRC-16, polynomial 0xA097, MSB first, initial value 0 (bitwise; independent of any table)."""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0xA097) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def crc8_teledisk(data):
    return crc16_teledisk(data) & 0xFF

# endregion

# region Data block encodings

def encode_raw(data):
    return bytes([0]) + bytes(data)


def encode_pattern(data):
    """Encoding 1: count(2) + 2-byte pattern. Only valid when data is that pattern repeated."""
    assert len(data) % 2 == 0 and len(data) > 0
    pattern = bytes(data[0:2])
    assert bytes(data) == pattern * (len(data) // 2), "encoding 1 requires a repeated 2-byte pattern"
    return bytes([1]) + struct.pack("<H", len(data) // 2) + pattern


def encode_rle(data):
    """Encoding 2: sequence of blocks; [0, count, count raw bytes] or [l, count, 2*l pattern bytes] (repeated count times)."""
    data = bytes(data)
    out = bytearray([2])
    literal = bytearray()

    def flush_literal():
        nonlocal literal
        while literal:
            chunk = literal[:255]
            literal = literal[255:]
            out.append(0)
            out.append(len(chunk))
            out.extend(chunk)

    i = 0
    n = len(data)
    while i < n:
        best_l, best_count = 0, 0
        for l in (1, 2, 3, 4):
            size = 2 * l
            if i + size > n:
                break
            pattern = data[i:i + size]
            count = 1
            while count < 255 and i + (count + 1) * size <= n and data[i + count * size:i + (count + 1) * size] == pattern:
                count += 1
            if count >= 2 and count * size > best_count * 2 * best_l:
                best_l, best_count = l, count
        if best_l:
            flush_literal()
            out.append(best_l)
            out.append(best_count)
            out.extend(data[i:i + 2 * best_l])
            i += best_count * 2 * best_l
        else:
            literal.append(data[i])
            i += 1
    flush_literal()
    return bytes(out)


def decode_block(block, size):
    """Reference decoder for the three encodings (used to self-check the encoders)."""
    method = block[0]
    payload = block[1:]
    if method == 0:
        return payload[:size]
    if method == 1:
        count = struct.unpack("<H", payload[0:2])[0]
        return (payload[2:4] * count)[:size]
    if method == 2:
        out = bytearray()
        p = 0
        while p < len(payload) and len(out) < size:
            l = payload[p]
            count = payload[p + 1]
            p += 2
            if l == 0:
                out.extend(payload[p:p + count])
                p += count
            else:
                pattern = payload[p:p + 2 * l]
                p += 2 * l
                out.extend(pattern * count)
        return bytes(out[:size])
    raise ValueError("unknown encoding %d" % method)

# endregion

# region LZHUF (LZSS + adaptive Huffman) - Okumura's scheme as used by Teledisk "td" images

N = 4096            # ring buffer size
F = 60              # longest match
THRESHOLD = 2       # matches longer than this are encoded as (length, position)
N_CHAR = 256 - THRESHOLD + F   # 314 symbols: 256 literals + 58 match lengths (3..60)
T = N_CHAR * 2 - 1  # 627 tree nodes
R = T - 1           # root
MAX_FREQ = 0x8000   # rebuild the tree when the root frequency reaches this


def build_position_tables():
    """Fixed prefix code for the upper 6 bits of a match position: canonical code with 1/3/8/12/24/16 codes of
    lengths 3..8 (a complete code: 32+48+64+48+48+16 = 256 leaves at depth 8)."""
    p_len = []
    for length, count in ((3, 1), (4, 3), (5, 8), (6, 12), (7, 24), (8, 16)):
        p_len += [length] * count
    p_code = []
    code = 0
    prev = p_len[0]
    for i, length in enumerate(p_len):
        if i > 0:
            code = (code + 1) << (length - prev)
        prev = length
        p_code.append(code << (8 - length))
    d_code = [0] * 256
    d_len = [0] * 256
    for k in range(64):
        length = p_len[k]
        for b in range(p_code[k], p_code[k] + (1 << (8 - length))):
            d_code[b] = k
            d_len[b] = length
    return p_len, p_code, d_code, d_len


P_LEN, P_CODE, D_CODE, D_LEN = build_position_tables()


class AdaptiveHuffman:
    """Adaptive Huffman tree over N_CHAR symbols (FGK style with sibling property, as in lzhuf)."""

    def __init__(self):
        self.freq = [0] * (T + 1)
        self.prnt = [0] * (T + N_CHAR)
        self.son = [0] * T
        for i in range(N_CHAR):
            self.freq[i] = 1
            self.son[i] = i + T
            self.prnt[i + T] = i
        i, j = 0, N_CHAR
        while j <= R:
            self.freq[j] = self.freq[i] + self.freq[i + 1]
            self.son[j] = i
            self.prnt[i] = j
            self.prnt[i + 1] = j
            i += 2
            j += 1
        self.freq[T] = 0xFFFF
        self.prnt[R] = 0

    def reconstruct(self):
        freq, son, prnt = self.freq, self.son, self.prnt
        # Collect leaves, halving their frequencies
        j = 0
        for i in range(T):
            if son[i] >= T:
                freq[j] = (freq[i] + 1) // 2
                son[j] = son[i]
                j += 1
        # Rebuild internal nodes keeping freq[] sorted
        i, j = 0, N_CHAR
        while j < T:
            k = i + 1
            f = freq[i] + freq[k]
            freq[j] = f
            k = j - 1
            while f < freq[k]:
                k -= 1
            k += 1
            freq[k + 1:j + 1] = freq[k:j]
            freq[k] = f
            son[k + 1:j + 1] = son[k:j]
            son[k] = i
            i += 2
            j += 1
        for i in range(T):
            k = son[i]
            if k >= T:
                prnt[k] = i
            else:
                prnt[k] = i
                prnt[k + 1] = i

    def update(self, c):
        freq, son, prnt = self.freq, self.son, self.prnt
        if freq[R] == MAX_FREQ:
            self.reconstruct()
        c = prnt[c + T]
        while True:
            freq[c] += 1
            k = freq[c]
            l = c + 1
            if k > freq[l]:
                while k > freq[l + 1]:
                    l += 1
                freq[c] = freq[l]
                freq[l] = k
                i = son[c]
                prnt[i] = l
                if i < T:
                    prnt[i + 1] = l
                j = son[l]
                son[l] = i
                prnt[j] = c
                if j < T:
                    prnt[j + 1] = c
                son[c] = j
                c = l
            c = prnt[c]
            if c == 0:
                break

    def code_for(self, c):
        """(bit count, code value) for symbol c, MSB = first bit sent (path from the root)."""
        bits = 0
        length = 0
        k = self.prnt[c + T]
        while k != R:
            bits |= (k & 1) << length
            length += 1
            k = self.prnt[k]
        return length, bits


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.nbits = 0

    def put(self, nbits, value):
        self.acc = (self.acc << nbits) | (value & ((1 << nbits) - 1))
        self.nbits += nbits
        while self.nbits >= 8:
            self.nbits -= 8
            self.out.append((self.acc >> self.nbits) & 0xFF)
        self.acc &= (1 << self.nbits) - 1 if self.nbits else 0

    def finish(self):
        if self.nbits:
            self.put(8 - self.nbits, 0)
        return bytes(self.out)


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.acc = 0
        self.nbits = 0

    def get_bits(self, n):
        while self.nbits < n:
            byte = self.data[self.pos] if self.pos < len(self.data) else 0
            self.pos += 1
            self.acc = (self.acc << 8) | byte
            self.nbits += 8
        self.nbits -= n
        value = (self.acc >> self.nbits) & ((1 << n) - 1)
        self.acc &= (1 << self.nbits) - 1 if self.nbits else 0
        return value

    def exhausted(self):
        return self.pos >= len(self.data) and self.nbits == 0


def lzhuf_encode(data, max_candidates=48):
    """LZSS (4 KB window, matches 3..60) + adaptive Huffman. Match positions are encoded as in lzhuf:
    position = (r - match_start - 1) & (N - 1), where r is the ring-buffer index of the current byte."""
    data = bytes(data)
    n = len(data)
    huff = AdaptiveHuffman()
    writer = BitWriter()
    heads = {}   # 3-byte prefix -> list of input positions (most recent last)
    max_distance = N - F  # mirrors the number of strings kept in lzhuf's search tree

    def emit_char(c):
        length, bits = huff.code_for(c)
        writer.put(length, bits)
        huff.update(c)

    def emit_position(pos):
        hi = pos >> 6
        writer.put(P_LEN[hi], P_CODE[hi] >> (8 - P_LEN[hi]))
        writer.put(6, pos & 0x3F)

    def insert(p):
        if p + 3 <= n:
            key = data[p:p + 3]
            lst = heads.get(key)
            if lst is None:
                heads[key] = [p]
            else:
                lst.append(p)
                if len(lst) > 4 * max_candidates:
                    del lst[:len(lst) - max_candidates]

    p = 0
    while p < n:
        best_len, best_pos = 0, 0
        if p + 3 <= n:
            lst = heads.get(data[p:p + 3])
            if lst:
                limit = min(F, n - p)
                checked = 0
                for q in reversed(lst):
                    if p - q > max_distance:
                        break
                    checked += 1
                    if checked > max_candidates:
                        break
                    if data[q + best_len] != data[p + best_len]:
                        continue
                    length = 3
                    while length < limit and data[q + length] == data[p + length]:
                        length += 1
                    if length > best_len:
                        best_len, best_pos = length, q
                        if length == limit:
                            break
        if best_len <= THRESHOLD:
            emit_char(data[p])
            insert(p)
            p += 1
        else:
            emit_char(255 - THRESHOLD + best_len)
            r = (N - F + p) & (N - 1)
            start = (N - F + best_pos) & (N - 1)
            emit_position((r - start - 1) & (N - 1))
            for k in range(best_len):
                insert(p + k)
            p += best_len
    return writer.finish()


def lzhuf_decode(data, max_out=1 << 26):
    """Reference decoder (the same algorithm the C++ loader implements)."""
    huff = AdaptiveHuffman()
    reader = BitReader(data)
    text = bytearray(N)
    for i in range(N - F):
        text[i] = 0x20
    r = N - F
    out = bytearray()
    while not reader.exhausted() and len(out) < max_out:
        c = huff.son[R]
        while c < T:
            c = huff.son[c + reader.get_bits(1)]
        c -= T
        huff.update(c)
        if c < 256:
            text[r] = c
            r = (r + 1) & (N - 1)
            out.append(c)
        else:
            byte = reader.get_bits(8)
            pos = D_CODE[byte] << 6
            extra = D_LEN[byte] - 2
            low = byte
            for _ in range(extra):
                low = (low << 1) | reader.get_bits(1)
            pos |= low & 0x3F
            i = (r - pos - 1) & (N - 1)
            j = c - 255 + THRESHOLD
            for k in range(j):
                b = text[(i + k) & (N - 1)]
                text[r] = b
                r = (r + 1) & (N - 1)
                out.append(b)
    return bytes(out)

# endregion

# region TD0 image builder

class Td0Sector:
    def __init__(self, c, h, r, n, flags=0, data=None, encoding=None, stale_crc=False):
        self.c, self.h, self.r, self.n, self.flags = c, h, r, n, flags
        self.data = data
        self.encoding = encoding     # None => pick automatically, else 0 / 1 / 2
        self.stale_crc = stale_crc   # deliberately wrong CRC-8 (loader must warn, not fail)

    def block(self):
        if self.flags & 0x30:
            return b""
        size = 128 << self.n
        data = self.data
        assert len(data) == size, "sector %d: %d bytes, expected %d" % (self.r, len(data), size)
        enc = self.encoding
        if enc is None:
            enc = 1 if bytes(data) == bytes(data[0:2]) * (size // 2) else 0
        block = {0: encode_raw, 1: encode_pattern, 2: encode_rle}[enc](data)
        assert decode_block(block, size) == bytes(data)
        return block


class Td0Track:
    def __init__(self, cylinder, head, fm=False, sectors=None):
        self.cylinder, self.head, self.fm = cylinder, head, fm
        self.sectors = sectors or []


class Td0Image:
    def __init__(self, advanced=False, version=0x15, data_rate=0, drive_type=3, stepping=0, dos_alloc=0, sides=2,
                 comment=None, date=(2026, 9, 2, 12, 34, 56)):
        self.advanced = advanced
        self.version, self.data_rate, self.drive_type = version, data_rate, drive_type
        self.stepping, self.dos_alloc, self.sides = stepping, dos_alloc, sides
        self.comment = comment    # str with "\n" line separators (stored as NUL) or None
        self.date = date
        self.tracks = []

    def body(self):
        out = bytearray()
        if self.comment is not None:
            text = self.comment.encode("ascii").replace(b"\n", b"\0")
            y, mo, d, hh, mm, ss = self.date
            rest = struct.pack("<HBBBBBB", len(text), y - 1900, mo - 1, d, hh, mm, ss) + text
            out += struct.pack("<H", crc16_teledisk(rest)) + rest
        for track in self.tracks:
            hdr = bytes([len(track.sectors), track.cylinder, track.head | (0x80 if track.fm else 0)])
            out += hdr + bytes([crc8_teledisk(hdr)])
            for s in track.sectors:
                block = s.block()
                crc = 0
                if block:
                    crc = crc8_teledisk(decode_block(block, 128 << s.n))
                    if s.stale_crc:
                        crc ^= 0x5A
                out += bytes([s.c, s.h, s.r, s.n, s.flags, crc])
                if block:
                    out += struct.pack("<H", len(block)) + block
        out += b"\xFF"
        return bytes(out)

    def build(self):
        stepping = self.stepping | (0x80 if self.comment is not None else 0)
        head = struct.pack("<2sBBBBBBBB", b"td" if self.advanced else b"TD", 0, 0, self.version, self.data_rate,
                           self.drive_type, stepping, self.dos_alloc, self.sides)
        head += struct.pack("<H", crc16_teledisk(head))
        body = self.body()
        if self.advanced:
            packed = lzhuf_encode(body)
            assert lzhuf_decode(packed)[:len(body)] == body, "LZHUF self-check failed"
            body = packed
        return head + body

# endregion

# region Fixture content

TRDOS_INTERLEAVE = [1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16]
TRDOS_CYLINDERS = 80
TRDOS_SIDES = 2
TRDOS_LABEL = b"TD0TEST "


def lcg_bytes(seed, count):
    x = seed
    out = bytearray(count)
    for i in range(count):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out[i] = (x >> 16) & 0xFF
    return bytes(out)


def trdos_volume_sector():
    data = bytearray(256)
    data[0xE1] = 0                    # first free sector
    data[0xE2] = 1                    # first free track
    data[0xE3] = 0x16                 # 80 tracks, double sided
    data[0xE4] = 0                    # files
    free = (TRDOS_CYLINDERS * TRDOS_SIDES - 1) * 16
    data[0xE5] = free & 0xFF
    data[0xE6] = free >> 8
    data[0xE7] = 0x10                 # TR-DOS signature
    data[0xF5:0xFD] = TRDOS_LABEL
    return bytes(data)


def sector_content(c, h, r):
    """(data, encoding) of one trdos-sample sector. Mirrored by the C++ test."""
    t = c * 2 + h
    if t == 0 and 1 <= r <= 8:
        return bytes(256), 1
    if t == 0 and r == 9:
        return trdos_volume_sector(), 0
    kind = (t + r) % 3
    if kind == 0:
        return lcg_bytes((c << 16) | (h << 8) | r, 256), 0
    if kind == 1:
        return bytes([c & 0xFF, r]) * 128, 1
    head = bytes(((i * 3 + r + c) & 0xFF) for i in range(64))
    return head + bytes([c & 0xFF, h, r, 0xAA]) * 48, 2


def build_trdos_sample(advanced):
    image = Td0Image(advanced=advanced, drive_type=3, sides=2, comment=None)
    for c in range(TRDOS_CYLINDERS):
        for h in range(TRDOS_SIDES):
            track = Td0Track(c, h)
            for r in TRDOS_INTERLEAVE:
                data, enc = sector_content(c, h, r)
                track.sectors.append(Td0Sector(c, h, r, 1, 0, data, enc))
            image.tracks.append(track)
    return image


def build_protected_sample():
    image = Td0Image(advanced=False, drive_type=3, sides=2,
                     comment="Protected sample\nsecond line", date=(2026, 9, 2, 12, 34, 56))

    # Cylinder 0 head 0: plain 16 x 256, sectors 1..16 in order, every encoding
    t = Td0Track(0, 0)
    for r in range(1, 17):
        t.sectors.append(Td0Sector(0, 0, r, 1, 0, bytes([r]) * 256, [0, 1, 2][r % 3]))
    image.tracks.append(t)

    # Cylinder 0 head 1: 9 x 512; sector 3 deleted, sector 5 data CRC error, sector 9 deleted + CRC error
    t = Td0Track(0, 1)
    for r in range(1, 10):
        flags = {3: 0x04, 5: 0x02, 9: 0x06}.get(r, 0)
        t.sectors.append(Td0Sector(0, 1, r, 2, flags, lcg_bytes(0x0100 | r, 512), 0))
    image.tracks.append(t)

    # Cylinder 1 head 0: 10 sectors; 4 = ID only (0x20), 6 = DOS-unallocated (0x10), 7 duplicated (0x01)
    t = Td0Track(1, 0)
    for r in range(1, 11):
        if r == 4:
            t.sectors.append(Td0Sector(1, 0, r, 1, 0x20))
        elif r == 6:
            t.sectors.append(Td0Sector(1, 0, r, 1, 0x10))
        else:
            t.sectors.append(Td0Sector(1, 0, r, 1, 0, bytes([0x10 + r, 0x20 + r]) * 128, 1))
        if r == 7:
            t.sectors.append(Td0Sector(1, 0, r, 1, 0x01, bytes([0x77]) * 256, 0))
    image.tracks.append(t)

    # Cylinder 1 head 1: FM track (head byte bit 7), 16 x 128
    t = Td0Track(1, 1, fm=True)
    for r in range(1, 17):
        t.sectors.append(Td0Sector(1, 1, r, 0, 0, lcg_bytes(0x0F00 | r, 128), 0))
    image.tracks.append(t)

    # Cylinder 2 head 0: 5 x 1024 plus one sector without an ID field (0x40) that the loader must skip
    t = Td0Track(2, 0)
    for r in range(1, 6):
        t.sectors.append(Td0Sector(2, 0, r, 3, 0, lcg_bytes(0x2000 | r, 1024), 0))
    t.sectors.insert(2, Td0Sector(2, 0, 0xEE, 1, 0x40, bytes([0xEE]) * 256, 0))
    image.tracks.append(t)

    # Cylinder 2 head 1: mixed sizes with a foreign C/H in one ID (copy protection style)
    t = Td0Track(2, 1)
    t.sectors.append(Td0Sector(2, 1, 1, 0, 0, bytes([0xA0]) * 128, 0))
    t.sectors.append(Td0Sector(2, 1, 2, 1, 0, bytes([0xA1]) * 256, 0))
    t.sectors.append(Td0Sector(40, 1, 7, 2, 0, bytes([0xA2]) * 512, 0))
    t.sectors.append(Td0Sector(2, 1, 4, 3, 0, bytes([0xA3]) * 1024, 0))
    image.tracks.append(t)

    # Cylinder 3 head 0: unformatted (no sectors)
    image.tracks.append(Td0Track(3, 0))

    # Cylinder 3 head 1: 16 x 256 with a stale sector CRC in sector 2 (warning only)
    t = Td0Track(3, 1)
    for r in range(1, 17):
        t.sectors.append(Td0Sector(3, 1, r, 1, 0, lcg_bytes(0x3100 | r, 256), 0, stale_crc=(r == 2)))
    image.tracks.append(t)

    return image

# endregion


def self_test():
    # CRC-16 reference value: the Teledisk polynomial over "123456789"
    assert crc16_teledisk(b"123456789") == crc16_teledisk(bytes(b"123456789"))
    # LZHUF round trip on assorted inputs
    for sample in (b"", b"a", b"abcabcabcabcabcabc" * 10, bytes(range(256)) * 20, lcg_bytes(7, 5000),
                   b"\0" * 10000 + lcg_bytes(9, 3000) + b"xyz" * 4000):
        assert lzhuf_decode(lzhuf_encode(sample))[:len(sample)] == sample
    # RLE / pattern encoders
    for sample in (bytes(256), bytes([1, 2]) * 128, lcg_bytes(3, 256), lcg_bytes(4, 64) + bytes([1, 2, 3, 4]) * 48):
        assert decode_block(encode_rle(sample), 256) == sample


def main():
    self_test()
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", "..", "..", "..", "..", ".."))
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "testdata", "loaders", "td0")
    os.makedirs(out_dir, exist_ok=True)

    normal = build_trdos_sample(advanced=False).build()
    with open(os.path.join(out_dir, "trdos-sample.td0"), "wb") as f:
        f.write(normal)
    print("trdos-sample.td0: %d bytes" % len(normal))

    advanced = build_trdos_sample(advanced=True).build()
    assert lzhuf_decode(advanced[12:])[:len(normal) - 12] == normal[12:]
    with open(os.path.join(out_dir, "trdos-sample-adv.td0"), "wb") as f:
        f.write(advanced)
    print("trdos-sample-adv.td0: %d bytes" % len(advanced))

    protected = build_protected_sample().build()
    with open(os.path.join(out_dir, "protected-sample.td0"), "wb") as f:
        f.write(protected)
    print("protected-sample.td0: %d bytes" % len(protected))


if __name__ == "__main__":
    main()
