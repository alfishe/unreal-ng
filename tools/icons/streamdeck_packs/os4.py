#!/usr/bin/env python3
"""The AmigaOS 4 pack: smooth 32-bit icons with gentle gradients and a soft drop shadow on a dark blue backdrop.

    python3 tools/icons/streamdeck_packs/os4.py         # writes data/streamdeck/icons/packs/os4/

Every shape of a drawing gets a gentle top-to-bottom gradient of its colour and a thin dark rim; the whole glyph
casts a soft shadow. Restrained: no gloss highlights. A toggle's "on" face has a lit blue backdrop.
"""

import numpy as np
from PIL import Image, ImageFilter

import vector as v

BG = (34, 46, 72)
BG_ON = (48, 78, 132)
RIM = (16, 22, 36)
# key: (top, bottom) of the gradient
GRAD = {
    'b': ((142, 197, 240), (30, 78, 142)),
    'l': ((233, 236, 240), (142, 149, 159)),
    'w': ((255, 255, 255), (206, 211, 218)),
    'd': ((104, 110, 120), (40, 43, 49)),
    'e': ((246, 196, 106), (210, 130, 40)),
    'k': ((44, 47, 54), (24, 26, 30)),
}
DETAIL = {'k': (26, 28, 34), 'w': (250, 250, 252), 'l': (200, 204, 212), 'd': (70, 74, 82), 'b': (70, 128, 196),
          'e': (232, 166, 70)}


def render(g):
    on = g['id'].endswith('.on')
    c = v.canvas_for(g['id'], gap=0.55)
    img = np.zeros((v.R, v.R, 3), float)
    alpha = np.zeros((v.R, v.R), float)
    yy = np.mgrid[0:v.R, 0:v.R][0]
    for sid, key in c.keys.items():
        sel = c.ids == sid
        if not sel.any() or key not in GRAD:
            continue
        ys = np.nonzero(sel)[0]
        t = ((yy - ys.min()) / max(1, ys.max() - ys.min()))[..., None]
        top, bot = GRAD[key]
        img[sel] = (v.rgb(top) * (1 - t) + v.rgb(bot) * t)[sel]
        alpha[sel] = 1
    det = c.ids == -1
    for key, col in DETAIL.items():
        sel = det & (c.c == key)
        if key == 'k':
            on_back = sel & ~c.cut
            img[on_back] = v.rgb((200, 206, 216))  # a black line on the backdrop would vanish: light grey
            alpha[on_back] = 1
            sel = sel & c.cut
        img[sel] = v.rgb(col)
        alpha[sel] = 1
    ring = c.gap & (v.ndimage.binary_dilation(alpha > 0, iterations=v.S))
    img[ring] = v.rgb(RIM)
    alpha[ring] = 1
    glyph = v.to_image(img, alpha)
    sh = Image.new('RGBA', glyph.size, (0, 0, 0, 0))
    sh.putalpha(glyph.split()[3].point(lambda a: int(a * 0.55)))
    sh = sh.filter(ImageFilter.GaussianBlur(v.R / 40))
    out = Image.new('RGBA', glyph.size, (BG_ON if on else BG) + (255,))
    scale = 0.8
    size = int(v.R * scale)
    off = (v.R - size) // 2
    out.alpha_composite(sh.resize((size, size), Image.LANCZOS), (off + v.R // 60, off + v.R // 30))
    out.alpha_composite(glyph.resize((size, size), Image.LANCZOS), (off, off))
    return out.convert('RGB')


if __name__ == '__main__':
    v.write_pack('os4', render, {
        'title': 'AmigaOS 4',
        'style': 'Smooth 32-bit icons: gentle gradients per shape, a thin dark rim, a soft drop shadow on a dark '
                 'blue backdrop, no gloss; on = a lit blue backdrop',
        'background': '#222E48'})
