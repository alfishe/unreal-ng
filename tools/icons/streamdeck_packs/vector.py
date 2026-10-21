"""The MagicWB drawings rendered smooth at 256 px for the vector-look packs (line, duotone, banner, keycap, os4).

`HiResCanvas` offers the `streamdeck_pixel.Canvas` interface the MagicWB glyphs draw with, in the same 32-unit
coordinates, but at S = 8 sub-pixels per unit: every solid shape gets an id, MagicWB's outline rings become thin gaps
between shapes, single-pixel details become crisp blocks and lines. A style turns the key / id / gap layers into a
256 x 256 picture that the pack renderer scales down smoothly (source "png-smooth").
"""

import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, HERE)

import magicwb  # noqa: E402
import streamdeck_pixel as sp  # noqa: E402

ROOT_DIR = magicwb.ROOT_DIR
S = 8
N = 32
R = N * S  # 256
SS = 2


class HiMasks:
    """The Masks of streamdeck_pixel in 32-unit coordinates, at R x R."""

    def _draw(self, fn):
        im = Image.new('L', (R * SS, R * SS), 0)
        fn(ImageDraw.Draw(im), S * SS)
        a = np.asarray(im, dtype=np.float32) / 255.0
        return a.reshape(R, SS, R, SS).mean(axis=(1, 3)) >= 0.5

    def poly(self, pts):
        return self._draw(lambda d, s: d.polygon([(x * s, y * s) for x, y in pts], fill=255))

    def ell(self, x0, y0, x1, y1):
        return self._draw(lambda d, s: d.ellipse([x0 * s, y0 * s, (x1 + 1) * s - 1, (y1 + 1) * s - 1], fill=255))

    def circle(self, cx, cy, r):
        return self._draw(lambda d, s: d.ellipse([(cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s], fill=255))

    def rect(self, x0, y0, x1, y1, r=0):
        return self._draw(lambda d, s: d.rounded_rectangle([x0 * s, y0 * s, (x1 + 1) * s - 1, (y1 + 1) * s - 1],
                                                           radius=r * s, fill=255))

    def thick(self, pts, w):
        def fn(d, s):
            q = [((x + 0.5) * s, (y + 0.5) * s) for x, y in pts]
            d.line(q, fill=255, width=int(round(w * s)), joint='curve')
            for x, y in (q[0], q[-1]):
                r = w * s / 2
                d.ellipse([x - r, y - r, x + r, y + r], fill=255)

        return self._draw(fn)

    def arc(self, cx, cy, r, a0, a1, w):
        def fn(d, s):
            ro = (r + w / 2) * s
            d.arc([(cx + 0.5) * s - ro, (cy + 0.5) * s - ro, (cx + 0.5) * s + ro, (cy + 0.5) * s + ro], a0, a1,
                  fill=255, width=int(round(w * s)))

        return self._draw(fn)


class HiResCanvas:
    """Keys (`c`), shape ids (`ids`: >0 a solid shape, -1 a detail, 0 nothing) and gaps (MagicWB's outline rings)."""

    def __init__(self, gap=0.8):
        self.n = N
        self.c = np.full((R, R), 'g', dtype='<U1')
        self.ids = np.zeros((R, R), int)
        self.gap = np.zeros((R, R), bool)
        self.cut = np.zeros((R, R), bool)  # a detail drawn over a solid shape (cut into it), not over the backdrop
        self.m = HiMasks()
        self.gap_w = gap
        self.next_id = 1
        self.keys = {}

    def solid(self, mask, colour, bevel=True, outline=True, dither=None):
        if outline and self.gap_w > 0:
            ring = ndimage.binary_dilation(mask, iterations=max(1, int(round(self.gap_w * S)))) & ~mask
            self.c[ring] = 'g'
            self.ids[ring] = 0
            self.gap |= ring
        self.c[mask] = colour
        self.ids[mask] = self.next_id
        self.keys[self.next_id] = colour
        self.gap &= ~mask
        self.next_id += 1

    def _block(self, x0, y0, x1, y1, col):
        x0, x1 = sorted((x0, x1))
        y0, y1 = sorted((y0, y1))
        a, b = max(0, x0 * S), min(R, (x1 + 1) * S)
        c, d = max(0, y0 * S), min(R, (y1 + 1) * S)
        if a < b and c < d:
            self.cut[c:d, a:b] = self.ids[c:d, a:b] > 0
            self.c[c:d, a:b] = col
            self.ids[c:d, a:b] = -1
            self.gap[c:d, a:b] = False

    def px(self, x, y, col):
        self._block(x, y, x, y, col)

    def hline(self, x0, x1, y, col):
        self._block(x0, y, x1, y, col)

    def vline(self, x, y0, y1, col):
        self._block(x, y0, x, y1, col)

    def box(self, x0, y0, x1, y1, col):
        self._block(x0, y0, x1, y1, col)

    def line(self, x0, y0, x1, y1, col):
        m = self.m.thick([(x0, y0), (x1, y1)], 1.0)
        self.cut[m] = self.ids[m] > 0
        self.c[m] = col
        self.ids[m] = -1
        self.gap[m] = False

    def text_mask(self, s, x, y, scale=1, gap=1):
        m = np.zeros((R, R), bool)
        for ch in s:
            for j, row in enumerate(sp.FONT_4X7[ch]):
                for i, bit in enumerate(row):
                    if bit == '1':
                        yy, xx = (y + j * scale) * S, (x + i * scale) * S
                        m[yy:yy + scale * S, xx:xx + scale * S] = True
            x += (4 + gap) * scale
        return m

    def text(self, s, x, y, col, scale=1, gap=1):
        m = self.text_mask(s, x, y, scale, gap)
        self.cut[m] = self.ids[m] > 0
        self.c[m] = col
        self.ids[m] = -1


def glyphs():
    with open(os.path.join(ROOT_DIR, 'data', 'streamdeck', 'icons', 'glyphs.json')) as f:
        return json.load(f)['glyphs']


def canvas_for(gid, gap=0.8):
    c = HiResCanvas(gap)
    magicwb.DRAW[gid](c)
    return c


def drawn(c):
    return c.c != 'g'


def rgb(t):
    return np.array(t, float)


def mix(a, b, t):
    return rgb(a) * (1 - t) + rgb(b) * t


def to_image(img, alpha=None):
    a = np.clip(np.round(img), 0, 255).astype(np.uint8)
    if alpha is None:
        return Image.fromarray(a, 'RGB')
    return Image.fromarray(np.dstack([a, np.clip(alpha * 255, 0, 255).astype(np.uint8)]), 'RGBA')


def flat_glyph(c, colours, black_fill=None, black_line=None):
    """RGB + alpha of the glyph: every key in its colour (None = knocked out), gaps knocked out. A solid shape in
    MagicWB's black (a filled square, a lit dot) takes `black_fill` (black details are cut out either way)."""
    img = np.zeros((R, R, 3), float)
    alpha = np.zeros((R, R), float)
    for key, col in colours.items():
        if col is None:
            continue
        sel = c.c == key
        img[sel] = rgb(col)
        alpha[sel] = 1.0
    if black_fill is not None:
        sel = (c.c == 'k') & (c.ids > 0)
        img[sel] = rgb(black_fill)
        alpha[sel] = 1.0
    if black_line is not None:  # a black line drawn on the backdrop (legs, rays) shows; on a shape it is a cut
        sel = (c.c == 'k') & (c.ids == -1) & ~c.cut
        img[sel] = rgb(black_line)
        alpha[sel] = 1.0
    return img, alpha


def place(dst, glyph_rgb, glyph_alpha, scale, cx, cy):
    """Paste the glyph (R x R) into the R x R picture `dst`, scaled and centred at (cx, cy) (in R pixels)."""
    size = int(round(R * scale))
    g = to_image(glyph_rgb, glyph_alpha).resize((size, size), Image.LANCZOS)
    base = to_image(dst).convert('RGBA')
    base.alpha_composite(g, (int(round(cx - size / 2)), int(round(cy - size / 2))))
    return np.asarray(base.convert('RGB'), float)


def write_pack(name, render, manifest):
    pack_dir = os.path.join(ROOT_DIR, 'data', 'streamdeck', 'icons', 'packs', name)
    src = os.path.join(pack_dir, 'src')
    os.makedirs(src, exist_ok=True)
    for f in os.listdir(src):
        os.remove(os.path.join(src, f))
    gl = glyphs()
    for g in gl:
        render(g).save(os.path.join(src, g['id'] + '.png'), optimize=True)
    base = {'format': 'streamdeck-icon-pack', 'version': 1, 'name': name, 'author': 'unreal-ng',
            'licence': ('Original drawings made for unreal-ng (the MagicWB pack\'s drawings, rendered smooth), the '
                        'licence of unreal-ng'),
            'source': 'png-smooth', 'sizes': [72, 80, 96, 112, 120], 'fallback': 'magicwb',
            'generated_by': 'tools/icons/streamdeck_packs/%s.py' % name}
    base.update(manifest)
    with open(os.path.join(pack_dir, 'pack.json'), 'w', newline='\n') as f:
        json.dump(base, f, indent=1)
        f.write('\n')
    print('%s: %d glyphs -> %s' % (name, len(gl), os.path.relpath(pack_dir, ROOT_DIR)))
