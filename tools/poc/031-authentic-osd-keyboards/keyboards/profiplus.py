"""Profi's own keyboard as its album draws it (v3.2 "KEYBOARD-ZX", v5.0 "COMPUTER PROFI PLUS V5.0 KEYBOARD"): the 40
ZX keys in the Spectrum's four rows, the cursor keys as an inverted T left of the block on their own joystick lines,
"*" (the joystick's FIRE) right of ENTER, a RESET button and an LED. No photo of it is known, so the case and the caps
are neutral: the black case and the "Profi///" badge of the only real Profi keyboard (keyboards/profi.py), the
Pentagon 2+'s caps and print on the 40 keys, grey for the others.

Sources: refs-clones §6.1, §6.2: the album pages references/photos/clones/profi-v50-keyboard-schematic-album-p29.png
(v5.0) and ProfiV32il.pdf sheet 9 (v3.2): the keys, their order and wiring. UNVERIFIED: every position (the album
is a wiring drawing; the rows step like the Spectrum's, the cluster stands half a key clear of the block), the case,
the colours, the RESET and LED places; the port the joystick lines land on (the manuals: the Sinclair joystick on
keyboard lines), so the cursor keys and "*" carry the lines "JOY:<line>".
"""

from keyboards import pentagon2plus
from keyboards.pentagon2plus import U
from keyboards.profi import CASE, badge
from model import Key, Legend, Model, Part

KEYBOARD = {"id": "profiplus", "name": "Profi (album keyboard)", "models": ["PROFI", "PROFI3"], "size": [325, 138],
            "matrix": "zx"}

CREAM, GREY = pentagon2plus.CREAM, pentagon2plus.GREY
INK, WORD = pentagon2plus.INK, pentagon2plus.WORD
X0, Y0 = 19.6, 40.0      # the cursor cluster's left, row 0's top
NAMES = {"CS": ["CAPS", "SHIFT"], "SS": ["SYMB", "SHIFT"], "ENTER": ["ENTER"], "SPACE": ["SPACE"]}
JOYSTICK = {"UP": "up", "LEFT": "left", "DOWN": "down", "RIGHT": "right"}

# Each row: (name, x) in keys from X0, all one key wide
ROWS = [
    [(d, 3.25 + i) for i, d in enumerate("1234567890")],
    [(c, 3.75 + i) for i, c in enumerate("QWERTYUIOP")],
    [("UP", 1)] + [(c, 4.0 + i) for i, c in enumerate("ASDFGHJKL")] + [("ENTER", 13.0), ("*", 14.0)],
    [("LEFT", 0), ("DOWN", 1), ("RIGHT", 2), ("CS", 3.5)] + [(c, 4.5 + i) for i, c in enumerate("ZXCVBNM")] +
    [("SS", 11.5), ("SPACE", 12.5)],
]


def legends(name):
    if name in NAMES:
        lines = NAMES[name]
        ys = [0.55] if len(lines) == 1 else [0.42, 0.8]
        return [Legend(t, "main", "cap", 0.5, y, 3.0, WORD, INK, squeeze=pentagon2plus.NARROW)
                for t, y in zip(lines, ys)]
    if name in JOYSTICK:
        return [Legend("", "main", "cap", 0.5, 0.55, 4.4, color=INK, glyph=f"arrow:{JOYSTICK[name]}")]
    if name == "*":
        return [Legend("*", "main", "cap", 0.5, 0.75, 6.0, "sans-bold", INK),
                Legend("FIRE", "symbol", "cap", 0.5, 1.4, 3.0, WORD, INK)]
    return pentagon2plus.legends(name)


def mapping(name):
    if name in JOYSTICK:
        return {"line": f"JOY:{name}"}
    if name == "*":
        return {"line": "JOY:FIRE"}
    return {"zx": [name]}


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    x1, y1 = X0 + 15 * U, Y0 + 4 * U
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 3.0, "matte": True}, CASE),
        Part("panel", {"x": X0 - 5.0, "y": Y0 - 5.0, "w": x1 - X0 + 10.0, "h": y1 - Y0 + 10.0, "r": 1.0}, "#5e5e5a"),
        Part("panel", {"x": X0 - 4.4, "y": Y0 - 4.4, "w": x1 - X0 + 8.8, "h": y1 - Y0 + 8.8, "r": 0.8}, "#2a2b2b"),
        Part("led", {"x": W - 30.0, "y": 18.0, "r": 1.2}, "#3a0c0c"),
        Part("logo", {"text": "RESET", "x": W - 14.0, "y": 29.0, "size": 2.6, "face": WORD, "anchor": "middle",
                      "spacing": 0.3}, "#a9a7a0"),
    ]
    m.case += badge(8.1, 8.1)
    for r, row in enumerate(ROWS):
        for name, x in row:
            colour = CREAM if len(name) == 1 and name.isalnum() else GREY
            m.keys.append(Key(name.lower(), (X0 + x * U, Y0 + r * U, U, U), "soviet", colour, legends(name),
                              label=name.title() if len(name) > 1 else name, **mapping(name)))
    m.keys.append(Key("reset", (W - 20.0, 12.0, 12.0, 12.0), "soviet", "#b8332e",
                      [], line="RESET", label="Reset"))
    return m
