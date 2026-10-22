"""Pixel-art helpers for the Stream Deck icon packs drawn at native size (MagicWB, NewIcons, GlowIcons, Workbench).

A Canvas holds palette keys (one character per pixel). Shapes are masks made at 4 x supersampling and cut at half
coverage, so diagonals and circles come out as clean hand-like pixel runs; `solid()` paints a mask in one colour with
the pack's shading rule (outline, light edge, shade edge, dithered band); `px` / `hline` / `vline` / `line` / `text`
put single-pixel details. Coordinates are pixels: pixel (x, y) covers [x, x+1) x [y, y+1).

Requires Pillow and numpy.
"""

import math

import numpy as np
from PIL import Image, ImageDraw

SS = 4


class Masks:
    """Boolean masks on an n x n grid."""

    def __init__(self, n):
        self.n = n

    def _draw(self, fn):
        im = Image.new('L', (self.n * SS, self.n * SS), 0)
        fn(ImageDraw.Draw(im), SS)
        a = np.asarray(im, dtype=np.float32) / 255.0
        return a.reshape(self.n, SS, self.n, SS).mean(axis=(1, 3)) >= 0.5

    def poly(self, pts):
        return self._draw(lambda d, s: d.polygon([(x * s, y * s) for x, y in pts], fill=255))

    def ell(self, x0, y0, x1, y1):
        """Ellipse inside the pixel box x0..x1, y0..y1 (inclusive)."""
        return self._draw(lambda d, s: d.ellipse([x0 * s, y0 * s, (x1 + 1) * s - 1, (y1 + 1) * s - 1], fill=255))

    def circle(self, cx, cy, r):
        return self._draw(lambda d, s: d.ellipse([(cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s], fill=255))

    def rect(self, x0, y0, x1, y1, r=0):
        """Rectangle over the pixels x0..x1, y0..y1 (inclusive), corner radius r."""
        return self._draw(lambda d, s: d.rounded_rectangle([x0 * s, y0 * s, (x1 + 1) * s - 1, (y1 + 1) * s - 1],
                                                           radius=r * s, fill=255))

    def thick(self, pts, w):
        """A polyline `w` pixels wide through pixel centres given as (x, y) in pixel units."""

        def fn(d, s):
            q = [((x + 0.5) * s, (y + 0.5) * s) for x, y in pts]
            d.line(q, fill=255, width=int(round(w * s)), joint='curve')
            for x, y in (q[0], q[-1]):
                r = w * s / 2
                d.ellipse([x - r, y - r, x + r, y + r], fill=255)

        return self._draw(fn)

    def arc(self, cx, cy, r, a0, a1, w):
        """An arc of radius r (to the stroke's middle), w wide, clockwise from a0 to a1 degrees (0 = east)."""

        def fn(d, s):
            ro = (r + w / 2) * s
            d.arc([(cx + 0.5) * s - ro, (cy + 0.5) * s - ro, (cx + 0.5) * s + ro, (cy + 0.5) * s + ro], a0, a1,
                  fill=255, width=int(round(w * s)))

        return self._draw(fn)


def polar(cx, cy, r, a):
    t = math.radians(a)
    return cx + r * math.cos(t), cy + r * math.sin(t)


def arrow_pts(x0, y0, x1, y1, shaft, head, head_len):
    """Polygon of a solid arrow from (x0, y0) to its tip (x1, y1) (pixel units, centre lines)."""
    ang = math.atan2(y1 - y0, x1 - x0)
    ux, uy = math.cos(ang), math.sin(ang)
    nx, ny = -uy, ux
    bx, by = x1 - ux * head_len, y1 - uy * head_len
    hs, hh = shaft / 2, head / 2
    pts = [(x0 + nx * hs, y0 + ny * hs), (bx + nx * hs, by + ny * hs), (bx + nx * hh, by + ny * hh), (x1, y1),
           (bx - nx * hh, by - ny * hh), (bx - nx * hs, by - ny * hs), (x0 - nx * hs, y0 - ny * hs)]
    return [(x + 0.5, y + 0.5) for x, y in pts]


FONT_4X7 = {
    '0': ['0110', '1001', '1001', '1001', '1001', '1001', '0110'],
    '1': ['0010', '0110', '0010', '0010', '0010', '0010', '0111'],
    '2': ['0110', '1001', '0001', '0010', '0100', '1000', '1111'],
    '4': ['0010', '0110', '1010', '1010', '1111', '0010', '0010'],
    '8': ['0110', '1001', '1001', '0110', '1001', '1001', '0110'],
    'N': ['1001', '1101', '1101', '1011', '1011', '1001', '1001'],
    'T': ['1111', '0110', '0110', '0110', '0110', '0110', '0110'],
}


class Canvas:
    """`style` gives: outline (key), hi(base) -> key, lo(base) -> key, dither (bool)."""

    def __init__(self, n, bg, style):
        self.n = n
        self.c = np.full((n, n), bg, dtype='<U1')
        self.m = Masks(n)
        self.style = style

    # --- shading
    def solid(self, mask, colour, bevel=True, outline=True, dither=None):
        n = self.n
        st = self.style
        pad = np.pad(mask, 3)
        inside = lambda dy, dx: pad[3 + dy:3 + dy + n, 3 + dx:3 + dx + n]  # noqa: E731
        if outline and st.outline:
            ring = np.zeros_like(mask)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ring |= inside(dy, dx)
            self.c[ring & ~mask] = st.outline
        self.c[mask] = colour
        if not bevel:
            return
        top = mask & (~inside(-1, 0) | ~inside(0, -1))
        bot = mask & (~inside(1, 0) | ~inside(0, 1)) & ~top
        self.c[top] = st.hi(colour)
        self.c[bot] = st.lo(colour)
        if dither if dither is not None else st.dither:
            yy, xx = np.mgrid[0:n, 0:n]
            thick = inside(-2, 0) & inside(0, -2) & inside(-3, 0) & inside(0, -3)
            band = mask & ~top & ~bot & (~inside(2, 0) | ~inside(0, 2)) & thick & ((xx + yy) % 2 == 1)
            self.c[band] = st.lo(colour)

    # --- single pixels
    def px(self, x, y, c):
        if 0 <= x < self.n and 0 <= y < self.n:
            self.c[y, x] = c

    def hline(self, x0, x1, y, c):
        for x in range(min(x0, x1), max(x0, x1) + 1):
            self.px(x, y, c)

    def vline(self, x, y0, y1, c):
        for y in range(min(y0, y1), max(y0, y1) + 1):
            self.px(x, y, c)

    def box(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            self.hline(x0, x1, y, c)

    def line(self, x0, y0, x1, y1, c):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.px(x0, y0, c)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def text(self, s, x, y, c, scale=1, gap=1):
        for ch in s:
            rows = FONT_4X7[ch]
            for j, row in enumerate(rows):
                for i, bit in enumerate(row):
                    if bit == '1':
                        for a in range(scale):
                            for b in range(scale):
                                self.px(x + i * scale + a, y + j * scale + b, c)
            x += (4 + gap) * scale

    def text_mask(self, s, x, y, scale=1, gap=1):
        m = np.zeros((self.n, self.n), bool)
        for ch in s:
            for j, row in enumerate(FONT_4X7[ch]):
                for i, bit in enumerate(row):
                    if bit == '1':
                        m[y + j * scale:y + (j + 1) * scale, x + i * scale:x + (i + 1) * scale] = True
            x += (4 + gap) * scale
        return m

    def image(self, palette):
        im = Image.new('RGB', (self.n, self.n))
        im.putdata([palette[k] for k in self.c.flatten()])
        return im
