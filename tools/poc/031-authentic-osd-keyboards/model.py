"""The keyboard model: what a style draws, data only.

Coordinates are millimetres from the top left of the picture. A case is a list of parts, each a kind a style knows
how to draw (`body`, `plate`, `panel`, `stripe`, `logo`, `badge`, `grille`, `slot`...), with its geometry and its
real colour. A key is its rectangle, its shape, its cap material and colour, its legends by role and what it presses.
"""

from dataclasses import dataclass, field

# The ZX matrix: half-row port (A8..A15 low) and its five keys from bit 0
ZX_MATRIX = {
    "FEFE": ["CS", "Z", "X", "C", "V"],
    "FDFE": ["A", "S", "D", "F", "G"],
    "FBFE": ["Q", "W", "E", "R", "T"],
    "F7FE": ["1", "2", "3", "4", "5"],
    "EFFE": ["0", "9", "8", "7", "6"],
    "DFFE": ["P", "O", "I", "U", "Y"],
    "BFFE": ["ENTER", "L", "K", "J", "H"],
    "7FFE": ["SPACE", "SS", "M", "N", "B"],
}


@dataclass
class Legend:
    """A legend: @p role says what it means (main, keyword, symbol, symbol-keyword, extend, extend-shift, colour,
    graphic, cyrillic, pc), @p at where it sits relative to the key: on the cap (`cap`, with x, y in the cap's own
    0..1 box), on the whole key `cell` (x, y in the key's 0..1 box: the Next's strip above its matte top), or on the
    case `above` / `below` the key; @p size the em in mm. @p glyph draws a shape instead of text: `block:N` the ZX
    block graphic N (bits: 1 top right, 2 top left, 4 bottom right, 8 bottom left), `arrow:up|down|left|right`"""
    text: str
    role: str
    at: str = "cap"
    x: float = 0.5
    y: float = 0.5
    size: float = 2.5
    face: str = "sans-bold"
    color: str = "#ffffff"
    anchor: str = "middle"
    squeeze: float = 1.0
    swatch: str = ""  # a colour patch instead of (or behind) the text: the 48K's colour keys
    glyph: str = ""
    inverse: bool = False  # dark text in a box of the legend's colour (the Next's BLACK)


@dataclass
class Key:
    id: str
    rect: tuple            # x, y, w, h
    cap: str               # the material: rubber, hard, mech, pc
    color: str
    legends: list = field(default_factory=list)
    zx: list = None        # the matrix keys it presses: ["CS", "1"] for EDIT
    pc: str = None         # the PC key it is
    line: str = None       # outside the ZX matrix: the machine's own line as its schematic names it (the Quorum's
                           # port #7E switch codes "#7E:4Fh", NMI, RESET)
    shape: list = None     # a polygon (x, y points) for an L-shaped ENTER; None: the rectangle
    label: str = None      # the overlay's name for it


@dataclass
class Part:
    kind: str
    geometry: dict
    color: str = "#000000"
    extra: dict = field(default_factory=dict)


@dataclass
class Model:
    case: list = field(default_factory=list)
    keys: list = field(default_factory=list)
    hints: list = field(default_factory=list)  # text for the hints layer: what a combo key does

    def keymap(self, keyboard):
        return {"schema": 1, "id": keyboard["id"], "models": keyboard["models"], "size": keyboard["size"],
                "keys": [{"id": k.id, "rect": [round(v, 2) for v in k.rect], "label": k.label or k.id,
                          **({"shape": [[round(v, 2) for v in p] for p in k.shape]} if k.shape else {}),
                          **({"zx": k.zx} if k.zx else {}), **({"pc": k.pc} if k.pc else {}), **({"line": k.line} if k.line else {})} for k in self.keys]}
