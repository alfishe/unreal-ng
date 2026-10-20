"""Amstrad ZX Spectrum +2 (1986): the grey slab with the Datacorder, dark grey sculpted caps, the button symbols on
the buttons.

Sources: references/sinclair.md §4 (the case, the badges, the deck); the colours sampled on the owner's photos
plus2-owner-top.png and plus2-owner-top2.png; the layout and the legends from keyboards/amstrad.py.
"""

from keyboards import amstrad
from model import Part

KEYBOARD = {"id": "zxplus2", "name": "ZX Spectrum +2", "models": ["PLUS2"], "size": amstrad.SIZE, "matrix": "zx",
            "twins": {"CS": 2, "SS": 2}}

CASE = "#6a6b69"
KEY = "#56585a"


def layout():
    badge = [Part("logo", {"text": "ZX Spectrum", "x": 300.6, "y": 22.0, "width": 74.0, "size": 9.0,
                           "face": "bold-italic"}, "#e6e6e4"),
             Part("logo", {"text": "+2", "x": 375.5, "y": 22.0, "width": 13.0, "size": 12.0, "face": "bold-italic"},
                  "#e6e6e4")]
    return amstrad.build(CASE, KEY, badge, amstrad.datacorder("#3a3c3e", on_case=False))
