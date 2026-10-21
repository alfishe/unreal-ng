"""The AT Enhanced 101-key keyboard of the 1990s (the ATM-Turbo 2+'s AT firmware, the Sprinter): the IBM Model M
geometry as its Cherry / BTC copies sold in Russia had it, a pebble-beige case with the lock LEDs in a window over
the keypad, two-tone caps (the letters, digits, space and keypad digits light, the modifiers, function, editing and
operator keys darker grey), Latin black at the top left, Cyrillic red at the bottom right.

Sources: refs-pc §2 (the ATM-Turbo 2+'s AT firmware reads an AT keyboard through its MCS-51 controller), §6 (the
Sprinter Sp2000 takes an AT 101-key keyboard on its 5-pin DIN), §7.3: the geometry (Model M: Backspace 2 u, \\ 1.5 u
over a 2.25 u Enter, Ctrl and Alt 1.5 u with a 1 u gap, Space 7 u), the two tones of the Model M and its copies, the
Russian legends as on the clone keyboards (references/photos/pc/russian-ps2-genius-cyrillic-legends.jpg). No brand.
UNVERIFIED: which keyboard any ATM or Sprinter owner used (a one-colour membrane clone was as common); the case's size
(the Model M's 492 x 210 mm, scaled to this layout's margins).
"""

from keyboards import pc
from keyboards.pc import U, digit_row, letters
from model import Model, Part

KEYBOARD = {"id": "pcat", "name": "AT Enhanced 101-key (Russian)", "models": ["ATM710", "SPRINTER"],
            "size": [470, 190], "matrix": "pc", "depth": U,
            # sculpted Model M caps: a deeper front than the budget board's, sides a little darker
            "soviet": {"side": 0.13, "back": 0.06, "front": 0.2, "tones": ((0.91, 0.91, 0.9), (0.8, 0.8, 0.79),
                                                                           (0.87, 0.87, 0.86)),
                       "shadow": 0.25, "outline": 0.16, "rim": 0.3}}

CASE, LIGHT, DARK = "#d6d2c6", "#e7e5de", "#b7b5ae"
INK, RED = "#26262a", "#cc3a30"
X0, Y0 = 20.0, 40.0      # the key area's top left (Escape's)
R = 1.5                  # the main block's top: a half-key gap under the function row
DARKER = {"Escape", "Tab", "CapsLock", "LeftShift", "RightShift", "LeftCtrl", "RightCtrl", "LeftAlt", "RightAlt",
          "Enter", "Backspace", "PrintScreen", "ScrollLock", "Pause", "Insert", "Home", "PageUp", "Delete", "End",
          "PageDown", "Up", "Down", "Left", "Right", "NumLock", "KeypadDivide", "KeypadMultiply", "KeypadMinus",
          "KeypadPlus", "KeypadEnter"} | {f"Function{i}" for i in range(1, 13)}


def layout():
    rows = pc.function_row(1) + digit_row(R, backslash_here=False)
    rows += [("Tab", 0, R + 1, 1.5, 1)] + letters("QWERTYUIOP", 1.5, R + 1)
    rows += [("LeftBracket", 11.5, R + 1, 1, 1), ("RightBracket", 12.5, R + 1, 1, 1), ("Backslash", 13.5, R + 1, 1.5, 1)]
    rows += [("CapsLock", 0, R + 2, 1.75, 1)] + letters("ASDFGHJKL", 1.75, R + 2)
    rows += [("Semicolon", 10.75, R + 2, 1, 1), ("Quote", 11.75, R + 2, 1, 1), ("Enter", 12.75, R + 2, 2.25, 1)]
    rows += [("LeftShift", 0, R + 3, 2.25, 1)] + letters("ZXCVBNM", 2.25, R + 3)
    rows += [("Comma", 9.25, R + 3, 1, 1), ("Period", 10.25, R + 3, 1, 1), ("Slash", 11.25, R + 3, 1, 1),
             ("RightShift", 12.25, R + 3, 2.75, 1)]
    rows += [("LeftCtrl", 0, R + 4, 1.5, 1), ("LeftAlt", 2.5, R + 4, 1.5, 1), ("Space", 4, R + 4, 7, 1),
             ("RightAlt", 11, R + 4, 1.5, 1), ("RightCtrl", 13.5, R + 4, 1.5, 1)]
    rows += pc.navigation(R) + pc.keypad(R)

    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 5.0, "matte": True}, CASE)]
    pc.blocks(m, X0, Y0, 1, R, "#c6c2b6")
    # the lock LEDs in a dark window over the keypad, each named under it
    wx = X0 + 18.5 * U
    m.case.append(Part("panel", {"x": wx, "y": 14.0, "w": 4 * U, "h": 15.0, "r": 1.0}, "#c9c5b9"))
    for i, name in enumerate(["Num Lock", "Caps Lock", "Scroll Lock"]):
        x = wx + (0.65 + i * 1.35) * U
        m.case += [Part("led", {"x": x, "y": 19.5, "r": 1.0}, "#2f4a2e"),
                   Part("logo", {"text": name, "x": x, "y": 26.0, "size": 2.2, "face": "sans", "anchor": "middle"},
                        "#4f4d47")]
    pc.keys(m, rows, X0, Y0, LIGHT, INK, RED, DARKER, DARK)
    return m
