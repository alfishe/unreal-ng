"""Форум БК-09 Турбо (Forum BK Turbo, the "Форум" cooperative, Berdsk, early 1990s): a 48K clone on the Т34ВГ1 gate
array in a black one-piece wedge case, the raised rear band a wide vent grille; the yellow name at its left is left
out (the owner, 2026-10-21: the picture carries no name); about 88 sculpted keys: the 40 ZX keys in their Spectrum grouping inside a PC-like frame, ivory, each with
its Latin letter, the SS symbol, the Cyrillic letter (ЙЦУКЕН) and the keyword on the front; grey function keys (F1-F5
on a row of their own, three blank ones, the speaker key), grey modifiers and editing keys, a big grey L-shaped
return, a grey arrow block, a red RES, and an ivory numeric keypad in a well of its own.

Sources: refs-clones §8 (written: ru.wikipedia "Клоны ZX Spectrum", "Т34ВМ1 и Т34ВГ1", speccy.info "Форум БК-09
Турбо", Frolov's museum page, zx-pk thread 24100); the owner's photo
references/photos/clones/forum-bk08-turbo-frolov-r86.jpg (photo (C) Sergei Frolov 2008, r86.com/frolov, a 3/4 view,
reference only): the layout, the colours and the legends read from it. No schematic, no manual: only the 40 ZX keys
are mapped (the Т34ВГ1 reads the 8x5 matrix); every other key presses nothing and says nothing on a click (the owner,
2026-10-21). UNVERIFIED: every width and position (±0.5 key, a perspective photo), the model number (the logo's last digit
reads 8 or 9; Frolov's page names this machine BK-09), which arrow is which, the legends of the icon keys, the
"FIRE" key, the E-mode words (unreadable), the case's size.
"""

from model import Key, Legend, Model, Part

U = 19.05
KEYBOARD = {"id": "forum", "name": "Forum", "models": ["48K"], "silent": True, "size": [404, 196], "matrix": "zx",
            "depth": U - 0.9,
            "soviet": {"tones": ((0.9, 0.89, 0.86), (0.76, 0.74, 0.7), (0.86, 0.84, 0.8)),
                       "shadow": 0.45, "outline": 0.3, "rim": 0.5}}

CASE = "#1d1e20"
IVORY, GREY, RED = "#e7e1cb", "#a7aaa4", "#d0502e"
INK = "#2a2a28"
X0, Y0 = 14.0, 76.0      # the main block's row 0, top left
TOP = Y0 - 1.35 * U      # the function row
PAD = X0 + 16.6 * U      # the keypad's left edge
FACE, WORD = "sans", "din-condensed"

CYRILLIC = dict(zip("QWERTYUIOPASDFGHJKLZXCVBNM", "ЙЦУКЕНГШЩЗФЫВАПРОЛДЯЧСМИТЬ"))
# The ZX keys: the SS item, the keyword on the front (the 48K's; GO TO, GO SUB as printed)
LETTERS = {
    "Q": ("<=", "PLOT"), "W": ("<>", "DRAW"), "E": (">=", "REM"), "R": ("<", "RUN"), "T": (">", "RAND"),
    "Y": ("AND", "RETURN"), "U": ("OR", "IF"), "I": ("AT", "INPUT"), "O": (";", "POKE"), "P": ('"', "PRINT"),
    "A": ("STOP", "NEW"), "S": ("NOT", "SAVE"), "D": ("STEP", "DIM"), "F": ("TO", "FOR"), "G": ("THEN", "GO TO"),
    "H": ("↑", "GO SUB"), "J": ("-", "LOAD"), "K": ("+", "LIST"), "L": ("=", "LET"), "Z": (":", "COPY"),
    "X": ("£", "CLEAR"), "C": ("?", "CONT"), "V": ("/", "CLS"), "B": ("*", "BORDER"), "N": (",", "NEXT"),
    "M": (".", "PAUSE"),
}
DIGITS = {"1": ("!", 1, "DEF FN"), "2": ("@", 2, "FN"), "3": ("#", 3, "LINE"), "4": ("$", 4, "OPEN"),
          "5": ("%", "left", "CLOSE"), "6": ("&", "down", "MOVE"), "7": ("'", "up", "ERASE"),
          "8": ("(", "right", "POINT"), "9": (")", None, "CAT"), "0": ("_", None, "FORMAT")}
# The PC punctuation keys: the upper and lower symbol, the Cyrillic letter
PUNCT = {"MINUS": ("_", "-", ""), "EQUALS": ("+", "=", ""), "LBRACKET": ("{", "[", "Х"), "RBRACKET": ("}", "]", "Ъ"),
         "SEMICOLON": (":", ";", "Ж"), "QUOTE": ('"', "'", "Э"), "TILDE": ("~", "`", ""),
         "COMMA": ("<", ",", "Б"), "PERIOD": (">", ".", "Ю"), "SLASH": ("?", "/", "")}
# The grey keys and their print
NAMES = {"F1": ["F1"], "F2": ["F2"], "F3": ["F3"], "F4": ["F4"], "F5": ["F5"], "EDIT": ["EDIT"], "RUS": ["RUS"],
         "LAT": ["LAT"], "DEL": ["DEL"], "CAPS SHIFT": ["CAPS", "SHIFT"], "FIRE": ["FIRE"],
         "SYMBOL SHIFT": ["SYMBOL", "SHIFT"], "RES": ["RES"], "ESC": ["ESC"]}

# The main block: (name, x, w) in keys from X0, one list per row
ROWS = [
    [("ICON 1", 0, 1)] + [(d, 1 + i, 1) for i, d in enumerate("1234567890")] +
    [("MINUS", 11, 1), ("EQUALS", 12, 1), ("ESC", 13, 1), ("DEL", 14, 1.5)],
    [("ICON 2", 0, 1.5)] + [(c, 1.5 + i, 1) for i, c in enumerate("QWERTYUIOP")] +
    [("LBRACKET", 11.5, 1), ("RBRACKET", 12.5, 1), ("BLANK", 13.5, 1)],
    [("EDIT", 0, 1.75)] + [(c, 1.75 + i, 1) for i, c in enumerate("ASDFGHJKL")] +
    [("SEMICOLON", 10.75, 1), ("QUOTE", 11.75, 1), ("TILDE", 12.75, 1)],
    [("RUS", 0, 1.25), ("LAT", 1.25, 1.25)] + [(c, 2.5 + i, 1) for i, c in enumerate("ZXCVBNM")] +
    [("COMMA", 9.5, 1), ("PERIOD", 10.5, 1), ("SLASH", 11.5, 1), ("UP", 12.5, 1), ("ARROW 2", 13.5, 1)],
    [("CAPS SHIFT", 0, 1.75), ("FIRE", 1.75, 1.25), ("SPACE", 3, 7.5), ("SYMBOL SHIFT", 10.5, 2),
     ("ARROW 3", 12.5, 1), ("ARROW 4", 13.5, 1)],
]
FUNCTION_ROW = [("F1", 0, 1.5), ("F2", 1.5, 1.5), ("F3", 3, 1.5), ("F4", 4.5, 1.5), ("F5", 6, 1.5),
                ("BLANK 1", 11.25, 1.5), ("BLANK 2", 12.75, 1.5), ("BLANK 3", 14.25, 1.5)]
KEYPAD = [["PAD BLANK 1", "PAD BLANK 2", "PAD BLANK 3"], ["7", "8", "9"], ["4", "5", "6"], ["1", "2", "3"],
          ["0", ".", "ENTER"]]
SINGLE = {"CAPS SHIFT": "CS", "SYMBOL SHIFT": "SS", "SPACE": "SPACE"}


def small(text, x, y, size=2.2, face=FACE, anchor="middle"):
    return Legend(text, "symbol", "cap", x, y, size, face, INK, anchor)


def legends(name):
    if name in LETTERS:
        ss, keyword = LETTERS[name]
        word = len(ss) > 1
        return [Legend(name, "main", "cap", 0.1, 0.42, 4.6, FACE, INK, "start"),
                small(ss, 0.12, 0.84, 2.0 if word else 2.8, WORD if word else FACE, "start"),
                Legend(CYRILLIC[name], "cyrillic", "cap", 0.82, 0.84, 3.4, FACE, INK, "end"),
                Legend(keyword, "keyword", "cap", 0.5, 1.36, 2.7, WORD, INK)]
    if name in DIGITS:
        symbol, graphic, word = DIGITS[name]
        result = [Legend(name, "main", "cap", 0.1, 0.42, 4.6, FACE, INK, "start"), small(symbol, 0.14, 0.86, 2.8,
                                                                                         anchor="start"),
                  Legend(word, "keyword", "cap", 0.5, 1.36, 2.7, WORD, INK)]
        if isinstance(graphic, int):
            result.append(Legend("", "graphic", "cap", 0.72, 0.62, 2.8, color=INK, glyph=f"block:{graphic}"))
        elif graphic:
            result.append(Legend("", "graphic", "cap", 0.72, 0.3, 3.0, color=INK, glyph=f"arrow:{graphic}"))
        return result
    if name in PUNCT:
        upper, lower, cyrillic = PUNCT[name]
        result = [small(upper, 0.24, 0.38, 2.8), small(lower, 0.24, 0.86, 2.8)]
        if cyrillic:
            result.append(Legend(cyrillic, "cyrillic", "cap", 0.72, 0.7, 3.4, FACE, INK))
        return result
    if name in NAMES:
        lines = NAMES[name]
        ys = [0.55] if len(lines) == 1 else [0.42, 0.78]
        return [Legend(t, "main", "cap", 0.5, y, 2.2, FACE, "#4a2015" if name == "RES" else INK)
                for t, y in zip(lines, ys)]
    if name == "UP":  # the hollow up arrow, the only arrow legible on the photo
        return [Legend("", "main", "cap", 0.5, 0.5, 4.2, color=INK, glyph="arrow:up")]
    if name.startswith("ICON"):
        return [Legend("", "main", "cap", 0.5, 0.5, 3.0, color=INK, glyph="box")]
    if name == "SPEAKER":
        return [Legend("", "main", "cap", 0.5, 0.5, 3.6, color=INK, glyph="speaker")]
    return []  # SPACE, the blank keys, the unreadable arrows


def pad(name):
    """A keypad key's print: its digit, the point or ENTER, centred (the three top keys are blank)"""
    if name.startswith("PAD BLANK"):
        return []
    return [Legend(name, "main", "cap", 0.5, 0.6, 2.2 if name == "ENTER" else 3.4, FACE, INK)]


def label(name):
    pretty = {"MINUS": "-", "EQUALS": "=", "LBRACKET": "[", "RBRACKET": "]", "SEMICOLON": ";", "QUOTE": "'",
              "TILDE": "~", "COMMA": ",", "PERIOD": ".", "SLASH": "/"}
    return pretty.get(name, name.title() if len(name) > 1 else name)


def key(ident, rect, colour, name, zx=None, text=None, print_=None):
    return Key(ident, rect, "soviet", colour, legends(name) if print_ is None else print_, zx=zx,
               label=text or label(name))


def return_shape():
    """The L: the upright on row 1 (x 14.5..15.5), the foot on row 2 (x 13.75..15.5)"""
    x1, x2, x3 = X0 + 13.75 * U, X0 + 14.5 * U, X0 + 15.5 * U
    y1, y2, y3 = Y0 + 1 * U, Y0 + 2 * U, Y0 + 3 * U
    return [(x2, y1), (x3, y1), (x3, y3), (x1, y3), (x1, y2), (x2, y2)]


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 3.0, "matte": True}, CASE),
        Part("steps", {"x": 1.5, "y": 36.0, "w": W - 3.0, "h": 4.0, "steps": 1}, CASE),  # the rear band's front edge
        Part("vents", {"x": 112.0, "y": 5.0, "w": W - 124.0, "h": 28.0, "cols": 22, "rows": 9}, CASE),
        Part("panel", {"x": 6.0, "y": TOP - 4.0, "w": PAD - 12.0, "h": Y0 + 5 * U - TOP + 8.0, "r": 1.0}, "#161719"),
        Part("panel", {"x": PAD - 3.0, "y": Y0 - 4.0, "w": 3 * U + 6.0, "h": 5 * U + 8.0, "r": 1.0}, "#161719"),
        Part("steps", {"x": 1.5, "y": Y0 + 5 * U + 14.0, "w": W - 3.0, "h": 3.5, "steps": 1}, CASE),  # the palm rest
        Part("panel", {"x": 9.0, "y": 6.0, "w": 6.0, "h": 6.0, "r": 0.6}, "#b89a34"),  # the small badge in the corner
    ]
    for name, x, w in FUNCTION_ROW:
        m.keys.append(key(name.lower().replace(" ", "-"), (X0 + x * U, TOP, w * U, U), GREY, name))
    m.keys.append(key("speaker", (PAD, TOP, 1.5 * U, U), GREY, "SPEAKER", text="Speaker"))
    m.keys.append(key("res", (PAD + 1.5 * U, TOP, 1.5 * U, U), RED, "RES", text="RES"))
    grey = set(NAMES) | {"ICON 1", "ICON 2", "UP", "ARROW 2", "ARROW 3", "ARROW 4"}
    for r, row in enumerate(ROWS):
        for name, x, w in row:
            matrix = name in LETTERS or name in DIGITS or name in SINGLE
            zx = [SINGLE.get(name, name)] if matrix else None
            colour = GREY if name in grey and name != "ESC" else IVORY
            m.keys.append(key(name.lower().replace(" ", "-"), (X0 + x * U, Y0 + r * U, w * U, U), colour, name, zx))
    m.keys.append(Key("enter", (X0 + 13.75 * U, Y0 + U, 1.75 * U, 2 * U), "soviet", GREY,
                      [Legend("", "main", "cap", 0.62, 0.5, 8.0, color=INK, glyph="enter")], zx=["ENTER"],
                      shape=return_shape(), label="Enter"))
    for r, row in enumerate(KEYPAD):
        for c, name in enumerate(row):
            ident = "pad-" + {".": "point"}.get(name, name.lower().replace("pad ", "").replace(" ", "-"))
            text = "Keypad " + (name.title().replace("Pad ", "") if len(name) > 1 else name)
            m.keys.append(key(ident, (PAD + c * U, Y0 + r * U, U, U), IVORY, name, text=text, print_=pad(name)))
    return m
