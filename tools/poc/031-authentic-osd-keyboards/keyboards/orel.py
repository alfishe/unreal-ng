"""Orel BK-08 (Орель БК-08): the separate keyboard unit of 67 reed keys in front of the main unit, in the same cool grey
plastic: white caps for the letters, digits and punctuation, dark blue-grey caps for the modifiers, the editing keys
and the arrows, a long white space bar, an L-shaped ENTER. Each white cap carries its Cyrillic letter (top left,
large) and its Latin letter or symbols (small); each grey cap its Russian name over its English one. The ribbed left
and right ends, the broad palm rest in front.

Sources: refs-clones §5: the layout and the colours from references/photos/clones/orel-bk08-chm-1.jpg and -2.jpg
(computinghistory.org.uk, 750 px; widths measured against the white keys' pitch in each row), the key names and the
mapping from the keyboard schematic (Orel_BK08_Schematic.djvu sheet 4, "Узел клавиатуры" БУ5.103.474) and the manual
(Рис. 5, Table 2, section 6.3). The matrix is the ZX one, columns KD0-KD4 and two more, KD5 and KD7, read on #FE bits 5
and 7: АЛТ is CAPS SHIFT (A8 KD0; АЛТ + a digit gives the CS + digit functions, УПР + АЛТ the E cursor), УПР is
SYMBOL SHIFT (A15 KD1; its zone holds the function words, Table 2); РЕГ (A8 KD7), РУС (A8 KD5), ЗГЛ (A15 KD7), Р/А
(A15 KD5), ТАБ, the ` ~ key and the punctuation keys sit on KD5 / KD7 (keyboard.h ZXKEY_BK08_*). The editing keys and
the arrows are off the matrix: a К561КТ3 switch and two К555ТМ2 flip-flops close CAPS SHIFT and the digit's place
(ВШ = CS + 0, ГРАФ = CS + 9, ← ↓ ↑ → = CS + 5..8, РЕД = CS + 1, НОРМ = CS + 3, ИНВ = CS + 4).
UNVERIFIED: the widths within ±0.25 key; the production caps show no BASIC words (Рис. 5 prints them, the photos,
at 35 px a key, show none); the blank grey key left of 1 (Рис. 5: VERIFY, USR, ~; on A11 KD5 in the schematic).
"""

from model import Key, Legend, Model, Part

U = 19.05
KEYBOARD = {"id": "orel", "name": "Orel BK-08", "models": ["LSY256"], "size": [386, 176], "matrix": "zx",
            "depth": U - 0.9,
            # the caps as the photos show them: a short light front skirt, light sides, the gaps between the caps the
            # case's grey (no dark contours), the top face's edge barely marked
            "soviet": {"front": 0.22, "tones": ((0.93, 0.93, 0.93), (0.8, 0.8, 0.81), (0.88, 0.88, 0.89)),
                       "shadow": 0.18, "outline": 0.12, "rim": 0.25}}

CASE = "#c2c1b7"
WHITE, GREY = "#efeeeb", "#59616a"
INK, GREY_INK = "#4c4f56", "#b4bac1"
X0, Y0 = 40.6, 15.0      # row 0's top left
FACE = "sans"

CYRILLIC = dict(zip("QWERTYUIOPASDFGHJKLZXCVBNM", "ЙЦУКЕНГШЩЗФЫВАПРОЛДЯЧСМИТЬ"))
SHIFTED = dict(zip("1234567890", "!@#$%^&*()"))
# The white punctuation keys: the symbol on top, the one below, the Cyrillic letter, the matrix key (KD5 / KD7)
PUNCT = {"MINUS": ("_", "-", "", "BK08_MINUS"), "EQUALS": ("+", "=", "", "BK08_EQUALS"),
         "LBRACKET": ("{", "[", "Х", "BK08_LBRACKET"), "RBRACKET": ("}", "]", "Ъ", "BK08_RBRACKET"),
         "SEMICOLON": (":", ";", "Ж", "BK08_SEMICOLON"), "QUOTE": ("\"", "'", "Э", "BK08_QUOTE"),
         "BACKSLASH": ("|", "\\", "", "BK08_BACKSLASH"), "COMMA": ("<", ",", "Б", "BK08_COMMA"),
         "PERIOD": (">", ".", "Ю", "BK08_PERIOD"), "SLASH": ("?", "/", "Ё", "BK08_SLASH")}
# The grey keys: the Russian and the English name, what they press
GREYS = {"TILDE": ("", "", {"zx": ["BK08_TILDE"]}), "TAB": ("ТАБ", "TAB", {"zx": ["BK08_TAB"]}),
         "CTRL": ("УПР", "CTRL", {"zx": ["SS"]}), "CAPS": ("ЗГЛ", "CAPS", {"zx": ["BK08_CAPS"]}),
         "SHIFT": ("РЕГ", "SHIFT", {"zx": ["BK08_SHIFT"]}), "ALT": ("АЛТ", "ALT", {"zx": ["CS"]}),
         "RUS": ("РУС", "", {"zx": ["BK08_RUS"]}), "R/A": ("Р/А", "", {"zx": ["BK08_RA"]}),
         "BS": ("ВШ", "BS", {"zx": ["CS", "0"]}), "GRAPH": ("ГРАФ", "GRAPH", {"zx": ["CS", "9"]}),
         "TRUE": ("НОРМ", "TRUE", {"zx": ["CS", "3"]}), "INV": ("ИНВ", "INV", {"zx": ["CS", "4"]}),
         "EDIT": ("РЕД", "EDIT", {"zx": ["CS", "1"]})}
ARROWS = {"LEFT": "5", "DOWN": "6", "UP": "7", "RIGHT": "8"}

# Each row: (name, x, w) in keys from X0
ROWS = [
    [("TILDE", 0, 1.5)] + [(d, 1.5 + i, 1) for i, d in enumerate("1234567890")] +
    [("MINUS", 11.5, 1), ("EQUALS", 12.5, 1), ("BS", 13.5, 1.5), ("GRAPH", 15, 1)],
    [("TAB", 0, 2)] + [(c, 2 + i, 1) for i, c in enumerate("QWERTYUIOP")] +
    [("LBRACKET", 12, 1), ("RBRACKET", 13, 1), ("TRUE", 15, 1)],
    [("CTRL", 0, 1), ("CAPS", 1, 1.25)] + [(c, 2.25 + i, 1) for i, c in enumerate("ASDFGHJKL")] +
    [("SEMICOLON", 11.25, 1), ("QUOTE", 12.25, 1), ("INV", 15, 1)],
    [("SHIFT", 0, 1.75), ("BACKSLASH", 1.75, 1)] + [(c, 2.75 + i, 1) for i, c in enumerate("ZXCVBNM")] +
    [("COMMA", 9.75, 1), ("PERIOD", 10.75, 1), ("SLASH", 11.75, 1), ("SHIFT", 12.75, 1.25), ("UP", 14, 1),
     ("EDIT", 15, 1)],
    [("ALT", 0, 2), ("RUS", 2, 1), ("R/A", 3, 1), ("SPACE", 4, 8), ("R/A", 12, 1), ("LEFT", 13, 1), ("DOWN", 14, 1),
     ("RIGHT", 15, 1)],
]


def legends(name):
    if name in CYRILLIC:
        return [Legend(CYRILLIC[name], "cyrillic", "cap", 0.32, 0.42, 3.4, FACE, INK),
                Legend(name, "main", "cap", 0.5, 0.9, 2.2, FACE, INK)]
    if name in SHIFTED:
        return [Legend(SHIFTED[name], "symbol", "cap", 0.5, 0.42, 3.0, FACE, INK),
                Legend(name, "main", "cap", 0.24, 0.9, 2.2, FACE, INK)]
    if name in PUNCT:
        upper, lower, cyrillic, _ = PUNCT[name]
        if not cyrillic:
            return [Legend(upper, "symbol", "cap", 0.5, 0.42, 3.0, FACE, INK),
                    Legend(lower, "symbol", "cap", 0.24, 0.9, 2.4, FACE, INK)]
        return [Legend(cyrillic, "cyrillic", "cap", 0.32, 0.42, 3.4, FACE, INK),
                Legend(upper, "symbol", "cap", 0.74, 0.4, 2.6, FACE, INK),
                Legend(lower, "symbol", "cap", 0.74, 0.9, 2.4, FACE, INK)]
    if name in GREYS:
        russian, english, _ = GREYS[name]
        if not english:
            return [Legend(russian, "main", "cap", 0.5, 0.62, 2.8, FACE, GREY_INK)] if russian else []
        return [Legend(russian, "main", "cap", 0.08, 0.42, 2.6, FACE, GREY_INK, "start"),
                Legend(english, "symbol", "cap", 0.92, 0.9, 1.8, FACE, GREY_INK, "end")]
    if name in ARROWS:
        return [Legend("", "main", "cap", 0.5, 0.5, 4.6, color=GREY_INK, glyph=f"arrow:{name.lower()}")]
    return []  # SPACE


def mapping(name):
    """What a key presses: its matrix keys"""
    if (len(name) == 1 and name.isalnum()) or name == "SPACE":
        return {"zx": [name]}
    if name in GREYS:
        return GREYS[name][2]
    if name in ARROWS:
        return {"zx": ["CS", ARROWS[name]]}
    return {"zx": [PUNCT[name][3]]}


def enter_shape():
    x1, x2, x3 = X0 + 13.25 * U, X0 + 14 * U, X0 + 15 * U
    y1, y2, y3 = Y0 + 1 * U, Y0 + 2 * U, Y0 + 3 * U
    return [(x2, y1), (x3, y1), (x3, y3), (x1, y3), (x1, y2), (x2, y2)]


def ribs(x, y, w, n, pitch):
    """The ribbed end of the case: @p n shallow grooves across, @p pitch mm apart, each a soft shadow over a faintly
    lit lip (moulded in the case's plastic, not cut through)"""
    parts = []
    for i in range(n):
        gy = y + i * pitch
        parts += [Part("stripe", {"points": [(x, gy), (x + w, gy), (x + w, gy + 1.2), (x, gy + 1.2)]}, "#9c9b92"),
                  Part("stripe", {"points": [(x, gy + 1.2), (x + w, gy + 1.2), (x + w, gy + 1.6), (x, gy + 1.6)]},
                       "#d0cfc6")]
    return parts


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 4.0, "matte": True}, CASE),
        Part("steps", {"x": 2.0, "y": Y0 + 5 * U + 1.5, "w": W - 4.0, "h": 26.0, "steps": 1}, CASE),
        Part("well", {"x": X0 - 1.2, "y": Y0 - 1.2, "w": 16 * U + 2.4, "h": 5 * U + 2.4, "r": 1.0}, "#8d8c84"),
    ]
    m.case += ribs(6.0, 40.0, X0 - 14.0, 18, 4.2) + ribs(W - X0 + 8.0, 40.0, X0 - 14.0, 18, 4.2)
    seen = {}
    for y, row in enumerate(ROWS):
        for name, x, w in row:
            n = seen[name] = seen.get(name, 0) + 1
            key_id = name.lower().replace("/", "") + ("-right" if n == 2 else "")
            colour = GREY if name in GREYS or name in ARROWS else WHITE
            label = {"R/A": "Р/А"}.get(name, name.title() if len(name) > 1 else name)
            m.keys.append(Key(key_id, (X0 + x * U, Y0 + y * U, w * U, U), "soviet", colour, legends(name),
                              label=label, **mapping(name)))
    m.keys.append(Key("enter", (X0 + 13.25 * U, Y0 + U, 1.75 * U, 2 * U), "soviet", GREY,
                      [Legend("", "main", "cap", 0.45, 0.5, 7.0, color=GREY_INK, glyph="enter")], zx=["ENTER"],
                      shape=enter_shape(), label="Enter"))
    return m
