"""The IBM PC/XT 83-key keyboard (Model F, 1981) and its clones: the XT firmware of the first ATM-Turbo 2+ boards,
the KAY's XT keyboard controller. F1-F10 in two columns at the left, no separate cursor or editing block (the keypad
doubles as both under Num Lock), the tall Enter, the symbols for Tab, Shift, Backspace and Enter, no lock LEDs; the
pebble-beige case with its broad raised top, light caps for the letters, digits, space and keypad digits, darker grey
for the rest; Cyrillic red as on the clone keyboards.

Sources: refs-pc §2 (the ATM-Turbo 2+'s first firmware v1.06 reads an XT keyboard), refs-clones §3 (the KAY's XT
keyboard controller), refs-pc §7.1: the geometry and the colours from references/photos/pc/xt-83-ibm-model-f-top.jpg
(the German XT, the US positions of \\ and ` from the US layout). No brand (the IBM badge is left out). UNVERIFIED:
the US 83-key positions of \\ and `, the widths within ±0.25 u, the Cyrillic (an XT clone sold in Russia, not the IBM).
"""

from keyboards import pc
from keyboards.pc import U, letters
from model import Model, Part

KEYBOARD = {"id": "pcxt", "name": "PC/XT 83-key (Russian)", "models": ["ATM710", "KAY"], "size": [485, 180],
            "matrix": "pc", "depth": U,
            "soviet": {"side": 0.13, "back": 0.06, "front": 0.22, "tones": ((0.9, 0.9, 0.89), (0.79, 0.79, 0.78),
                                                                           (0.86, 0.86, 0.85)),
                       "shadow": 0.25, "outline": 0.16, "rim": 0.3}}

CASE, LIGHT, DARK = "#d8d7cc", "#e5e4db", "#b4b3a9"
INK, RED = "#26262a", "#cc3a30"
X0, Y0 = 29.0, 51.0      # the key area's top left (F1's)
M, K = 2.5, 17.75        # the main block's and the keypad's left, in u from F1
NAMES = {"Tab": "glyph:tab", "LeftShift": "glyph:arrow:up", "RightShift": "glyph:arrow:up",
         "Backspace": "glyph:arrow:left", "Enter": "glyph:enter",
         "KeypadMultiply": ["PrtSc", "*"], "CapsLock": ["Caps", "Lock"], "LeftAlt": ["Alt"], "LeftCtrl": ["Ctrl"],
         "NumLock": ["Num Lock"], "ScrollLock": ["Scroll Lock"]}
DARKER = {"Escape", "Tab", "LeftCtrl", "LeftShift", "RightShift", "LeftAlt", "CapsLock", "Backspace", "Enter",
          "NumLock", "ScrollLock", "KeypadMinus", "KeypadPlus", "KeypadMultiply"} | {f"Function{i}" for i in
                                                                                       range(1, 11)}


def layout():
    rows = [(f"Function{2 * r + c + 1}", c, r, 1, 1) for r in range(5) for c in range(2)]
    rows += [("Escape", M, 0, 1, 1)] + [(f"Digit{d}", M + 1 + i, 0, 1, 1) for i, d in enumerate("1234567890")]
    rows += [("Minus", M + 11, 0, 1, 1), ("Equal", M + 12, 0, 1, 1), ("Backspace", M + 13, 0, 2, 1)]
    rows += [("Tab", M, 1, 1.5, 1)] + letters("QWERTYUIOP", M + 1.5, 1)
    rows += [("LeftBracket", M + 11.5, 1, 1, 1), ("RightBracket", M + 12.5, 1, 1, 1), ("Enter", M + 13.75, 1, 1.25, 2)]
    rows += [("LeftCtrl", M, 2, 1.75, 1)] + letters("ASDFGHJKL", M + 1.75, 2)
    rows += [("Semicolon", M + 10.75, 2, 1, 1), ("Quote", M + 11.75, 2, 1, 1), ("Backquote", M + 12.75, 2, 1, 1)]
    rows += [("LeftShift", M, 3, 1.25, 1), ("Backslash", M + 1.25, 3, 1, 1)] + letters("ZXCVBNM", M + 2.25, 3)
    rows += [("Comma", M + 9.25, 3, 1, 1), ("Period", M + 10.25, 3, 1, 1), ("Slash", M + 11.25, 3, 1, 1),
             ("RightShift", M + 12.25, 3, 1.5, 1), ("KeypadMultiply", M + 13.75, 3, 1.25, 1)]
    rows += [("LeftAlt", M, 4, 1.75, 1), ("Space", M + 1.75, 4, 10, 1), ("CapsLock", M + 11.75, 4, 2, 1)]
    rows += [("NumLock", K, 0, 2, 1), ("ScrollLock", K + 2, 0, 2, 1)]
    for r, digits in enumerate(["789", "456", "123"]):
        rows += [(f"Keypad{d}", K + i, 1 + r, 1, 1) for i, d in enumerate(digits)]
    rows += [("KeypadMinus", K + 3, 1, 1, 1), ("KeypadPlus", K + 3, 2, 1, 2), ("Keypad0", K, 4, 2, 1),
             ("KeypadDecimal", K + 2, 4, 2, 1)]

    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 6.0, "matte": True}, CASE),
               Part("steps", {"x": 3.0, "y": 40.0, "w": W - 6.0, "h": 4.0, "steps": 1}, CASE)]  # the raised top's edge
    for bx, bw in [(0, 2), (M, K + 4 - M)]:  # the two wells the keys stand in
        m.case.append(Part("panel", {"x": X0 + bx * U - 1.2, "y": Y0 - 1.2, "w": bw * U + 2.4, "h": 5 * U + 2.4,
                                     "r": 1.0}, "#c4c3b8"))
    pc.keys(m, rows, X0, Y0, LIGHT, INK, RED, DARKER, DARK, NAMES)
    return m
