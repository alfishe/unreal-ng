#!/usr/bin/env python3
"""The NewIcons pack: the MagicWB drawings in the Amiga NewIcons look.

    python3 tools/icons/streamdeck_packs/newicons.py    # writes data/streamdeck/icons/packs/newicons/

32 x 32 hard pixels on a plain backdrop (no button), a black outline, 5-step pastel ramps lit from the top left, no
dither and no shadow. A toggle's "on" face sits on a lit blue-grey backdrop.
"""

import reskin


class NewIcons(reskin.Skin):
    steps = 5
    light = 0.9
    edge = 0.3
    bg = (128, 132, 144)
    bg_on = (92, 116, 168)
    ramps = {
        'b': [(26, 44, 104), (58, 92, 170), (104, 142, 214), (160, 190, 238), (222, 234, 252)],
        'l': [(84, 84, 96), (128, 128, 142), (174, 174, 188), (214, 214, 224), (246, 246, 250)],
        'w': [(150, 150, 162), (196, 196, 208), (226, 226, 234), (244, 244, 248), (255, 255, 255)],
        'd': [(40, 40, 48), (66, 66, 78), (96, 96, 110), (128, 128, 142), (164, 164, 178)],
        'e': [(116, 74, 36), (168, 118, 64), (210, 164, 104), (236, 204, 150), (252, 238, 206)],
        'k': [(0, 0, 0)] * 5,
        'g': [(128, 132, 144)] * 5,
    }
    flats = {'k': (0, 0, 0), 'w': (250, 250, 252), 'l': (196, 196, 208), 'd': (78, 78, 90), 'b': (82, 118, 196),
             'e': (214, 166, 104)}


if __name__ == '__main__':
    reskin.write_pack('newicons', NewIcons(), {
        'title': 'NewIcons',
        'style': 'Amiga NewIcons: 32 x 32 hard pixels, black outline, 5-step pastel ramps lit from the top left, '
                 'no shadow; on = a lit blue-grey backdrop',
        'licence': reskin.LICENCE, 'source': 'png-pixel', 'native': [32, 32], 'pixel_aspect': [1, 1],
        'background': '#808490'})
