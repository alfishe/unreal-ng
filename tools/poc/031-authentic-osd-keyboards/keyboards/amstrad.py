"""The Amstrad Spectrums (+2 grey, +2A black, +3 black): the 128's 58-key layout under sculpted typewriter caps with
white italic legends, the long slab case with castellated edges, the deck on the right (the Datacorder or the disc
drive). Not a keyboard itself: zxplus2.py, zxplus2a.py and zxplus3.py build theirs from it.

Sources: references/sinclair.md §2.3 (the geometry, the same as the 128's), §4-6 (the cases, the caps, the legends);
the positions and the colours measured on the owner's straight-down photo references/photos/sinclair/plus2-owner-top.png
(3.42 px/mm both ways), the colours checked on the neutral-light plus2-owner-top2.png (plus2-top.jpg is shot from the
front and its light is yellow: not for colours or depths).
"""

from keyboards.zx128 import EXTRA, ROWS, SINGLE
from model import Key, Legend, Model, Part

SIZE = [440, 171]
U = 19.05                # key pitch, mm
X0, Y0 = 18.0, 63.5      # the key block's top left
INK = "#f4f4f4"
RED = "#b2352c"
ITALIC = "bold-italic"
RAINBOW = ["#c8452c", "#d88a2e", "#cfc13a", "#4f9a48", "#3a68b8"]

# The symbol shown above the main character (refs §4.2): only these, the 128's SS keywords are gone
SYMBOLS = {"1": "!", "2": "@", "3": "#", "4": "$", "5": "%", "6": "&", "7": "'", "8": "(", "9": ")", "0": "_",
           "R": "<", "T": ">", "H": "↑", "J": "-", "K": "+", "L": "=", "Z": ":", "X": "£", "C": "?", "V": "/",
           "B": "*"}
KEYWORDS = {"R": "RUN", "J": "LOAD", "I": "CODE"}  # the three keys that keep a keyword
NAMED = {"TRUE VIDEO": ["TRUE", "VIDEO"], "INV VIDEO": ["INV", "VIDEO"], "BREAK": ["BREAK"], "DELETE": ["DELETE"],
         "GRAPH": ["GRAPH"], "EXTEND MODE": ["EXTEND MODE"], "EDIT": ["EDIT"], "CAPS LOCK": ["CAPS", "LOCK"],
         "SYMBOL SHIFT": ["SYMB", "SHIFT"], "SYMBOL SHIFT 2": ["SYMB", "SHIFT"]}
CHEVRONS = {"UP": "up", "DOWN": "down", "LEFT": "left", "RIGHT": "right"}
TAPE = ["dot", "chevron:right", "chevron2:left", "chevron2:right", "eject", "pause"]  # on the +2's buttons
TAPE_PRINTED = ["dot", "tri:right", "tri2:left", "tri2:right", "eject:fill", "pause:fill"]  # the +2A's, on the case


def legends(name):
    if name in SYMBOLS or name in KEYWORDS or (len(name) == 1 and name.isalnum()):
        result = [Legend("Ø" if name == "0" else name, "main", "cap", 0.42, 0.9, 4.6, ITALIC, INK)]
        if name in SYMBOLS:
            result.append(Legend(SYMBOLS[name], "symbol", "cap", 0.46, 0.42, 3.1, ITALIC, INK))
        if name in KEYWORDS:  # beside the symbol, or alone over the letter (CODE)
            x = 0.56 if name in SYMBOLS else 0.3
            result.append(Legend(KEYWORDS[name], "keyword", "cap", x, 0.36, 2.0, ITALIC, INK, anchor="start"))
        return result
    if name in NAMED:
        lines = NAMED[name]
        centred = name == "CAPS LOCK"
        ys = [0.8] if len(lines) == 1 else [0.5, 0.82]
        return [Legend(t, "main", "cap", 0.5 if centred else 0.1, y, 2.2, ITALIC, INK,
                       anchor="middle" if centred else "start", squeeze=0.95) for t, y in zip(lines, ys)]
    if name.startswith("CAPS SHIFT"):
        return [Legend("CAPS SHIFT", "main", "cap", 0.08, 0.76, 2.7, ITALIC, INK, anchor="start")]
    if name == "ENTER":  # on the foot, the return hook after it
        return [Legend("ENTER", "main", "cap", 0.1, 0.8, 2.3, ITALIC, INK, anchor="start"),
                Legend("", "main", "cap", 0.9, 0.8, 4.2, color=INK, glyph="hook")]
    if name in CHEVRONS:
        return [Legend("", "main", "cap", 0.5, 0.55, 3.6, color=INK, glyph=f"chevron:{CHEVRONS[name]}")]
    if name in ';",.':
        return [Legend(name, "main", "cap", 0.4, 0.82, 3.8, ITALIC, INK)]
    return []  # SPACE: blank


def enter_shape():
    x1, x2, x3 = X0 + 11.75 * U, X0 + 12.5 * U, X0 + 13.5 * U
    y1, y2, y3 = Y0 + 1 * U, Y0 + 2 * U, Y0 + 3 * U
    return [(x2, y1), (x3, y1), (x3, y3), (x1, y3), (x1, y2), (x2, y2)]


def case(m, colour, badge, deck):
    """The slab: the castellated rear, the raised rear strip with its groove lines under the badges, the
    grooves across the margins at the row lines, the keyboard's opening; @p badge the model's name on the strip,
    @p deck the parts on the right"""
    W, H = SIZE
    right = X0 + 13.5 * U
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 2.5, "matte": True}, colour),
        Part("hump", {"x": 0, "y": 0, "w": W, "h": 38.0, "r": 2.5}, colour),
        Part("teeth", {"x": 3.0, "y": 0, "w": W - 6.0, "h": 1.6, "pitch": 3.7}, "#0b0b0b"),
        Part("groove", {"x": 16.0, "y": 26.6, "w": 260.0, "h": 0.8}, colour),
        Part("groove", {"x": 300.0, "y": 26.6, "w": 123.0, "h": 0.8}, colour),
        Part("groove", {"x": 1.2, "y": 46.0, "w": W - 2.4, "h": 0.8}, colour),  # the ledge in front of the strip
    ]
    for k in range(0, 6):  # the grooves run on the row lines across both margins
        y = Y0 + k * U - 0.4
        m.case.append(Part("groove", {"x": 1.2, "y": y, "w": X0 - 2.2, "h": 0.8}, colour))
        m.case.append(Part("groove", {"x": right + 1.0, "y": y, "w": W - right - 2.2, "h": 0.8}, colour))
    m.case += [  # the margin blocks are cut off from the key block's right side
        Part("groove", {"x": right + 0.6, "y": 46.4, "w": 0.8, "h": Y0 + 5 * U - 46.4}, colour),
        Part("well", {"x": X0 - 0.4, "y": Y0 - 0.4, "w": 13.5 * U + 0.8, "h": 5 * U + 0.8, "r": 0.6}, "#202122"),
        Part("logo", {"text": "sinclair", "x": 16.0, "y": 22.0, "width": 69.0, "size": 12.0, "spacing": -1.0,
                      "face": "black"}, RED),
        Part("logo", {"text": "128K", "x": 252.0, "y": 22.0, "width": 24.0, "size": 9.6, "face": ITALIC}, RED),
        Part("tab", {"x": 286.5, "y": 26.4, "w": 4.4, "h": 1.5, "r": 0.3}, "#5a1410"),  # the power LED
    ]
    m.case += badge
    for part in deck:  # the lid is the case's plastic
        part.color = part.color or colour
    m.case += deck


def keys(m, colour):
    for r, row in enumerate(ROWS):
        for name, x, w in row:
            rect = (X0 + x * U, Y0 + r * U, w * U, U)
            shape = None
            if name == "ENTER":
                rect, shape = (X0 + 11.75 * U, Y0 + U, 1.75 * U, 2 * U), enter_shape()
            ident = name.lower().replace(" ", "-")
            zx = EXTRA.get(name) or [SINGLE.get(name, name)]
            m.keys.append(Key(ident, rect, "amstrad", colour, legends(name), zx=zx, shape=shape,
                              label=name.title() if len(name) > 1 else name))


def datacorder(button_colour, on_case, inside=None, plate=None):
    """The deck of the +2 / +2A, its six buttons below it; @p inside the well's plastic, @p plate the one between the
    spindles (the black +2A's are light grey)"""
    return [
        Part("datacorder", {"x": 300.0, "y": 41.0, "w": 121.0, "h": 68.0, "window": (314.0, 46.8, 92.0, 34.0),
                            "band": (302.5, 84.0, 117.0, 22.5), "spindles": (32.0, 60.0), "label": (41.0, 18.0),
                            "rainbow": RAINBOW, "text": "DATACORDER", "mic": (351.0, 116.4),
                            **({"inside": inside} if inside else {}), **({"plate": plate} if plate else {})}, None),
        Part("buttons", {"x": 308.5, "y": 137.0, "w": 16.6, "h": 20.5, "gap": 0.7, "symbols": TAPE_PRINTED if on_case else TAPE,
                         "on_case": on_case}, button_colour),
    ]


def disc_panel(colour):
    """The +3's label panel where the +2 has its Datacorder (the drive itself is in the front face, out of view)"""
    return [Part("fddpanel", {"x": 299.0, "y": 58.0, "w": 104.0, "h": 82.0, "size": 28.0, "ink": "#e0522c",
                              "rainbow": ["#d8392a", "#e3832a", "#dcc832", "#46a046", "#2fa8d8", "#2f66c6"]}, colour)]


def build(case_colour, key_colour, badge, deck):
    m = Model()
    case(m, case_colour, badge, deck)
    keys(m, key_colour)
    return m
