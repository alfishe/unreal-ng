#!/usr/bin/env python3
"""The crisp line pack: even strokes along every shape's contour, light on near-black, one calm cyan accent.

    python3 tools/icons/streamdeck_packs/line.py        # writes data/streamdeck/icons/packs/line/

The MagicWB drawings traced: a stroke of one width runs centred on each boundary between shapes (and between a
shape and the backdrop), interiors stay the backdrop; the drawing's blue parts are traced in cyan, small details
(lines, pixel runs) are drawn solid. A toggle's "on" face fills its shapes in a dim tone.
"""

import numpy as np

import vector as v

BG = (17, 18, 21)
LINE = (236, 234, 226)
CYAN = (110, 196, 214)
FILL_ON = (58, 62, 70)
STROKE = 1.5  # units of the 32 grid


def render(g):
    on = g['id'].endswith('.on')
    c = v.canvas_for(g['id'], gap=0)
    lab = np.where(c.ids > 0, c.ids, 0)
    change = np.zeros_like(lab, bool)
    for dy, dx in ((0, 1), (1, 0)):
        a = lab[:v.R - dy, :v.R - dx]
        b = lab[dy:, dx:]
        diff = a != b
        change[:v.R - dy, :v.R - dx] |= diff
        change[dy:, dx:] |= diff
    r = max(1, int(round(STROKE * v.S / 2)))
    band = v.ndimage.binary_dilation(change, iterations=r)
    blue = np.zeros_like(band)
    other = np.zeros_like(band)
    for sid, key in c.keys.items():
        region = lab == sid
        if key == 'b':
            blue |= region
        elif key != 'g':
            other |= region
    near_blue = v.ndimage.binary_dilation(blue, iterations=r + 1)
    img = np.zeros((v.R, v.R, 3)) + v.rgb(BG)
    alpha = np.zeros((v.R, v.R))
    if on:
        filled = (lab > 0)
        img[filled] = v.rgb(FILL_ON)
        alpha[filled] = 1
    img[band] = v.rgb(LINE)
    cy = band & near_blue & ~v.ndimage.binary_erosion(other, iterations=r)
    img[cy] = v.rgb(CYAN)
    alpha[band] = 1
    det = (c.ids == -1) & (c.c != 'g')
    img[det] = v.rgb(LINE)
    img[det & (c.c == 'b')] = v.rgb(CYAN)
    alpha[det] = 1
    base = np.zeros((v.R, v.R, 3)) + v.rgb(BG)
    return v.to_image(v.place(base, img, alpha, 0.8, v.R / 2, v.R / 2))


if __name__ == '__main__':
    v.write_pack('line', render, {
        'title': 'Crisp line',
        'style': 'Even strokes along every shape\'s contour, light on near-black, the blue parts in a calm cyan; '
                 'on = the shapes filled in a dim tone',
        'background': '#111215'})
