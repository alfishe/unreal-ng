"""Amstrad ZX Spectrum +3 (1987): the +2A's black case and keyboard, the 3" disc drive on the right: on the top its
label panel (the outlined 128K, the rainbow lines, FDD, FLOPPY DISC DRIVE), the drive's slot in the front face.

Sources: references/sinclair.md §6 (plus3-top.jpg, plus3-k1-floppy.jpg: the panel measured on plus3-top.jpg against
the key block, 104 x 82 mm); the layout and the legends from keyboards/amstrad.py.
"""

from keyboards import amstrad
from model import Part

KEYBOARD = {"id": "zxplus3", "name": "ZX Spectrum +3", "models": ["PLUS3"], "size": amstrad.SIZE, "matrix": "zx",
            "twins": {"CS": 2, "SS": 2}}

CASE = "#2a2c2e"
KEY = "#232527"
BADGE = "#ececea"


def layout():
    badge = [Part("logo", {"text": "ZX Spectrum", "x": 300.6, "y": 22.0, "width": 74.0, "size": 9.0,
                           "face": "bold-italic"}, BADGE),
             Part("logo", {"text": "+3", "x": 375.5, "y": 22.0, "width": 13.0, "size": 12.0, "face": "bold-italic"},
                  BADGE)]
    return amstrad.build(CASE, KEY, badge, amstrad.disc_panel("#1c1d1f"))
