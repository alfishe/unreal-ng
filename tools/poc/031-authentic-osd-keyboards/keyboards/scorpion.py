"""ZS Scorpion ZS-256 (and its PROF ROM and GMX variants): the Soviet 58-key keyboard in its own dark grey case, the
keyboard the Scorpion was sold with (refs-clones §2, "стандартной 58-кнопочной клавиатуры"). The caps and legends
are the Pentagon 2+'s (the same keyboard); the case has the raised "ZX SPECTRUM / SINCLAIR" plaque at the rear left
with ribs running from it, a light grey plate in the recess and a square blank right of BREAK.

Sources: refs-clones §1.4 (the case, the colours), the positions measured on references/photos/clones/pentagon-zxbyte-04.jpg
against its key pitch (row 0, keys 1 to 0: 102 px a pitch, the margins taken at the same height to cancel the
keystone); the caps from keyboards/pentagon2plus.py.
"""

from keyboards import pentagon2plus
from keyboards.pentagon2plus import U
from model import Model, Part

KEYBOARD = {"id": "scorpion", "name": "ZS Scorpion ZS-256", "models": ["SCORPION", "PROFSCORP", "GMX"],
            "size": [327, 155], "matrix": "zx", "twins": {"CS": 2, "SS": 2}}

CASE = "#3d3f3c"
PLATE = "#9fa19d"
GREY = "#908f88"
X0, Y0 = 33.0, 37.0      # the key block's top left
WELL = (X0 - 2.0, Y0 - 2.0, 13.5 * U + 4.0, 5 * U + 4.0)


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    x, y, w, h = WELL
    m.case.append(Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 3.0, "matte": True}, CASE))
    m.case += pentagon2plus.plaque(3.7, 2.4, CASE)  # the ribs beside it, the long one under both
    m.case += [Part("boss", {"x": 63.5, "y": ry, "w": W - 67.5, "h": 1.2, "r": 0.4}, CASE) for ry in (3.7, 8.0, 12.2)]
    m.case += [
        Part("boss", {"x": 3.7, "y": 19.4, "w": W - 7.7, "h": 1.2, "r": 0.4}, CASE),
        Part("groove", {"x": 3.0, "y": H - 9.0, "w": W - 6.0, "h": 0.8}, CASE),  # the front lip
        Part("well", {"x": x, "y": y, "w": w, "h": h, "r": 0.8}, PLATE),
        Part("panel", {"x": x + w + 2.0, "y": y - 3.0, "w": 17.5, "h": 17.5, "r": 0.6}, "#343632"),  # the blank
    ]
    pentagon2plus.keys(m, X0, Y0, GREY)
    return m
