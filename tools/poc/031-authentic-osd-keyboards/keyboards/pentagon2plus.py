"""Pentagon 128 "2+": the Pentagon board in a black plastic case with the 58-key Spectrum+ layout (the 128's
geometry and matrix), cream sculpted caps printed in black, the grey edit and shift keys, a red BREAK, the
"ZX-SPECTR 128" plate at the rear left, a vent grille along the rear right.

Sources: the owner's photo references/photos/clones/pentagon-128-2plus-owner.png (2026-10-19): the layout, the legends
read key by key (the SS item and the E + SS item on top, the letter, its keyword beside it, the E-mode word on the
front slope), the case measured against the key block's 13.5 u; the caps' facets measured on the straight-down
pentagon-zxbyte-kbd-top-owner.png (the same keyboard); the layout from keyboards/zx128.py.
"""

from keyboards.zx128 import EXTRA, ROWS, SINGLE
from model import Key, Legend, Model, Part
from svgkit import text_width

KEYBOARD = {"id": "pentagon2plus", "name": "Pentagon 128 2+", "models": ["PENTAGON"], "size": [343, 165],
            "matrix": "zx", "twins": {"CS": 2, "SS": 2}}

U = 19.0
X0, Y0 = 30.0, 50.0      # the key block's top left
CASE = "#1f2225"
CREAM, GREY, RED = "#eedfb2", "#b4b3ac", "#e0403a"
INK = "#2b2722"  # the black print reads warm on the cream
LOGO = "#c2da62"
WORD, SYMBOL = "din-condensed", "sans-bold"
NARROW = 1.15  # the print is a heavy DIN-like grotesque, less condensed than DIN Condensed

# Top left, top right, the keyword beside the letter, the E-mode word on the skirt (as printed)
LETTERS = {
    "Q": ("ASN", "<=", "PLOT", "SIN"), "W": ("ACS", "<>", "DRAW", "COS"), "E": ("ATN", ">=", "REM", "TAN"),
    "R": ("VERIFY", "<", "RUN", "INT"), "T": ("MERGE", ">", "RAND", "RND"), "Y": ("AND", "[", "RETURN", "STR$"),
    "U": ("OR", "]", "IF", "CHR$"), "I": ("IN", "AT", "INPUT", "CODE"), "O": ("OUT", "", "POKE", "PEEK"),
    "P": ("(C)", "", "PRINT", "TAB"), "A": ("~", "STOP", "NEW", "READ"), "S": ("|", "NOT", "SAVE", "RESTR"),
    "D": ("\\", "STEP", "DIM", "DATA"), "F": ("{", "TO", "FOR", "SGN"), "G": ("}", "THEN", "GOTO", "ABS"),
    "H": ("CIRCLE", "↑", "GOSUB", "SQR"), "J": ("VAL$", "-", "LOAD", "VAL"), "K": ("SCRN$", "+", "LIST", "LEN"),
    "L": ("ATTR", "=", "LET", "USR"), "Z": ("BEEP", ":", "COPY", "LN"), "X": ("INK", "£", "CLEAR", "EXP"),
    "C": ("PAPER", "?", "CONT", "LPRINT"), "V": ("FLASH", "/", "CLS", "LLIST"), "B": ("BRIGHT", "*", "BORDER", "BIN"),
    "N": ("OVER", "", "NEXT", "INKEY$"), "M": ("INVERS", "", "PAUSE", "PI"),
}
# Digits: top left, top right, the block graphic, the colour on the skirt
DIGITS = {"1": ("DEF FN", "!", 1, "BLUE"), "2": ("FN", "@", 2, "RED"), "3": ("LINE", "#", 3, "MGNTA"),
          "4": ("OPEN", "$", 4, "GREEN"), "5": ("CLOSE", "%", 5, "CYAN"), "6": ("MOVE", "&", 6, "YELLOW"),
          "7": ("ERASE", "'", 7, "WHITE"), "8": ("POINT", "(", 0, ""), "9": ("CAT", ")", None, ""),
          "0": ("FORMAT", "_", None, "BLACK")}
NAMED = {"TRUE VIDEO": ["TRUE", "VIDEO"], "INV VIDEO": ["INV", "VIDEO"], "BREAK": ["BREAK"], "DELETE": ["DELETE"],
         "GRAPH": ["GRAPH"], "EXTEND MODE": ["EXT", "MODE"], "EDIT": ["EDIT"], "CAPS SHIFT": ["CAPS  SHIFT"],
         "CAPS SHIFT 2": ["CAPS  SHIFT"], "CAPS LOCK": ["CAPS", "LOCK"], "SYMBOL SHIFT": ["SYMB", "SHIFT"],
         "SYMBOL SHIFT 2": ["SYMB", "SHIFT"]}
ARROWS = {"UP": "up", "DOWN": "down", "LEFT": "left", "RIGHT": "right"}
CREAM_KEYS = set(LETTERS) | set(DIGITS) | set(ARROWS) | set(';",.')


def top(text, x, anchor):
    """An item on the cap's top: a symbol large, a word small"""
    word = any(c.isalpha() for c in text) and len(text) > 1
    size = 2.4 if word else 3.2
    face = WORD if word else SYMBOL
    squeeze = min(NARROW, round(7.0 / max(text_width(text, size, face), 0.01), 3)) if word else 1.0
    return Legend(text, "symbol", "cap", x, 0.2 if word else 0.26, size, face, INK, anchor, squeeze=squeeze)


def front(text):
    """The E-mode word or the colour, printed on the front slope under the top face"""
    return Legend(text, "extend", "cap", 0.5, 1.4, 3.0, WORD, INK,
                  squeeze=min(NARROW, round(12.0 / text_width(text, 3.0, WORD), 3)))


def legends(name):
    if name in LETTERS:
        left, right, keyword, extend = LETTERS[name]
        result = [Legend(name, "main", "cap", 0.1, 0.84, 6.0, WORD, INK, "start", squeeze=1.25),
                  Legend(keyword, "keyword", "cap", 0.94, 0.84, 2.4, WORD, INK, "end",
                         squeeze=min(NARROW, round(6.0 / text_width(keyword, 2.4, WORD), 3))),
                  front(extend)]
        result += [top(t, x, a) for t, x, a in ((left, 0.08, "start"), (right, 0.92, "end")) if t]
        return result
    if name in DIGITS:
        left, right, block, colour = DIGITS[name]
        result = [Legend(name, "main", "cap", 0.1, 0.84, 5.8, WORD, INK, "start", squeeze=1.25), top(left, 0.08, "start"),
                  top(right, 0.92, "end")]
        if block is not None:
            result.append(Legend("", "graphic", "cap", 0.7, 0.66, 3.0, color=INK, glyph=f"block:{block}"))
        if colour:
            result.append(front(colour))
        return result
    if name in NAMED:
        lines = NAMED[name]
        ys = [0.55] if len(lines) == 1 else [0.42, 0.8]
        return [Legend(t, "main", "cap", 0.5, y, 3.2, WORD, INK, squeeze=NARROW) for t, y in zip(lines, ys)]
    if name in ARROWS:
        return [Legend("", "main", "cap", 0.5, 0.55, 4.4, color=INK, glyph=f"arrow:{ARROWS[name]}")]
    if name == "ENTER":  # on the upright
        return [Legend("", "main", "cap", 0.75, -0.4, 9.0, color=INK, glyph="enter")]
    if name in ';",.':
        return [Legend({";": ":", '"': ",,"}.get(name, name), "main", "cap", 0.5, 0.6, 2.2, SYMBOL, INK)]
    return []  # SPACE: blank


def enter_shape():
    x1, x2, x3 = X0 + 11.75 * U, X0 + 12.5 * U, X0 + 13.5 * U
    y1, y2, y3 = Y0 + 1 * U, Y0 + 2 * U, Y0 + 3 * U
    return [(x2, y1), (x3, y1), (x3, y3), (x1, y3), (x1, y2), (x2, y2)]


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 4.0, "matte": True}, CASE),
        Part("grille", {"x": 180.0, "y": 1.2, "w": 150.0, "h": 4.0, "slots": 64}, CASE),
        Part("panel", {"x": X0 - 4.0, "y": Y0 - 4.0, "w": 13.5 * U + 8.0, "h": 5 * U + 8.0, "r": 1.2}, "#141517"),
        Part("panel", {"x": X0 - 0.6, "y": Y0 - 0.6, "w": 13.5 * U + 1.2, "h": 5 * U + 1.2, "r": 0.6}, "#4a4a47"),
        # the name plate: hollow oblique letters spaced out, underlined, the Omega underlined too, a plain 128
        Part("panel", {"x": 33.0, "y": 8.0, "w": 76.0, "h": 9.5, "r": 0.6}, "#141517"),
        Part("logo", {"text": "ZX-SPECTR", "x": 41.5, "y": 14.6, "width": 34.0, "size": 5.0, "spacing": 1.0,
                      "face": "oblique-bold", "stroke": 0.18}, LOGO),
        Part("stripe", {"points": [(41.0, 15.4), (75.5, 15.4), (75.5, 15.75), (41.0, 15.75)]}, LOGO),
        Part("logo", {"text": "Ω", "x": 81.5, "y": 14.6, "size": 5.0, "face": "oblique-bold", "stroke": 0.18}, LOGO),
        Part("stripe", {"points": [(81.0, 15.4), (85.6, 15.4), (85.6, 15.75), (81.0, 15.75)]}, LOGO),
        Part("logo", {"text": "128", "x": 90.0, "y": 14.6, "width": 8.0, "size": 4.6, "face": "sans"}, LOGO),
        Part("led", {"x": 329.0, "y": 12.8, "r": 1.0}, "#1a0c0c"),
    ]
    for r, row in enumerate(ROWS):
        for name, x, w in row:
            rect = (X0 + x * U, Y0 + r * U, w * U, U)
            shape = None
            if name == "ENTER":
                rect, shape = (X0 + 11.75 * U, Y0 + U, 1.75 * U, 2 * U), enter_shape()
            colour = RED if name == "BREAK" else CREAM if name in CREAM_KEYS else GREY
            zx = EXTRA.get(name) or [SINGLE.get(name, name)]
            m.keys.append(Key(name.lower().replace(" ", "-"), rect, "soviet", colour, legends(name), zx=zx,
                              shape=shape, label=name.title() if len(name) > 1 else name))
    return m
