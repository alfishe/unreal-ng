"""Profi v3 in the БК-0010 case: the black case of the БК-0010, navy in the light, with the Soviet 58-key keyboard (the
Spectrum+ layout) set in its recess on a finely textured surround, a plain panel left of the keys, the raised rear with
the "Profi///" badge (purple rounded "Profi" and three slanted stripes, red, yellow and green, on a white plate) and the
broad palm rest in front. White sculpted caps, light grey modifiers, a red BREAK; each white cap carries the Spectrum
print and its Cyrillic letter: the keyword on top, the italic Latin letter with the Cyrillic one right of it, the SS
item under it, the E-mode and the E + SS words on the front.

Sources: refs-clones §6.1; the owner's photos (2026-10-20): references/photos/clones/profi-bk0010-case-top-owner.png,
a straight top view of a real Profi keyboard (the case and the layout measured against the key block's 13.5 u, the
badge's print traced, trace.py) and profi-bk0010-case-thumbnail-owner.png, the same keyboard at 3/4; references/photos/clones/profi-v3-in-bk0010-case-speccyinfo-screenshot.jpg. The
legends read key by key from the top view; the caps' yellowing is age (no patina, owner). UNVERIFIED: the case's
height and the palm rest's slope (a top view), the caps' sculpt (the Pentagon 2+'s), the Cyrillic of the two keys
right of M and right of ↓ (Б, Ю); the badge's inks are the owner's chosen shades, not the photo's (its red darker,
its yellow lime, its green teal).
"""

import json
from pathlib import Path

from keyboards.pentagon2plus import DIGITS, LETTERS, U, enter_shape
from keyboards.zx128 import EXTRA, ROWS, SINGLE
from model import Key, Legend, Model, Part

KEYBOARD = {"id": "profi", "name": "Profi (БК-0010 case)", "models": ["PROFI", "PROFI3"], "size": [379, 202],
            "matrix": "zx", "twins": {"CS": 2, "SS": 2}}

CASE = "#20242c"
WHITE, GREY, RED = "#eae7de", "#a6a7a5", "#b9392f"
INK = "#1f1f22"
X0, Y0 = 69.8, 58.1      # the key block's top left
LATIN, CYR, WORD, NAME = "oblique", "sans", "din-condensed", "sans-bold"

CYRILLIC = dict(zip("QWERTYUIOPASDFGHJKLZXCVBNM", "ЦУКЕНГШЩЗХФЫВАПРОЛДЯЧСМИТЬ"))
# Where the Profi's print differs from the Pentagon 2+'s words: the keyword, the E-mode word, the E + SS item
WORDS = {"S": {3: "RESTORE"}, "G": {2: "GO TO"}, "H": {2: "GO SUB"}, "K": {0: "SCREEN$"}}
# The punctuation keys: the symbol, the Cyrillic letter
PUNCT = {";": (";", "Ж"), '"': (",,", "Э"), ".": (".", "Б"), ",": (",", "Ю")}
NAMED = {"TRUE VIDEO": ["TRUE", "VIDEO"], "INV VIDEO": ["INV", "VIDEO"], "BREAK": ["BREAK"], "DELETE": ["DELETE"],
         "GRAPH": ["GRAPH"], "EXTEND MODE": ["EXTEND", "MODE"], "EDIT": ["EDIT"], "CAPS SHIFT": ["CAPS    SHIFT"],
         "CAPS SHIFT 2": ["CAPS    SHIFT"], "CAPS LOCK": ["CAPS", "LOCK"], "SYMBOL SHIFT": ["SYMBOL", "SHIFT"],
         "SYMBOL SHIFT 2": ["SYMBOL", "SHIFT"]}
ARROWS = {"UP": "up", "DOWN": "down", "LEFT": "left", "RIGHT": "right"}
WHITE_KEYS = set(LETTERS) | set(DIGITS) | set(ARROWS) | set(PUNCT)


def small(text, role, x, y, size=2.0, face=WORD):
    return Legend(text, role, "cap", x, y, size, face, INK, squeeze=1.1)


def letter(name):
    item = list(LETTERS[name])
    for i, word in WORDS.get(name, {}).items():
        item[i] = word
    left, right, keyword, extend = item
    result = [small(keyword, "keyword", 0.5, 0.2, face="oblique"),
              Legend(name, "main", "cap", 0.3, 0.54, 4.4, LATIN, INK),
              Legend(CYRILLIC[name], "cyrillic", "cap", 0.68, 0.62, 4.2, CYR, INK),
              small(extend, "extend", 0.5, 1.02), small(left, "symbol", 0.5, 1.3)]
    if right:
        word = any(c.isalpha() for c in right) and len(right) > 1
        result.append(small(right, "symbol", 0.32, 0.82, 2.0 if word else 2.4, WORD if word else "sans"))
    return result


def legends(name):
    if name in LETTERS:
        return letter(name)
    if name in DIGITS:
        left, right, block, colour = DIGITS[name]
        result = [Legend(name, "main", "cap", 0.42, 0.74, 5.2, CYR, INK),
                  small(right, "symbol", 0.66, 0.3, 3.0, "sans")]
        if block is not None:
            result.append(Legend("", "graphic", "cap", 0.3, 0.2, 2.6, color=INK, glyph=f"block:{block}"))
        # the colour over the E-mode word on the front
        result += [small(t, "extend", 0.5, y) for t, y in zip([t for t in (colour, left) if t], (1.02, 1.3))]
        return result
    if name in NAMED:
        lines = NAMED[name]
        ys = [0.42] if len(lines) == 1 else [0.32, 0.6]
        colour = "#6e2019" if name == "BREAK" else INK
        return [Legend(t, "main", "cap", 0.5, y, 2.4, NAME, colour) for t, y in zip(lines, ys)]
    if name in ARROWS:
        return [Legend("", "main", "cap", 0.5, 0.5, 5.0, color=INK, glyph=f"arrow:{ARROWS[name]}")]
    if name == "ENTER":  # on the upright
        return [Legend("", "main", "cap", 0.75, -0.4, 9.0, color="#3b3d40", glyph="enter")]
    if name in PUNCT:
        symbol, cyrillic = PUNCT[name]
        return [Legend(cyrillic, "cyrillic", "cap", 0.62, 0.42, 4.2, CYR, INK),
                Legend(symbol, "symbol", "cap", 0.32, 0.74, 3.4, NAME, INK)]
    return []  # SPACE: blank


def keys(m, x0=X0, y0=Y0):
    """The 58 caps with their legends, the key block's top left at @p x0, @p y0"""
    for r, row in enumerate(ROWS):
        for name, x, w in row:
            rect = (x0 + x * U, y0 + r * U, w * U, U)
            shape = None
            if name == "ENTER":
                rect, shape = (x0 + 11.75 * U, y0 + U, 1.75 * U, 2 * U), enter_shape(x0, y0)
            colour = RED if name == "BREAK" else WHITE if name in WHITE_KEYS else GREY
            zx = EXTRA.get(name) or [SINGLE.get(name, name)]
            m.keys.append(Key(name.lower().replace(" ", "-"), rect, "soviet", colour, legends(name), zx=zx,
                              shape=shape, label=name.title() if len(name) > 1 else name))


def badge(x, y):
    """The "Profi///" badge, its top left at @p x, @p y: the white plate, 52.6 x 16.6 mm, and its print traced from
    the owner's top view (references/traces/profi-badge.json, trace.py)"""
    traced = json.loads((Path(__file__).parent.parent / "references/traces/profi-badge.json").read_text())
    w, h = traced["size"]
    inks = {"word": "#9d58c6", "red": "#d4412f", "yellow": "#d6b531", "green": "#4f9b42"}
    return [Part("panel", {"x": x, "y": y, "w": w, "h": h, "r": 0.4}, "#ebe9eb")] + [
        Part("print", {"x": x, "y": y, "d": traced["inks"][name]["d"]}, colour) for name, colour in inks.items()]


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 2.5, "matte": True}, CASE),
        Part("steps", {"x": 1.5, "y": 36.0, "w": W - 3.0, "h": 4.0, "steps": 1}, CASE),  # the rear panel's edge
        Part("steps", {"x": 1.5, "y": 153.0, "w": W - 3.0, "h": 3.5, "steps": 1}, CASE),  # the palm rest's edge
        Part("panel", {"x": 6.4, "y": 40.0, "w": 365.0, "h": 113.0, "r": 0.4}, "#1b1e25"),  # the recess
        Part("panel", {"x": 8.6, "y": 41.6, "w": 59.0, "h": 110.0, "r": 0.4}, "#22262e"),  # the plain left panel
        Part("mesh", {"x": 69.4, "y": 41.6, "w": 300.0, "h": 110.0, "r": 0.4, "pitch": 0.9}, "#30343b"),
    ]
    m.case += badge(23.6, 11.9)
    keys(m)
    return m
