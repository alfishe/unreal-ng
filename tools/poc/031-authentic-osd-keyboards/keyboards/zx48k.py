"""ZX Spectrum 16K / 48K (1982): blue-grey rubber keys through a black printed plate, the raised rear hump.

Sources: references/sinclair.md §0-1 (case 233 x 144 mm, rows, legends from the plate,
matrix from the 48K manual ch. 23); the pitch and the key faces re-measured on the top-down photo references/photos/sinclair/48k-k1-top2.jpg
(the pitch 19.8 mm puts ENTER's and SPACE's right edges where the photo has them).
"""

from model import Key, Legend, Model, Part

KEYBOARD = {"id": "zx48k", "name": "ZX Spectrum 16K / 48K", "models": ["48K"], "size": [233, 144], "matrix": "zx",
            "skirt": 1.8}

U = 19.8                 # key pitch across, mm
LEFT = 11.4              # key 1's left edge
ROWS_Y = [57.5, 76.2, 95.0, 115.0]  # row centres from the rear edge
FACE_W, FACE_H = 0.71 * U, 10.8
KEY = "#56676a"
WHITE, RED, GREEN = "#f2f2f2", "#e0302a", "#3dbe3d"
KEY_RED = "#ff4a3a"  # the same red ink on the rubber reads lighter than on the black plate
COLOURS = {"BLUE": "#3b6cf0", "RED": RED, "MAGENTA": "#c838c8", "GREEN": GREEN, "CYAN": "#28c0f0",
           "YELLOW": "#f0d020", "WHITE": WHITE}
FACE = "sans-bold"

# Rows: (name, x in u from key 1's left edge, width in u)
ROWS = [
    [(d, i, 0.71) for i, d in enumerate("1234567890")],
    [(c, 0.5 + i, 0.71) for i, c in enumerate("QWERTYUIOP")],
    [(c, 0.75 + i, 0.71) for i, c in enumerate("ASDFGHJKL")] + [("ENTER", 9.75, 0.71)],
    [("CAPS SHIFT", 0, 1.0)] + [(c, 1.25 + i, 0.71) for i, c in enumerate("ZXCVBNM")] +
    [("SYMBOL SHIFT", 8.25, 0.71), ("SPACE", 9.25, 1.22)],
]
MATRIX = {"CAPS SHIFT": "CS", "SYMBOL SHIFT": "SS"}

# Digits: above (colour name, the CAPS SHIFT function), the graphic block, the red symbol, below (E + SS)
DIGITS = {"1": ("BLUE", "EDIT", 1, "!", "DEF FN"), "2": ("RED", "CAPS LOCK", 2, "@", "FN"),
          "3": ("MAGENTA", "TRUE VIDEO", 3, "#", "LINE"), "4": ("GREEN", "INV. VIDEO", 4, "$", "OPEN #"),
          "5": ("CYAN", "arrow:left", 5, "%", "CLOSE #"), "6": ("YELLOW", "arrow:down", 6, "&", "MOVE"),
          "7": ("WHITE", "arrow:up", 7, "'", "ERASE"), "8": ("", "arrow:right", 0, "(", "POINT"),
          "9": ("", "GRAPHICS", None, ")", "CAT"), "0": ("BLACK", "DELETE", None, "_", "FORMAT")}

# Letters: above (E mode, green), the keyword (white), the red symbol or SS keyword, below (E + SS, red)
LETTERS = {
    "Q": ("SIN", "PLOT", "<=", "ASN"), "W": ("COS", "DRAW", "<>", "ACS"), "E": ("TAN", "REM", ">=", "ATN"),
    "R": ("INT", "RUN", "<", "VERIFY"), "T": ("RND", "RAND", ">", "MERGE"), "Y": ("STR $", "RETURN", "AND", "["),
    "U": ("CHR $", "IF", "OR", "]"), "I": ("CODE", "INPUT", "AT", "IN"), "O": ("PEEK", "POKE", ";", "OUT"),
    "P": ("TAB", "PRINT", '"', "©"), "A": ("READ", "NEW", "STOP", "~"), "S": ("RESTORE", "SAVE", "NOT", "|"),
    "D": ("DATA", "DIM", "STEP", "\\"), "F": ("SGN", "FOR", "TO", "{"), "G": ("ABS", "GOTO", "THEN", "}"),
    "H": ("SQR", "GOSUB", "↑", "CIRCLE"), "J": ("VAL", "LOAD", "-", "VAL $"), "K": ("LEN", "LIST", "+", "SCREEN $"),
    "L": ("USR", "LET", "=", "ATTR"), "Z": ("LN", "COPY", ":", "BEEP"), "X": ("EXP", "CLEAR", "£", "INK"),
    "C": ("L PRINT", "CONT", "?", "PAPER"), "V": ("L LIST", "CLS", "/", "FLASH"), "B": ("BIN", "BORDER", "*", "BRIGHT"),
    "N": ("IN KEY $", "NEXT", ",", "OVER"), "M": ("PI", "PAUSE", ".", "INVERSE"),
}

PLATE = 2.1    # the plate's print, mm
ABOVE1, ABOVE2 = 1.1, 3.7  # baselines above the key: the line next to it, the one over that


def above(text, line, colour, inverse=False):
    if text.startswith("arrow:"):  # a hollow arrow, centred on its line
        return Legend("", "extend", "above", 1.6 / FACE_W, line + 0.75, 2.6, color=colour, glyph=text)
    return Legend(text, "extend", "above", 0, line, PLATE, FACE, colour, anchor="start", inverse=inverse)


def below(text):
    return Legend(text, "extend-shift", "below", 0, 0.9 + KEYBOARD["skirt"], PLATE, FACE, RED, anchor="start")


def legends(name):
    if name in DIGITS:
        colour, function, block, symbol, under = DIGITS[name]
        result = [above(function, ABOVE1, WHITE)]
        if colour:
            result.append(above(colour, ABOVE2, "#ffffff" if colour == "BLACK" else COLOURS[colour],
                                inverse=colour == "BLACK"))
        result.append(Legend("Ø" if name == "0" else name, "main", "cap", 0.1, 0.55, 5.2, FACE, WHITE, "start"))
        if block is not None:  # white is paper: the inked quadrants stay the key's colour (8 is a white square)
            result.append(Legend("", "graphic", "cap", 0.72, 0.33, 3.4, color=WHITE, glyph=f"block:{block}:paper"))
        result.append(Legend(symbol, "symbol", "cap", 0.72 if block is not None else 0.75, 0.86, 2.8, FACE, KEY_RED))
        result.append(below(under))
        return result
    if name in LETTERS:
        e, keyword, ss, under = LETTERS[name]
        return [above(e, ABOVE1, GREEN),
                Legend(name, "main", "cap", 0.1, 0.55, 5.2, FACE, WHITE, "start"),
                Legend(ss, "symbol", "cap", 0.92, 0.42, 2.2 if len(ss) > 2 else 2.6, FACE, KEY_RED, "end"),
                Legend(keyword, "keyword", "cap", 0.92, 0.88, 2.1, FACE, WHITE, "end"),
                below(under)]
    if name == "CAPS SHIFT":
        return [Legend("CAPS", "main", "cap", 0.5, 0.44, 2.6, FACE, WHITE), Legend("SHIFT", "main", "cap", 0.5, 0.8,
                                                                                  2.6, FACE, WHITE)]
    if name == "SYMBOL SHIFT":
        return [Legend("SYMBOL", "main", "cap", 0.5, 0.44, 2.0, FACE, KEY_RED),
                Legend("SHIFT", "main", "cap", 0.5, 0.78, 2.0, FACE, KEY_RED)]
    if name == "ENTER":
        return [Legend("ENTER", "main", "cap", 0.5, 0.62, 2.1, FACE, WHITE)]
    if name == "SPACE":
        return [Legend("BREAK", "main", "cap", 0.5, 0.4, 2.2, FACE, WHITE),
                Legend("SPACE", "main", "cap", 0.5, 0.84, 3.6, FACE, WHITE)]
    raise ValueError(name)


def rainbow():
    """Four diagonal stripes from the plate's front edge (x 199..216 mm) up to its right edge (y 64..90 mm), red on
    the left / top; the keys sit over them (refs-sinclair §1.1, re-measured on 48k-k1-top2.jpg)"""
    parts = []
    bottom, right = 137.0, 229.0
    for i, colour in enumerate(["#e2341f", "#e0a20a", "#5e9418", "#1a88b4"]):
        x0, y0 = 199.0 + 4.3 * i, 64.0 + 6.5 * i
        parts.append(Part("stripe", {"points": [(x0, bottom), (x0 + 4.3, bottom), (right, y0 + 6.5), (right, y0)]},
                          colour))
    return parts


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 4.5, "matte": True}, "#1c1c1b"),
        Part("plate", {"x": 3.0, "y": 41.0, "w": W - 6.0, "h": H - 44.5, "r": 1.0}, "#141414",
             {"sheen": 0.035}),
    ]
    m.case += rainbow()
    m.case += [
        Part("hump", {"x": 0, "y": 0, "w": W, "h": 40.5, "r": 4.5}, "#1f1f1e"),
        Part("logo", {"text": "sinclair", "x": 3.0, "y": 10.6, "size": 11.5, "squeeze": 1.95, "spacing": 0.3,
                      "face": "rounded", "outline": True}, "#2c2c2b"),
        Part("logo", {"text": "ZX Spectrum", "x": 10.5, "y": 18.4, "size": 5.4, "face": "sans-bold"}, "#e8e8e8"),
    ]
    for r, row in enumerate(ROWS):
        for name, x, w in row:
            width = w * U if w != 0.71 else FACE_W
            rect = (LEFT + x * U, ROWS_Y[r] - FACE_H / 2 - 0.6, width, FACE_H)
            ident = name.lower().replace(" ", "-")
            m.keys.append(Key(ident, rect, "rubber", KEY, legends(name), zx=[MATRIX.get(name, name)],
                              label=name.title() if len(name) > 1 else name))
    return m
