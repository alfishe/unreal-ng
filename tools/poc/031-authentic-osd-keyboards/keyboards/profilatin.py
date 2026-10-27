"""Profi with the Soviet 58-key keyboard of the Pentagon 2+ (keyboards/pentagon2plus.py): the same sculpted caps and
the same Spectrum print, Latin only (the SS item and the E + SS item on top, the letter and its keyword, the E-mode
word on the front), ivory caps, grey modifiers, a red BREAK, the grey L-shaped return; in a black case of its own: the
keys tight in a flat dark frame, plain case surface left of them, the raised rear band with the "Profi®" mark (light
grey bold italic and four slanted stripes, red, orange, yellow and cyan, on a dark plate) and a palm rest in front.

Sources: the owner's photo references/photos/clones/profi-keyboard-top-owner.jpg (2026-10-21, a straight top view of
the keyboard and the case's left rear corner): the print read key by key (it is the Pentagon 2+'s, ":" and ",," on the
two punctuation keys included), the colours sampled (the photo is cool and dark: lifted to neutral), the case parts
measured against the key block's 13.5 u (2.3 px/mm). UNVERIFIED: everything the photo does not show (the case's
right, front and rear edges, its size, the palm rest's depth: drawn as the BK-0010 case's proportions), the mark's
inks (lifted from an underexposed photo), which Profi version and case this is (refs-clones §6.1).
"""

from keyboards import pentagon2plus
from model import Model, Part

U = pentagon2plus.U
X0, Y0 = 62.0, 50.0      # the key block's top left
KEYBOARD = {"id": "profilatin", "name": "Profi (Spectrum print)", "models": ["PROFI", "PROFI3"],
            "size": [X0 + 13.5 * U + 20.0, Y0 + 5 * U + 42.0], "matrix": "zx", "twins": {"CS": 2, "SS": 2}}

CASE = "#25292d"
IVORY, GREY, RED = "#e6e2cf", "#a6a8a6", "#c23a31"


def mark(x, y):
    """The "Profi®" mark on its dark plate, the plate's top left at @p x, @p y: the word 30 mm long, the four stripes
    2.6 mm each, leaning 7 mm over their 12 mm"""
    parts = [Part("panel", {"x": x, "y": y, "w": 50.0, "h": 18.7, "r": 0.4}, "#1f2225"),
             Part("logo", {"text": "Profi", "x": x + 0.6, "y": y + 16.6, "width": 30.0, "face": "bold-italic"},
                  "#c9cac4"),
             Part("logo", {"text": "®", "x": x + 30.4, "y": y + 6.6, "size": 3.2, "face": "sans"}, "#c9cac4")]
    for i, colour in enumerate(["#cf4a35", "#dc9530", "#d9c24a", "#86c4b4"]):
        sx = x + 31.8 + 2.6 * i
        parts.append(Part("stripe", {"points": [(sx, y + 17.6), (sx + 2.6, y + 17.6), (sx + 9.6, y + 5.6),
                                                (sx + 7.0, y + 5.6)]}, colour))
    return parts


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    bottom = Y0 + 5 * U
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 2.5, "matte": True}, CASE),
        Part("steps", {"x": 1.5, "y": Y0 - 12.5, "w": W - 3.0, "h": 3.5, "steps": 1}, CASE),  # the rear band's edge
        Part("steps", {"x": 1.5, "y": bottom + 9.0, "w": W - 3.0, "h": 3.5, "steps": 1}, CASE),  # the palm rest's
        Part("panel", {"x": X0 - 3.0, "y": Y0 - 4.0, "w": 13.5 * U + 6.0, "h": 5 * U + 7.0, "r": 0.6}, "#33383b"),
    ]
    m.case += mark(X0 - 53.5, Y0 - 38.7)
    pentagon2plus.keys(m, X0, Y0, grey=GREY, cream=IVORY, red=RED)
    return m
