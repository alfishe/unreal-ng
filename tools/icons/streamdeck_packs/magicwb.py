#!/usr/bin/env python3
"""The MagicWB icon pack: every glyph of data/streamdeck/icons/glyphs.json as 32 x 32 pixel art.

    python3 tools/icons/streamdeck_packs/magicwb.py     # writes data/streamdeck/icons/packs/magicwb/

The look of Amiga MagicWB (original drawings; only the palette and the technique are imitated): the 8-colour MagicWB
palette, a black outline, white light from the top left, shade bottom right with a dithered band, on a Workbench 2
button (white / black bevel). A toggle's "on" face is the button pressed in (bevel inverted) and lit blue.
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

import streamdeck_pixel as sp  # noqa: E402

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
PACK_DIR = os.path.join(ROOT_DIR, 'data', 'streamdeck', 'icons', 'packs', 'magicwb')
N = 32

PALETTE = {'g': (149, 149, 149), 'k': (0, 0, 0), 'w': (255, 255, 255), 'b': (59, 103, 162), 'd': (123, 123, 123),
           'l': (175, 175, 175), 'e': (170, 144, 124)}
LO = {'b': 'k', 'l': 'd', 'e': 'd', 'd': 'k', 'w': 'l', 'g': 'd', 'k': 'k'}


class Style:
    outline = 'k'
    dither = True

    @staticmethod
    def hi(c):
        return 'w' if c != 'w' else 'w'

    @staticmethod
    def lo(c):
        return LO[c]


class Glyph(sp.Canvas):
    def __init__(self, on=False):
        super().__init__(N, 'b' if on else 'g', Style)
        self.on = on
        hi, lo = ('k', 'w') if on else ('w', 'k')
        self.hline(0, N - 1, 0, hi)
        self.vline(0, 0, N - 1, hi)
        self.hline(0, N - 1, N - 1, lo)
        self.vline(N - 1, 0, N - 1, lo)

    def solid(self, mask, colour, **kw):
        if self.on and colour == 'b':
            colour = 'l'
        super().solid(mask, colour, **kw)


# ---------------------------------------------------------------------------
# Building blocks (pixel coordinates on the 32 grid; the button bevel is the outer ring, content in 3..28)
# ---------------------------------------------------------------------------

def tri_right(c, x0, y0, x1, y1, col='b'):
    c.solid(c.m.poly([(x0, y0), (x1 + 1, (y0 + y1 + 1) / 2), (x0, y1 + 1)]), col)


def tri_left(c, x0, y0, x1, y1, col='b'):
    c.solid(c.m.poly([(x1 + 1, y0), (x0, (y0 + y1 + 1) / 2), (x1 + 1, y1 + 1)]), col)


def tri_up(c, x0, y0, x1, y1, col='b'):
    c.solid(c.m.poly([(x0, y1 + 1), ((x0 + x1 + 1) / 2, y0), (x1 + 1, y1 + 1)]), col)


def bar(c, x0, y0, x1, y1, col='b', r=0):
    c.solid(c.m.rect(x0, y0, x1, y1, r), col)


def arrow(c, x0, y0, x1, y1, col='b', shaft=4, head=11, head_len=6):
    c.solid(c.m.poly(sp.arrow_pts(x0, y0, x1, y1, shaft, head, head_len)), col)


def dot(c, cx, cy, r, col='e'):
    c.solid(c.m.circle(cx + 0.5, cy + 0.5, r), col)


def ring(c, cx, cy, r_out, r_in, col):
    c.solid(c.m.circle(cx + 0.5, cy + 0.5, r_out) & ~c.m.circle(cx + 0.5, cy + 0.5, r_in), col)


def plus_mark(c, cx, cy, col='w'):
    m = c.m.rect(cx - 3, cy - 1, cx + 3, cy + 1) | c.m.rect(cx - 1, cy - 3, cx + 1, cy + 3)
    c.solid(m, col, bevel=False)


def minus_mark(c, cx, cy, col='w'):
    c.solid(c.m.rect(cx - 3, cy - 1, cx + 3, cy + 1), col, bevel=False)


def cross_mark(c, cx, cy, col='w', s=3):
    m = c.m.thick([(cx - s, cy - s), (cx + s, cy + s)], 2.2) | c.m.thick([(cx + s, cy - s), (cx - s, cy + s)], 2.2)
    c.solid(m, col, bevel=False)


def chevron(c, cx, cy, size, direction, col='b', w=4.5):
    s = size / 2
    pts = {'right': [(cx - s / 2, cy - s), (cx + s / 2, cy), (cx - s / 2, cy + s)],
           'left': [(cx + s / 2, cy - s), (cx - s / 2, cy), (cx + s / 2, cy + s)],
           'up': [(cx - s, cy + s / 2), (cx, cy - s / 2), (cx + s, cy + s / 2)],
           'down': [(cx - s, cy - s / 2), (cx, cy + s / 2), (cx + s, cy - s / 2)]}[direction]
    c.solid(c.m.thick(pts, w), col)


def code_lines(c, x0=10, rows=(6, 11, 16, 21), lens=(16, 12, 15, 9), hl=1):
    for i, (y, ln) in enumerate(zip(rows, lens)):
        bar(c, x0, y, x0 + ln, y + 2, 'w' if i == hl else 'l')


def raster(c, x0=9, x1=27, skip=None, hl=None):
    for i, y in enumerate((5, 10, 15, 20, 25)):
        if i == skip:
            continue
        bar(c, x0, y, x1, y + 1, 'b' if i == hl else 'l', )


def film(c, x0, y0, x1, y1):
    bar(c, x0, y0, x1, y1, 'd')
    c.box(x0 + 3, y0 + 3, x1 - 3, y1 - 3, 'l')
    c.box(x0 + 3, y0 + 3, x1 - 3, y0 + 3, 'w')
    for x in range(x0 + 2, x1 - 1, 3):
        c.px(x, y0 + 1, 'k')
        c.px(x, y1 - 1, 'k')


def cassette(c, x0=4, y0=8, x1=27, y1=24, label='e'):
    bar(c, x0, y0, x1, y1, 'd', r=1)
    c.box(x0 + 3, y0 + 2, x1 - 3, y0 + 9, label)
    c.hline(x0 + 3, x1 - 3, y0 + 2, 'w')
    c.vline(x0 + 3, y0 + 2, y0 + 9, 'w')
    cx = (x0 + x1) // 2
    wx0, wx1 = cx - 6, cx + 6
    c.box(wx0, y0 + 4, wx1, y0 + 8, 'k')
    for rx in (wx0 + 2, wx1 - 2):
        c.box(rx - 1, y0 + 5, rx + 1, y0 + 7, 'w')
        c.px(rx, y0 + 6, 'k')
    c.box(cx - 2, y0 + 6, cx + 2, y0 + 6, 'd')
    c.hline(cx - 6, cx + 6, y1 - 3, 'k')
    c.hline(cx - 5, cx + 5, y1 - 2, 'l')
    c.px(cx - 3, y1 - 2, 'k')
    c.px(cx + 3, y1 - 2, 'k')


def floppy(c, x0=5, y0=4, x1=26, y1=27, col='b'):
    c.solid(c.m.poly([(x0, y0), (x1 - 2, y0), (x1 + 1, y0 + 3), (x1 + 1, y1 + 1), (x0, y1 + 1)]), col)
    c.box(x0 + 5, y0 + 1, x1 - 5, y0 + 6, 'l')
    c.hline(x0 + 5, x1 - 5, y0 + 1, 'w')
    c.box(x1 - 9, y0 + 2, x1 - 7, y0 + 5, 'k')
    c.box(x0 + 3, y0 + 10, x1 - 3, y1 - 1, 'w')
    c.hline(x0 + 3, x1 - 3, y1 - 1, 'l')
    for y in range(y0 + 13, y1 - 2, 3):
        c.hline(x0 + 5, x1 - 5, y, 'd')


def clock(c, cx=16, cy=16, r=11, arrow_back=False):
    c.solid(c.m.circle(cx, cy, r), 'l')
    face = c.m.circle(cx, cy, r - 3)
    c.c[face] = 'w'
    for x, y in ((cx, cy - r + 4), (cx, cy + r - 5), (cx - r + 4, cy), (cx + r - 5, cy)):
        c.px(x, y, 'd')
    c.vline(cx, cy - 6, cy, 'k')
    c.line(cx, cy, cx + 4, cy + 2, 'k')
    if arrow_back:
        c.solid(c.m.poly([(0.5, 8), (12.5, 8), (6.5, 16)]), 'b')


def ribbon(c, x0=9, y0=4, x1=22, y1=27, col='b'):
    cx = (x0 + x1 + 1) / 2
    c.solid(c.m.poly([(x0, y0), (x1 + 1, y0), (x1 + 1, y1 + 1), (cx, y1 - 5), (x0, y1 + 1)]), col)


def octagon(c, cx, cy, r, col='e'):
    pts = [sp.polar(cx + 0.5, cy + 0.5, r, 22.5 + 45 * i) for i in range(8)]
    c.solid(c.m.poly(pts), col)


def speaker(c, x0=4):
    c.solid(c.m.poly([(x0, 11), (x0 + 5, 11), (x0 + 11, 5), (x0 + 11, 27), (x0 + 5, 21), (x0, 21)]), 'l')


def waves(c, x=16):
    c.solid(c.m.arc(x - 3, 16, 4.5, -50, 50, 2.4), 'b', bevel=False)
    c.solid(c.m.arc(x - 3, 16, 10, -45, 45, 2.4), 'b', bevel=False)


def chip(c, x0=6, y0=7, x1=25, y1=24, col='d'):
    for x in range(x0 + 2, x1 - 1, 3):
        c.box(x, y0 - 3, x + 1, y0 - 1, 'l')
        c.box(x, y1 + 1, x + 1, y1 + 3, 'l')
        c.px(x + 1, y0 - 3, 'k')
        c.px(x + 1, y1 + 3, 'k')
    bar(c, x0, y0, x1, y1, col)


def monitor(c, screen='b'):
    bar(c, 3, 4, 28, 22, 'l', r=1)
    c.box(6, 7, 25, 19, 'k')
    c.box(7, 8, 24, 18, screen)
    c.hline(7, 24, 8, 'w' if screen != 'w' else 'l')
    bar(c, 13, 23, 18, 25, 'd')
    bar(c, 8, 26, 23, 27, 'l')


def camera(c):
    bar(c, 11, 6, 20, 9, 'd')
    bar(c, 3, 9, 28, 25, 'l', r=1)
    ring(c, 15, 17, 6.5, 0, 'd')
    dot(c, 15, 17, 4.2, 'b')
    c.box(14, 15, 15, 16, 'w')
    c.box(23, 11, 26, 12, 'w')


def videocam(c, light=False):
    bar(c, 3, 10, 20, 24, 'l', r=1)
    c.solid(c.m.poly([(21, 14), (29, 9), (29, 26), (21, 21)]), 'd')
    if light is None:
        return
    dot(c, 8, 14, 2, 'e' if light else 'd')
    if light:
        c.px(7, 13, 'w')


def keycap(c, x0=4, y0=5, x1=27, y1=26):
    bar(c, x0, y0, x1, y1, 'd', r=2)
    bar(c, x0 + 3, y0 + 2, x1 - 3, y1 - 5, 'l', r=1)


def keyboard(c, x0=2, y0=9, x1=29, y1=24):
    bar(c, x0, y0, x1, y1, 'l', r=1)
    for row, y in enumerate((y0 + 3, y0 + 6, y0 + 9)):
        off = (row % 2) * 1
        for x in range(x0 + 3 + off, x1 - 2, 3):
            c.box(x, y, x + 1, y + 1, 'k')
            c.px(x, y, 'w')
    c.box(x0 + 8, y1 - 3, x1 - 8, y1 - 2, 'k')


def joystick(c):
    bar(c, 6, 21, 25, 27, 'd', r=1)
    bar(c, 14, 9, 17, 21, 'k')
    dot(c, 15, 8, 4.6, 'e')
    c.box(14, 6, 15, 7, 'w')
    dot(c, 22, 19, 1.6, 'e')


def listing(c, x0=5, y0=4, x1=26, y1=27):
    bar(c, x0, y0, x1, y1, 'w')
    for i, y in enumerate(range(y0 + 4, y1 - 2, 4)):
        c.hline(x0 + 3, x0 + 5, y, 'b')
        c.hline(x0 + 8, x1 - 3 - (i % 3) * 3, y, 'd')


def gear(c, cx=15.5, cy=15.5):
    pts = []
    for i in range(16):
        a = i * 22.5 - 11.25
        r = 12.5 if i % 2 == 0 else 9.5
        for da in (-6, 6):
            pts.append(sp.polar(cx + 0.5, cy + 0.5, r, a + da))
    c.solid(c.m.poly(pts) & ~c.m.circle(cx + 0.5, cy + 0.5, 4), 'l')


def switch(c, on):
    bar(c, 3, 12, 28, 19, 'e' if on else 'd', r=3)
    dot(c, 21 if on else 9, 15, 6.5, 'w' if on else 'l')


def gauge(c, angle):
    c.solid(c.m.circle(16, 22, 13) & c.m.rect(0, 0, 31, 22), 'l')
    c.c[c.m.circle(16, 22, 10) & c.m.rect(0, 0, 31, 21)] = 'w'
    for a in (180, 225, 270, 315, 360):
        x, y = sp.polar(15.5, 21.5, 8.5, a)
        c.px(int(round(x)), int(round(y)), 'd')
    x, y = sp.polar(15.5, 21.5, 9, angle)
    c.solid(c.m.thick([(15.5, 21), (x, y)], 2.6), 'k', bevel=False, outline=False)
    dot(c, 15, 21, 2.6, 'b')
    bar(c, 3, 23, 28, 25, 'd')


def lock(c, closed):
    bar(c, 7, 14, 24, 27, 'e', r=1)
    if closed:
        c.solid(c.m.arc(15, 14, 6.5, 180, 360, 3) | c.m.rect(7, 14, 10, 15) | c.m.rect(21, 14, 24, 15), 'l')
    else:
        c.solid(c.m.arc(20, 9, 6.5, 180, 360, 3) | c.m.rect(24, 9, 27, 13), 'l')
    c.box(15, 18, 16, 22, 'k')


def bug(c, body='e'):
    for (x0, y0), (x1, y1) in (((9, 14), (4, 11)), ((8, 19), (3, 19)), ((9, 24), (5, 27))):
        c.line(x0, y0, x1, y1, 'k')
        c.line(x0, y0 + 1, x1, y1 + 1, 'k')
        c.line(31 - x0, y0, 31 - x1, y1, 'k')
        c.line(31 - x0, y0 + 1, 31 - x1, y1 + 1, 'k')
    c.line(13, 7, 10, 3, 'k')
    c.line(18, 7, 21, 3, 'k')
    c.solid(c.m.ell(8, 10, 23, 28), body)
    c.solid(c.m.ell(11, 5, 20, 12), 'd')
    c.vline(15, 13, 27, 'k')
    c.vline(16, 13, 27, 'k')


def module(c, y0=6):
    bar(c, 4, y0, 27, y0 + 11, 'b')
    for x in range(6, 26, 3):
        c.box(x, y0 + 12, x + 1, y0 + 14, 'e')
    c.box(8, y0 + 3, 12, y0 + 7, 'k')
    c.box(19, y0 + 3, 23, y0 + 7, 'k')
    c.hline(8, 12, y0 + 3, 'd')
    c.hline(19, 23, y0 + 3, 'd')


def tray(c):
    c.solid(c.m.rect(3, 19, 28, 27) & ~c.m.rect(7, 19, 24, 23), 'l')


def house(c):
    bar(c, 7, 14, 24, 27, 'l')
    c.solid(c.m.poly([(2, 16), (16, 3), (30, 16)]), 'b')
    bar(c, 13, 19, 18, 27, 'e')


def folder(c, open_=False):
    bar(c, 3, 7, 13, 11, 'e')
    bar(c, 3, 10, 28, 26, 'e')
    if open_:
        c.solid(c.m.poly([(3, 26.9), (8, 15), (32, 15), (28, 26.9)]), 'l')
    else:
        c.hline(4, 27, 13, 'd')


def windows(c):
    bar(c, 3, 4, 20, 19, 'l')
    c.box(4, 5, 19, 7, 'b')
    bar(c, 11, 12, 28, 27, 'l')
    c.box(12, 13, 27, 15, 'b')


def sun(c):
    for i in range(8):
        x, y = sp.polar(15.5, 15.5, 11, i * 45)
        c.solid(c.m.thick([sp.polar(15.5, 15.5, 8, i * 45), (x, y)], 2.6), 'e', bevel=False)
    dot(c, 15, 15, 6, 'e')


def deck_grid(c):
    bar(c, 3, 6, 28, 26, 'd', r=1)
    for j in range(2):
        for i in range(3):
            x, y = 6 + i * 7, 9 + j * 8
            bar(c, x, y, x + 4, y + 4, 'b' if (i, j) == (0, 0) else 'l')


def bolt(c, col='e'):
    c.solid(c.m.poly([(18, 2), (7, 18), (14, 18), (11, 30), (25, 12), (18, 12), (22, 2)]), col)


def zx(c, dy=0):
    c.solid(c.m.poly([(2, 11 + dy), (29.9, 11 + dy), (29.9, 22 + dy), (2, 22 + dy)]), 'k')
    c.box(3, 12 + dy, 28, 12 + dy, 'd')
    for j, y in enumerate((14, 16, 18)):
        for x in range(4 + j, 22, 3):
            c.box(x, y + dy, x + 1, y + dy, 'l')
    for i, col in enumerate(('w', 'e', 'b', 'l')):
        for k in range(2):
            c.line(22 + i * 2 + k, 21 + dy, 26 + i * 2 + k, 13 + dy, col)
    c.solid(c.m.poly([(2, 22 + dy), (29.9, 22 + dy), (28, 25 + dy), (4, 25 + dy)]), 'd', bevel=False)


def magnifier(c, mark='pencil'):
    c.solid(c.m.thick([(19, 19), (27, 27)], 4.5), 'd')
    ring(c, 12, 12, 9, 6, 'l')
    c.c[c.m.circle(12.5, 12.5, 6)] = 'w'
    if mark == 'pencil':
        c.line(9, 15, 15, 9, 'b')
        c.line(9, 14, 14, 9, 'b')
    else:
        c.solid(c.m.rect(10, 10, 15, 15), 'b')


def bin_(c):
    bar(c, 5, 6, 26, 8, 'l')
    bar(c, 12, 3, 19, 5, 'l')
    c.solid(c.m.poly([(7, 9), (25, 9), (23, 28), (9, 28)]), 'l')
    for x in (12, 16, 20):
        c.vline(x, 12, 25, 'd')


def book(c, opened):
    if opened:
        c.solid(c.m.poly([(2, 8), (15.5, 10), (15.5, 27), (2, 25)]), 'w')
        c.solid(c.m.poly([(16.5, 10), (30, 8), (30, 25), (16.5, 27)]), 'w')
        for y in (13, 16, 19, 22):
            c.hline(5, 13, y, 'd')
            c.hline(19, 27, y, 'd')
        c.vline(16, 10, 27, 'k')
    else:
        bar(c, 6, 4, 25, 27, 'b')
        c.box(9, 4, 9, 27, 'k')
        bar(c, 12, 8, 22, 12, 'e')


def hourglass(c):
    bar(c, 6, 3, 25, 5, 'e')
    bar(c, 6, 26, 25, 28, 'e')
    c.solid(c.m.poly([(8, 6), (24, 6), (17, 16), (24, 26), (8, 26), (15, 16)]), 'w')
    c.solid(c.m.poly([(12, 25), (20, 25), (16, 20)]), 'e', bevel=False, outline=False)


def plug(c):
    bar(c, 10, 4, 12, 10, 'l')
    bar(c, 19, 4, 21, 10, 'l')
    bar(c, 7, 10, 24, 19, 'd', r=2)
    bar(c, 14, 20, 17, 28, 'k')


def stopwatch(c):
    bar(c, 13, 2, 18, 4, 'l')
    clock(c, 16, 18, 11)


def check_mark(c):
    c.solid(c.m.thick([(5, 16), (12, 23), (26, 8)], 5), 'b')


def scissors(c):
    c.solid(c.m.thick([(9, 4), (20, 20)], 3.4), 'l')
    c.solid(c.m.thick([(22, 4), (11, 20)], 3.4), 'l')
    ring(c, 9, 23, 5, 2.4, 'b')
    ring(c, 22, 23, 5, 2.4, 'b')


def tag(c):
    c.solid(c.m.poly([(3, 9), (20, 9), (29, 16), (20, 23), (3, 23)]), 'e')
    dot(c, 20, 15, 2, 'g')


def panes(c, lit):
    for i, x in enumerate((3, 17)):
        bar(c, x, 6, x + 11, 25, 'b' if i == lit else 'l')


def grid3(c, lit=(1, 1), x0=6, y0=6, s=6):
    for j in range(3):
        for i in range(3):
            x, y = x0 + i * (s + 1), y0 + j * (s + 1)
            bar(c, x, y, x + s - 2, y + s - 2, 'b' if (i, j) == lit else 'l')


def arc_arrow(c, cx, cy, r, a0, a1, w, col='b', head=None, back=False):
    """An arc (clockwise from a0 to a1 degrees, 0 = east) with an arrowhead at a1 (at a0 pointing back if `back`)."""
    head = head or w * 2.6
    hl = head * 0.85
    da = math.degrees(hl / r)
    if back:
        a0 += da
    else:
        a1 -= da
    m = c.m.arc(cx, cy, r, a0, a1, w)
    a = a0 if back else a1
    t = math.radians(a)
    sgn = -1 if back else 1
    px_, py_ = cx + 0.5 + r * math.cos(t), cy + 0.5 + r * math.sin(t)
    dx, dy = -math.sin(t) * sgn, math.cos(t) * sgn
    nx, ny = math.cos(t), math.sin(t)
    m |= c.m.poly([(px_ + nx * head / 2, py_ + ny * head / 2), (px_ + dx * hl, py_ + dy * hl),
                   (px_ - nx * head / 2, py_ - ny * head / 2)])
    c.solid(m, col)


def page(c, x0, y0, x1, y1, hl=None, step=4):
    """A source page with a folded corner and text lines (line `hl` lit as a bar)."""
    c.solid(c.m.poly([(x0, y0), (x1 - 4, y0), (x1 + 1, y0 + 5), (x1 + 1, y1 + 1), (x0, y1 + 1)]), 'w')
    c.solid(c.m.poly([(x1 - 4, y0), (x1 - 4, y0 + 5), (x1 + 1, y0 + 5)]), 'l', bevel=False)
    for i, y in enumerate(range(y0 + 8, y1 - 1, step)):
        x_end = x1 - 3 - (i % 3) * 3
        if i == hl:
            bar(c, x0 + 2, y - 1, x1 - 2, y + 1, 'b')
        else:
            c.hline(x0 + 3, x_end, y, 'd')


def pencil(c, x0, y0, x1, y1, w=4.6):
    """A pencil from its tip (x0, y0) to its end (x1, y1), pixel centres."""
    ln = math.hypot(x1 - x0, y1 - y0)
    ux, uy = (x1 - x0) / ln, (y1 - y0) / ln
    nx, ny = -uy, ux
    h = w / 2
    tip = 5.0
    bx, by = x0 + ux * tip, y0 + uy * tip
    ex, ey = x1 - ux * 3, y1 - uy * 3
    q = lambda x, y: (x + 0.5, y + 0.5)  # noqa: E731
    c.solid(c.m.poly([q(bx + nx * h, by + ny * h), q(ex + nx * h, ey + ny * h), q(ex - nx * h, ey - ny * h),
                      q(bx - nx * h, by - ny * h)]), 'e')
    c.solid(c.m.poly([q(ex + nx * h, ey + ny * h), q(x1 + nx * h, y1 + ny * h), q(x1 - nx * h, y1 - ny * h),
                      q(ex - nx * h, ey - ny * h)]), 'l')
    c.solid(c.m.poly([q(x0, y0), q(bx + nx * h, by + ny * h), q(bx - nx * h, by - ny * h)]), 'w', bevel=False)
    c.box(int(round(x0 + ux)), int(round(y0 + uy)), int(round(x0 + ux)), int(round(y0 + uy)), 'k')


def pointer(c, x, y, s=1.0, col='w'):
    """A mouse pointer (arrow cursor) with its tip at (x, y)."""
    pts = [(0, 0), (0, 14), (3.4, 10.8), (5.8, 16), (8.4, 14.8), (6.2, 9.8), (10.6, 9.8)]
    c.solid(c.m.poly([(x + px_ * s, y + py_ * s) for px_, py_ in pts]), col)


def mouse(c, x0, y0, x1, y1, lit=None, col='b'):
    """A mouse seen from above; `lit`: 'left' / 'middle' / 'right' button in the accent."""
    body = c.m.rect(x0, y0, x1, y1, r=(x1 - x0) / 2.2)
    c.solid(body, 'l')
    ym = y0 + int(round((y1 - y0) * 0.42))
    xa = x0 + (x1 - x0 + 1) // 3
    xb = x1 - (x1 - x0 + 1) // 3
    regions = {'left': (x0, xa - 1), 'middle': (xa + 1, xb - 1), 'right': (xb + 1, x1)}
    if lit:
        a, b = regions[lit]
        c.solid(body & c.m.rect(a, y0, b, ym - 1), col, outline=False)
    c.hline(x0 + 1, x1 - 1, ym, 'k')
    c.vline(xa, y0 + 1, ym, 'k')
    c.vline(xb, y0 + 1, ym, 'k')


def click_rays(c, x, y, side):
    """Three short rays of a click from (x, y): up-left (side -1), up-right (1) or straight up (0)."""
    base = {-1: 225, 1: 315, 0: 270}[side]
    for da in (-45, 0, 45):
        a = base + da
        c.solid(c.m.thick([sp.polar(x, y, 2.5, a), sp.polar(x, y, 6.5, a)], 2), 'w', bevel=False, outline=False)


def metronome(c, cx=15.5, y0=3, y1=27, swing=0):
    """A metronome: a wooden pyramid, its pendulum upright (swing 0) or swung by `swing` degrees."""
    c.solid(c.m.poly([(cx - 3.5, y0), (cx + 3.5, y0), (cx + 9.5, y1 - 2), (cx - 9.5, y1 - 2)]), 'e')
    bar(c, int(cx - 10.5), y1 - 2, int(cx + 10.5), y1, 'd')
    px_, py_ = cx - 0.5, y1 - 6
    tx, ty = sp.polar(px_, py_, 18, -90 + swing)
    c.solid(c.m.thick([(px_, py_), (tx, ty)], 2.2), 'k', bevel=False, outline=False)
    wx, wy = sp.polar(px_, py_, 12, -90 + swing)
    c.solid(c.m.rect(int(round(wx)) - 2, int(round(wy)) - 1, int(round(wx)) + 2, int(round(wy)) + 1), 'w',
            bevel=False)


def reel(c, cx, cy, r):
    """A film reel: a disc with a hub and three windows."""
    c.solid(c.m.circle(cx + 0.5, cy + 0.5, r), 'l')
    c.solid(c.m.circle(cx + 0.5, cy + 0.5, 2.2), 'd', bevel=False)
    for a in (-90, 30, 150):
        x, y = sp.polar(cx, cy, r * 0.56, a)
        x, y = int(round(x)), int(round(y))
        c.box(x - 1, y - 1, x + 1, y + 1, 'k')


def lever_switch(c, on):
    bar(c, 4, 21, 27, 27, 'd', r=1)
    tip = (24, 5) if on else (7, 5)
    c.solid(c.m.thick([(15.5, 19), tip], 3.4), 'w')
    dot(c, tip[0], tip[1], 2.8, 'e' if on else 'l')
    dot(c, 15, 19, 4.6, 'l')
    c.box(14, 18, 17, 19, 'd')


def small_clock(c, cx, cy, r):
    c.solid(c.m.circle(cx + 0.5, cy + 0.5, r), 'w')
    c.vline(cx, cy - int(r) + 2, cy, 'k')
    c.hline(cx, cx + int(r) - 3, cy, 'k')


def floppy_small(c, x0, y0, x1, y1, col='b'):
    c.solid(c.m.poly([(x0, y0), (x1 - 1, y0), (x1 + 1, y0 + 2), (x1 + 1, y1 + 1), (x0, y1 + 1)]), col)
    c.box(x0 + 3, y0 + 1, x1 - 4, y0 + 3, 'l')
    c.box(x0 + 2, y1 - 4, x1 - 2, y1 - 1, 'w')


def tag_at(c, x0, y0, x1, y1, col='e', hole=True):
    ym = (y0 + y1 + 1) / 2
    h = (y1 - y0 + 1) / 2
    c.solid(c.m.poly([(x0, y0), (x1 + 1 - h * 0.9, y0), (x1 + 1, ym), (x1 + 1 - h * 0.9, y1 + 1), (x0, y1 + 1)]), col)
    if hole:
        dot(c, int(x1 - h * 0.9), int(ym - 0.5), 1.6, 'g')


def histogram(c, heights, col='b', x0=4, base=24, w=4):
    bar(c, x0 - 2, 3, x0 - 1, base + 3, 'd')
    bar(c, x0 - 2, base + 2, x0 + len(heights) * (w + 1) - 1, base + 3, 'd')
    for i, h in enumerate(heights):
        x = x0 + i * (w + 1)
        bar(c, x, base - h, x + w - 1, base, col)


def scope(c, trace=None):
    bar(c, 3, 5, 28, 26, 'l', r=1)
    bar(c, 6, 8, 25, 23, 'k')
    for x in (10, 15, 20):
        for y in (11, 15, 19):
            c.px(x, y, 'd')
    if trace == 'flat':
        c.solid(c.m.rect(7, 15, 24, 16), 'e', bevel=False, outline=False)
    elif trace == 'pulse':
        c.solid(c.m.thick([(7, 19), (10, 19), (10, 12), (15, 12), (15, 19), (19, 19), (19, 12), (24, 12)], 2.2),
                'e', bevel=False, outline=False)


def window(c, x0, y0, x1, y1):
    bar(c, x0, y0, x1, y1, 'l')
    c.box(x0 + 1, y0 + 1, x1 - 1, y0 + 3, 'b')


def plate(c, x0, y, x1, h, col, skew=5):
    c.solid(c.m.poly([(x0 + skew, y), (x1 + 1, y), (x1 + 1 - skew, y + h), (x0, y + h)]), col)


def layer_stack(c, x0=3, x1=28, spread=False, top='l'):
    dy = 8 if spread else 4
    y = 21 if spread else 17
    for i, col in enumerate(('d', 'l', top)):
        plate(c, x0, y - i * dy, x1, 6, col, skew=6)


def crt(c, x0=3, y0=8, x1=28, y1=26, screen='d', scan=False):
    c.solid(c.m.thick([(x0 + 10, y0), (x0 + 5, y0 - 5)], 2), 'd', bevel=False)
    c.solid(c.m.thick([(x0 + 12, y0), (x0 + 17, y0 - 5)], 2), 'd', bevel=False)
    bar(c, x0, y0, x1, y1, 'l', r=2)
    c.solid(c.m.rect(x0 + 2, y0 + 2, x1 - 7, y1 - 2, r=3), screen)
    if scan:
        for y in range(y0 + 4, y1 - 3, 4):
            c.hline(x0 + 4, x1 - 9, y, 'k')
    kx = x1 - 3
    dot(c, kx, y0 + 4, 1.6, 'd')
    dot(c, kx, y0 + 9, 1.6, 'd')


def gpu_card(c, on):
    bar(c, 2, 5, 4, 25, 'l')
    bar(c, 5, 7, 29, 22, 'd')
    for x in range(9, 27, 3):
        c.box(x, 23, x + 1, 25, 'e')
    if on:
        dot(c, 14, 14, 6, 'b')
        for a in (0, 120, 240):
            c.solid(c.m.arc(14, 14, 3.4, a, a + 70, 1.8), 'w', bevel=False, outline=False)
    else:
        dot(c, 14, 14, 6, 'l')
        c.box(13, 13, 15, 15, 'd')
    bar(c, 23, 10, 26, 19, 'l')


def corners(c, inward):
    w = 3.2
    for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
        cx = 15.5 - sx * 11.5
        cy = 15.5 - sy * 11.5
        if inward:
            pts = [(cx + sx * 0.5, cy + sy * 7), (cx + sx * 7, cy + sy * 7), (cx + sx * 7, cy + sy * 0.5)]
        else:
            pts = [(cx, cy + sy * 7), (cx, cy), (cx + sx * 7, cy)]
        c.solid(c.m.thick(pts, w), 'b')


def bubble(c, lines):
    c.solid(c.m.rect(3, 4, 28, 21, r=4) | c.m.poly([(8, 19), (15, 19), (7.5, 28)]), 'w' if lines else 'l')
    if lines:
        bar(c, 7, 9, 24, 10, 'd')
        bar(c, 7, 14, 18, 15, 'd')


def headphones(c, lit):
    c.solid(c.m.arc(15, 17, 10.5, 180, 360, 3), 'l')
    for x0 in (3, 22):
        bar(c, x0, 15, x0 + 6, 27, 'b' if lit else 'd', r=2)
    if lit:
        dot(c, 15, 22, 2.8, 'e')


def reg_rows(c, rows=((4, 14), (11, 9), (18, 12)), x0=3):
    for y, w in rows:
        bar(c, x0, y, x0 + 4, y + 3, 'b')
        bar(c, x0 + 7, y, x0 + 7 + w, y + 3, 'l')


def ruler(c, x0=3, y0=20, x1=28, y1=27):
    bar(c, x0, y0, x1, y1, 'e')
    for i, x in enumerate(range(x0 + 3, x1 - 1, 3)):
        c.vline(x, y0 + 1, y0 + (4 if i % 2 == 0 else 2), 'k')


def oct_ring(c, cx, cy, r, col='l', t=3.2):
    pts = [sp.polar(cx + 0.5, cy + 0.5, r, 22.5 + 45 * i) for i in range(8)]
    inner = [sp.polar(cx + 0.5, cy + 0.5, r - t, 22.5 + 45 * i) for i in range(8)]
    c.solid(c.m.poly(pts) & ~c.m.poly(inner), col)


def bp_mark(c, cx, cy, r, filled):
    if filled:
        octagon(c, cx, cy, r, 'e')
    else:
        oct_ring(c, cx, cy, r, 'l', t=max(2.6, r * 0.3))


def palette_(c):
    c.solid(c.m.ell(3, 5, 28, 26) & ~c.m.circle(21.5, 20.5, 3) & ~c.m.poly([(25, 30), (21.5, 20.5), (31, 22)]), 'e')
    for (x, y), col in (((9, 11), 'b'), ((15, 9), 'w'), ((21, 11), 'd'), ((9, 18), 'l')):
        dot(c, x, y, 2.4, col)


# ---------------------------------------------------------------------------
# The glyphs
# ---------------------------------------------------------------------------

def draw(gid):
    on = gid.endswith('.on')
    c = Glyph(on)
    DRAW[gid](c)
    return c.image(PALETTE)


def g_record(c, lit):
    ring(c, 15, 15, 12, 8.5, 'l')
    dot(c, 15, 15, 7, 'k' if lit else 'd')
    if lit:
        c.box(12, 11, 13, 12, 'w')


DRAW = {
    # navigation and deck
    'nav.back': lambda c: arrow(c, 27, 15.5, 3, 15.5, shaft=6, head=17, head_len=10),
    'nav.home': house,
    'nav.folder': folder,
    'nav.prev_page': lambda c: chevron(c, 14, 15.5, 20, 'left', w=6),
    'nav.next_page': lambda c: chevron(c, 17, 15.5, 20, 'right', w=6),
    'nav.target': lambda c: (ring(c, 15, 15, 10, 7, 'l'), bar(c, 14, 2, 17, 7, 'l'), bar(c, 14, 24, 17, 29, 'l'),
                             bar(c, 2, 14, 7, 17, 'l'), bar(c, 24, 14, 29, 17, 'l'), dot(c, 15, 15, 2.6, 'b')),
    'nav.target.on': lambda c: (c.solid(c.m.thick([(16, 17), (16, 29)], 2), 'l'),
                                c.solid(c.m.poly([(10, 3), (22, 3), (21, 10), (25, 16), (7, 16), (11, 10)]), 'b')),
    'nav.instances': windows,
    'deck.menu': deck_grid,
    'deck.brightness': sun,
    # run control
    'run.play': lambda c: tri_right(c, 9, 5, 25, 26),
    'run.pause': lambda c: (bar(c, 8, 6, 13, 25), bar(c, 18, 6, 23, 25)),
    'run.stop': lambda c: bar(c, 7, 7, 24, 24),
    'run.step_in': lambda c: (arrow(c, 15.5, 3, 15.5, 19, shaft=5, head=14, head_len=7), dot(c, 15, 25, 3)),
    'run.step_over': lambda c: (c.solid(c.m.arc(14, 19, 9, 195, 350, 4), 'b'),
                                c.solid(c.m.poly([(18.5, 17), (29.5, 17), (24, 24)]), 'b'), dot(c, 13, 25, 3)),
    'run.step_out': lambda c: (arrow(c, 15.5, 19, 15.5, 3, shaft=5, head=14, head_len=7), dot(c, 15, 25, 3)),
    'run.frame_step': lambda c: (film(c, 3, 6, 21, 25), tri_right(c, 23, 9, 29, 22)),
    'run.wait_int': lambda c: (c.solid(c.m.thick([(3, 24), (12, 24), (12, 8), (19, 8), (19, 24), (28, 24)], 3.2), 'l'),
                               tri_right(c, 2, 7, 7, 16)),
    'run.to_scanline': lambda c: (raster(c, skip=2), bar(c, 9, 14, 28, 17, 'b'), tri_right(c, 2, 11, 7, 20, 'e')),
    'run.n_scanlines': lambda c: (raster(c), c.solid(c.m.rect(3, 9, 5, 22) | c.m.rect(3, 9, 7, 10) |
                                                     c.m.rect(3, 21, 7, 22), 'e', bevel=False)),
    'run.turbo_hold': lambda c: (tri_right(c, 3, 5, 15, 21), tri_right(c, 16, 5, 28, 21), bar(c, 4, 24, 27, 27, 'e')),
    'run.steps': lambda c: (arrow(c, 9.5, 3, 9.5, 20, shaft=4, head=11, head_len=6), dot(c, 9, 25, 2.6),
                            c.solid(c.text_mask('N', 17, 9, 2), 'e', bevel=False)),
    'run.frames': lambda c: (film(c, 2, 6, 15, 25), c.solid(c.text_mask('N', 18, 9, 2), 'e', bevel=False)),
    'run.tstates': lambda c: (c.solid(c.m.thick([(2, 22), (5, 22), (5, 10), (9, 10), (9, 22), (13, 22), (13, 10),
                                                 (16, 10)], 2.2), 'l', bevel=False),
                              c.solid(c.text_mask('T', 19, 9, 2), 'e', bevel=False)),
    'run.to_pixel': lambda c: (bar(c, 8, 4, 28, 27, 'l'), bar(c, 12, 9, 24, 22, 'b'), bar(c, 12, 9, 15, 12, 'w'),
                               tri_right(c, 2, 6, 7, 15, 'e')),
    'run.until_pc': lambda c: (tri_right(c, 2, 6, 13, 25), ring(c, 22, 15, 6.5, 3.8, 'l'), bar(c, 21, 4, 22, 7, 'l'),
                               bar(c, 21, 23, 22, 26, 'l'), bar(c, 27, 15, 29, 16, 'l'), dot(c, 22, 15, 1.6, 'e')),
    'run.to_cursor': lambda c: (tri_right(c, 2, 8, 10, 23), pointer(c, 15, 4, 1.4)),
    'run.step_line': lambda c: (page(c, 2, 3, 18, 28), arrow(c, 24.5, 3, 24.5, 19, shaft=4, head=10, head_len=5),
                                dot(c, 24, 25, 2.6)),
    'run.to_line': lambda c: (page(c, 9, 3, 28, 28, hl=2), tri_right(c, 2, 13, 6, 21, 'e')),
    'run.pace': lambda c: metronome(c, 15.5),
    'run.pace_next': lambda c: (metronome(c, 11.5), chevron(c, 25, 15.5, 12, 'right', 'b', 4)),
    'run.pace_prev': lambda c: (metronome(c, 19.5), chevron(c, 6, 15.5, 12, 'left', 'b', 4)),
    'run.pace_toggle': lambda c: (metronome(c, 11.5), tri_right(c, 22, 9, 29, 22)),
    'run.pace_toggle.on': lambda c: (metronome(c, 11.5, swing=22), bar(c, 22, 9, 24, 22), bar(c, 27, 9, 29, 22)),
    'run.pace_stop': lambda c: (metronome(c, 11.5), bar(c, 21, 12, 28, 19)),
    'run.pace_step': lambda c: (metronome(c, 11.5), tri_right(c, 21, 9, 25, 22), bar(c, 27, 9, 29, 22)),
    # machine
    'mach.reset': lambda c: (c.solid(c.m.arc(15, 16, 9, -70, 220, 4.5), 'b'),
                             c.solid(c.m.poly([(3, 8), (13, 9), (6, 17)]), 'b')),
    'mach.cold_reset': lambda c: (c.solid(c.m.arc(15, 16, 9.5, -55, 235, 4), 'l'), bar(c, 14, 2, 17, 16, 'b')),
    'mach.nmi': bolt,
    'mach.turbo': lambda c: gauge(c, 210),
    'mach.turbo.on': lambda c: gauge(c, 330),
    'mach.start': zx,
    'mach.chip': lambda c: (chip(c), bar(c, 11, 12, 20, 19, 'k'), c.hline(11, 20, 12, 'd'), c.vline(11, 12, 19, 'd'),
                            c.px(8, 9, 'w')),
    'mach.previous': lambda c: (arrow(c, 27, 7.5, 3, 7.5, shaft=4, head=10, head_len=6), zx(c, 4)),
    'mach.switch': lambda c: lever_switch(c, False),
    'mach.switch.on': lambda c: lever_switch(c, True),
    # time travel
    'ttd.menu': lambda c: clock(c, 17, 16, 12, arrow_back=True),
    'ttd.record': lambda c: g_record(c, False),
    'ttd.record.on': lambda c: g_record(c, True),
    'ttd.stop': lambda c: (ring(c, 15, 15, 12, 8.5, 'l'), bar(c, 11, 11, 20, 20, 'k')),
    'ttd.step_back_frame': lambda c: (bar(c, 4, 5, 8, 26), tri_left(c, 10, 5, 27, 26)),
    'ttd.step_forward_frame': lambda c: (tri_right(c, 4, 5, 21, 26), bar(c, 23, 5, 27, 26)),
    'ttd.step_back_instruction': lambda c: (code_lines(c, x0=12), tri_left(c, 2, 10, 9, 21)),
    'ttd.step_forward_instruction': lambda c: (code_lines(c, x0=3), tri_right(c, 22, 10, 29, 21)),
    'ttd.step_back_scanline': lambda c: (raster(c, x0=11, x1=28), bar(c, 11, 14, 28, 17, 'b'),
                                         tri_left(c, 2, 10, 9, 21)),
    'ttd.step_forward_scanline': lambda c: (raster(c, x0=3, x1=20), bar(c, 3, 14, 20, 17, 'b'),
                                            tri_right(c, 22, 10, 29, 21)),
    'ttd.unit_back': lambda c: (tri_left(c, 8, 2, 22, 17), ruler(c)),
    'ttd.unit_forward': lambda c: (tri_right(c, 9, 2, 23, 17), ruler(c)),
    'ttd.scrub_unit': lambda c: (c.solid(c.m.poly([(9, 4), (22, 4), (15.5, 15)]), 'b'), ruler(c)),
    'ttd.reverse_step': lambda c: (arrow(c, 15.5, 28, 15.5, 12, shaft=5, head=14, head_len=7), dot(c, 15, 6, 3)),
    'ttd.reverse_continue': lambda c: (tri_left(c, 3, 6, 15, 25), tri_left(c, 16, 6, 28, 25)),
    'ttd.go_start': lambda c: (bar(c, 3, 6, 6, 25), tri_left(c, 7, 6, 17, 25), tri_left(c, 18, 6, 28, 25)),
    'ttd.go_end': lambda c: (tri_right(c, 3, 6, 13, 25), tri_right(c, 14, 6, 24, 25), bar(c, 25, 6, 28, 25)),
    'ttd.resume_live': lambda c: (tri_right(c, 5, 5, 21, 26), dot(c, 25, 7, 3.2)),
    'ttd.bookmark': ribbon,
    'ttd.bookmark_add': lambda c: (ribbon(c), plus_mark(c, 15, 12)),
    'ttd.bookmark_delete': lambda c: (ribbon(c), cross_mark(c, 15, 12)),
    'ttd.bookmark_prev': lambda c: (ribbon(c, 13, 4, 26, 27), chevron(c, 6, 15.5, 12, 'left', 'e', 4)),
    'ttd.bookmark_next': lambda c: (ribbon(c, 5, 4, 18, 27), chevron(c, 25, 15.5, 12, 'right', 'e', 4)),
    'ttd.seek': lambda c: (arrow(c, 2, 15.5, 20, 15.5, shaft=5, head=15, head_len=8), bar(c, 23, 4, 27, 27, 'e')),
    'ttd.find_last_write': magnifier,
    'ttd.find_last': lambda c: magnifier(c, 'cell'),
    'ttd.invalidate': bin_,
    'ttd.history_limit': lambda c: (bar(c, 5, 10, 26, 13, 'l'), bar(c, 5, 16, 26, 19, 'l'), bar(c, 5, 22, 26, 25, 'l'),
                                    bar(c, 2, 4, 29, 6, 'b')),
    'ttd.journal': lambda c: book(c, False),
    'ttd.journal.on': lambda c: book(c, True),
    'ttd.journal_build': lambda c: (book(c, False), plus_mark(c, 23, 23, 'e')),
    'ttd.journal_build_cancel': lambda c: (book(c, False), cross_mark(c, 23, 23, 'w', 3)),
    'ttd.dump': lambda c: (tray(c), arrow(c, 15.5, 2, 15.5, 20, shaft=5, head=14, head_len=7)),
    'ttd.load': lambda c: (tray(c), arrow(c, 15.5, 21, 15.5, 2, shaft=5, head=14, head_len=7)),
    'ttd.export_clip': lambda c: (film(c, 2, 5, 19, 26), arrow(c, 25.5, 4, 25.5, 28, shaft=4, head=10, head_len=5,
                                                               col='e')),
    'ttd.timecode': lambda c: clock(c),
    # debugger
    'debug.menu': bug,
    'dbg.mode': lambda c: bug(c, 'l'),
    'dbg.mode.on': lambda c: bug(c, 'e'),
    'dbg.breakpoint_toggle_pc': lambda c: (octagon(c, 19, 15, 10), tri_right(c, 2, 9, 8, 22, 'b')),
    'dbg.breakpoints_clear': lambda c: (octagon(c, 15, 15, 12), cross_mark(c, 15, 15, s=5)),
    'dbg.breakpoint_hits_reset': lambda c: (octagon(c, 15, 15, 12), c.text('0', 13, 10, 'w', 2),
                                            c.text('0', 12, 9, 'k', 2)),
    'dbg.breakpoint_toggle_cursor': lambda c: (octagon(c, 13, 13, 11), pointer(c, 16, 13, 1.05)),
    'dbg.bp_add': lambda c: (octagon(c, 15, 15, 12), c.solid(c.m.rect(9, 14, 22, 17) | c.m.rect(14, 9, 17, 22), 'w',
                                                               bevel=False)),
    'dbg.bp_remove': lambda c: (octagon(c, 15, 15, 12), c.solid(c.m.rect(9, 14, 22, 17), 'w', bevel=False)),
    'dbg.bp_enable': lambda c: (octagon(c, 15, 15, 12), c.solid(c.m.thick([(9, 16), (13, 20), (21, 10)], 3.2), 'w',
                                                                bevel=False)),
    'dbg.bp_disable': lambda c: (oct_ring(c, 15, 15, 12, 'l', 3.4), c.solid(c.m.thick([(7, 24), (24, 7)], 3.2), 'e')),
    'dbg.bp_toggle': lambda c: oct_ring(c, 15, 15, 12, 'l', 3.4),
    'dbg.bp_toggle.on': lambda c: octagon(c, 15, 15, 12),
    'dbg.bp_group': lambda c: [oct_ring(c, x, y, 6.8, 'l', 2.6) for x, y in ((8, 9), (23, 9), (15, 22))],
    'dbg.bp_group.on': lambda c: [octagon(c, x, y, 6.8) for x, y in ((8, 9), (23, 9), (15, 22))],
    'dbg.register_set': lambda c: (reg_rows(c), pencil(c, 15, 27, 28, 14)),
    'mem.write': lambda c: (grid3(c, (0, 0), 3, 2, 7), pencil(c, 15, 27, 28, 14)),
    'port.out': lambda c: (chip(c, 3, 8, 16, 23), arrow(c, 13, 15.5, 29, 15.5, shaft=5, head=13, head_len=7,
                                                         col='e')),
    'dbg.labels_toggle': tag,
    'dbg.focus_prev_pane': lambda c: (panes(c, 0), chevron(c, 9, 15.5, 9, 'left', 'w', 3)),
    'dbg.focus_next_pane': lambda c: (panes(c, 1), chevron(c, 22, 15.5, 9, 'right', 'w', 3)),
    'dbg.cursor_up': lambda c: arrow(c, 15.5, 28, 15.5, 3, shaft=6, head=17, head_len=10),
    'dbg.cursor_down': lambda c: arrow(c, 15.5, 3, 15.5, 28, shaft=6, head=17, head_len=10),
    'dbg.cursor_left': lambda c: arrow(c, 28, 15.5, 3, 15.5, shaft=6, head=17, head_len=10),
    'dbg.cursor_right': lambda c: arrow(c, 3, 15.5, 28, 15.5, shaft=6, head=17, head_len=10),
    'dbg.page_up': lambda c: (bar(c, 4, 3, 27, 5, 'e'), chevron(c, 15.5, 13, 20, 'up', w=5),
                              chevron(c, 15.5, 22, 20, 'up', w=5)),
    'dbg.page_down': lambda c: (bar(c, 4, 26, 27, 28, 'e'), chevron(c, 15.5, 9, 20, 'down', w=5),
                                chevron(c, 15.5, 18, 20, 'down', w=5)),
    'dbg.memory_follow': lambda c: (grid3(c, (1, 1), 10, 3, 6), arrow(c, 2, 28, 15, 15, shaft=4, head=11, head_len=6,
                                                                     col='e')),
    'debug.disasm': lambda c: (code_lines(c, x0=10), tri_right(c, 3, 9, 8, 16, 'b')),
    'dbg.follow_pc': lambda c: (code_lines(c, x0=8, lens=(13, 9, 12, 7)), tri_right(c, 2, 9, 6, 15, 'b'),
                                arrow(c, 26, 3, 26, 28, shaft=4, head=10, head_len=6, col='e')),
    'dbg.scroll_mode': lambda c: (code_lines(c, x0=3, lens=(14, 10, 13, 8)),
                                  c.solid(c.m.poly(sp.arrow_pts(25, 15.5, 25, 2, 4, 10, 6)) |
                                          c.m.poly(sp.arrow_pts(25, 15.5, 25, 29, 4, 10, 6)), 'e')),
    'dbg.goto': lambda c: (code_lines(c, x0=14, lens=(13, 9, 13, 7), hl=2), c.box(14, 16, 27, 18, 'b'),
                           arrow(c, 2, 17, 12, 17, shaft=4, head=10, head_len=5, col='e')),
    'sym.import': lambda c: (tag_at(c, 3, 17, 28, 27), arrow(c, 15.5, 15, 15.5, 2, shaft=4, head=11, head_len=6)),
    'sym.export': lambda c: (tag_at(c, 3, 3, 28, 13), arrow(c, 15.5, 16, 15.5, 29, shaft=4, head=11, head_len=6)),
    'sym.import_source': lambda c: (page(c, 2, 2, 18, 27), tag_at(c, 12, 18, 29, 28)),
    'sym.import_live': lambda c: (bar(c, 2, 3, 26, 11, 'b'), [bar(c, x, 5, x + 4, 9, 'd') for x in (5, 12, 19)],
                                  [c.box(x, 12, x + 1, 14, 'e') for x in range(4, 25, 3)],
                                  tag_at(c, 7, 18, 29, 28)),
    'sym.drop': lambda c: (tag(c), cross_mark(c, 12, 16, 'w', 4)),
    'sym.set': lambda c: (tag_at(c, 2, 4, 24, 15, 'l'), tag_at(c, 7, 15, 29, 26, 'l')),
    'sym.set.on': lambda c: (tag_at(c, 2, 4, 24, 15, 'e'), tag_at(c, 7, 15, 29, 26, 'e'),
                             c.solid(c.m.thick([(10, 20), (13, 23), (18, 17)], 2.4), 'w', bevel=False)),
    'prof.toggle': lambda c: histogram(c, (6, 6, 6, 6, 6), 'l'),
    'prof.toggle.on': lambda c: histogram(c, (8, 17, 11, 20, 6)),
    'prof.pause': lambda c: (histogram(c, (8, 17, 11)), bar(c, 21, 9, 23, 21, 'e'), bar(c, 26, 9, 28, 21, 'e')),
    'prof.pause.on': lambda c: (histogram(c, (8, 17, 11)), tri_right(c, 21, 8, 29, 22, 'e')),
    'prof.clear': lambda c: (histogram(c, (8, 17, 11)), cross_mark(c, 24, 9, 'k', 4)),
    'analyzer.toggle': lambda c: scope(c, 'flat'),
    'analyzer.toggle.on': lambda c: scope(c, 'pulse'),
    'analyzer.clear': lambda c: (scope(c), cross_mark(c, 15, 15, 'w', 4)),
    'debug.memory': lambda c: grid3(c, (2, 0), 5, 5, 8),
    'debug.registers': lambda c: [(bar(c, 3, y, 8, y + 3, 'e'), bar(c, 11, y, 11 + w, y + 3, 'l'))
                                  for y, w in ((4, 17), (11, 12), (18, 15), (25 - 1, 9))],
    'debug.stack': lambda c: (bar(c, 4, 3, 27, 7, 'b'), bar(c, 4, 10, 27, 14, 'l'), bar(c, 4, 17, 27, 21, 'l'),
                              c.solid(c.m.poly([(10, 23), (22, 23), (16, 29)]), 'e')),
    # media
    'media.open': lambda c: (folder(c, True), arrow(c, 18.5, 18, 18.5, 2, shaft=4, head=11, head_len=6, col='b')),
    'media.open_recent': lambda c: (folder(c), small_clock(c, 21, 20, 6.5)),
    'media.software': lambda c: (floppy(c, 2, 2, 20, 21, 'd'), floppy(c, 10, 9, 28, 28, 'b')),
    'media.tape': cassette,
    'media.tape.play': lambda c: tri_right(c, 9, 5, 25, 26, 'e'),
    'media.tape.pause': lambda c: (bar(c, 8, 6, 13, 25, 'e'), bar(c, 18, 6, 23, 25, 'e')),
    'media.tape.stop': lambda c: bar(c, 7, 7, 24, 24, 'e'),
    'media.tape.rewind': lambda c: (tri_left(c, 3, 6, 15, 25, 'e'), tri_left(c, 16, 6, 28, 25, 'e')),
    'media.tape.eject': lambda c: (cassette(c, 3, 2, 28, 18), tri_up(c, 9, 20, 22, 25, 'b'), bar(c, 9, 27, 22, 28, 'b')),
    'media.tape.load': lambda c: (cassette(c, 3, 13, 28, 29), arrow(c, 15.5, 1, 15.5, 12, shaft=4, head=11,
                                                                     head_len=6)),
    'media.disk': floppy,
    'media.eject': lambda c: (tri_up(c, 4, 4, 27, 17), bar(c, 4, 21, 27, 26)),
    'media.protect': lambda c: lock(c, False),
    'media.protect.on': lambda c: lock(c, True),
    'media.save': lambda c: (floppy(c, 2, 2, 22, 25), arrow(c, 24.5, 9, 24.5, 28, shaft=4, head=11, head_len=6,
                                                           col='e')),
    'media.create': lambda c: (floppy(c, 2, 2, 22, 25), plus_mark(c, 24, 24, 'e')),
    'media.disk.cat': lambda c: (floppy(c, 2, 2, 20, 23), listing(c, 15, 13, 29, 29)),
    'media.cdrom': lambda c: (ring(c, 15, 15, 12.5, 2.5, 'l'), ring(c, 15, 15, 5, 2.5, 'w'),
                              c.line(8, 8, 11, 6, 'w'), c.line(20, 24, 23, 21, 'd')),
    'media.hdd': lambda c: (bar(c, 3, 7, 28, 25, 'd', r=1), ring(c, 13, 15, 6.5, 1.5, 'l'), c.line(13, 15, 20, 21, 'k'),
                            bar(c, 21, 21, 25, 22, 'e')),
    'media.insert': lambda c: (floppy(c, 7, 2, 24, 20), bar(c, 3, 17, 28, 19, 'd'),
                               c.solid(c.m.poly([(9, 22), (23, 22), (16, 29)]), 'e')),
    'media.swap': lambda c: (floppy_small(c, 2, 2, 13, 14, 'b'), floppy_small(c, 18, 2, 29, 14, 'd'),
                             arrow(c, 4, 19, 27, 19, shaft=3, head=7, head_len=5, col='e'),
                             arrow(c, 27, 26, 4, 26, shaft=3, head=7, head_len=5, col='e')),
    'media.export': lambda c: (floppy(c, 2, 2, 20, 26, 'd'), arrow(c, 11, 18.5, 29, 18.5, shaft=5, head=13,
                                                                  head_len=7, col='e')),
    'media.discard': lambda c: (floppy(c, 2, 2, 21, 25), arc_arrow(c, 22.5, 22.5, 5.2, -60, 200, 2.8, 'e', head=7.5)),
    'media.rescan': lambda c: (folder(c), arc_arrow(c, 15.5, 18, 5, -60, 230, 2.4, 'b', head=6.5)),
    'rzx.play': lambda c: (reel(c, 12, 12, 10), tri_right(c, 18, 14, 28, 28)),
    'rzx.stop': lambda c: (reel(c, 12, 12, 10), bar(c, 18, 17, 28, 27)),
    'rzx.seek': lambda c: (reel(c, 12, 12, 10), arrow(c, 13, 24, 24, 24, shaft=3.4, head=9, head_len=5),
                           bar(c, 26, 18, 28, 29, 'e')),
    'snapshot.quick_save': lambda c: (module(c, 2), arrow(c, 15.5, 18, 15.5, 29, shaft=4, head=11, head_len=6,
                                                          col='e')),
    'snapshot.quick_load': lambda c: (module(c, 14), arrow(c, 15.5, 12, 15.5, 1, shaft=4, head=11, head_len=6,
                                                           col='e')),
    'snapshot.save': lambda c: (floppy(c, 10, 10, 28, 29, 'd'), module(c, 2)),
    # audio
    'audio.sound': lambda c: (speaker(c), waves(c, 18)),
    'audio.muted': lambda c: (speaker(c), cross_mark(c, 23, 16, 'k', 4)),
    'audio.volume_up': lambda c: (speaker(c, 1), c.solid(c.m.rect(18, 14, 29, 17) | c.m.rect(22, 10, 25, 21), 'b')),
    'audio.volume_down': lambda c: (speaker(c, 1), c.solid(c.m.rect(18, 14, 29, 17), 'b')),
    'audio.source': lambda c: (chip(c), c.solid(c.m.arc(12, 19, 4, 180, 360, 2) | c.m.arc(19, 13, 4, 0, 180, 2), 'w',
                                                bevel=False, outline=False)),
    'audio.source.on': lambda c: (chip(c, col='l'), c.solid(c.m.arc(12, 19, 4, 180, 360, 2) |
                                                            c.m.arc(19, 13, 4, 0, 180, 2), 'd', bevel=False,
                                                            outline=False), cross_mark(c, 22, 21, 'w', 3)),
    'audio.solo': lambda c: headphones(c, False),
    'audio.solo.on': lambda c: headphones(c, True),
    'audio.source_volume': lambda c: (chip(c, 3, 9, 16, 22), c.solid(c.m.arc(7, 17, 2.4, 180, 360, 1.8) |
                                                                      c.m.arc(12, 13, 2.4, 0, 180, 1.8), 'w',
                                                                      bevel=False, outline=False),
                                      bar(c, 23, 3, 24, 28, 'd'), bar(c, 20, 15, 27, 19, 'b')),
    # video and capture
    'video.menu': monitor,
    'video.overscan': lambda c: (bar(c, 3, 5, 28, 26, 'l'), c.box(5, 7, 26, 24, 'g'), bar(c, 10, 11, 21, 20, 'b')),
    'video.overscan.on': lambda c: (bar(c, 3, 5, 28, 26, 'e'), bar(c, 9, 10, 22, 21, 'b')),
    'video.temporal': lambda c: (bar(c, 3, 3, 20, 18, 'd'), bar(c, 11, 12, 28, 27, 'l')),
    'video.temporal.on': lambda c: (bar(c, 3, 3, 20, 18, 'b'), bar(c, 11, 12, 28, 27, 'w')),
    'video.blending': lambda c: (bar(c, 3, 11, 9, 12, 'l'), bar(c, 5, 16, 11, 17, 'l'), bar(c, 3, 21, 9, 22, 'l'),
                                 dot(c, 20, 16, 7.5, 'b')),
    'video.blending.on': lambda c: (dot(c, 8, 16, 5.5, 'd'), dot(c, 13, 16, 6.5, 'l'), dot(c, 20, 16, 7.5, 'b')),
    'video.viewport': lambda c: (bar(c, 6, 7, 25, 24, 'l'), bar(c, 11, 11, 20, 20, 'b'),
                                 [c.solid(c.m.thick([(x, y + sy * 6), (x, y), (x + sx * 6, y)], 2.4), 'e')
                                  for x, y, sx, sy in ((3, 4, 1, 1), (28, 4, -1, 1), (3, 27, 1, -1),
                                                       (28, 27, -1, -1))]),
    'video.crt': lambda c: crt(c),
    'video.crt.on': lambda c: crt(c, screen='b', scan=True),
    'video.crt_profile': lambda c: (crt(c, 2, 9, 21, 25), chevron(c, 26, 17, 10, 'right', 'e', 4)),
    'video.crt_reset': lambda c: (arc_arrow(c, 15.5, 15.5, 12, -50, 235, 2.8, 'b', head=8),
                                  bar(c, 9, 10, 22, 21, 'l', r=1), c.solid(c.m.rect(11, 12, 18, 19, r=2), 'd'),
                                  dot(c, 20, 13, 1, 'd')),
    'video.gpu': lambda c: gpu_card(c, False),
    'video.gpu.on': lambda c: gpu_card(c, True),
    'view.fullscreen': lambda c: corners(c, False),
    'view.fullscreen.on': lambda c: corners(c, True),
    'view.scale': lambda c: (window(c, 3, 13, 17, 27), arrow(c, 14, 16, 27, 3, shaft=4, head=11, head_len=6,
                                                             col='e')),
    'view.toolbar': lambda c: (window(c, 3, 4, 28, 27), bar(c, 5, 9, 26, 12, 'd')),
    'view.toolbar.on': lambda c: (window(c, 3, 4, 28, 27), [bar(c, x, 8, x + 4, 12, 'e') for x in (5, 11, 17)]),
    'view.status_bar': lambda c: (window(c, 3, 4, 28, 27), bar(c, 5, 22, 26, 25, 'd')),
    'view.status_bar.on': lambda c: (window(c, 3, 4, 28, 27), bar(c, 5, 22, 13, 25, 'e'), bar(c, 16, 22, 21, 25, 'e'),
                                     bar(c, 24, 22, 26, 25, 'e')),
    'view.next_layers': lambda c: layer_stack(c),
    'view.next_layers.on': lambda c: layer_stack(c, spread=True, top='b'),
    'view.next_layers_mode': lambda c: (layer_stack(c, 2, 21), chevron(c, 26, 15.5, 10, 'right', 'e', 4)),
    'view.next_layer': lambda c: (plate(c, 2, 18, 29, 8, 'l', 7), bar(c, 11, 3, 20, 12, 'w')),
    'view.next_layer.on': lambda c: (plate(c, 2, 18, 29, 8, 'b', 7), bar(c, 11, 3, 20, 12, 'w'),
                                     c.solid(c.m.thick([(13, 7), (15, 9), (19, 4)], 2.2), 'k', bevel=False,
                                             outline=False)),
    'hud': lambda c: (bar(c, 3, 4, 28, 27, 'l', r=1), bar(c, 6, 7, 25, 24, 'b'),
                      c.solid(c.m.rect(9, 10, 17, 15) & ~c.m.rect(10, 11, 16, 14), 'w', bevel=False)),
    'hud.on': lambda c: (bar(c, 3, 4, 28, 27, 'l', r=1), bar(c, 6, 7, 25, 24, 'b'), bar(c, 9, 10, 17, 15, 'w'),
                         c.box(11, 12, 15, 13, 'd'), bar(c, 9, 19, 22, 21, 'w'), c.box(10, 20, 16, 20, 'e')),
    'hud.theme': palette_,
    'hud.notice': lambda c: bubble(c, False),
    'hud.notice.on': lambda c: bubble(c, True),
    'cap.screenshot': camera,
    'cap.screenshot_save': lambda c: (camera(c), arrow(c, 25.5, 14, 25.5, 29, shaft=4, head=10, head_len=5, col='e')),
    'cap.record': lambda c: videocam(c, False),
    'cap.record.on': lambda c: videocam(c, True),
    'cap.clip_last': scissors,
    'cap.record_pause': lambda c: (videocam(c, None), bar(c, 7, 13, 9, 21, 'b'), bar(c, 13, 13, 15, 21, 'b')),
    'cap.audio_capture': lambda c: ([bar(c, x, 16 - h, x + 1, 16 + h, 'b') for x, h in
                                     ((3, 3), (6, 7), (9, 11), (12, 6), (15, 9), (18, 4))],
                                    arrow(c, 25.5, 4, 25.5, 28, shaft=4, head=10, head_len=5, col='e')),
    # input
    'input.key': keycap,
    'input.keyboard': keyboard,
    'input.macro': lambda c: (keycap(c), tri_right(c, 12, 9, 19, 18, 'b')),
    'input.release_all': lambda c: (keycap(c), arrow(c, 15.5, 19, 15.5, 7, shaft=3, head=9, head_len=5)),
    'input.mouse.left': lambda c: (mouse(c, 12, 9, 26, 28, 'left'), click_rays(c, 10, 7.5, -1)),
    'input.mouse.right': lambda c: (mouse(c, 5, 9, 19, 28, 'right'), click_rays(c, 21, 7.5, 1)),
    'input.mouse.middle': lambda c: (mouse(c, 9, 10, 22, 28, 'middle'), click_rays(c, 15.5, 8, 0)),
    'input.mouse_release': lambda c: (mouse(c, 2, 7, 16, 28), arrow(c, 24, 26, 24, 3, shaft=4, head=11, head_len=6)),
    'input.joystick_type': lambda c: (bar(c, 9, 10, 22, 13, 'd', r=1), bar(c, 15, 4, 16, 9, 'k'),
                                      dot(c, 15, 4, 2.8, 'e'),
                                      bar(c, 2, 16, 29, 28, 'l', r=1),
                                      c.solid(c.m.poly([(6, 17.5), (26, 17.5), (23, 26.5), (9, 26.5)]), 'k'),
                                      [c.box(x, 20, x + 1, 20, 'w') for x in (10, 13, 16, 19, 22)],
                                      [c.box(x, 23, x + 1, 23, 'w') for x in (11, 14, 17, 20)],
                                      dot(c, 4, 21, 1, 'd'), dot(c, 27, 21, 1, 'd')),
    'input.joystick': joystick,
    'input.joy.up': lambda c: arrow(c, 15.5, 26, 15.5, 3, shaft=6, head=17, head_len=10),
    'input.joy.down': lambda c: arrow(c, 15.5, 5, 15.5, 28, shaft=6, head=17, head_len=10),
    'input.joy.left': lambda c: arrow(c, 26, 15.5, 3, 15.5, shaft=6, head=17, head_len=10),
    'input.joy.right': lambda c: arrow(c, 5, 15.5, 28, 15.5, shaft=6, head=17, head_len=10),
    'input.joy.up_left': lambda c: arrow(c, 25, 25, 4, 4, shaft=6, head=16, head_len=9),
    'input.joy.up_right': lambda c: arrow(c, 6, 25, 27, 4, shaft=6, head=16, head_len=9),
    'input.joy.down_left': lambda c: arrow(c, 25, 6, 4, 27, shaft=6, head=16, head_len=9),
    'input.joy.down_right': lambda c: arrow(c, 6, 6, 27, 27, shaft=6, head=16, head_len=9),
    'input.joy.fire': lambda c: (dot(c, 15, 18, 11, 'd'), dot(c, 15, 15, 9.5, 'e'), c.box(11, 10, 13, 11, 'w')),
    'osd.keyboard': lambda c: (monitor(c, 'b'), keyboard(c, 6, 13, 25, 24)),
    'osd.keyboard_picture': lambda c: (keyboard(c, 2, 9, 22, 24), chevron(c, 27, 16.5, 10, 'right', 'b', 4)),
    'osd.quick_menu': lambda c: (bar(c, 3, 4, 28, 27, 'l'), c.box(4, 5, 27, 8, 'b'), bar(c, 7, 12, 24, 14, 'w'),
                                 bar(c, 7, 17, 24, 19, 'b'), bar(c, 7, 22, 20, 24, 'w')),
    # BASIC
    'basic.menu': listing,
    'basic.run': lambda c: (listing(c, 3, 3, 22, 28), tri_right(c, 16, 10, 28, 23, 'b')),
    'basic.inject': lambda c: (listing(c, 3, 3, 22, 28), plus_mark(c, 24, 23, 'b')),
    'basic.clear': lambda c: (listing(c, 3, 3, 22, 28), cross_mark(c, 23, 22, 'k', 4)),
    'basic.mode.48': lambda c: (c.text('48', 4, 6, 'w', 3, 1), c.text('48', 3, 5, 'b', 3, 1)),
    'basic.mode.128': lambda c: (c.text('128', 3, 10, 'w', 2, 1), c.text('128', 2, 9, 'b', 2, 1)),
    # settings
    'settings.menu': gear,
    'settings.toggle': lambda c: switch(c, False),
    'settings.toggle.on': lambda c: switch(c, True),
    'settings.value': lambda c: [(bar(c, 3, y, 28, y + 1, 'd'), bar(c, x, y - 3, x + 3, y + 4, 'b'))
                                 for y, x in ((6, 18), (15, 7), (24, 14))],
    'set.speed': lambda c: gauge(c, 270),
    # status
    'status.ok': check_mark,
    'status.warn': lambda c: (c.solid(c.m.poly([(16, 2), (30.5, 28), (1.5, 28)]), 'e'), bar(c, 15, 10, 16, 20, 'k'),
                              bar(c, 15, 23, 16, 24, 'k')),
    'status.error': lambda c: (dot(c, 15, 15, 12.5, 'd'), cross_mark(c, 15, 15, 'w', 5)),
    'status.busy': hourglass,
    'status.offline': lambda c: (plug(c), c.solid(c.m.thick([(4, 28), (27, 3)], 3), 'w', bevel=False)),
    'status.hold': stopwatch,
}


def main():
    with open(os.path.join(ROOT_DIR, 'data', 'streamdeck', 'icons', 'glyphs.json')) as f:
        ids = [g['id'] for g in json.load(f)['glyphs']]
    missing = [i for i in ids if i not in DRAW]
    if missing:
        sys.exit('magicwb: no drawing for %s' % ', '.join(missing))
    src = os.path.join(PACK_DIR, 'src')
    os.makedirs(src, exist_ok=True)
    for name in os.listdir(src):
        os.remove(os.path.join(src, name))
    for gid in ids:
        draw(gid).save(os.path.join(src, gid + '.png'), optimize=True)
    manifest = {
        'format': 'streamdeck-icon-pack', 'version': 1, 'name': 'magicwb', 'title': 'MagicWB',
        'style': 'Amiga MagicWB: the 8-colour MagicWB palette, black outline, light top-left, shade and a dithered '
                 'band bottom-right, Workbench 2 button tile; on = the button pressed in, lit blue',
        'author': 'unreal-ng', 'licence': 'Original drawings made for unreal-ng, the licence of unreal-ng; the MagicWB '
                                          'palette and shading technique are imitated, no original artwork is used',
        'source': 'png-pixel', 'native': [N, N], 'pixel_aspect': [1, 1], 'background': '#959595',
        'sizes': [72, 80, 96, 112, 120], 'fallback': None, 'generated_by': 'tools/icons/streamdeck_packs/magicwb.py',
    }
    with open(os.path.join(PACK_DIR, 'pack.json'), 'w', newline='\n') as f:
        json.dump(manifest, f, indent=1)
        f.write('\n')
    print('magicwb: %d glyphs -> %s' % (len(ids), os.path.relpath(PACK_DIR, ROOT_DIR)))


if __name__ == '__main__':
    main()
