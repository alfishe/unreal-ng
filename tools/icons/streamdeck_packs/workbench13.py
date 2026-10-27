#!/usr/bin/env python3
"""The Workbench 1.3 pack: the MagicWB drawings in the 4 colours of Amiga Workbench 1.3.

    python3 tools/icons/streamdeck_packs/workbench13.py   # writes data/streamdeck/icons/packs/workbench13/

Blue backdrop, white, black, orange; no shading. Drawn for the 640 x 200 screen where a pixel is twice as tall as it
is wide: 32 x 16 native (each pair of rows of the square drawing becomes one tall row, details win over the backdrop),
shown with pixel_aspect [1, 2]. A toggle's "on" face is inverted: an orange backdrop.
"""

import numpy as np
from PIL import Image

import reskin

BLUE = (0, 85, 170)
WHITE = (255, 255, 255)
BLACK = (0, 0, 34)
ORANGE = (255, 136, 0)


class Workbench13(reskin.Skin):
    steps = 1
    bg = BLUE
    bg_on = ORANGE
    flats = {'k': BLACK, 'd': BLACK, 'w': WHITE, 'l': WHITE, 'b': ORANGE, 'e': ORANGE}
    # A shaded fill (a solid shape) in the dark grey becomes white with its black outline, like the 1.3 icons; the
    # dark grey of a detail (a line, a pixel run) stays black
    fills = {'k': BLACK, 'd': WHITE, 'w': WHITE, 'l': WHITE, 'b': ORANGE, 'e': ORANGE}

    def flat(self, key, on):
        if key == 'g':
            return self.bg_on if on else self.bg
        if on and key in 'be':
            return BLUE  # on the orange backdrop the orange parts turn blue
        return self.flats[key]

    def ramp(self, key, on):
        if key == 'g':
            return [self.flat(key, on)]
        if on and key in 'be':
            return [BLUE]
        return [self.fills[key]]


def squash(img):
    """32 x 32 -> 32 x 16: of each pair of rows, per column, the colour that is rarer in the whole picture wins (a
    1-row detail beats the fill round it), the backdrop never wins over a drawn colour."""
    img = np.clip(np.round(img), 0, 255).astype(int)
    keys = [tuple(p) for p in img.reshape(-1, 3)]
    count = {}
    for k in keys:
        count[k] = count.get(k, 0) + 1
    bg = tuple(img[0, 0])
    out = np.zeros((16, 32, 3), int)
    for y in range(16):
        for x in range(32):
            a, b = tuple(img[2 * y, x]), tuple(img[2 * y + 1, x])
            if a == bg or (b != bg and count[b] < count[a]):
                a = b
            out[y, x] = a
    return Image.fromarray(out.astype(np.uint8), 'RGB')

if __name__ == '__main__':
    reskin.write_pack('workbench13', Workbench13(), {
        'title': 'Workbench 1.3',
        'style': 'Amiga Workbench 1.3: 4 colours (blue, white, black, orange), no shading, tall pixels (32 x 16 '
                 'native, pixel aspect 1:2); on = an orange backdrop',
        'licence': reskin.LICENCE, 'source': 'png-pixel', 'native': [32, 16], 'pixel_aspect': [1, 2],
        'background': '#0055AA'}, image_of=squash)
