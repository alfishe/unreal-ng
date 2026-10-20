"""Quorum 128+ (Кворум): the ivory desktop case with its own 88-key keyboard of Soviet sculpted caps (the MS 7004
family): F1-F5, the boxed G, NMI and INV in the top row, the speaker and RES keys over the keypad; the main block with
Latin, ЙЦУКЕН and the BASIC words on each cap, the long L-shaped ENTER, the inverted-T arrows with tall left and right
keys, the keypad. The rear half of the top is a field of vent slots with the "КВОРУМ" badge and the maker's emblem.

Sources: refs-clones §4: the case and the layout from references/photos/clones/quorum128plus-photo-1.jpg (retropc.org,
measured against row 1's pitch, 29 px a key), the switch codes from quorum-kbd-extended-schematic.jpg ("KEYBOARD for
QUORUM and MAGIC", 1991). The 40 ZX keys, FIRE (in parallel with 0) and the arrows (with 5-8) are on the matrix; the
other keys on the port #7E columns by their schematic code, NMI, INV, the speaker key and RES on their own lines.
UNVERIFIED: which boxed code is which punctuation key (the codes 53h, 52h, 51h, 2Fh, 36h, 2Ah, 32h, 31h, 43h, 46h, 42h
are assigned to the eleven keys by their symbols where a symbol matches, by elimination otherwise), the keypad * as
the schematic's F8, the small print on the caps (read from a 1280 px photo: the BASIC words follow the Spectrum's).
"""

from keyboards.pentagon2plus import DIGITS, LETTERS
from model import Key, Legend, Model, Part

U = 19.05
KEYBOARD = {"id": "quorum", "name": "Quorum 128+", "models": ["QUORUM"], "size": [438, 281], "matrix": "zx",
            "twins": {"0": 2, "5": 2, "6": 2, "7": 2, "8": 2}, "depth": U - 0.9}

CASE = "#e6e1d8"
CREAM, GREY = "#ecebe5", "#a2a5ad"
INK = "#25262d"
X0, Y0 = 34.0, 166.5     # row 1's top left; the top row is 1.4 keys above it
PAD = 16.4               # the keypad's left, in keys from X0
WORD, LATIN = "din-condensed", "sans-bold"

CYRILLIC = dict(zip("QWERTYUIOPASDFGHJKLZXCVBNM", "ЙЦУКЕНГШЩЗФЫВАПРОЛДЯЧСМИТЬ"))
# The punctuation keys: the upper and lower symbol, the Cyrillic letter, the schematic's code (UNVERIFIED, above)
PUNCT = {"MINUS": ("−", "^", "", "53h"), "PLUS": ("+", "=", "", "43h"), "LBRACKET": ("{", "[", "Х", "2Ah"),
         "RBRACKET": ("}", "]", "Ъ", "32h"), "BACKSLASH": ("\\", "|", "", "42h"), "SEMICOLON": (";", ":", "Ж", "52h"),
         "QUOTE": ("*", "\"", "Э", "51h"), "TILDE": ("<", "~", "", "46h"), "COMMA": (",", "<", "Б", "2Fh"),
         "PERIOD": (".", ">", "Ю", "36h"), "SLASH": ("/", "?", "", "31h")}
# The grey keys off the matrix: the label lines, the code or line
GREYS = {"F1": (["F1"], "48h"), "F2": (["F2"], "4Eh"), "F3": (["F3"], "4Ah"), "F4": (["F4"], "4Dh"),
         "F5": (["F5"], "4Ch"), "BOX G": (["G"], "2Bh"), "NMI": (["NMI"], "NMI"), "INV": (["INV"], "INV"),
         "SPEAKER": ([], "SPEAKER"), "RES": (["RES"], "RESET"), "BOX E": (["E"], "54h"), "DEL": (["DEL"], "3Bh"),
         "EDIT": (["EDIT"], "55h"), "CAPS LOCK": (["CAPS", "LOCK"], "56h"), "RUS": (["RUS"], "57h"),
         "LAT": (["LAT"], "4Fh")}
GREY_MATRIX = {"CAPS SHIFT": (["CAPS", "SHIFT"], ["CS"]), "SYMBOL SHIFT": (["SYMBOL", "SHIFT"], ["SS"]),
               "FIRE": (["FIRE"], ["0"])}
ARROWS = {"LEFT": "5", "DOWN": "6", "UP": "7", "RIGHT": "8"}
KEYPAD = {"/": "3Ch", "*": "33h", "−": "2Ch", "7": "3Dh", "8": "34h", "9": "2Dh", "4": "3Eh", "5": "35h", "6": "2Eh",
          "1": "3Fh", "2": "37h", "3": "29h", "0": "38h", ".": "30h", "+": "28h"}
KEYPAD_ARROWS = {"8": "↑", "4": "←", "6": "→", "2": "↓"}
DIGIT_ARROWS = {"5": "←", "6": "↓", "7": "↑", "8": "→"}

# Each row: (name, x, w) in keys from X0; y in rows from row 1 (the top row at -1.4); h in rows
ROWS = [
    [(f"F{i + 1}", 1.5 * i, -1.4, 1.5, 1) for i in range(5)] +
    [(n, 11.0 + 1.5 * i, -1.4, 1.5, 1) for i, n in enumerate(["BOX G", "NMI", "INV"])] +
    [("SPEAKER", PAD, -1.4, 1.5, 1), ("RES", PAD + 1.5, -1.4, 1.5, 1)],
    [("BOX E", 0, 0, 1, 1)] + [(d, 1 + i, 0, 1, 1) for i, d in enumerate("1234567890")] +
    [("MINUS", 11, 0, 1, 1), ("PLUS", 12, 0, 1, 1), ("BS", 13, 0, 1, 1), ("DEL", 14, 0, 1.5, 1)],
    [("EDIT", 0, 1, 1.5, 1)] + [(c, 1.5 + i, 1, 1, 1) for i, c in enumerate("QWERTYUIOP")] +
    [("LBRACKET", 11.5, 1, 1, 1), ("RBRACKET", 12.5, 1, 1, 1), ("BACKSLASH", 13.5, 1, 1, 1)],
    [("CAPS LOCK", 0, 2, 1.75, 1)] + [(c, 1.75 + i, 2, 1, 1) for i, c in enumerate("ASDFGHJKL")] +
    [("SEMICOLON", 10.75, 2, 1, 1), ("QUOTE", 11.75, 2, 1, 1), ("TILDE", 12.75, 2, 1, 1)],
    [("RUS", 0, 3, 1.25, 1), ("LAT", 1.25, 3, 1, 1)] + [(c, 2.25 + i, 3, 1, 1) for i, c in enumerate("ZXCVBNM")] +
    [("COMMA", 9.25, 3, 1, 1), ("PERIOD", 10.25, 3, 1, 1), ("SLASH", 11.25, 3, 1, 1),
     ("LEFT", 12.25, 3, 1, 2), ("UP", 13.25, 3, 1, 1), ("RIGHT", 14.25, 3, 1.25, 2)],
    [("CAPS SHIFT", 0, 4, 2, 1), ("FIRE", 2, 4, 1.25, 1), ("SPACE", 3.25, 4, 7.25, 1), ("SYMBOL SHIFT", 10.5, 4, 1.75, 1),
     ("DOWN", 13.25, 4, 1, 1)],
    [(f"KP{n}", PAD + (i % 3), i // 3, 1, 1) for i, n in enumerate("/*−789456123")] +
    [(f"KP{n}", PAD + i, 4, 1, 1) for i, n in enumerate("0.+")],
]


def words(lines, size=3.0):
    ys = [0.55] if len(lines) == 1 else [0.38, 0.8]
    return [Legend(t, "main", "cap", 0.5, y, size, WORD, INK, squeeze=1.1) for t, y in zip(lines, ys)]


def small(text, x, y, anchor="middle", role="keyword", size=1.5, width=None):
    """A BASIC word or a symbol in the small print, squeezed to @p width mm if given"""
    from svgkit import text_width
    squeeze = min(1.1, round(width / max(text_width(text, size, WORD), 0.01), 3)) if width else 1.1
    return Legend(text, role, "cap", x, y, size, WORD, INK, anchor, squeeze=squeeze)


def legends(name):
    if name in LETTERS:
        e_ss, ss, keyword, extend = LETTERS[name]
        return [small(keyword, 0.5, 0.22, width=9.0), Legend(name, "main", "cap", 0.3, 0.68, 3.6, LATIN, INK),
                Legend(CYRILLIC[name], "cyrillic", "cap", 0.72, 0.74, 3.2, LATIN, INK),
                small(ss, 0.5, 0.97, role="symbol", width=8.0),
                small(f"{extend}  {e_ss}".strip(), 0.5, 1.4, role="extend", size=1.4, width=11.0)]
    if name in DIGITS:
        e_ss, ss, block, colour = DIGITS[name]
        result = [Legend(name, "main", "cap", 0.32, 0.8, 3.6, LATIN, INK), small(ss, 0.76, 0.32, role="symbol", size=2.2),
                  small(f"{colour}  {e_ss}".strip(), 0.5, 1.4, role="extend", size=1.4, width=11.0)]
        if block is not None:
            result.append(Legend("", "graphic", "cap", 0.26, 0.24, 2.0, color=INK, glyph=f"block:{block}"))
        if name in DIGIT_ARROWS:
            result.append(Legend(DIGIT_ARROWS[name], "symbol", "cap", 0.74, 0.82, 2.6, LATIN, INK))
        return result
    if name in PUNCT:
        upper, lower, cyrillic, _ = PUNCT[name]
        result = [Legend(upper, "symbol", "cap", 0.3, 0.42, 2.6, LATIN, INK),
                  Legend(lower, "symbol", "cap", 0.3, 0.9, 2.6, LATIN, INK)]
        if cyrillic:
            result.append(Legend(cyrillic, "cyrillic", "cap", 0.72, 0.74, 3.2, LATIN, INK))
        return result
    if name.startswith("BOX "):  # the boxed letter
        return [Legend("", "main", "cap", 0.5, 0.5, 4.2, color=INK, glyph="box"),
                Legend(name[-1], "main", "cap", 0.5, 0.62, 3.0, LATIN, INK)]
    if name == "SPEAKER":
        return [Legend("", "main", "cap", 0.5, 0.5, 4.0, color=INK, glyph="speaker")]
    if name in GREYS:
        return words(GREYS[name][0])
    if name in GREY_MATRIX:
        return words(GREY_MATRIX[name][0])
    if name == "BS":
        return words(["BS"])
    if name in ARROWS:
        return [Legend("", "main", "cap", 0.5, 0.5, 5.0, color=INK, glyph=f"arrow:{name.lower()}")]
    if name.startswith("KP"):
        n = name[2:]
        result = [Legend(n, "main", "cap", 0.38, 0.55, 3.0, LATIN, INK)]
        if n in KEYPAD_ARROWS:
            result.append(Legend(KEYPAD_ARROWS[n], "symbol", "cap", 0.62, 0.92, 2.2, LATIN, INK))
        return result
    return []  # SPACE, ENTER: the ENTER's arrow is added on its upright


def mapping(name):
    """What a key presses: the ZX matrix keys, or its line"""
    if (len(name) == 1 and name.isalnum()) or name in ("SPACE", "ENTER"):
        return {"zx": [name]}
    if name in GREY_MATRIX:
        return {"zx": GREY_MATRIX[name][1]}
    if name in ARROWS:  # wired in parallel with 5-8, no CAPS SHIFT (the cursor joystick)
        return {"zx": [ARROWS[name]]}
    if name in PUNCT:
        return {"line": f"#7E:{PUNCT[name][3]}"}
    if name in GREYS:
        code = GREYS[name][1]
        return {"line": f"#7E:{code}" if code.endswith("h") else code}
    if name == "BS":
        return {"line": "#7E:44h"}
    return {"line": f"#7E:{KEYPAD[name[2:]]}"}


def enter_shape():
    x1, x2, x3 = X0 + 13.75 * U, X0 + 14.5 * U, X0 + 15.5 * U
    y1, y2, y3 = Y0 + 1 * U, Y0 + 2 * U, Y0 + 3 * U
    return [(x2, y1), (x3, y1), (x3, y3), (x1, y3), (x1, y2), (x2, y2)]


def well(x, y, w, h):
    """The recess a block of keys stands in, @p x, @p y, @p w, @p h in keys"""
    return Part("well", {"x": X0 + x * U - 1.0, "y": Y0 + y * U - 1.0, "w": w * U + 2.0, "h": h * U + 2.0, "r": 0.8},
                "#c9c6c0")


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 3.0, "matte": True}, CASE),
        Part("vents", {"x": 12.0, "y": 9.0, "w": W - 24.0, "h": 101.0, "cols": 19, "rows": 33}, CASE),
        Part("steps", {"x": 1.0, "y": 110.0, "w": W - 2.0, "h": 23.0, "steps": 3}, CASE),
        # the maker's emblem at the rear left; the badge: the red, green and blue cells, then КВОРУМ white on black
        Part("panel", {"x": 27.0, "y": 25.0, "w": 14.0, "h": 12.0, "r": 0.6}, "#18181a"),
        Part("logo", {"text": "Z", "x": 34.0, "y": 33.4, "size": 7.0, "face": "sans-bold", "anchor": "middle"},
             "#d8d6d0"),  # UNVERIFIED: the emblem is a white ring round a mark too small in the photo to read
        Part("ring", {"x": 34.0, "y": 31.0, "r": 4.6, "width": 0.7}, "#d8d6d0"),
        Part("panel", {"x": 274.0, "y": 13.0, "w": 58.0, "h": 12.0, "r": 0.3}, "#121214"),
        Part("stripe", {"points": [(274.0, 13.0), (280.5, 13.0), (280.5, 25.0), (274.0, 25.0)]}, "#d02828"),
        Part("stripe", {"points": [(281.0, 13.0), (287.5, 13.0), (287.5, 25.0), (281.0, 25.0)]}, "#2a9a3a"),
        Part("stripe", {"points": [(288.0, 13.0), (294.5, 13.0), (294.5, 25.0), (288.0, 25.0)]}, "#2f62c8"),
        Part("logo", {"text": "К", "x": 291.25, "y": 22.6, "size": 8.4, "face": "sans", "anchor": "middle"},
             "#f2f2f0"),
        Part("logo", {"text": "ВОРУМ", "x": 296.0, "y": 22.6, "width": 34.0, "size": 8.4, "face": "sans",
                      "spacing": 1.6}, "#f2f2f0"),
        well(0, -1.4, 7.5, 1), well(11.0, -1.4, 4.5, 1), well(PAD, -1.4, 3, 1), well(0, 0, 15.5, 5), well(PAD, 0, 3, 5),
    ]
    for row in ROWS:
        for name, x, y, w, h in row:
            rect, shape = (X0 + x * U, Y0 + y * U, w * U, h * U), None
            colour = GREY if name in GREYS or name in GREY_MATRIX or name in ARROWS else CREAM
            m.keys.append(Key(name.lower().replace(" ", "-"), rect, "soviet", colour, legends(name), shape=shape,
                              label=name.title() if len(name) > 1 else name, **mapping(name)))
    m.keys.append(Key("enter", (X0 + 13.75 * U, Y0 + U, 1.75 * U, 2 * U), "soviet", GREY,
                      [Legend("", "main", "cap", 0.75, -0.45, 9.0, color=INK, glyph="enter")], zx=["ENTER"],
                      shape=enter_shape(), label="Enter"))
    return m
