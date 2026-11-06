#!/usr/bin/env python3
"""Tests of wsconv.py, wslink.py and lzsa2.py.  Run from this folder:

    python3 -m unittest -v test_wildsound_tools

The XM / S3M modules are generated here. The comparisons with the author's MasonXM outputs and the
relink of the real prosiak.exe run only when those files are present (skipped otherwise); the
official lzsa tool, when present, decodes the compressed blocks as a second check.
"""
import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import lzsa2  # noqa: E402
from lzsa2 import read_file, write_file  # noqa: E402
import wsconv  # noqa: E402
import wslink  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
LZSA_TOOL = next((p for p in [
    os.path.join(REPO, "scratch", "prosiak", "ext", "lzsa", "lzsa"),
    "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch/prosiak/ext/lzsa/lzsa",
] if os.path.exists(p)), None)
AUTHOR_MUSIC = next((p for p in [
    os.path.expanduser("~/Downloads/zx-spectrum/sprinter/peripherals/wild-sound/Wild-Sound/STM_AYX32_TSL/TMT/MasonXM/Music"),
    os.path.expanduser("~/Downloads/zx-spectrum/hardware/sound/wild-sound/Wild-Sound/STM_AYX32_TSL/TMT/MasonXM/Music"),
] if os.path.exists(os.path.join(p, "CONSUL.XM")) and os.path.exists(os.path.join(p, "Prosiak.xmPatPack"))), None)
PROSIAK = next((p for p in wslink.PROSIAK_PATHS if os.path.exists(p)), None)


# ---------------------------------------------------------------------------------------------
# Module builders

def xm_sample_bytes(values, sixteen):
    """Signed sample values -> XM delta-coded bytes."""
    out, prev = bytearray(), 0
    for v in values:
        if sixteen:
            out += struct.pack("<H", (v - prev) & 0xFFFF)
        else:
            out.append((v - prev) & 0xFF)
        prev = v
    return bytes(out)


def make_xm(channels, patterns, orders, instruments, flags=0, speed=6, bpm=125, name=b"test song"):
    """patterns: list of (rows, cells or None); cells: rows*channels lists [n, i, v, e, p]; None = packed
    size 0. instruments: list of lists of sample dicts (values, type, loop, looplen, vol, fine, pan,
    rel); a dict 'env': True in the first sample marks a volume envelope."""
    out = bytearray(b"Extended Module: " + name.ljust(20, b"\0") + b"\x1a" + b"unit test".ljust(20, b" "))
    out += struct.pack("<HI", 0x0104, 276)
    out += struct.pack("<8H", len(orders), 0, channels, len(patterns), len(instruments), flags, speed, bpm)
    out += bytes(orders).ljust(256, b"\0")
    for rows, cells in patterns:
        data = wsconv.pack_cells(cells) if cells is not None else b""
        out += struct.pack("<IBHH", 9, 0, rows, len(data)) + data
    for samples in instruments:
        if not samples:
            out += struct.pack("<I22sBH", 29, b"empty", 0, 0)
            continue
        hdr = bytearray(struct.pack("<I22sBHI", 263, b"ins", 0, len(samples), 40))
        hdr += bytes(96 + 48 + 48)
        misc = bytearray(14)
        if samples[0].get("env"):
            misc[8] = 1                                    # volume envelope on
        hdr += misc + struct.pack("<H", 0) + bytes(22)
        assert len(hdr) == 263
        out += hdr
        datas = []
        for s in samples:
            sixteen = bool(s["type"] & 0x10)
            d = xm_sample_bytes(s["values"], sixteen)
            w = 2 if sixteen else 1
            out += struct.pack("<IIIBbBBbB22s", len(d), s.get("loop", 0) * w, s.get("looplen", 0) * w,
                               s.get("vol", 64), s.get("fine", 0), s["type"], s.get("pan", 0x80),
                               s.get("rel", 0), 0, b"smp")
            datas.append(d)
        for d in datas:
            out += d
    return bytes(out)


def s3m_pattern(rows):
    """rows: list of up to 64 lists of (channel, note, ins, vol, cmd, info) with None for absent."""
    out = bytearray()
    for r in range(64):
        for ch, note, ins, vol, cmd, info in (rows[r] if r < len(rows) else []):
            what = ch
            body = bytearray()
            if note is not None:
                what |= 0x20
                body += bytes([note, ins or 0])
            if vol is not None:
                what |= 0x40
                body.append(vol)
            if cmd is not None:
                what |= 0x80
                body += bytes([cmd, info])
            out.append(what)
            out += body
        out.append(0)
    return struct.pack("<H", len(out) + 2) + bytes(out)


def make_s3m(orders, patterns, samples, chset, gvol=64, speed=6, tempo=125, stereo=True, ffi=2, pantab=None):
    """samples: dicts (data raw bytes, length, loop (beg, end) or None, vol, flags, c2spd) or None for
    an empty instrument slot."""
    head = bytearray(b"s3m unit test".ljust(28, b"\0") + b"\x1a\x10\0\0")
    head += struct.pack("<6H", len(orders), len(samples), len(patterns), 0, 0x1320, ffi)
    head += b"SCRM" + bytes([gvol, speed, tempo, 0xB0 if stereo else 0x30, 0, 252 if pantab else 0])
    head += bytes(8) + struct.pack("<H", 0)
    head += bytes(chset).ljust(32, b"\xff")
    head += bytes(orders)
    tables_at = len(head)
    head += bytes(2 * len(samples) + 2 * len(patterns))
    if pantab:
        head += bytes(pantab)
    out = bytearray(head)

    def para():
        while len(out) % 16:
            out.append(0)
        return len(out) // 16

    ins_ptr, pat_ptr, smp_fix = [], [], []
    for s in samples:
        if s is None:
            ins_ptr.append(0)
            continue
        p = para()
        ins_ptr.append(p)
        h = bytearray(0x50)
        h[0] = 1
        loop = s.get("loop")
        struct.pack_into("<III", h, 0x10, s["length"], loop[0] if loop else 0, loop[1] if loop else 0)
        h[0x1C] = s.get("vol", 64)
        h[0x1F] = s.get("flags", 0) | (1 if loop else 0)
        struct.pack_into("<I", h, 0x20, s.get("c2spd", 8363))
        h[0x4C:0x50] = b"SCRS"
        out += h
        smp_fix.append((p * 16, s["data"]))
    for rows in patterns:
        pat_ptr.append(para())
        out += s3m_pattern(rows)
    for at, data in smp_fix:
        seg = para()
        out[at + 0x0D] = seg >> 16
        struct.pack_into("<H", out, at + 0x0E, seg & 0xFFFF)
        out += data
    for k, p in enumerate(ins_ptr + pat_ptr):
        struct.pack_into("<H", out, tables_at + 2 * k, p)
    return bytes(out)


def cells_of(pack, n):
    """Decode pattern n of a track pack -> (rows, list of cells)."""
    size = struct.unpack_from("<I", pack, 0)[0]
    channels = struct.unpack_from("<H", pack, 24)[0]
    patnum = (len(pack) - 292 - size) // 10
    ofs, psize, rows = struct.unpack_from("<IIH", pack, 292 + 10 * n)
    assert n < patnum
    cells, used = wsconv.unpack_cells(pack[ofs:ofs + psize], rows * channels)
    assert used == psize
    return rows, cells


def instrument(table, i):
    return struct.unpack_from("<IIIIBbBBb", table, 21 * i)


# ---------------------------------------------------------------------------------------------

class Lzsa2Test(unittest.TestCase):
    def cases(self):
        rnd = random.Random(7)
        noise = bytes(rnd.randrange(256) for _ in range(3000))
        return {
            "empty": b"",
            "one": b"\x42",
            "two": b"ab",
            "zeros": bytes(40000),
            "noise": noise,
            "text": b"the quick brown fox jumps over the lazy dog; " * 200,
            "mixed": bytes(rnd.choice(b"abcab") for _ in range(9000)) + bytes(600) + noise[:700]
                     + bytes(range(256)) * 3 + noise[:300],
            "long literal": noise + noise[:5] + bytes(rnd.randrange(256) for _ in range(70000)),
            "far match": noise + bytes(rnd.randrange(256) for _ in range(60000)) + noise,
        }

    def test_round_trip(self):
        for name, data in self.cases().items():
            with self.subTest(name):
                packed = lzsa2.compress(data)
                out, used = lzsa2.decode(packed)
                self.assertEqual(out, data)
                self.assertEqual(used, len(packed))

    def test_ratio(self):
        self.assertLess(len(lzsa2.compress(bytes(40000))), 600)
        self.assertLess(len(lzsa2.compress(b"abcdefgh" * 1000)), 200)
        noise = self.cases()["noise"]
        self.assertLessEqual(len(lzsa2.compress(noise)), len(noise) + 16)

    def test_literal_block(self):
        for name, data in self.cases().items():
            with self.subTest(name):
                if len(data) > 0xFFFF:
                    continue
                packed = lzsa2.literal_block(data)
                self.assertEqual(lzsa2.decode(packed)[0], data)
                self.assertEqual(lzsa2.unpack_in_place(packed, len(data), 32), data)

    def test_in_place(self):
        # good compression first, noise at the end: the plain stream may overrun itself in place
        rnd = random.Random(3)
        data = bytes(20000) + bytes(rnd.randrange(256) for _ in range(12000))
        packed = lzsa2.compress_in_place(data, 32)
        self.assertEqual(lzsa2.decode(packed)[0], data)
        self.assertEqual(lzsa2.unpack_in_place(packed, len(data), 32), data)
        for name, data in self.cases().items():
            with self.subTest(name):
                if len(data) > 0xFFFF:
                    continue
                packed = lzsa2.compress_in_place(data, 32)
                self.assertEqual(lzsa2.unpack_in_place(packed, len(data), 32), data)

    @unittest.skipUnless(LZSA_TOOL, "official lzsa tool not built")
    def test_official_decoder(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name, data in self.cases().items():
                if len(data) > 0xFFFF:                  # the tool's raw blocks stop at 64 KB
                    continue
                with self.subTest(name):
                    src, dst = os.path.join(tmp, "in.lz"), os.path.join(tmp, "out.bin")
                    write_file(src, lzsa2.compress(data))
                    subprocess.run([LZSA_TOOL, "-d", "-r", "-f", "2", src, dst], check=True, capture_output=True)
                    self.assertEqual(read_file(dst), data)


class XmTest(unittest.TestCase):
    def module(self):
        ch = 2
        p0 = [[49, 1, 0x50, 0x0F, 3], [0, 0, 0, 0, 0]] + [[0, 0, 0, 0, 0]] * (2 * 63)
        p3 = [[97, 0, 0, 0, 0], [61, 2, 0, 0x0A, 0x0F]] + [[0, 0, 0, 0, 0]] * (2 * 15)
        patterns = [(64, p0), (32, None), (64, [[0, 0, 0, 0, 0]] * 128), (16, p3)]
        fwd = {"values": [0, 50, 127, -128, -50, 0, 10, 20], "type": 1, "loop": 2, "looplen": 4,
               "vol": 48, "fine": -16, "pan": 0x40, "rel": 3, "env": True}
        w16 = [0x1234, -0x1234, 0x7FFF, -0x8000, 0x00FF, -0x0001, 0x0080, 0x4000]
        pp16 = {"values": w16, "type": 0x12, "loop": 2, "looplen": 4}
        extra = {"values": [1, 2, 3], "type": 0}
        return make_xm(ch, patterns, [0, 1, 3, 0], [[fwd, extra], [pp16], []]), w16

    def test_pack(self):
        data, _ = self.module()
        mod = wsconv.parse_xm(data)
        r = wsconv.convert(mod)
        pack = r.pack
        size, name, channels, tmp, bpm, freq, patlen, patres = struct.unpack_from("<I20s6H", pack, 0)
        self.assertEqual(name, b"test song".ljust(20, b"\0"))
        self.assertEqual((channels, tmp, bpm, freq, patlen, patres), (2, 6, 125, 0, 4, 0))
        self.assertEqual(pack[36:40], bytes([0, 1, 3, 0]))
        self.assertEqual(r.patnum, 4)
        hsize = 292 + 40
        recs = [struct.unpack_from("<IIH", pack, 292 + 10 * n) for n in range(4)]
        self.assertEqual(recs[0][0], hsize)                       # PatREC[0].OFS = header size
        self.assertEqual(recs[1][1:], (2 * 32, 32))               # used but empty: explicit 0x80 cells
        self.assertEqual(pack[recs[1][0]:recs[1][0] + 64], b"\x80" * 64)
        self.assertEqual(recs[2], (hsize, 0, 0))                  # unused: valid pointer, no data
        self.assertEqual(size, len(pack) - hsize)
        rows, cells = cells_of(pack, 0)
        self.assertEqual(cells[0], [49, 1, 0x50, 0x0F, 3])
        rows, cells = cells_of(pack, 3)
        self.assertEqual((rows, cells[0][0], cells[1]), (16, 97, [61, 2, 0, 0x0A, 0x0F]))
        self.assertTrue(any("key-off" in w for w in r.warnings))
        self.assertTrue(any("envelopes" in w for w in r.warnings))
        self.assertTrue(any("2 samples" in w for w in r.warnings))

    def test_samples(self):
        data, w16 = self.module()
        r = wsconv.convert(wsconv.parse_xm(data))
        self.assertEqual(len(r.table), 3 * 21)
        ofs, length, loop, loop_end, vol, fine, stype, pan, rel = instrument(r.table, 0)
        self.assertEqual((ofs, length, loop, loop_end, vol, fine, stype, pan, rel), (63, 8, 2, 6, 48, -16, 1, 0x40, 3))
        s0 = r.stream[:8]
        # signed -> unsigned (XOR #80); -128 -> #00 -> #01 (0 is the firmware's loop marker)
        self.assertEqual(list(s0), [0x80, 0x80 + 50, 0xFF, 0x01, 0x80 - 50, 0x80, 0x8A, 0x94])
        ofs, length, loop, loop_end, vol, fine, stype, pan, rel = instrument(r.table, 1)
        self.assertEqual((ofs, length, loop, loop_end, stype), (63 + 8, 8, 2, 6, 2))   # ping-pong kept, & 3
        want = bytes((((v & 0xFFFF) >> 8) ^ 0x80) or 1 for v in w16)
        self.assertEqual(r.stream[8:16], want)                     # the high byte of each decoded word
        self.assertEqual(instrument(r.table, 2), (0, 0, 0, 0, 0, 0, 0, 0, 0))
        self.assertEqual(len(r.stream), 16)

    def test_masonxm_mode(self):
        data, w16 = self.module()
        r = wsconv.convert(wsconv.parse_xm(data), masonxm=True)
        recs = [struct.unpack_from("<IIH", r.pack, 292 + 10 * n) for n in range(r.patnum)]
        self.assertEqual(r.patnum, 4)
        self.assertEqual(recs[1][1], 0)                           # the empty pattern is left out
        self.assertEqual(recs[2], (0, 0, 0))
        ofs, length, loop, loop_end, vol, fine, stype, pan, rel = instrument(r.table, 1)
        self.assertEqual(stype, 0x12)                             # the raw type byte
        self.assertEqual((length, loop, loop_end), (2 + 4 + 3, 3, 7))   # ping-pong unrolled
        self.assertNotEqual(r.stream[8:10], bytes((((v & 0xFFFF) >> 8) ^ 0x80) or 1 for v in w16[:2]))

    def test_pattern0_unused(self):
        cells = [[0, 0, 0, 0, 0]] * 4
        data = make_xm(1, [(4, cells), (4, cells)], [1], [[{"values": [1, 2], "type": 0}]])
        r = wsconv.convert(wsconv.parse_xm(data))
        self.assertEqual(r.patnum, 2)
        self.assertEqual(struct.unpack_from("<IIH", r.pack, 292), (312, 0, 0))
        self.assertEqual(struct.unpack_from("<IIH", r.pack, 302), (312, 4, 4))

    def test_no_loop(self):
        data = make_xm(1, [(1, [[0, 0, 0, 0, 0]])], [0], [[{"values": [5] * 10, "type": 1, "loop": 4, "looplen": 0}],
                                                          [{"values": [5] * 10, "type": 3, "loop": 2, "looplen": 4}],
                                                          [{"values": [5] * 10, "type": 1, "loop": 6, "looplen": 9}]])
        r = wsconv.convert(wsconv.parse_xm(data))
        self.assertEqual(instrument(r.table, 0)[1:4] + (instrument(r.table, 0)[6],), (10, 10, 10, 0))
        self.assertEqual(instrument(r.table, 1)[1:4] + (instrument(r.table, 1)[6],), (10, 10, 10, 0))
        self.assertEqual(instrument(r.table, 2)[1:4] + (instrument(r.table, 2)[6],), (10, 6, 10, 1))

    def test_chunks_and_limits(self):
        rnd = random.Random(5)
        ch = 33
        cells = [[rnd.randrange(1, 96), rnd.randrange(1, 9), 0x20, 0x0C, 0x20] for _ in range(256 * ch)]
        ins = [[{"values": [rnd.randrange(-128, 128) for _ in range(500)], "type": 0}] for _ in range(62)]
        data = make_xm(ch, [(256, cells)], [0], ins)
        r = wsconv.convert(wsconv.parse_xm(data))
        tc, ic = wsconv.chunks(r)
        self.assertEqual(len(tc), 2)
        self.assertEqual(len(tc[0]), 32768)
        self.assertEqual(struct.unpack_from("<I", tc[0], 0)[0], len(r.pack))
        self.assertEqual(tc[0][4:] + tc[1], r.pack)
        self.assertEqual(ic[0], r.table)
        self.assertEqual(b"".join(ic[1:]), r.stream)
        self.assertTrue(all(len(c) == 32768 for c in ic[1:-1]))
        self.assertTrue(any("33 channels" in w for w in r.warnings))
        self.assertTrue(any("62 instruments" in w for w in r.warnings))
        big = make_xm(1, [(1, [[0, 0, 0, 0, 0]])], [0], [[{"values": [0] * 600000, "type": 0}],
                                                         [{"values": [0] * 500000, "type": 0}]])
        r = wsconv.convert(wsconv.parse_xm(big))
        self.assertTrue(any("storage" in w for w in r.warnings))

    def test_cli_writes_files(self):
        data, _ = self.module()
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "song.xm")
            write_file(src, data)
            self.assertEqual(wsconv.main([src, "-o", tmp, "--quiet", "--title", "Title"]), 0)
            trk = read_file(os.path.join(tmp, "song.trk00"))
            self.assertEqual(trk[8:28], b"Title".ljust(20, b"\0"))
            self.assertTrue(os.path.exists(os.path.join(tmp, "song.ins00")))
            self.assertTrue(os.path.exists(os.path.join(tmp, "song.ins01")))


class S3mTest(unittest.TestCase):
    EFFECTS = [  # (S3M letter, info) -> XM (effect, param) or None = dropped
        ("A", 0x06, (0x0F, 0x06)), ("T", 0x80, (0x0F, 0x80)), ("T", 0x10, None), ("B", 0x02, (0x0B, 0x01)),
        ("C", 0x32, (0x0D, 0x32)), ("C", 0x70, (0x0D, 0x00)), ("D", 0x05, (0x0A, 0x05)), ("D", 0x50, (0x0A, 0x50)),
        ("D", 0xF3, (0x0E, 0xB3)), ("D", 0x3F, (0x0E, 0xA3)), ("D", 0x00, (0x0A, 0x00)),
        ("E", 0x10, (0x02, 0x10)), ("E", 0xF2, (0x0E, 0x22)), ("E", 0xE3, (0x21, 0x23)),
        ("F", 0x10, (0x01, 0x10)), ("F", 0xF2, (0x0E, 0x12)), ("F", 0xE3, (0x21, 0x13)),
        ("G", 0x10, (0x03, 0x10)), ("H", 0x45, (0x04, 0x45)), ("I", 0x23, (0x1D, 0x23)), ("J", 0x37, (0x00, 0x37)),
        ("K", 0x05, (0x06, 0x05)), ("L", 0x50, (0x05, 0x50)), ("O", 0x10, (0x09, 0x10)), ("Q", 0x12, (0x1B, 0x12)),
        ("R", 0x34, (0x07, 0x34)), ("S", 0x12, (0x0E, 0x32)), ("S", 0x25, (0x0E, 0x55)), ("S", 0x31, (0x0E, 0x41)),
        ("S", 0x42, (0x0E, 0x72)), ("S", 0x8F, (0x08, 0xFF)), ("S", 0xB2, (0x0E, 0x62)), ("S", 0xC3, (0x0E, 0xC3)),
        ("S", 0xD4, (0x0E, 0xD4)), ("S", 0xE5, (0x0E, 0xE5)), ("X", 0x40, (0x08, 0x80)), ("X", 0x80, (0x08, 0xFF)),
        ("V", 0x30, (0x10, 0x30)), ("U", 0x12, None), ("S", 0x91, None),
    ]

    def module(self):
        rows0 = [[(0, 0x40, 1, None, None, None), (1, 0x7B, 2, 32, None, None)],   # C-4, B-7 + volume 32
                 [(0, 254, None, None, None, None), (1, 254, None, 10, 1, 4)]]      # ^^ x2
        for k, (letter, info, _) in enumerate(self.EFFECTS):
            rows0.append([(0, None, None, None, ord(letter) - 64, info)])
        rows1 = [[(0, 0x30, 1, None, None, None), (3, 0x40, 1, None, None, None)]]  # channel 3 is disabled
        rnd = random.Random(9)
        s8 = bytes(rnd.randrange(256) for _ in range(100))
        s16 = struct.pack("<50H", *[rnd.randrange(65536) for _ in range(50)])
        samples = [{"data": s8, "length": 100, "loop": (10, 90), "vol": 50, "c2spd": 16726},
                   {"data": s16, "length": 50, "flags": 4, "vol": 70},
                   None]
        data = make_s3m([0, 254, 1, 255, 0], [rows0, rows1], samples, [0, 8, 1, 0x83], gvol=48, speed=4, tempo=140)
        return data, s8, s16

    def test_header_orders_channels(self):
        data, _, _ = self.module()
        mod = wsconv.parse_s3m(data)
        r = wsconv.convert(mod)
        size, name, channels, tmp, bpm, freq, patlen, patres = struct.unpack_from("<I20s6H", r.pack, 0)
        self.assertEqual(name, b"s3m unit test".ljust(20, b"\0"))
        self.assertEqual((channels, tmp, bpm, freq, patlen, patres), (3, 4, 140, 0, 2, 0))
        self.assertEqual(r.pack[36:39], bytes([0, 1, 0]))
        self.assertEqual(r.instruments, 3)

    def test_notes_and_effects(self):
        data, _, _ = self.module()
        mod = wsconv.parse_s3m(data)
        r = wsconv.convert(mod)
        rows, cells = cells_of(r.pack, 0)
        self.assertEqual(rows, 64)
        row = lambda n: cells[3 * n:3 * n + 3]                    # noqa: E731
        # row 0: notes, the volume column, the injected channel pans (8xx); no cell left for G
        self.assertEqual(row(0)[0], [49, 1, 0, 0x08, 0x33])
        self.assertEqual(row(0)[1], [96, 2, 0x30, 0x08, 0xCC])
        self.assertEqual(row(0)[2], [0, 0, 0, 0x08, 0x33])
        # row 1: note cut -> volume 0, or C00 when the volume column is taken
        self.assertEqual(row(1)[0], [0, 0, 0x10, 0, 0])
        self.assertEqual(row(1)[1][:3], [0, 0, 0x10 + 10])
        for k, (letter, info, want) in enumerate(self.EFFECTS):
            with self.subTest(f"{letter}{info:02X}"):
                got = row(2 + k)[0][3:]
                self.assertEqual(got, list(want) if want and want != (0, 0) else [0, 0])
        joined = "\n".join(r.warnings)
        for key in ("Txx below 20", "Uxx", "S9x", "1 initial global volume", "1 note cut(s) lost"):
            self.assertIn(key, joined)
        rows, cells = cells_of(r.pack, 1)
        self.assertEqual(cells[0][:2], [37, 1])                   # C-3; channel 3 (disabled) dropped
        self.assertEqual(sum(1 for c in cells if c[0]), 1)

    def test_samples(self):
        data, s8, s16 = self.module()
        r = wsconv.convert(wsconv.parse_s3m(data))
        ofs, length, loop, loop_end, vol, fine, stype, pan, rel = instrument(r.table, 0)
        self.assertEqual((ofs, length, loop, loop_end, vol, stype, pan, rel, fine), (63, 100, 10, 90, 50, 1, 0x80, 12, 0))
        self.assertEqual(r.stream[:100], bytes(b or 1 for b in s8))          # unsigned stays unsigned
        ofs, length, loop, loop_end, vol, fine, stype, pan, rel = instrument(r.table, 1)
        self.assertEqual((length, loop, loop_end, vol, stype, rel), (50, 50, 50, 64, 0, 0))
        words = struct.unpack("<50H", s16)
        self.assertEqual(r.stream[100:150], bytes((w >> 8) or 1 for w in words))
        self.assertEqual(instrument(r.table, 2), (0,) * 9)

    def test_signed_samples(self):
        smp = bytes([0, 1, 0x7F, 0x80, 0xFF])
        data = make_s3m([0], [[]], [{"data": smp, "length": 5}], [0], ffi=1, stereo=False, gvol=40)
        r = wsconv.convert(wsconv.parse_s3m(data))
        self.assertEqual(r.stream, bytes([0x80, 0x81, 0xFF, 0x01, 0x7F]))
        rows, cells = cells_of(r.pack, 0)
        self.assertEqual(cells[0], [0, 0, 0, 0x10, 40])           # mono: no pans; global volume 40 -> G28


@unittest.skipUnless(PROSIAK, "prosiak.exe not found")
class LinkRealTest(unittest.TestCase):
    @unittest.skipUnless(AUTHOR_MUSIC, "the author's MasonXM folder is absent")
    def test_relink_prosiak(self):
        prosiak = read_file(PROSIAK)
        tc, ic = wslink.find_chunks(os.path.join(AUTHOR_MUSIC, "Prosiak"))
        exe, report = wslink.link(prosiak, tc, ic, None)
        wslink.verify(exe, prosiak, tc, ic, None, LZSA_TOOL)
        orig = wslink.unpack_exe(prosiak)
        new = wslink.unpack_exe(exe)
        self.assertEqual(len(new["blocks"]), 8)
        for k in range(8):
            self.assertEqual(new["blocks"][k]["data"], orig["blocks"][k]["data"], f"block {k}")
            self.assertEqual(new["blocks"][k]["call"], orig["blocks"][k]["call"])
            self.assertEqual(new["blocks"][k]["out"], orig["blocks"][k]["out"])
        self.assertEqual(new["loader"], orig["loader"])
        self.assertEqual(exe[:4] + exe[8:0x100], prosiak[:4] + prosiak[8:0x100])
        self.assertEqual(exe[0x180:0x180 + orig["blocks"][0]["bc"]], prosiak[0x180:0x180 + orig["blocks"][0]["bc"]])

    def test_original_records_unpack_in_place(self):
        info = wslink.unpack_exe(read_file(PROSIAK))       # emulates every record
        self.assertEqual([len(b["data"]) for b in info["blocks"]],
                         [0x8D5, 0x4AA7, 0x1B9, 0x8000, 0x8000, 0x8000, 0x8000, 0x5852])


def fake_prosiak():
    """A file with prosiak.exe's layout: header, script, block 0 (random 'player'), loader with the banner."""
    rnd = random.Random(11)
    player = bytes(rnd.randrange(256) for _ in range(0x8D5))
    rec0 = struct.pack("<H", len(player)) + lzsa2.compress_in_place(player, 32)
    loader = bytearray(rnd.randrange(256) for _ in range(0x6AC))
    at = wslink.BANNER_ADDR - wslink.LOADER_LOAD
    loader[at:at + len(wslink.BANNER_ORIG)] = wslink.BANNER_ORIG
    hl0 = 0x4500 + len(player) - len(rec0)
    script = struct.pack("<BBHHH", 5, 0x80, 0, hl0, len(rec0)) + bytes([0x24, 0x00, 0x45, 0x24, 0xC0, 0x47, 0x24, 0x1C, 0x01])
    exe = bytearray(0x100)
    exe[0:4] = b"EXE\0"
    struct.pack_into("<II", exe, 4, 0x180 + len(rec0), len(loader))
    struct.pack_into("<HHH", exe, 0x10, 0x4327, 0x4409, 0x40FF)
    exe += script + bytes(0x80 - len(script)) + rec0 + loader
    return bytes(exe), player


class LinkTest(unittest.TestCase):
    def chunks(self, samples):
        rnd = random.Random(4)
        cells = [[rnd.randrange(1, 96), 1, 0, 0, 0] if rnd.random() < 0.3 else [0, 0, 0, 0, 0] for _ in range(4 * 64)]
        ins = [[{"values": [int(100 * ((i * 37) % 17 - 8) / 8) + rnd.randrange(-3, 4) for i in range(n)],
                 "type": 1, "loop": 0, "looplen": n}] for n in samples]
        r = wsconv.convert(wsconv.parse_xm(make_xm(4, [(64, cells)], [0, 0], ins, name=b"Link Test")))
        return wsconv.chunks(r)

    def test_link_round_trip(self):
        prosiak, player = fake_prosiak()
        tc, ic = self.chunks([20000, 30000, 9000])
        exe, report = wslink.link(prosiak, tc, ic, "Link Test")
        info = wslink.verify(exe, prosiak, tc, ic, "Link Test", LZSA_TOOL)
        self.assertEqual(info["blocks"][0]["data"], player)
        self.assertEqual(info["blocks"][0]["offset"], 0x180)
        self.assertEqual([b["call"] for b in info["blocks"]],
                         [0x4500, 0x4755, 0x4796] + [0x47AB] * (len(ic) - 1))
        self.assertEqual([b["data"] for b in info["blocks"][1:]], tc + ic)
        for b in info["blocks"][1:]:
            self.assertEqual(b["out"], 0x4E00)
            self.assertLessEqual(b["hl"] + b["bc"] + 32, 0x10000)
            self.assertGreaterEqual(b["hl"], 0x4E00)
        at = wslink.BANNER_ADDR - wslink.LOADER_LOAD
        self.assertEqual(info["loader"][at:at + 12], b"Link Test\r\n\0")
        self.assertEqual(struct.unpack_from("<I", exe, 4)[0], len(exe) - 0x6AC)

    def test_long_script_moves_block0(self):
        prosiak, player = fake_prosiak()
        tc, ic = self.chunks([30000] * 12)                         # > 10 chunks: the script passes #180
        exe, report = wslink.link(prosiak, tc, ic, None)
        info = wslink.verify(exe, prosiak, tc, ic, None)
        self.assertGreater(info["blocks"][0]["offset"], 0x180)
        self.assertEqual(info["blocks"][0]["data"], player)
        self.assertEqual(len(info["blocks"]), 1 + len(tc) + len(ic))

    def test_two_track_chunks(self):
        prosiak, _ = fake_prosiak()
        rnd = random.Random(8)
        cells = [[rnd.randrange(1, 96), rnd.randrange(1, 9), 0x20, 0x0C, rnd.randrange(64)] for _ in range(256 * 32)]
        r = wsconv.convert(wsconv.parse_xm(make_xm(32, [(256, cells)], [0], [[{"values": [1, 2, 3], "type": 0}]])))
        tc, ic = wsconv.chunks(r)
        self.assertEqual(len(tc), 2)
        exe, _ = wslink.link(prosiak, tc, ic, None)
        info = wslink.verify(exe, prosiak, tc, ic, None)
        self.assertEqual([b["call"] for b in info["blocks"][1:3]], [0x4755, 0x4755])

    def test_bad_prefix(self):
        prosiak, _ = fake_prosiak()
        tc, ic = self.chunks([100])
        bad = [struct.pack("<I", 999) + tc[0][4:]]
        with self.assertRaises(ValueError):
            wslink.link(prosiak, bad, ic, None)
        with self.assertRaises(ValueError):
            wslink.link(prosiak, tc, ic, "x" * 20)


@unittest.skipUnless(AUTHOR_MUSIC, "the author's MasonXM folder is absent")
class MasonXmTest(unittest.TestCase):
    def test_masonxm_mode_is_byte_identical(self):
        for module, prefix in wsconv.AUTHOR_TRACKS:
            with self.subTest(module):
                ref_pack, ref_tc, ref_ic = wsconv.read_masonxm(AUTHOR_MUSIC, prefix)
                mod = wsconv.load(os.path.join(AUTHOR_MUSIC, module))
                r = wsconv.convert(mod, masonxm=True, title=ref_pack[4:24].decode("latin-1"))
                tc, ic = wsconv.chunks(r)
                self.assertEqual(r.pack, ref_pack)
                self.assertEqual(tc, ref_tc)
                self.assertEqual(ic, ref_ic)

    def test_differences_explained(self):
        lines = []
        self.assertEqual(wsconv.check_against(AUTHOR_MUSIC, out=lines.append), 0)
        self.assertNotIn("UNEXPLAINED", "\n".join(lines))


if __name__ == "__main__":
    unittest.main()
