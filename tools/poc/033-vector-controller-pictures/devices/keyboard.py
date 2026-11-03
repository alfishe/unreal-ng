"""The generic PC keyboard: a full-size 104-key ANSI board seen from straight above.

The picture stands for any keyboard the controllers dialog shows (device-database.md §6); it carries no maker's
details. The geometry is the standard one: keys on a 19.05 mm (0.75 in) pitch, the 15-unit main block, the
navigation block and the keypad each set a quarter unit apart, the function row half a unit above the number row and
split 1 + 1 / 4 / 4 / 4. A key is drawn as its cap: the skirt at about 18 mm and the dished top about 5 mm narrower,
pushed a little to the back as on a sculpted profile. The shifted characters stand above the plain ones as on most
boards. Replaces the line drawing of `tools/controllers/make_fallback_images.py`; the control names (`key.<PcKey>`),
the labels and the quiet hotspots are kept.
"""

import style
from model import H, W, Picture
from svgkit import circle_d, rounded_d, shade

U = 19.05  # the key pitch
MAIN = [
    [("esc", "Esc", 1), (None, 1), ("f1", "F1", 1), ("f2", "F2", 1), ("f3", "F3", 1), ("f4", "F4", 1), (None, 0.5),
     ("f5", "F5", 1), ("f6", "F6", 1), ("f7", "F7", 1), ("f8", "F8", 1), (None, 0.5), ("f9", "F9", 1),
     ("f10", "F10", 1), ("f11", "F11", 1), ("f12", "F12", 1)],
    [("backquote", "`", 1), ("1", "1", 1), ("2", "2", 1), ("3", "3", 1), ("4", "4", 1), ("5", "5", 1),
     ("6", "6", 1), ("7", "7", 1), ("8", "8", 1), ("9", "9", 1), ("0", "0", 1), ("minus", "-", 1),
     ("equal", "=", 1), ("backspace", "Backspace", 2)],
    [("tab", "Tab", 1.5), ("q", "Q", 1), ("w", "W", 1), ("e", "E", 1), ("r", "R", 1), ("t", "T", 1),
     ("y", "Y", 1), ("u", "U", 1), ("i", "I", 1), ("o", "O", 1), ("p", "P", 1), ("lbracket", "[", 1),
     ("rbracket", "]", 1), ("backslash", "\\", 1.5)],
    [("capslock", "Caps", 1.75), ("a", "A", 1), ("s", "S", 1), ("d", "D", 1), ("f", "F", 1), ("g", "G", 1),
     ("h", "H", 1), ("j", "J", 1), ("k", "K", 1), ("l", "L", 1), ("semicolon", ";", 1), ("quote", "'", 1),
     ("enter", "Enter", 2.25)],
    [("lshift", "Shift", 2.25), ("z", "Z", 1), ("x", "X", 1), ("c", "C", 1), ("v", "V", 1), ("b", "B", 1),
     ("n", "N", 1), ("m", "M", 1), ("comma", ",", 1), ("period", ".", 1), ("slash", "/", 1),
     ("rshift", "Shift", 2.75)],
    [("lctrl", "Ctrl", 1.25), ("lgui", "Win", 1.25), ("lalt", "Alt", 1.25), ("space", "", 6.25),
     ("ralt", "Alt", 1.25), ("rgui", "Win", 1.25), ("menu", "Menu", 1.25), ("rctrl", "Ctrl", 1.25)],
]
# The navigation block (x from its left, y in rows) and the keypad (x, y, w, h in key units)
NAV = [("printscreen", "PrtSc", 0, 0), ("scrolllock", "ScrLk", 1, 0), ("pause", "Pause", 2, 0),
       ("insert", "Ins", 0, 1), ("home", "Home", 1, 1), ("pageup", "PgUp", 2, 1),
       ("delete", "Del", 0, 2), ("end", "End", 1, 2), ("pagedown", "PgDn", 2, 2),
       ("up", "↑", 1, 4), ("left", "←", 0, 5), ("down", "↓", 1, 5), ("right", "→", 2, 5)]
PAD = [("numlock", "Num", 0, 1, 1, 1), ("kp_divide", "/", 1, 1, 1, 1), ("kp_multiply", "*", 2, 1, 1, 1),
       ("kp_minus", "-", 3, 1, 1, 1), ("kp_7", "7", 0, 2, 1, 1), ("kp_8", "8", 1, 2, 1, 1),
       ("kp_9", "9", 2, 2, 1, 1), ("kp_plus", "+", 3, 2, 1, 2), ("kp_4", "4", 0, 3, 1, 1),
       ("kp_5", "5", 1, 3, 1, 1), ("kp_6", "6", 2, 3, 1, 1), ("kp_1", "1", 0, 4, 1, 1),
       ("kp_2", "2", 1, 4, 1, 1), ("kp_3", "3", 2, 4, 1, 1), ("kp_enter", "Enter", 3, 4, 1, 2),
       ("kp_0", "0", 0, 5, 2, 1), ("kp_decimal", ".", 2, 5, 1, 1)]
SHIFTED = {"backquote": "~", "1": "!", "2": "@", "3": "#", "4": "$", "5": "%", "6": "^", "7": "&", "8": "*",
           "9": "(", "0": ")", "minus": "_", "equal": "+", "lbracket": "{", "rbracket": "}", "backslash": "|",
           "semicolon": ":", "quote": "\"", "comma": "<", "period": ">", "slash": "?"}
ARROWS = {"up": "up", "down": "down", "left": "left", "right": "right"}
NAV_X, PAD_X = 15.25, 18.5  # where the navigation block and the keypad start, in units from the left
WIDTH, ROWS = 22.5, 6.5  # the key field in units
BEZEL, BEZEL_TOP = 9.0, 24.0  # the case around the key field; the top holds the lock lights
COLORS = {"case": "#2b2c2f", "well": "#1b1c1e", "cap": "#3a3c41", "legend": "#c3c6cc", "light": "#46484c"}


def cap(view, control, label, x, y, w, h, colors):
    """One key at (x, y) mm of its cell's top left, @p w x @p h units: the skirt, the top, the legends"""
    pw, ph = w * U, h * U
    skirt_d = rounded_d(x + 0.55, y + 0.55, pw - 1.1, ph - 1.1, 1.4)
    view.add("controls", f'<path d="{rounded_d(x + 0.35, y + 0.75, pw - 0.7, ph - 0.9, 1.6)}" fill="{style.GAP}" '
                         f'fill-opacity="0.6"/>', control)
    skirt = view.defs.linear([(0, shade(colors["cap"], 0.9)), (1, shade(colors["cap"], 0.76))])
    view.add("controls", f'<path d="{skirt_d}" fill="{skirt}" stroke="{shade(colors["cap"], 1.12)}" '
                         f'stroke-width="0.18"/>', control)
    tx, ty, tw, th = x + 3.0, y + 2.2, pw - 6.0, ph - 6.2
    top = view.defs.linear([(0, shade(colors["cap"], 1.05)), (1, shade(colors["cap"], 0.97))])
    view.add("controls", f'<path d="{rounded_d(tx, ty, tw, th, 2.0)}" fill="{top}" '
                         f'stroke="{shade(colors["cap"], 1.18)}" stroke-width="0.14"/>', control)
    name = control.split(".", 1)[1]
    cx, cy = tx + tw / 2, ty + th / 2
    if name in ARROWS:
        style.glyph(view, style.arrow_d(cx, cy, 3.4, ARROWS[name]), colors["legend"], control, fill=True,
                    opacity=0.85)
    elif name in SHIFTED:
        style.label(view, SHIFTED[name], cx, cy - 2.6, 3.0, colors["legend"], control, face="sans", opacity=0.6)
        style.label(view, label, cx, cy + 2.4, 3.4, colors["legend"], control, face="sans", opacity=0.85)
    elif len(label) == 1:
        style.label(view, label, cx, cy, 4.0, colors["legend"], control, face="sans", opacity=0.85)
    elif label:
        style.label(view, label, cx, cy, 2.5 if len(label) <= 5 else 2.2, colors["legend"], control, face="sans",
                    opacity=0.8)
    view.spot(control, callout_name(name, label), ("rect", x + 0.55, y + 0.55, pw - 1.1, ph - 1.1),
              "left" if x + pw / 2 < 0 else "right", quiet=True)


def callout_name(name, label):
    """The callout of a key: its legend, the keypad's ("Num 1") and the two-sided modifiers' ("Left Shift") with their
    place, as the main block and the keypad repeat legends"""
    if name.startswith("kp_"):
        return f"Num {label}"
    if name[0] in "lr" and name[1:] in ("shift", "ctrl", "alt", "gui"):
        return f"{'Left' if name[0] == 'l' else 'Right'} {label}"
    return label or "Space"


def keyboard():
    pic = Picture("generic-keyboard", "Generic PC keyboard, 104 keys ANSI")
    pic.sources = [
        {"what": "the key pitch 19.05 mm and the 104-key ANSI layout: the main block of 15 units, the navigation "
                 "block and the keypad a quarter unit apart, the key widths of the bottom rows",
         "source": "https://en.wikipedia.org/wiki/Keyboard_layout#Physical_layouts and ISO/IEC 9995-1",
         "retrieved": "2026-11-01"},
        {"what": "the cap: about 18 mm at the skirt, the top about 5 mm narrower and set back on a sculpted profile",
         "source": "https://deskthority.net/wiki/Keycap", "retrieved": "2026-11-01"},
    ]
    colors = COLORS
    kw, kh = WIDTH * U, ROWS * U
    case_w, case_h = kw + 2 * BEZEL, kh + BEZEL + BEZEL_TOP
    scale = 1880 / case_w
    view = pic.view("front", ox=W / 2, oy=H / 2 + (BEZEL_TOP - BEZEL) / 2 * scale, scale=scale)
    left, top = -kw / 2, -kh / 2

    # the cable leaves the back edge in the middle
    view.add("shoulders", f'<path d="{rounded_d(-2.2, top - BEZEL_TOP - 16, 4.4, 20, 2.2)}" '
                          f'fill="{shade(colors["case"], 0.8)}"/>')
    view.add("shoulders", f'<path d="{rounded_d(-3.6, top - BEZEL_TOP - 4.5, 7.2, 6, 1.4)}" '
                          f'fill="{shade(colors["case"], 0.9)}"/>')
    case_d = rounded_d(left - BEZEL, top - BEZEL_TOP, case_w, case_h, 7)
    style.body(view, case_d, colors["case"], width=0.4)
    style.sheen(view, case_d, cy=0.1, r=0.9, opacity=0.04)

    # the wells the key blocks sit in: the plate shows between the caps
    blocks = [(0, 0, 1, 1), (2, 0, 4, 1), (6.5, 0, 4, 1), (11, 0, 4, 1), (0, 1.5, 15, 5), (NAV_X, 0, 3, 1),
              (NAV_X, 1.5, 3, 2), (NAV_X + 1, 4.5, 1, 1), (NAV_X, 5.5, 3, 1), (PAD_X, 1.5, 4, 5)]
    for bx, by, bw, bh in blocks:
        well_d = rounded_d(left + bx * U - 0.4, top + by * U - 0.4, bw * U + 0.8, bh * U + 0.8, 1.8)
        view.add("case", f'<path d="{well_d}" fill="{colors["well"]}" stroke="{shade(colors["case"], 0.7)}" '
                         f'stroke-width="0.25"/>')

    # the lock lights above the keypad, off
    for i, text in enumerate(("Num Lock", "Caps Lock", "Scroll Lock")):
        cx = left + (PAD_X + 0.5 + i * 1.5) * U
        view.add("case", f'<path d="{circle_d(cx, top - 11.5, 1.1)}" fill="{colors["light"]}" '
                         f'stroke="{shade(colors["case"], 0.6)}" stroke-width="0.2"/>')
        style.label(view, text, cx, top - 6.8, 1.9, colors["legend"], None, face="sans", opacity=0.45)

    for row, keys in enumerate(MAIN):
        x = 0.0
        y = row + (0 if row == 0 else 0.5)  # the function row stands apart
        for name, *rest in keys:
            if name is None:
                x += rest[0]
                continue
            label, w = rest
            cap(view, f"key.{name}", label, left + x * U, top + y * U, w, 1, colors)
            x += w
    for name, label, x, y in NAV:
        cap(view, f"key.{name}", label, left + (NAV_X + x) * U, top + (y + (0 if y == 0 else 0.5)) * U, 1, 1, colors)
    for name, label, x, y, w, h in PAD:
        cap(view, f"key.{name}", label, left + (PAD_X + x) * U, top + (y + 0.5) * U, w, h, colors)
    return pic


SETS = {"generic-keyboard": keyboard}
