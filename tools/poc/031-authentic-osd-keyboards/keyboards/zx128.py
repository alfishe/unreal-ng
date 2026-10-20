"""ZX Spectrum 128 (UK, 1986, the "toastrack"): the Spectrum+ case and its 58-key keyboard, the red logo, the "128K"
badge and the heatsink on the right end.

Sources: references/sinclair.md §0, §2-3 (the layout measured on the flat scan 128k-k1-keyboard.jpg against the
ZEsarUX bitmaps, the legends read from that scan; the case and the badge from 128k-top.jpg). The Spectrum+ itself is
not a model of the emulator; its keyboard is this one with the moulded black logo and no heatsink.
"""

from model import Key, Legend, Model, Part

KEYBOARD = {"id": "zx128", "name": "ZX Spectrum 128", "models": ["128k"], "size": [343, 149], "matrix": "zx",
            "twins": {"CS": 2, "SS": 2}}

U = 19.0                 # key pitch, mm
X0, Y0 = 10.0, 40.0      # the key block's top left
BODY_W = 319.0
CASE = "#262829"
KEY = "#2a2b2e"
INK = "#f0f0f0"
RED = "#c8323a"
FACE = "arial-bold"

ROWS = [
    [("TRUE VIDEO", 0, 1), ("INV VIDEO", 1, 1)] + [(d, 2 + i, 1) for i, d in enumerate("1234567890")] +
    [("BREAK", 12, 1.5)],
    [("DELETE", 0, 1.5), ("GRAPH", 1.5, 1)] + [(c, 2.5 + i, 1) for i, c in enumerate("QWERTYUIOP")] +
    [("ENTER", 12.5, 1)],
    [("EXTEND MODE", 0, 1.75), ("EDIT", 1.75, 1)] + [(c, 2.75 + i, 1) for i, c in enumerate("ASDFGHJKL")],
    [("CAPS SHIFT", 0, 2.25), ("CAPS LOCK", 2.25, 1)] + [(c, 3.25 + i, 1) for i, c in enumerate("ZXCVBNM")] +
    [(".", 10.25, 1), ("CAPS SHIFT 2", 11.25, 2.25)],
    [("SYMBOL SHIFT", 0, 1), (";", 1, 1), ('"', 2, 1), ("LEFT", 3, 1), ("RIGHT", 4, 1), ("SPACE", 5, 4.5),
     ("UP", 9.5, 1), ("DOWN", 10.5, 1), (",", 11.5, 1), ("SYMBOL SHIFT 2", 12.5, 1)],
]

# The extra keys close two matrix positions (refs §0, the TBBlue KeyboardTester table)
EXTRA = {"BREAK": ["CS", "SPACE"], "EDIT": ["CS", "1"], "DELETE": ["CS", "0"], "TRUE VIDEO": ["CS", "3"],
         "INV VIDEO": ["CS", "4"], "CAPS LOCK": ["CS", "2"], "GRAPH": ["CS", "9"], "EXTEND MODE": ["CS", "SS"],
         "UP": ["CS", "7"], "DOWN": ["CS", "6"], "LEFT": ["CS", "5"], "RIGHT": ["CS", "8"], ";": ["SS", "O"],
         '"': ["SS", "P"], ",": ["SS", "N"], ".": ["SS", "M"]}
SINGLE = {"CAPS SHIFT": "CS", "CAPS SHIFT 2": "CS", "SYMBOL SHIFT": "SS", "SYMBOL SHIFT 2": "SS",
          "SPACE": "SPACE", "ENTER": "ENTER"}

# Digits: the ledge (colour, E + SS keyword), the graphic block, the SS symbol
DIGITS = {"1": ("BLUE", "DEF FN", 1, "!"), "2": ("RED", "FN", 2, "@"), "3": ("MGNTA", "LINE", 3, "#"),
          "4": ("GREEN", "OPEN #", 4, "$"), "5": ("CYAN", "CLOSE #", 5, "%"), "6": ("YELLOW", "MOVE", 6, "&"),
          "7": ("WHITE", "ERASE", 7, "'"), "8": ("POINT", "", 0, "("), "9": ("CAT", "", None, ")"),
          "0": ("BLACK", "FORMAT", None, "_")}

# Letters: the ledge (E mode, E + SS), the keyword, the SS item ("" where the 58-key layout has its own key)
LETTERS = {
    "Q": ("SIN", "ASN", "PLOT", "<="), "W": ("COS", "ACS", "DRAW", "<>"), "E": ("TAN", "ATN", "REM", ">="),
    "R": ("INT", "VERIFY", "RUN", "<"), "T": ("RND", "MERGE", "RAND", ">"), "Y": ("STR$", "[", "RETURN", "AND"),
    "U": ("CHR$", "]", "IF", "OR"), "I": ("CODE", "IN", "INPUT", "AT"), "O": ("PEEK", "OUT", "POKE", ""),
    "P": ("TAB", "(c)", "PRINT", ""), "A": ("READ", "~", "NEW", "STOP"), "S": ("RESTR", "|", "SAVE", "NOT"),
    "D": ("DATA", "\\", "DIM", "STEP"), "F": ("SGN", "{", "FOR", "TO"), "G": ("ABS", "}", "GOTO", "THEN"),
    "H": ("SQR", "CIRCLE", "GOSUB", "↑"), "J": ("VAL", "VAL$", "LOAD", "-"), "K": ("LEN", "SCRN$", "LIST", "+"),
    "L": ("USR", "ATTR", "LET", "="), "Z": ("LN", "BEEP", "COPY", ":"), "X": ("EXP", "INK", "CLEAR", "£"),
    "C": ("LPRINT", "PAPER", "CONT", "?"), "V": ("LLIST", "FLASH", "CLS", "/"), "B": ("BIN", "BRIGHT", "BORDER", "*"),
    "N": ("INKEY$", "OVER", "NEXT", ""), "M": ("PI", "INVERS", "PAUSE", ""),
}

NAMED = {"TRUE VIDEO": ["TRUE", "VIDEO"], "INV VIDEO": ["INV", "VIDEO"], "BREAK": ["BREAK"], "DELETE": ["DELETE"],
         "GRAPH": ["GRAPH"], "EXTEND MODE": ["EXTEND", "MODE"], "EDIT": ["EDIT"], "CAPS SHIFT": ["CAPS SHIFT"],
         "CAPS SHIFT 2": ["CAPS SHIFT"], "CAPS LOCK": ["CAPS", "LOCK"], "SYMBOL SHIFT": ["SYMBOL", "SHIFT"],
         "SYMBOL SHIFT 2": ["SYMBOL", "SHIFT"], "ENTER": ["ENTER"]}
ARROWS = {"UP": "up", "DOWN": "down", "LEFT": "left", "RIGHT": "right"}

LEDGE = 1.95  # the ledge's text, mm


def ledge(first, second):
    lines = [t for t in (first, second) if t]
    ys = [0.19] if len(lines) == 1 else [0.135, 0.255]
    return [Legend(t, "extend" if i == 0 else "extend-shift", "cell", 0.5, y, LEDGE, FACE, INK, squeeze=0.9)
            for i, (t, y) in enumerate(zip(lines, ys))]


def legends(name):
    if name in DIGITS:
        first, second, block, symbol = DIGITS[name]
        result = ledge(first, second)
        if block is not None:
            result.append(Legend("", "graphic", "cap", 0.3, 0.32, 2.9, color=INK, glyph=f"block:{block}"))
        result += [Legend(symbol, "symbol", "cap", 0.7 if block is not None else 0.6, 0.45, 3.2, FACE, INK),
                   Legend("Ø" if name == "0" else name, "main", "cap", 0.5, 0.9, 4.4, FACE, INK)]
        return result
    if name in LETTERS:
        first, second, keyword, ss = LETTERS[name]
        result = ledge(first, second) + [Legend(keyword, "keyword", "cap", 0.5, 0.27, 2.3, FACE, INK, squeeze=0.92)]
        if ss:
            symbol = not any(c.isalpha() for c in ss)
            result.append(Legend(ss, "symbol" if symbol else "symbol-keyword", "cap", 0.6 if symbol else 0.5, 0.52,
                                 2.8 if symbol else 2.3, FACE, INK, squeeze=0.92))
        result.append(Legend(name, "main", "cap", 0.5, 0.91, 4.4, FACE, INK))
        return result
    if name in NAMED:
        lines = NAMED[name]
        ys = [0.5] if len(lines) == 1 else [0.38, 0.66]
        return [Legend(t, "main", "cap", 0.5, y, 2.2, FACE, INK, squeeze=0.92) for t, y in zip(lines, ys)]
    if name in ARROWS:
        return [Legend("", "main", "cap", 0.5, 0.48, 4.6, color=INK, glyph=f"arrow:{ARROWS[name]}")]
    if name in ';",.':
        return [Legend(name, "main", "cap", 0.5, 0.6, 4.6, FACE, INK)]
    return []  # SPACE: blank


def enter_shape():
    x1, x2, x3 = X0 + 11.75 * U, X0 + 12.5 * U, X0 + 13.5 * U
    y1, y2, y3 = Y0 + 1 * U, Y0 + 2 * U, Y0 + 3 * U
    return [(x2, y1), (x3, y1), (x3, y3), (x1, y3), (x1, y2), (x2, y2)]


def badge():
    """The "128K" in red outline and the four-stripe rainbow beside it, bottom right"""
    parts = [Part("logo", {"text": "128K", "x": 270.0, "y": 136.5, "size": 10.5, "face": "sans", "stroke": 0.35,
                           "squeeze": 0.95}, RED)]
    for i, colour in enumerate(["#e8392c", "#f5c518", "#5daa3c", "#2fa8e0"]):
        x = 296.0 + 4.6 * i
        parts.append(Part("stripe", {"points": [(x, 137.0), (x + 4.6, 137.0), (x + 4.6 + 6.5, 125.0),
                                                (x + 6.5, 125.0)]}, colour))
    return parts


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    right = X0 + 13.5 * U
    m.case += [
        Part("fins", {"x": BODY_W - 3, "y": 2.0, "w": W - BODY_W + 3, "h": H - 4.0, "fins": 40, "r": 2}, "#1c1c1e"),
        Part("body", {"x": 0, "y": 0, "w": BODY_W, "h": H, "r": 3, "matte": True}, CASE),
        Part("grille", {"x": 150.0, "y": 1.0, "w": 150.0, "h": 3.6, "slots": 52}, CASE),
    ]
    for k in range(1, 5):  # the grooves continue the row lines across both side margins
        y = Y0 + k * U - 0.45
        m.case.append(Part("groove", {"x": 0.6, "y": y, "w": X0 - 1.2, "h": 0.9}, CASE))
        m.case.append(Part("groove", {"x": right + 0.6, "y": y, "w": BODY_W - right - 1.2, "h": 0.9}, CASE))
    m.case += [
        Part("well", {"x": X0 - 0.3, "y": Y0 - 0.3, "w": 13.5 * U + 0.6, "h": 5 * U + 0.6, "r": 0.8}, "#0c0c0d"),
        Part("logo", {"text": "sinclair", "x": 13.0, "y": 22.0, "size": 13.0, "squeeze": 1.9, "spacing": 0.25,
                      "face": "rounded"}, RED),
        Part("logo", {"text": "ZX Spectrum +", "x": 13.5, "y": 30.5, "size": 4.4, "face": "sans"}, RED),
    ]
    m.case += badge()
    for r, row in enumerate(ROWS):
        for name, x, w in row:
            rect = (X0 + x * U, Y0 + r * U, w * U, U)
            shape = None
            if name == "ENTER":
                rect, shape = (X0 + 11.75 * U, Y0 + U, 1.75 * U, 2 * U), enter_shape()
            ident = name.lower().replace(" ", "-")
            zx = EXTRA.get(name) or [SINGLE.get(name, name)]
            m.keys.append(Key(ident, rect, "plus", KEY, legends(name), zx=zx, shape=shape,
                              label=name.title() if len(name) > 1 else name))
    return m
