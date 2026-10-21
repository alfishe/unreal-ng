"""The PS/2 keyboard of the ZX-Evo era (2009 on): a 104-key Russian clone keyboard, the off-white case and caps all
one colour, Latin black at the top left, Cyrillic red at the bottom right, the "big-ass" L-shaped Enter with \\ beside
a 1u Backspace, the Windows and menu keys, the three lock LEDs top right.

Sources: refs-pc §3, §4, §5, §7.3: the ZX-Evo and TS-Conf read a PS/2 keyboard through the board's AVR, the ZXM-Phoenix
through Caro's ZX Multi Card 2; the look from references/photos/pc/russian-ps2-genius-cyrillic-legends.jpg (a beige
Genius, red Cyrillic, L Enter). The geometry is the 104-key ANSI layout (keyboard-layout-editor "ANSI 104 (big-ass
enter)"); the function row lower than the others and close to the digits as on the photo. No brand: the photo's
maker's logo, its web address and its sleep button are left out. UNVERIFIED: which keyboard any ZX-Evo owner used;
the case's size (the photo is shot from above at a slant).
"""

from keyboards import pc
from keyboards.pc import U, digit_row, letters
from model import Model, Part

KEYBOARD = {"id": "pcps2", "name": "PS/2 104-key (Russian)", "models": ["ATM3", "TSL", "PHOENIX"], "size": [449, 162],
            "matrix": "pc", "depth": U,
            # one-colour budget caps: a cylindrical top, light even sides, soft contours
            "soviet": {"side": 0.13, "back": 0.07, "front": 0.17, "tones": ((0.93, 0.93, 0.92), (0.84, 0.84, 0.83),
                                                                            (0.9, 0.9, 0.89)),
                       "shadow": 0.22, "outline": 0.14, "rim": 0.3}}

CASE, CAP = "#dedcd5", "#e9e8e3"
INK, RED = "#2c2c2e", "#d0362f"
X0, Y0 = 10.0, 28.0      # the key area's top left (Escape's)
F = 0.85                 # the function row's height in u; the main block starts 1.05 u below its top
R = 1.05


def layout():
    rows = pc.function_row(F) + digit_row(R)
    rows += [("Tab", 0, R + 1, 1.5, 1)] + letters("QWERTYUIOP", 1.5, R + 1)
    rows += [("LeftBracket", 11.5, R + 1, 1, 1), ("RightBracket", 12.5, R + 1, 1, 1), ("Enter", 13.5, R + 1, 1.5, 1,
                                                                                         12.75, 2.25)]
    rows += [("CapsLock", 0, R + 2, 1.75, 1)] + letters("ASDFGHJKL", 1.75, R + 2)
    rows += [("Semicolon", 10.75, R + 2, 1, 1), ("Quote", 11.75, R + 2, 1, 1)]
    rows += [("LeftShift", 0, R + 3, 2.25, 1)] + letters("ZXCVBNM", 2.25, R + 3)
    rows += [("Comma", 9.25, R + 3, 1, 1), ("Period", 10.25, R + 3, 1, 1), ("Slash", 11.25, R + 3, 1, 1),
             ("RightShift", 12.25, R + 3, 2.75, 1)]
    rows += [(name, i * 1.25, R + 4, 1.25, 1) for i, name in enumerate(["LeftCtrl", "LeftGui", "LeftAlt"])]
    rows += [("Space", 3.75, R + 4, 6.25, 1)]
    rows += [(name, 10 + i * 1.25, R + 4, 1.25, 1) for i, name in enumerate(["RightAlt", "RightGui", "Menu",
                                                                             "RightCtrl"])]
    rows += pc.navigation(R) + pc.keypad(R)

    m = Model()
    W, H = KEYBOARD["size"]
    m.case += [Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 7.0, "matte": True}, CASE)]
    pc.blocks(m, X0, Y0, F, R, "#cfccc4")  # each block sits in its own shallow frame
    for i, mark in enumerate(["1", "A", "⇩"]):  # the lock LEDs, off, their marks over them
        x = X0 + (18.9 + i * 1.3) * U
        m.case += [Part("led", {"x": x, "y": 18.0, "r": 0.9}, "#3d5a3b"),
                   Part("logo", {"text": mark, "x": x, "y": 13.6, "size": 2.4, "face": "sans", "anchor": "middle"},
                        "#55534e")]
    pc.keys(m, rows, X0, Y0, CAP, INK, RED)
    return m
