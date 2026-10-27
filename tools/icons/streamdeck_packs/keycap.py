#!/usr/bin/env python3
"""The matte keycap pack: a dark rubber-key cap, an off-white legend and a small coloured bar per action group.

    python3 tools/icons/streamdeck_packs/keycap.py      # writes data/streamdeck/icons/packs/keycap/

No gradient and no sheen: the cap is flat with a flat lower lip; the legend sits high, the group's bar low with a
clear gap between them. A toggle's "on" face lights the bar wide and the cap a little.
"""

import numpy as np
from PIL import ImageDraw

import duotone
import vector as v

PAD = (18, 19, 22)
CAP = (44, 46, 51)
CAP_ON = (58, 61, 68)
LIP = (32, 33, 37)
LEGEND = (232, 228, 214)


def render(g):
    on = g['id'].endswith('.on')
    accent = duotone.GROUPS[g['group']][1]
    cap = CAP_ON if on else CAP
    dim = tuple(v.mix(LEGEND, cap, 0.5))
    c = v.canvas_for(g['id'], gap=0.9)
    glyph, alpha = v.flat_glyph(c, {'w': LEGEND, 'l': LEGEND, 'e': LEGEND, 'b': LEGEND, 'd': dim, 'k': None,
                                    'g': None}, black_fill=dim, black_line=LEGEND)
    base = v.to_image(np.zeros((v.R, v.R, 3)) + v.rgb(PAD))
    d = ImageDraw.Draw(base)
    m = int(v.R * 0.05)
    rad = int(v.R * 0.14)
    d.rounded_rectangle([m, m, v.R - 1 - m, v.R - 1 - m], radius=rad, fill=LIP)
    d.rounded_rectangle([m, m, v.R - 1 - m, v.R - 1 - m - int(v.R * 0.06)], radius=rad, fill=cap)
    bw = int(v.R * (0.34 if on else 0.2))
    by = int(v.R * 0.78)
    d.rounded_rectangle([v.R // 2 - bw // 2, by, v.R // 2 + bw // 2, by + int(v.R * 0.035)],
                        radius=int(v.R * 0.018), fill=accent)
    img = v.place(np.asarray(base, float), glyph, alpha, 0.56, v.R / 2, v.R * 0.42)
    return v.to_image(img)


if __name__ == '__main__':
    v.write_pack('keycap', render, {
        'title': 'Matte keycap',
        'style': 'A dark matte rubber-key cap with a flat lip, an off-white legend high, a small coloured bar per '
                 'action group low with a clear gap; no gradient, no sheen; on = a wide bar and a lighter cap',
        'background': '#121316'})
