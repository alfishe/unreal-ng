"""ZX Spectrum Next (cased, KS1 / KS2: one drawing serves both).

Sources: references/next.md (layout measured on a rectified top-down photo, ±0.05u, against
the ZEsarUX bitmap; legends read from photos; matrix from membrane.vhd). 58 keys: the 40 of the matrix, 16 extra
keys that press a shift and a key (and set their NR 0xB0 / 0xB1 bit), and a second CAPS SHIFT and SYMBOL SHIFT.
"""

from model import Key, Legend, Model, Part

KEYBOARD = {"id": "zxnext", "name": "ZX Spectrum Next", "models": ["NEXT"], "size": [330, 146], "matrix": "zx",
            "twins": {"CS": 2, "SS": 2}, "highlights": 1.0}  # approved by the owner at full strength

U = 19.0                 # key pitch, mm
X0, Y0 = 0.25 * U, 2.25 * U + 0.065 * U  # the key grid's top left in the case
WELL = (X0 - 0.17 * U, 2.25 * U, 13.85 * U, 5.13 * U)
CASE = "#2e2f31"
CAP = "#3a3c40"
INK = "#f2f2f2"

# Rows as (id, x, w) in u; ENTER is drawn once, from its upper leg
ROWS = [
    [("BREAK", 0, 1), ("EDIT", 1, 1)] + [(d, 2 + i, 1) for i, d in enumerate("1234567890")] + [("DELETE", 12, 1.5)],
    [("TRUE VIDEO", 0, 1.25), ("INV VIDEO", 1.25, 1.25)] + [(c, 2.5 + i, 1) for i, c in enumerate("QWERTYUIOP")] +
    [("ENTER", 12.5, 1)],
    [("CAPS LOCK", 0, 1.5), ("GRAPH", 1.5, 1.25)] + [(c, 2.75 + i, 1) for i, c in enumerate("ASDFGHJKL")],
    [("CAPS SHIFT", 0, 2.25), ("EXTEND MODE", 2.25, 1.25)] + [(c, 3.5 + i, 1) for i, c in enumerate("ZXCVBNM")] +
    [("UP", 10.5, 1), ("CAPS SHIFT 2", 11.5, 2)],
    [("SYMBOL SHIFT", 0, 1), (";", 1, 1), ('"', 2, 1), (",", 3, 1), (".", 4, 1), ("SPACE", 5, 4.5), ("LEFT", 9.5, 1),
     ("DOWN", 10.5, 1), ("RIGHT", 11.5, 1), ("SYMBOL SHIFT 2", 12.5, 1)],
]

# The extra keys: what they press on the 40-key matrix (membrane.vhd:186-244)
EXTRA = {"BREAK": ["CS", "SPACE"], "EDIT": ["CS", "1"], "DELETE": ["CS", "0"], "TRUE VIDEO": ["CS", "3"],
         "INV VIDEO": ["CS", "4"], "CAPS LOCK": ["CS", "2"], "GRAPH": ["CS", "9"], "EXTEND MODE": ["CS", "SS"],
         "UP": ["CS", "7"], "DOWN": ["CS", "6"], "LEFT": ["CS", "5"], "RIGHT": ["CS", "8"], ";": ["SS", "O"],
         '"': ["SS", "P"], ",": ["SS", "N"], ".": ["SS", "M"]}
SINGLE = {"CAPS SHIFT": "CS", "CAPS SHIFT 2": "CS", "SYMBOL SHIFT": "SS", "SYMBOL SHIFT 2": "SS",
          "SPACE": "SPACE", "ENTER": "ENTER"}

# Digits: the strip (colour, E-mode keyword), the graphic block, the SS symbol
DIGITS = {"1": ("BLUE", "DEF FN", 1, "!"), "2": ("RED", "FN", 2, "@"), "3": ("MAGENTA", "LINE", 3, "#"),
          "4": ("GREEN", "OPEN #", 4, "$"), "5": ("CYAN", "CLOSE #", 5, "%"), "6": ("YELLOW", "MOVE", 6, "&"),
          "7": ("WHITE", "ERASE", 7, "'"), "8": ("", "POINT", 0, "("), "9": ("", "CAT", None, ")"),
          "0": ("BLACK", "FORMAT", None, "_")}

# Letters: the strip (E mode, SS + E mode; a single char second is a symbol set at the left), the keyword, the SS
# item (a symbol at the right, or a keyword as a second line)
LETTERS = {
    "Q": ("SIN", "ASN", "PLOT", "<="), "W": ("COS", "ACS", "DRAW", "<>"), "E": ("TAN", "ATN", "REM", ">="),
    "R": ("INT", "VERIFY", "RUN", "<"), "T": ("RND", "MERGE", "RAND", ">"), "Y": ("STR$", "[", "RETURN", "AND"),
    "U": ("CHR$", "]", "IF", "OR"), "I": ("CODE", "IN", "INPUT", "AT"), "O": ("PEEK", "OUT", "POKE", ";"),
    "P": ("TAB", "©", "PRINT", '"'), "A": ("READ", "~", "NEW", "STOP"), "S": ("RESTR", "|", "SAVE", "NOT"),
    "D": ("DATA", "\\", "DIM", "STEP"), "F": ("SGN", "{", "FOR", "TO"), "G": ("ABS", "}", "GOTO", "THEN"),
    "H": ("SQR", "CIRCLE", "GOSUB", "↑"), "J": ("VAL", "VAL$", "LOAD", "-"), "K": ("LEN", "SCRN$", "LIST", "+"),
    "L": ("USR", "ATTR", "LET", "="), "Z": ("LN", "BEEP", "COPY", ":"), "X": ("EXP", "INK", "CLEAR", "£"),
    "C": ("LPRINT", "PAPER", "CONT", "?"), "V": ("LLIST", "FLASH", "CLS", "/"), "B": ("BIN", "BRIGHT", "BORDER", "*"),
    "N": ("INKEY$", "OVER", "NEXT", ","), "M": ("PI", "INVERSE", "PAUSE", "."),
}

NAMED = {"BREAK": ["BREAK"], "EDIT": ["EDIT"], "DELETE": ["DELETE"], "TRUE VIDEO": ["TRUE", "VIDEO"],
         "INV VIDEO": ["INV", "VIDEO"], "CAPS LOCK": ["CAPS", "LOCK"], "GRAPH": ["GRAPH"],
         "EXTEND MODE": ["EXTEND", "MODE"], "CAPS SHIFT": ["CAPS SHIFT"], "CAPS SHIFT 2": ["CAPS SHIFT"],
         "SYMBOL SHIFT": ["SYMBOL", "SHIFT"], "SYMBOL SHIFT 2": ["SYMBOL", "SHIFT"], "ENTER": ["ENTER"]}
ARROWS = {"UP": "up", "DOWN": "down", "LEFT": "left", "RIGHT": "right"}

FACE = "rounded"
STRIP = 1.45  # the strip's text, mm


def strip(lines):
    """The legends on the glossy strip above the matte top: one or two centred lines"""
    if len(lines) == 1:
        return [Legend(lines[0], "extend", "cell", 0.5, 0.165, STRIP, FACE, INK)]
    first, second = lines
    if len(second) == 1:  # a symbol: under the keyword, to the left (the [ STR$ of Y)
        return [Legend(first, "extend", "cell", 0.55, 0.105, STRIP, FACE, INK),
                Legend(second, "extend-shift", "cell", 0.3, 0.2, STRIP * 1.1, FACE, INK)]
    return [Legend(first, "extend", "cell", 0.5, 0.105, STRIP, FACE, INK),
            Legend(second, "extend-shift", "cell", 0.5, 0.195, STRIP, FACE, INK)]


def legends(name):
    if name in DIGITS:
        colour, keyword, block, symbol = DIGITS[name]
        result = []
        if colour == "BLACK":
            result += [Legend(colour, "colour", "cell", 0.5, 0.105, STRIP, FACE, INK, inverse=True),
                       Legend(keyword, "extend-shift", "cell", 0.5, 0.195, STRIP, FACE, INK)]
        else:
            result += strip([colour, keyword] if colour else [keyword])
        if block is not None:
            result.append(Legend("", "graphic", "cap", 0.3, 0.4, 3.0, color=INK, glyph=f"block:{block}"))
        result.append(Legend(symbol, "symbol", "cap", 0.72 if block is not None else 0.5, 0.52, 3.6, FACE, INK))
        result.append(Legend("Ø" if name == "0" else name, "main", "cap", 0.5, 0.9, 4.2, FACE, INK))
        return result
    if name in LETTERS:
        e1, e2, keyword, ss = LETTERS[name]
        result = strip([e1, e2])
        if not any(c.isalpha() for c in ss):
            result += [Legend(keyword, "keyword", "cap", 0.5, 0.27, 2.0, FACE, INK),
                       Legend(ss, "symbol", "cap", 0.8, 0.6, 3.0, FACE, INK)]
        else:
            result += [Legend(keyword, "keyword", "cap", 0.5, 0.25, 2.0, FACE, INK),
                       Legend(ss, "symbol-keyword", "cap", 0.5, 0.45, 2.0, FACE, INK)]
        result.append(Legend(name, "main", "cap", 0.5, 0.9, 4.2, FACE, INK))
        return result
    if name in NAMED:
        lines = NAMED[name]
        ys = [0.55] if len(lines) == 1 else [0.42, 0.68]
        return [Legend(line, "main", "cap", 0.5, y, 1.9, FACE, INK) for line, y in zip(lines, ys)]
    if name in ARROWS:
        return [Legend("", "main", "cap", 0.5, 0.5, 4.2, color=INK, glyph=f"arrow:{ARROWS[name]}")]
    if name in ";\",.":
        return [Legend(name, "main", "cap", 0.5, 0.62, 4.6, FACE, INK)]
    return []  # SPACE: blank


def enter_shape():
    """The inverted L: the narrow leg on R2 (x 12.5..13.5), the bar on R3 (x 11.75..13.5), mm"""
    x1, x2, x3 = X0 + 11.75 * U, X0 + 12.5 * U, X0 + 13.5 * U
    y1, y2, y3 = Y0 + 1 * U, Y0 + 2 * U, Y0 + 3 * U
    return [(x2, y1), (x3, y1), (x3, y3), (x1, y3), (x1, y2), (x2, y2)]


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 4, "matte": True, "rolloff": 0.95 * U}, CASE),
        Part("well", {"x": WELL[0], "y": WELL[1], "w": WELL[2], "h": WELL[3], "r": 1.2}, "#18191c"),
    ]
    # The right section: the deck's grooves continue the row lines to the case end, a coloured insert in each
    right = WELL[0] + WELL[2]
    for i, colour in enumerate(["#c81e3c", "#ebd52e", "#4cc04c", "#0ba8e8"]):
        y = Y0 + (i + 1) * U - 0.03 * U
        m.case.append(Part("groove", {"x": right, "y": y, "w": W - right - 0.4, "h": 0.9}, CASE))
        m.case.append(Part("insert", {"x": X0 + 16.45 * U, "y": y - 0.35, "w": W - (X0 + 16.45 * U) - 0.3, "h": 1.6},
                           colour))
    m.case += [
        Part("grille", {"x": X0 + 9.05 * U, "y": 1.6, "w": 4.35 * U, "h": 0.27 * U, "slots": 28}, CASE),
        Part("tab", {"x": X0 + 8.2 * U, "y": 1.4, "w": 0.8 * U, "h": 0.3 * U, "r": 1.0}, "#141517"),
        Part("led", {"x": X0 + 13.65 * U, "y": 1.6 + 0.135 * U, "r": 0.9}, "#1a0c0c"),
        Part("logo", {"text": "sinclair", "x": X0 + 0.35 * U, "y": 2.25 * U - 1.3 * U, "size": 11,
                      "squeeze": 1.5, "spacing": 0.25, "face": "rounded", "gloss": True}, "#141517"),
        Part("logo", {"text": "ZX Spectrum Next", "x": X0 + 0.4 * U, "y": 2.25 * U - 0.95 * U, "width": 2.45 * U,
                      "face": "bold-italic"}, "#d9502a"),
    ]
    for r, row in enumerate(ROWS):
        for name, x, w in row:
            rect = (X0 + x * U, Y0 + r * U, w * U, U)
            shape = None
            if name == "ENTER":
                rect = (X0 + 11.75 * U, Y0 + U, 1.75 * U, 2 * U)
                shape = enter_shape()
            ident = name.lower().replace(" ", "-")
            zx = EXTRA.get(name) or [SINGLE.get(name, name)]
            m.keys.append(Key(ident, rect, "next", CAP, legends(name), zx=zx, shape=shape,
                              label=name.title() if len(name) > 1 else name))
    return m
