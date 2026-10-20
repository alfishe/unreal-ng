"""Amstrad ZX Spectrum +2A (1987; the +2B looks the same): the +2 mould in black, black caps, plain black Datacorder
buttons with their symbols printed on the case above them; the badge still reads "ZX Spectrum +2".

Sources: references/sinclair.md §5 (plus2a-k1-datacorder.jpg, plus2a-k1-view.jpg); the layout, the legends and the
deck from keyboards/amstrad.py, measured for the +2.
"""

from keyboards import amstrad
from model import Part

KEYBOARD = {"id": "zxplus2a", "name": "ZX Spectrum +2A", "models": ["PLUS2A"], "size": amstrad.SIZE, "matrix": "zx",
            "twins": {"CS": 2, "SS": 2}}

CASE = "#2a2c2e"
KEY = "#232527"
BADGE = "#ececea"


def layout():
    badge = [Part("logo", {"text": "ZX Spectrum", "x": 300.6, "y": 22.0, "width": 74.0, "size": 9.0,
                           "face": "bold-italic"}, BADGE),
             Part("logo", {"text": "+2", "x": 375.5, "y": 22.0, "width": 13.0, "size": 12.0, "face": "bold-italic"},
                  BADGE)]
    return amstrad.build(CASE, KEY, badge, amstrad.datacorder("#1d1f21", on_case=True, inside="#5c5b58",
                                                              plate="#d8d8d4"))
