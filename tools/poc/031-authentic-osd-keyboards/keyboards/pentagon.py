"""Pentagon 128 (1989-): the board came bare, its builder fitted a keyboard; this is the early-1990s default, the
Soviet 40-key mechanical keyboard, low black caps on a square grid without stagger, each with a paper sticker (the
E-mode word on a grey band at the top, the letter in a yellow disc, the SS symbol in white, the keyword in red, the
E + SS word on a grey band at the bottom); the shifts and BREAK SPACE yellow, ENTER red. The plate sits in a black box
in the style of the Pentagon cases: the raised rear with two rows of vent slots, one opening for the key block, no
badge and no logo (the owner 2026-10-20: the bare plate looked unnatural).

Sources: references/clones.md §0, §1.2 (pentagon-habr446366-kbd.jpg: the grid measured on it against a 19 mm pitch;
the stickers read on it); the box after the owner's pentagon-case-top-owner.png (the rear's proportions and its two
grilles of long slots, the opening below them); the words from the 48K's set (keyboards/zx48k.py).
"""

from keyboards.zx48k import LETTERS
from model import Key, Legend, Model, Part
from svgkit import text_width

KEYBOARD = {"id": "pentagon", "name": "Pentagon 128", "models": ["PENTAGON"], "size": [250, 182], "matrix": "zx"}

U = 19.0                 # the grid's pitch, mm
CAP = 17.4               # the cap's side
X0, Y0 = 30.8, 92.0      # the first cap's top left: the block centred in the opening
CASE = "#1e1d21"
WELL = "#141416"
KEY = "#1b1b1c"
BAND = "#cacac6"
YELLOW, RED, WHITE, INK = "#f0c419", "#e2392c", "#f2f2f2", "#141414"
STICKER = 14.6           # the sticker's side
FACE = "sans-bold"
THIN = "sans"

ROWS = ["1234567890", "QWERTYUIOP", ["A", "S", "D", "F", "G", "H", "J", "K", "L", "ENTER"],
        ["CAPS SHIFT", "Z", "X", "C", "V", "B", "N", "M", "SYMBOL SHIFT", "BREAK SPACE"]]
MATRIX = {"CAPS SHIFT": "CS", "SYMBOL SHIFT": "SS", "BREAK SPACE": "SPACE"}

# Digits: the top band, the SS symbol (red), the block graphic, the bottom band
DIGITS = {"1": ("EDIT", "!", 1, "DEF FN"), "2": ("C. LOCK", "@", 2, "FN"), "3": ("TR.VIDEO", "#", 3, "LINE"),
          "4": ("INV.VIDEO", "$", 4, "OPEN"), "5": ("arrow:left", "%", 5, "CLOSE"), "6": ("arrow:down", "&", 6, "MOVE"),
          "7": ("arrow:up", "'", 7, "ERASE"), "8": ("arrow:right", "(", 0, "POINT"), "9": ("GRAPHICS", ")", None, "CAT"),
          "0": ("DELETE", "_", None, "FORMAT")}


def fit(text, size, face, width):
    """The squeeze that keeps @p text within @p width mm"""
    natural = text_width(text, size, face)
    return 1.0 if natural <= width else round(width / natural, 3)


def band(text, y):
    """A word on a grey band, black, thin, squeezed to the band"""
    size = 1.9
    return Legend(text, "extend" if y < 0.5 else "extend-shift", "cap", 0.5, y, size, THIN, INK,
                  squeeze=fit(text, size, THIN, STICKER * 0.78))


def sticker(name):
    result = [Legend("", "sticker", "cap", 0.5, 0.5, STICKER, color=BAND, glyph="sticker:zx"),
              Legend("", "sticker", "cap", 0.27, 0.53, 5.4, color=YELLOW, glyph="dot"),
              Legend("Ø" if name == "0" else name, "main", "cap", 0.27, 0.635, 3.7, FACE, INK)]
    if name in DIGITS:
        top, symbol, block, bottom = DIGITS[name]
        result.append(Legend("", "extend", "cap", 0.5, 0.155, 1.7, color=INK, glyph=top) if top.startswith("arrow:")
                      else band(top, 0.2))
        if block is not None:  # white is paper, as on the 48K's plate
            result.append(Legend("", "graphic", "cap", 0.72, 0.42, 2.6, color=WHITE, glyph=f"block:{block}"))
        result.append(Legend(symbol, "symbol", "cap", 0.6, 0.66, 2.6, FACE, RED))
        result.append(band(bottom, 0.9))
        return result
    e, keyword, ss, under = LETTERS[name]
    e, under = e.replace(" ", ""), under.replace(" ", "")
    words = any(c.isalpha() for c in ss)
    result += [band(e, 0.2),
               Legend(ss, "symbol", "cap", 0.71, 0.5, 1.8 if words else 2.8, FACE, WHITE,
                      squeeze=fit(ss, 1.8, FACE, STICKER * 0.38) if words else 1.0),
               Legend(keyword, "keyword", "cap", 0.71, 0.69, 1.7, FACE, RED,
                      squeeze=fit(keyword, 1.7, FACE, STICKER * 0.42)),
               band(under, 0.9)]
    return result


def plain(name):
    colour = RED if name == "ENTER" else YELLOW
    ink = "#6e1d16" if name == "ENTER" else "#2a2410"
    lines = name.split() if name != "ENTER" else [name]
    ys = [0.57] if len(lines) == 1 else [0.45, 0.7]
    return [Legend("", "sticker", "cap", 0.5, 0.5, STICKER, color=colour, glyph="sticker:plain")] + [
        Legend(t, "main", "cap", 0.5, y, 2.6, FACE, ink, squeeze=fit(t, 2.6, FACE, STICKER * 0.85))
        for t, y in zip(lines, ys)]


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    block_w, block_h = 9 * U + CAP, 3 * U + CAP
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 4.0, "matte": True}, CASE),
        Part("hump", {"x": 5.0, "y": 3.0, "w": W - 10.0, "h": 72.0, "r": 2.5}, CASE),  # the raised rear
        Part("grille", {"x": 14.0, "y": 9.0, "w": W - 28.0, "h": 19.0, "slots": 70}, CASE),
        Part("grille", {"x": 14.0, "y": 50.0, "w": W - 28.0, "h": 19.0, "slots": 70}, CASE),
        Part("well", {"x": X0 - 6.0, "y": Y0 - 6.0, "w": block_w + 12.0, "h": block_h + 12.0, "r": 1.0}, WELL),
    ]
    for r, row in enumerate(ROWS):
        for c, name in enumerate(row):
            rect = (X0 + c * U, Y0 + r * U, CAP, CAP)
            legends = plain(name) if len(name) > 1 else sticker(name)
            m.keys.append(Key(name.lower().replace(" ", "-"), rect, "mech", KEY, legends,
                              zx=[MATRIX.get(name, name)], label=name.title() if len(name) > 1 else name))
    return m
