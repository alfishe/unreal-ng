#!/usr/bin/env python3
"""Self-tests of accept.py: the oracle tells a correct cut from one a byte off, a wrong width, a piece of a larger
picture and random bytes, a scrolled backdrop under sprites from one a byte off, and a font from the same font moved
by a character. python3 test_accept.py"""

import os
import random
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import accept  # noqa: E402

rng = random.Random(7)


def blank_screen():
    return np.zeros((192, 256), dtype=bool)


def sprite_bits(rows):
    """rows of bytes -> bool picture"""
    return np.unpackbits(np.array(rows, dtype=np.uint8), axis=1).astype(bool)


def put(shot, picture, y, x):
    h, w = picture.shape
    shot[y:y + h, x:x + w] |= picture


def region(kind, start, data_len, **geometry):
    return {'kind': kind, 'start': f'${start:04X}', 'end': f'${start + data_len - 1:04X}', 'geometry': geometry}


# A 16 x 16 man: a shape no shift or cut of it repeats
MAN = [[0x03, 0xC0], [0x07, 0xE0], [0x07, 0xE0], [0x03, 0xC0], [0x01, 0x80], [0x1F, 0xF8], [0x3F, 0xFC],
       [0x6F, 0xF6], [0x4F, 0xF2], [0x0F, 0xF0], [0x0E, 0x70], [0x0C, 0x30], [0x0C, 0x30], [0x18, 0x18],
       [0x30, 0x0C], [0x70, 0x0E]]
MAN_BYTES = bytes(b for row in MAN for b in row)


def font_bytes():
    """96 distinct, non-empty glyphs (space empty)"""
    out = bytearray(8)
    for code in range(33, 128):
        g = [rng.randrange(1, 255) for _ in range(6)]
        out += bytes([0] + g + [0])
    return bytes(out)


class Sprites(unittest.TestCase):
    def setUp(self):
        self.shots = []
        for i, (y, x) in enumerate(((40, 37), (100, 130), (150, 8))):
            s = blank_screen()
            put(s, sprite_bits(MAN), y, x)
            self.shots.append((f'{i}.scr', s))

    def check(self, data, **geometry):
        return accept.check_sprite(region('sprite', 0x8000, len(data), **geometry), data, self.shots)

    def test_correct_cut_is_verified(self):
        r = self.check(MAN_BYTES, width=16, height=16, frames=1, stride=32)
        self.assertEqual(r['status'], 'verified', r)

    def test_cut_a_byte_off_is_rejected(self):
        r = self.check(b'\x00' + MAN_BYTES[:-1], width=16, height=16, frames=1, stride=32)
        self.assertNotEqual(r['status'], 'verified', r)

    def test_wrong_width_is_rejected(self):
        r = self.check(MAN_BYTES, width=8, height=32, frames=1, stride=32)
        self.assertNotEqual(r['status'], 'verified', r)
        r = self.check(MAN_BYTES[:30], width=24, height=10, frames=1, stride=30)
        self.assertNotEqual(r['status'], 'verified', r)

    def test_random_bytes_are_rejected(self):
        data = bytes(rng.randrange(256) for _ in range(32))
        r = self.check(data, width=16, height=16, frames=1, stride=32)
        self.assertEqual(r['status'], 'unverified', r)

    def test_top_half_is_a_fragment(self):
        r = self.check(MAN_BYTES[:16], width=16, height=8, frames=1, stride=16)
        self.assertEqual(r['status'], 'fragment', r)
        self.assertIn('bottom', r['fragment'])

    def test_columns_layout(self):
        cols = bytes(MAN_BYTES[y * 2 + c] for c in range(2) for y in range(16))
        r = self.check(cols, width=16, height=16, frames=1, stride=32, layout='columns')
        self.assertEqual(r['status'], 'verified', r)
        r = self.check(cols, width=16, height=16, frames=1, stride=32)
        self.assertNotEqual(r['status'], 'verified', r)

    def test_a_window_of_a_wider_picture(self):
        # the man's rows inside a picture 5 bytes wide: pitch 5, the claim names the 2 bytes shown
        wide = bytearray()
        for row in MAN:
            wide += bytes(row) + bytes([0x81, 0x42, 0x24])
        self.assertEqual(self.check(bytes(wide), width=16, height=len(MAN), pitch=5)['status'], 'verified')
        self.assertNotEqual(self.check(bytes(wide), width=16, height=len(MAN))['status'], 'verified')

    def test_masked_and_shifted(self):
        s = blank_screen()
        # 3 pixels right of a byte boundary, on a busy background the mask cleared
        s[:, :] = (np.indices((192, 256)).sum(axis=0) % 2).astype(bool)
        pic = sprite_bits(MAN)
        cover = np.zeros_like(pic)
        cover[:, :] = True
        s[60:76, 83:99] = pic
        shots = [('m.scr', s)]
        data = bytes(v for b in MAN_BYTES for v in (0x00, b))
        r = accept.check_sprite(region('sprite', 0x8000, len(data), width=16, height=16, mask=True, stride=64),
                                data, shots)
        self.assertEqual(r['status'], 'verified', r)

    def test_a_wall_of_one_tile_is_no_fragment(self):
        tile = [[0xFF], [0x81], [0xBD], [0xA5], [0xA5], [0xBD], [0x81], [0xFF]]
        s = blank_screen()
        for x in range(0, 256, 8):
            put(s, sprite_bits(tile), 64, x)
        data = bytes(b[0] for b in tile)
        r = accept.check_sprite(region('tile', 0x8000, 8, width=8, height=8, stride=8), data, [('w.scr', s)])
        self.assertEqual(r['status'], 'verified', r)


def landscape(rows=60, cols=30):
    """Hills and stripes, each row different: a backdrop's bytes"""
    out = bytearray()
    for y in range(rows):
        for x in range(cols):
            out.append(((x * 37 + y * 11) ^ (y * y)) & 0xFF if y % 7 else 0x55)
    return bytes(out)


class Backdrops(unittest.TestCase):
    """A backdrop 240 pixels wide copied to the screen moved right by part of a byte, each band its own, a sprite over
    it"""
    def setUp(self):
        self.data = landscape()
        pic = sprite_bits([list(self.data[r * 30:r * 30 + 30]) for r in range(60)])
        s = blank_screen()
        for r in range(60):
            d = (0, 4, 2)[r // 20]
            s[40 + r, 8 + d:248] = pic[r, :240 - d]
            s[40 + r, 8:8 + d] = True  # the bits the copy carried in
        put(s, sprite_bits(MAN), 60, 120)
        self.shots = [('b.scr', s)]

    def check(self, data, around=None):
        return accept.check_sprite(region('sprite', 0x8000, len(data), width=240, height=60), data, self.shots,
                                   around)

    def test_backdrop_is_verified(self):
        r = self.check(self.data)
        self.assertEqual(r['status'], 'verified', r)
        self.assertEqual((r['placements'][0]['y'], r['placements'][0]['x']), (40, 8))

    def test_backdrop_a_byte_or_a_row_off_is_rejected(self):
        for data in (self.data[1:] + bytes(1), bytes(1) + self.data[:-1]):
            r = self.check(data)
            self.assertNotEqual(r['status'], 'verified', r)
        # a row short at the top: the row before it in memory shows above it
        r = self.check(self.data[30:] + bytes(30), (self.data[:30], None))
        self.assertEqual(r['status'], 'fragment', r)
        self.assertEqual(self.check(self.data, (bytes(30), None))['status'], 'verified')
        # a row of something else at the bottom, the blank top row left out: no row before it to tell
        top_blank = bytes(30) + self.data[30:]
        r = accept.check_sprite(region('sprite', 0x8000, len(top_blank), width=240, height=60), top_blank[30:] +
                                bytes(range(7, 247, 8)), self.shots)
        self.assertNotEqual(r['status'], 'verified', r)


class Fonts(unittest.TestCase):
    def setUp(self):
        self.font = font_bytes()
        self.folder = tempfile.mkdtemp(prefix='accept-test-')
        texts = [b'PRESS FIRE TO START', b'HIGH SCORE 012345', b'GAME OVER']
        memory = bytearray(65536)
        at = 0x9000
        for t in texts:
            memory[at:at + len(t)] = t
            at += len(t) + 3
        with open(os.path.join(self.folder, 'ram.bin'), 'wb') as f:
            f.write(memory)
        s = blank_screen()
        for row, t in enumerate(texts):
            for col, ch in enumerate(t):
                g = self.font[(ch - 32) * 8:(ch - 32) * 8 + 8]
                put(s, sprite_bits([[b] for b in g]), (row * 3 + 2) * 8, (col + 4) * 8)
        self.shots = [('f.scr', s)]
        self.memory = accept.Memory(self.folder)

    def check(self, data):
        r = region('font', 0x6000, len(data), width=8, height=8, frames=96, stride=8)
        return accept.check_font(r, data, self.shots, self.memory)

    def test_font_is_verified(self):
        res, cells = self.check(self.font)
        self.assertEqual(res['status'], 'verified', res)
        self.assertGreater(len(cells), 30)

    def test_font_moved_by_a_character_is_rejected(self):
        res, _ = self.check(self.font[8:] + bytes(8))
        self.assertNotEqual(res['status'], 'verified', res)
        res, _ = self.check(bytes(8) + self.font[:-8])
        self.assertNotEqual(res['status'], 'verified', res)

    def test_font_off_by_bytes_is_rejected(self):
        res, _ = self.check(self.font[3:] + bytes(3))
        self.assertNotEqual(res['status'], 'verified', res)

    def rom_font(self):
        return accept.read_file(accept.REFERENCE_ROM)[0x3D00:0x4000]

    def test_font_by_shapes_is_verified(self):
        # the screen shows the font's glyphs, but no string the game holds: recognised by shape
        font = bytearray(self.rom_font())
        for i in range(len(font)):  # a bold variant, not the ROM's own bytes
            font[i] |= font[i] >> 1
        s = blank_screen()
        for col, ch in enumerate(b'012345'):
            put(s, sprite_bits([[b] for b in font[(ch - 32) * 8:(ch - 32) * 8 + 8]]), 16, (col + 4) * 8)
        empty = tempfile.mkdtemp(prefix='accept-test-')
        with open(os.path.join(empty, 'ram.bin'), 'wb') as f:
            f.write(bytes(65536))
        r = region('font', 0x6000, 768, width=8, height=8, frames=96, stride=8)
        res, _ = accept.check_font(r, bytes(font), [('d.scr', s)], accept.Memory(empty))
        self.assertEqual(res['status'], 'verified', res)
        for moved in (bytes(font[8:]) + bytes(8), bytes(16) + bytes(font[:-16])):
            res, _ = accept.check_font(r, moved, [('d.scr', s)], accept.Memory(empty))
            self.assertNotEqual(res['status'], 'verified', res)

    def test_font_of_own_codes(self):
        # 0..9 then A..Z, 36 bold glyphs (codes 0..35 of the game's own), on the screen as a score and a word
        rom = self.rom_font()
        font = bytearray()
        for ch in b'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ':
            font += bytes(b | (b >> 1) for b in rom[(ch - 32) * 8:(ch - 32) * 8 + 8])
        s = blank_screen()
        for col, ch in enumerate(b'SCORE 0420'):
            if ch != 32:
                i = ch - 48 if ch < 65 else ch - 55
                put(s, sprite_bits([[b] for b in font[8 * i:8 * i + 8]]), 16, (col + 4) * 8)
        r = region('font', 0x6000, len(font), width=8, height=8, frames=36, stride=8)
        res, cells = accept.check_font(r, bytes(font), [('o.scr', s)], self.memory)
        self.assertEqual(res['status'], 'verified', res)
        self.assertGreaterEqual(len(cells), 9)
        for moved in (bytes(font[3:]) + bytes(3), bytes(5) + bytes(font[:-5])):
            res, _ = accept.check_font(r, moved, [('o.scr', s)], self.memory)
            self.assertNotEqual(res['status'], 'verified', res)
        res, _ = accept.check_font(r, bytes(rng.randrange(256) for _ in range(len(font))), [('o.scr', s)],
                                   self.memory)
        self.assertNotEqual(res['status'], 'verified', res)

    def test_font_of_own_shapes_by_placement(self):
        # 40 glyphs like no character, printed on the cells: told by where they show, not by their shapes
        font = bytes(b for _ in range(40) for b in [rng.randrange(1, 255) for _ in range(8)])
        s = blank_screen()
        for cell in range(96):
            i = (cell * 7) % 40
            put(s, sprite_bits([[b] for b in font[8 * i:8 * i + 8]]), (cell // 24) * 16 + 8, (cell % 24 + 4) * 8)
        r = region('font', 0x6000, len(font), width=8, height=8, frames=40, stride=8)
        res, _ = accept.check_font(r, font, [('p.scr', s)], self.memory)
        self.assertEqual((res['status'], res.get('by')), ('verified', 'placement'), res)
        for moved in (font[3:] + bytes(3), bytes([0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81]) + font[:-8]):
            res, _ = accept.check_font(r, moved, [('p.scr', s)], self.memory)
            self.assertNotEqual(res['status'], 'verified', res)

    def test_random_bytes_are_no_font(self):
        res, _ = self.check(bytes(rng.randrange(256) for _ in range(768)))
        self.assertNotEqual(res['status'], 'verified', res)


if __name__ == '__main__':
    unittest.main()
