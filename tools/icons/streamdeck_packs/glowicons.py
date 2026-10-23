#!/usr/bin/env python3
"""The GlowIcons pack: the MagicWB drawings in the AmigaOS 3.5 / 3.9 GlowIcons look.

    python3 tools/icons/streamdeck_packs/glowicons.py   # writes data/streamdeck/icons/packs/glowicons/

32 x 32 pixels with smooth 8-step ramps, a dark outline (not black) that sits softly on the backdrop, a soft drop
shadow; a toggle's "on" face has the warm yellow GlowIcons selection glow round its silhouette.
"""

import numpy as np
from PIL import Image, ImageFilter

import reskin

BG = np.array((168, 172, 180), float)


class GlowIcons(reskin.Skin):
    steps = 8
    light = 0.85
    edge = 0.22
    bg = tuple(BG)
    bg_on = tuple(BG)
    ramps = {
        'b': [(18, 38, 86), (30, 62, 128), (46, 92, 168), (70, 122, 196), (100, 152, 216), (138, 182, 232),
              (180, 210, 244), (222, 236, 252)],
        'l': [(84, 86, 94), (114, 117, 126), (146, 149, 158), (176, 179, 188), (204, 207, 214), (222, 224, 230),
              (236, 238, 242), (248, 248, 250)],
        'w': [(176, 179, 188), (204, 207, 214), (220, 222, 228), (232, 234, 238), (240, 241, 244), (246, 247, 249),
              (251, 251, 252), (255, 255, 255)],
        'd': [(40, 42, 48), (52, 54, 60), (66, 68, 76), (84, 86, 94), (100, 103, 112), (116, 119, 128),
              (132, 135, 144), (150, 153, 162)],
        'e': [(96, 52, 10), (140, 80, 18), (184, 112, 30), (218, 146, 48), (238, 178, 76), (248, 206, 116),
              (252, 228, 168), (255, 244, 214)],
        'k': [(34, 36, 44)] * 8,
        'g': [tuple(BG)] * 8,
    }
    flats = {'k': (34, 36, 44), 'w': (246, 247, 249), 'l': (190, 193, 200), 'd': (70, 72, 80), 'b': (70, 122, 196),
             'e': (232, 166, 64)}

    def finish(self, img, c):
        sil = c.shapes
        # The outline ring sits softly: halfway between the dark rim and the backdrop where it borders the backdrop
        ring = (c.c == 'k') & (c.vk == '')
        pad = np.pad(c.c == 'g', 1)
        touches_bg = np.zeros_like(ring)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            touches_bg |= pad[1 + dy:1 + dy + reskin.N, 1 + dx:1 + dx + reskin.N]
        soft = ring & touches_bg
        img[soft] = img[soft] * 0.6 + BG * 0.4
        bgmask = c.c == 'g'
        if c.on:
            g = Image.fromarray((sil * 255).astype(np.uint8))
            g = np.asarray(g.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(1.6)), float) / 255
            g = (np.clip(g * 1.6, 0, 1) * 0.9)[..., None]
            col = np.array((255, 214, 64), float)
            img[bgmask] = img[bgmask] * (1 - g[bgmask]) + col * g[bgmask]
        else:
            sh = Image.fromarray((sil * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.3))
            sh = np.roll(np.roll(np.asarray(sh, float) / 255.0, 2, 0), 2, 1) * 0.45
            img[bgmask] = img[bgmask] * (1 - sh[bgmask, None])
        return img


if __name__ == '__main__':
    reskin.write_pack('glowicons', GlowIcons(), {
        'title': 'GlowIcons',
        'style': 'AmigaOS 3.5 / 3.9 GlowIcons: smooth 8-step ramps, a soft dark outline, a soft drop shadow; on = '
                 'the warm yellow selection glow',
        'licence': reskin.LICENCE, 'source': 'png-pixel', 'native': [32, 32], 'pixel_aspect': [1, 1],
        'background': '#A8ACB4'})
