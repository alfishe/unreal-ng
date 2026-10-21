"""The PC keyboards of the PC-keyboard clones (refs-pc §7): the layouts as key lists in units, the Russian legends of
the clone keyboards, the caps; each keyboard module (pcxt, pcat, pcps2) picks its layout, colours and case.

A layout is a list of (PcKey name, x, y, w, h) in units of U from the top left of the key area; an L-shaped Enter
is ("Enter", x, y, w, h, x_bottom, w_bottom): the top bar at x, w on row y, the wider bottom bar on row y + 1. Every
key is a `PcKey` of the emulator (core/src/emulator/io/keyboard/pckey.h) by name.
"""

from model import Key, Legend, Part

U = 19.05

# The Russian ЙЦУКЕН legends of the 1990s clone keyboards (refs-pc §7.3, the Genius photo)
CYRILLIC = dict(zip("QWERTYUIOPASDFGHJKLZXCVBNM", "ЙЦУКЕНГШЩЗФЫВАПРОЛДЯЧСМИТЬ"))
# The punctuation keys: shifted, plain (black, left), the Russian legend (red, right)
PUNCT = {"Backquote": ("~", "`", "Ё"), "Minus": ("_", "-", ""), "Equal": ("+", "=", ""),
         "LeftBracket": ("{", "[", "Х"), "RightBracket": ("}", "]", "Ъ"), "Backslash": ("|", "\\", "/"),
         "Semicolon": (":", ";", "Ж"), "Quote": ("\"", "'", "Э"), "Comma": ("<", ",", "Б"), "Period": (">", ".", "Ю"),
         "Slash": ("?", "/", ",.")}
SHIFTED = dict(zip("1234567890", "!@#$%^&*()"))
RUSSIAN_SHIFTED = {"2": "\"", "3": "№", "4": ";", "6": ":", "7": "?"}
# The keys printed with a name: its lines
NAMES = {"Escape": ["Esc"], "Tab": ["Tab"], "CapsLock": ["Caps Lock"], "LeftShift": ["⇧ Shift"],
         "RightShift": ["⇧ Shift"], "LeftCtrl": ["Ctrl"], "RightCtrl": ["Ctrl"], "LeftAlt": ["Alt"], "RightAlt": ["Alt"],
         "Enter": ["↵ Enter"], "Backspace": ["←"], "PrintScreen": ["Print", "Screen", "SysRq"],
         "ScrollLock": ["Scroll", "Lock"], "Pause": ["Pause", "Break"], "Insert": ["Insert"], "Home": ["Home"],
         "PageUp": ["Page", "Up"], "Delete": ["Delete"], "End": ["End"], "PageDown": ["Page", "Down"],
         "NumLock": ["Num", "Lock"], "KeypadEnter": ["Enter"]}
NAMES.update({f"Function{i}": [f"F{i}"] for i in range(1, 13)})
KEYPAD = {"Keypad7": ("7", "Home"), "Keypad8": ("8", "↑"), "Keypad9": ("9", "Pg Up"), "Keypad4": ("4", "←"),
          "Keypad5": ("5", ""), "Keypad6": ("6", "→"), "Keypad1": ("1", "End"), "Keypad2": ("2", "↓"),
          "Keypad3": ("3", "Pg Dn"), "Keypad0": ("0", "Ins"), "KeypadDecimal": (".", "Del"),
          "KeypadDivide": ("/", ""), "KeypadMultiply": ("*", ""), "KeypadMinus": ("−", ""), "KeypadPlus": ("+", "")}
ARROWS = {"Up": "↑", "Down": "↓", "Left": "←", "Right": "→"}
GLYPHS = {"LeftGui": "win", "RightGui": "win", "Menu": "menu"}


def digit_row(y, backslash_here=True):
    """` 1..0 - = and, on the "big-ass Enter" boards, \\ beside a 1u Backspace"""
    keys = [("Backquote", 0, y, 1, 1)] + [(f"Digit{d}", 1 + i, y, 1, 1) for i, d in enumerate("1234567890")]
    keys += [("Minus", 11, y, 1, 1), ("Equal", 12, y, 1, 1)]
    if backslash_here:
        return keys + [("Backslash", 13, y, 1, 1), ("Backspace", 14, y, 1, 1)]
    return keys + [("Backspace", 13, y, 2, 1)]


def letters(row, x, y):
    return [(c, x + i, y, 1, 1) for i, c in enumerate(row)]


def function_row(h):
    """Esc, F1-F12 in three groups of four, Print Screen, Scroll Lock, Pause: the Enhanced keyboard's top row, @p h
    units tall"""
    keys = [("Escape", 0, 0, 1, h)] + [(f"Function{i + 1}", 2 + i + 0.5 * (i // 4), 0, 1, h) for i in range(12)]
    return keys + [("PrintScreen", 15.25, 0, 1, h), ("ScrollLock", 16.25, 0, 1, h), ("Pause", 17.25, 0, 1, h)]


def navigation(y):
    """The editing six, the inverted-T arrows: the Enhanced keyboard's middle block, its top on row @p y"""
    keys = []
    for i, (top, bottom) in enumerate([("Insert", "Delete"), ("Home", "End"), ("PageUp", "PageDown")]):
        keys += [(top, 15.25 + i, y, 1, 1), (bottom, 15.25 + i, y + 1, 1, 1)]
    return keys + [("Up", 16.25, y + 3, 1, 1), ("Left", 15.25, y + 4, 1, 1), ("Down", 16.25, y + 4, 1, 1),
                   ("Right", 17.25, y + 4, 1, 1)]


def keypad(y):
    """The numeric keypad, its top on row @p y"""
    keys = [(name, 18.5 + i, y, 1, 1) for i, name in enumerate(["NumLock", "KeypadDivide", "KeypadMultiply",
                                                               "KeypadMinus"])]
    for r, digits in enumerate(["789", "456", "123"]):
        keys += [(f"Keypad{d}", 18.5 + i, y + 1 + r, 1, 1) for i, d in enumerate(digits)]
    return keys + [("KeypadPlus", 21.5, y + 1, 1, 2), ("KeypadEnter", 21.5, y + 3, 1, 2),
                   ("Keypad0", 18.5, y + 4, 2, 1), ("KeypadDecimal", 20.5, y + 4, 1, 1)]


def blocks(m, x0, y0, fh, y, colour):
    """The Enhanced keyboard's blocks, each in its own shallow frame of @p colour: the function groups @p fh tall, the
    main block, the editing six, the arrows and the keypad from row @p y"""
    for bx, bw, by, bh in [(0, 1, 0, fh), (2, 4, 0, fh), (6.5, 4, 0, fh), (11, 4, 0, fh), (15.25, 3, 0, fh),
                           (0, 15, y, 5), (15.25, 3, y, 2), (15.25, 3, y + 3, 2), (18.5, 4, y, 5)]:
        m.case.append(Part("panel", {"x": x0 + bx * U - 0.8, "y": y0 + by * U - 0.8, "w": bw * U + 1.6,
                                     "h": bh * U + 1.6, "r": 1.2}, colour))


def legends(name, ink, red, face="sans", names=None):
    """What a key of a Russian clone keyboard carries: Latin and symbols black on the left, the Russian red on the
    right; names small, left aligned on the wide keys"""
    if names and name in names:  # the keyboard's own print first
        return named(names[name], ink, face)
    if name in CYRILLIC:
        return [Legend(name, "main", "cap", 0.12, 0.5, 5.6, face, ink, "start"),
                Legend(CYRILLIC[name], "cyrillic", "cap", 0.88, 0.95, 5.0, face, red, "end")]
    if name.startswith("Digit"):
        d = name[-1]
        result = [Legend(SHIFTED[d], "symbol", "cap", 0.12, 0.46, 4.4, face, ink, "start"),
                  Legend(d, "main", "cap", 0.12, 0.95, 4.4, face, ink, "start")]
        if d in RUSSIAN_SHIFTED:
            result.append(Legend(RUSSIAN_SHIFTED[d], "cyrillic", "cap", 0.88, 0.46, 4.4, face, red, "end"))
        return result
    if name in PUNCT:
        shifted, plain, russian = PUNCT[name]
        result = [Legend(shifted, "symbol", "cap", 0.12, 0.46, 4.4, face, ink, "start"),
                  Legend(plain, "main", "cap", 0.12, 0.95, 4.4, face, ink, "start")]
        if russian:
            result.append(Legend(russian, "cyrillic", "cap", 0.88, 0.95, 5.0, face, red, "end"))
        return result
    if name in KEYPAD:
        main, sub = KEYPAD[name]
        if not sub:
            return [Legend(main, "main", "cap", 0.5, 0.68, 5.0, face, ink)]
        return [Legend(main, "main", "cap", 0.12, 0.5, 4.8, face, ink, "start"),
                Legend(sub, "symbol", "cap", 0.12, 0.95, 2.8, face, ink, "start")]
    if name in ARROWS:
        return [Legend(ARROWS[name], "main", "cap", 0.5, 0.74, 5.4, face, ink)]
    if name in GLYPHS:
        return [Legend("", "main", "cap", 0.5, 0.5, 3.2, color=ink, glyph=GLYPHS[name])]
    if name in NAMES:
        return named(NAMES[name], ink, face, name.startswith("Function"))
    return []  # Space


def named(lines, ink, face, centred=False):
    """A key printed with its name in @p lines, or with a glyph (`glyph:<glyph>`, the XT's arrows), centred, large"""
    if isinstance(lines, str):
        return [Legend("", "main", "cap", 0.5, 0.5, 5.0, color=ink, glyph=lines.removeprefix("glyph:"))]
    size = 2.4 if len(lines) == 3 else 3.0
    ys = {1: [0.6], 2: [0.42, 0.85], 3: [0.3, 0.62, 0.94]}[len(lines)]
    symbol = len(lines) == 1 and len(lines[0]) == 1  # an arrow alone: centred, larger
    x, anchor = (0.5, "middle") if centred or len(lines) > 1 or symbol else (0.1, "start")
    return [Legend(t, "main", "cap", x, y, size * (1.4 if symbol else 1), face, ink, anchor) for t, y in zip(lines, ys)]


def keys(m, layout, x0, y0, colour, ink, red, darker=(), dark_colour=None, names=None):
    """The caps of @p layout, the key area's top left at @p x0, @p y0; the keys in @p darker take @p dark_colour,
    @p names overrides the printed names (the XT's symbols)"""
    for entry in layout:
        name, x, y, w, h = entry[:5]
        rect = (x0 + x * U, y0 + y * U, w * U, h * U)
        shape = None
        if len(entry) > 5:  # the L Enter: the top bar, then the wider bottom bar
            xb, wb = entry[5], entry[6]
            x1, x2, x3 = x0 + xb * U, x0 + x * U, x0 + (x + w) * U
            ya, yb, yc = y0 + y * U, y0 + (y + 1) * U, y0 + (y + 2) * U
            rect, shape = (x1, ya, x3 - x1, yc - ya), [(x2, ya), (x3, ya), (x3, yc), (x1, yc), (x1, yb), (x2, yb)]
        m.keys.append(Key(name.lower(), rect, "soviet", dark_colour if name in darker else colour,
                          legends(name, ink, red, names=names), pc=name, shape=shape, label=label(name)))


def label(name):
    if name.startswith("Digit"):
        return name[-1]
    if name.startswith("Function"):
        return "F" + name[8:]
    if name in PUNCT:
        return PUNCT[name][1]
    return name
