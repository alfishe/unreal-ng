"""The homemade 40-key ZX keyboard of the Soviet clones (owner 2026-10-20): the 48K's staggered rows built from the
cream and grey caps of a Soviet terminal keyboard (the MS 7004's) in a black box, each cap printed with its letter,
the Russian ЙЦУКЕН letter under it, the SS symbol and the keyword; the E-mode words on the box over each key (green,
the digits' CAPS SHIFT functions white, the colours over them), the E + SS words under it (red), as on the 48K. The
only keyboard of the ATM-Turbo 512 v4.50 (no PC keyboard yet), the switchable second of the PC-keyboard clones.

Sources: the matrix, the words and the stagger from keyboards/zx48k.py; the caps from
references/photos/pc/ms7004-soviet-keyboard.jpg (cream letters, grey modifiers, Cyrillic over Latin, black print);
the ЙЦУКЕН from keyboards/pc.py. No brand. UNVERIFIED: homemade blocks differed from builder to builder; this is a
typical one, not a measured one.
"""

from keyboards.pc import CYRILLIC, U
from keyboards.zx48k import COLOURS, DIGITS, LETTERS
from model import Key, Legend, Model, Part
from svgkit import text_width

KEYBOARD = {"id": "zxmatrix", "name": "ZX 40-key (homemade)",
            "models": ["ATM450", "ATM710", "KAY", "SPRINTER", "ATM3", "TSL", "PHOENIX"], "size": [236, 132],
            "matrix": "zx", "depth": U,
            # the MS 7004's tall sculpted caps: a deep front, the sides a little darker
            "soviet": {"side": 0.13, "back": 0.06, "front": 0.2, "tones": ((0.92, 0.92, 0.91), (0.81, 0.81, 0.8),
                                                                           (0.88, 0.88, 0.87)),
                       "shadow": 0.3, "outline": 0.16, "rim": 0.3}}

CASE, PLATE = "#242426", "#1b1b1d"
CREAM, GREY = "#e8e2d0", "#8e908e"
INK, RED, KEY_RED = "#202022", "#e0362c", "#c8322a"
GREEN, WHITE = "#3dbe3d", "#ecece8"
X0, Y0 = 13.0, 20.0      # key 1's top left
PITCH = 1.45             # the row pitch in u: room for the words printed between the rows
FACE, WORD = "sans-bold", "din-condensed"
PRINT = 2.3              # the box's print, mm

# Rows: (name, x in u from key 1's left edge, width in u), the 48K's stagger with whole caps
ROWS = [
    [(d, i, 1) for i, d in enumerate("1234567890")],
    [(c, 0.5 + i, 1) for i, c in enumerate("QWERTYUIOP")],
    [(c, 0.75 + i, 1) for i, c in enumerate("ASDFGHJKL")] + [("ENTER", 9.75, 1.25)],
    [("CAPS SHIFT", 0, 1.25)] + [(c, 1.25 + i, 1) for i, c in enumerate("ZXCVBNM")] +
    [("SYMBOL SHIFT", 8.25, 1), ("SPACE", 9.25, 1.75)],
]
MATRIX = {"CAPS SHIFT": "CS", "SYMBOL SHIFT": "SS"}
GREYS = {"CAPS SHIFT", "SYMBOL SHIFT", "ENTER", "SPACE"}


def fit(text, size, face, width):
    natural = text_width(text, size, face)
    return 1.0 if natural <= width else round(width / natural, 3)


def above(text, line, colour, inverse=False):
    """A word on the box over the key, @p line mm above it, left aligned with the cap"""
    if text.startswith("arrow:"):
        return Legend("", "extend", "above", 0.12, line + 0.8, 2.6, color=colour, glyph=text)
    return Legend(text, "extend", "above", 0.04, line, PRINT, FACE, colour, "start", inverse=inverse,
                  squeeze=fit(text, PRINT, FACE, U * 0.95))


def below(text):
    return Legend(text, "extend-shift", "below", 0.04, 0.8, PRINT, FACE, RED, "start",
                  squeeze=fit(text, PRINT, FACE, U * 0.95))


def legends(name):
    if name in DIGITS:
        colour, function, block, symbol, under = DIGITS[name]
        result = [above(function, 1.2, WHITE),
                  Legend(name, "main", "cap", 0.12, 0.48, 4.6, FACE, INK, "start"),
                  Legend(symbol, "symbol", "cap", 0.88, 0.48, 3.4, FACE, KEY_RED, "end"),
                  below(under)]
        if colour:
            result.append(above(colour, 4.0, "#ffffff" if colour == "BLACK" else COLOURS[colour],
                                inverse=colour == "BLACK"))
        if block is not None:  # ink on the cream: the inked quadrants black, the box drawn
            result.append(Legend("", "graphic", "cap", 0.74, 0.76, 3.4, color=INK, glyph=f"block:{block}"))
        return result
    if name in LETTERS:
        e, keyword, ss, under = LETTERS[name]
        words = len(ss) > 2
        return [above(e, 1.2, GREEN),
                Legend(name, "main", "cap", 0.12, 0.48, 4.6, FACE, INK, "start"),
                Legend(CYRILLIC[name], "cyrillic", "cap", 0.12, 0.95, 4.0, "sans", INK, "start"),
                Legend(ss, "symbol", "cap", 0.9, 0.4, 2.2 if words else 3.2, FACE, KEY_RED, "end",
                       squeeze=fit(ss, 2.2, FACE, 7.5) if words else 1.0),
                Legend(keyword, "keyword", "cap", 0.9, 0.93, 2.3, WORD, INK, "end",
                       squeeze=fit(keyword, 2.3, WORD, 7.5)),
                below(under)]
    lines = {"CAPS SHIFT": ["CAPS", "SHIFT"], "SYMBOL SHIFT": ["SYMB", "SHIFT"], "ENTER": ["ENTER"],
             "SPACE": ["BREAK", "SPACE"]}[name]
    colour = KEY_RED if name == "SYMBOL SHIFT" else INK
    ys = [0.62] if len(lines) == 1 else [0.42, 0.85]
    return [Legend(t, "main", "cap", 0.5, y, 2.6, FACE, colour) for t, y in zip(lines, ys)]


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 3.0, "matte": True}, CASE),
               Part("plate", {"x": 5.0, "y": 5.0, "w": W - 10.0, "h": H - 10.0, "r": 1.5}, PLATE, {"sheen": 0.03})]
    for r, row in enumerate(ROWS):
        for name, x, w in row:
            rect = (X0 + x * U, Y0 + r * PITCH * U, w * U, U)
            m.keys.append(Key(name.lower().replace(" ", "-"), rect, "soviet", GREY if name in GREYS else CREAM,
                              legends(name), zx=[MATRIX.get(name, name)],
                              label=name.title() if len(name) > 1 else name))
    return m
