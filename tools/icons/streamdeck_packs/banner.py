#!/usr/bin/env python3
"""The UnrealNG banner pack: white solid glyphs with the banner letters' blue drop shadow on the strip's navy.

    python3 tools/icons/streamdeck_packs/banner.py      # writes data/streamdeck/icons/packs/banner/

The look of data/streamdeck/themes/unrealng/strip.png without its rainbow stripes (they do not show on the deck's
acrylic). Dark parts of a drawing are dimmed white, MagicWB's black details are cut out of the glyph. A toggle's
"on" face has a lit blue tile.
"""

import numpy as np

import vector as v

NAVY = (10, 12, 40)
NAVY_ON = (30, 44, 120)
SHADOW = (58, 64, 168)
WHITE = (250, 250, 252)
COLOURS = {'w': WHITE, 'l': WHITE, 'e': WHITE, 'b': WHITE, 'd': (150, 156, 196), 'k': None, 'g': None}


def render(g):
    on = g['id'].endswith('.on')
    c = v.canvas_for(g['id'], gap=0.9)
    glyph, alpha = v.flat_glyph(c, COLOURS, black_fill=(96, 104, 176), black_line=WHITE)
    tile = np.zeros((v.R, v.R, 3)) + v.rgb(NAVY_ON if on else NAVY)
    shadow_rgb = np.zeros_like(glyph) + v.rgb(SHADOW)
    off = int(round(1.1 * v.S))
    sh = np.roll(np.roll(alpha, off, 0), off, 1)
    img = v.place(tile, shadow_rgb, sh, 0.8, v.R / 2, v.R / 2)
    img = v.place(img, glyph, alpha, 0.8, v.R / 2, v.R / 2)
    return v.to_image(img)


if __name__ == '__main__':
    v.write_pack('banner', render, {
        'title': 'UnrealNG banner',
        'style': 'White solid glyphs with the UnrealNG banner\'s blue drop shadow on its navy, no stripes; on = a '
                 'lit blue tile',
        'background': '#0A0C28'})
