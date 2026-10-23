#!/usr/bin/env python3
"""The duotone pack: a solid off-white glyph on a lightly tinted rounded plate, one calm accent per action group.

    python3 tools/icons/streamdeck_packs/duotone.py     # writes data/streamdeck/icons/packs/duotone/

The plate's tint and the accent (the drawing's blue parts) come from the glyph's group: green for run control, cyan
for time travel, amber for media and so on. Dark parts of a drawing are a dimmed tone, black details are cut out.
A toggle's "on" face has a brighter plate.
"""

import numpy as np

import vector as v

PAD = (23, 24, 28)
GLYPH = (238, 235, 226)
# group: (plate, accent)
GROUPS = {
    'nav': ((44, 48, 56), (150, 170, 196)),
    'run': ((40, 56, 46), (126, 196, 136)),
    'machine': ((60, 50, 40), (222, 162, 96)),
    'ttd': ((36, 54, 64), (104, 186, 222)),
    'debug': ((36, 58, 56), (98, 200, 184)),
    'media': ((62, 54, 40), (226, 188, 96)),
    'audio': ((48, 58, 40), (180, 204, 108)),
    'video': ((44, 48, 66), (146, 160, 232)),
    'input': ((52, 52, 56), (196, 196, 204)),
    'basic': ((58, 56, 42), (230, 214, 136)),
    'settings': ((50, 52, 56), (176, 182, 192)),
    'status': ((56, 48, 44), (226, 150, 108)),
}


def render(g):
    on = g['id'].endswith('.on')
    plate, accent = GROUPS[g['group']]
    if on:
        plate = tuple(int(x * 1.55) for x in plate)
    dim = tuple(v.mix(GLYPH, plate, 0.45))
    c = v.canvas_for(g['id'], gap=0.9)
    glyph, alpha = v.flat_glyph(c, {'w': GLYPH, 'l': GLYPH, 'e': GLYPH, 'b': accent, 'd': dim, 'k': None,
                                    'g': None}, black_fill=dim, black_line=GLYPH)
    img = np.zeros((v.R, v.R, 3)) + v.rgb(PAD)
    from PIL import Image, ImageDraw
    base = v.to_image(img)
    d = ImageDraw.Draw(base)
    m = int(v.R * 0.06)
    d.rounded_rectangle([m, m, v.R - 1 - m, v.R - 1 - m], radius=int(v.R * 0.17), fill=plate)
    img = np.asarray(base, float)
    img = v.place(img, glyph, alpha, 0.66, v.R / 2, v.R / 2)
    return v.to_image(img)


if __name__ == '__main__':
    v.write_pack('duotone', render, {
        'title': 'Duotone',
        'style': 'A solid off-white glyph on a lightly tinted rounded plate, one calm accent per action group; on = '
                 'a brighter plate',
        'background': '#17181C'})
