"""ZX Spectrum+ (1984, "48K+"): the 48K in the QL-style case with the 58-key keyboard of the 128 (keyboards/zx128.py),
the "sinclair" wordmark moulded black on black, "ZX Spectrum+" in thin red print under it, the vent comb along the
rear right and the rainbow badge alone in the bottom right corner; no heatsink, no "128K".

Sources: references/sinclair.md §2 (the case 319 x 149 mm, the margins, the badge, the keyboard); the photos
plus-top.jpg, plus-k1-top.jpg and the owner's top view plus-top-owner.jpg (2026-10-21: the badge's and the wordmark's
places, the groove under the wordmark measured on it, UNVERIFIED ±3 mm). The keys press their matrix keys as the 128's do (TRUE VIDEO = CAPS SHIFT +
3 ...). The emulator has no Spectrum+ model: the 48K is its machine, so this is the 48K's second picture.
"""

from keyboards import zx128
from keyboards.zx128 import CASE, RED
from model import Model, Part

KEYBOARD = {"id": "zxplus", "name": "ZX Spectrum+", "models": ["48K"], "size": [319, 149], "matrix": "zx",
            "twins": {"CS": 2, "SS": 2}}


def badge():
    """The four-stripe rainbow, red to cyan, bottom right: 10.4 mm of stripes leaning 3.6 mm over 11.5 mm, from
    x 288.3 mm at its foot, its foot level with the keys' front edge (measured on plus-top-owner.jpg, UNVERIFIED)"""
    parts = []
    stripe, lean, foot, top = 2.6, 3.6, 134.5, 123.0
    for i, colour in enumerate(["#e8392c", "#f5c518", "#5daa3c", "#2fa8e0"]):
        x = 288.3 + stripe * i
        parts.append(Part("stripe", {"points": [(x, foot), (x + stripe, foot), (x + stripe + lean, top),
                                                (x + lean, top)]}, colour))
    return parts


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 3, "matte": True}, CASE),
        Part("grille", {"x": 150.0, "y": 1.0, "w": 150.0, "h": 3.6, "slots": 52}, CASE),
        Part("groove", {"x": 0.6, "y": 20.6, "w": W - 1.2, "h": 0.9}, CASE),  # the rear band's front edge
    ]
    zx128.deck(m, W)
    m.case += [
        Part("logo", {"text": "sinclair", "x": 15.0, "y": 19.0, "width": 85.0, "size": 12.0, "spacing": 0.25,
                      "face": "rounded", "outline": True}, "#1e2021"),
        Part("logo", {"text": "ZX Spectrum+", "x": 15.0, "y": 27.2, "width": 38.0, "size": 4.6, "face": "sans"}, RED),
    ]
    m.case += badge()
    zx128.keys(m)
    return m
