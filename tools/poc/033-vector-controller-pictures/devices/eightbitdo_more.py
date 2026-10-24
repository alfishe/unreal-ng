"""8BitDo, the second round: Ultimate 2 Wireless, Lite 2, Micro, 64 Bluetooth, Arcade Stick (Switch / PC).

Sizes are 8BitDo's (product pages, "Dimension / Weight", retrieved 2026-10-24): Ultimate 2 Wireless 147 x 103 x
61.3 mm, Lite 2 120 x 65.95 x 35.45 mm, Micro 72 x 40.7 x 14.1 mm, 64 140.8 x 104 x 60.5 mm, Arcade Stick 303 x
203 x 111.5 mm. The outlines are measured as in eightbitdo.py: trace.py on 8BitDo's orthographic views (the
colourway strips cut out on a transparent ground, the 64's grey front on black), the left half mirrored about the
best symmetry axis, scaled to the maker's width; the control positions and sizes read on the same views on a 5 mm
grid and with circle and colour-blob fits (about 0.5 mm). The Ultimate 2 has the Ultimate's shell (its outline
agrees with the Ultimate 2C's and the Ultimate's within 0.5 mm at every depth), so it reuses ULTIMATE_SHELL; the
Ultimate 2C is drawn by this set too (the same shell and the same face buttons, without the profile switch and the
light rings). Wordmarks and the 8BitDo logos are left out (D5); printed letters and marks stay.
"""

import style
from devices import common
from devices.eightbitdo import ULTIMATE_SHELL, body, dpad, face, round_key, star_button, star_d, sticks
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

SITE = "https://www.8bitdo.com/"
XBOX_LETTERS = {"north": "Y", "east": "B", "south": "A", "west": "X"}

LITE2_SHELL = [("M", (0, -32.7)),
    ("C", (10.5, -32.7), (20.9, -32.6), (31.3, -32.7)),
    ("C", (36.1, -32.7), (41.1, -33.1), (45.8, -32.7)),
    ("C", (46.9, -32.6), (47.8, -32.1), (48.7, -31.7)),
    ("C", (53.0, -29.7), (57.4, -25.7), (59.1, -21.1)),
    ("C", (59.6, -19.6), (59.9, -15.2), (60.0, -13.5)),
    ("C", (60.2, -10.5), (60.0, -7.4), (60.0, -4.4)),
    ("C", (60.0, 12.3), (62.6, 25.4), (43.6, 32.0)),
    ("C", (37.4, 34.2), (0, 32.3), (0, 32.3))]
MICRO_SHELL = [("M", (0, -18.8)),
    ("C", (3.8, -18.8), (28.4, -19.3), (29.9, -18.5)),
    ("C", (32.0, -17.5), (34.3, -14.8), (35.0, -12.6)),
    ("C", (35.4, -11.4), (35.9, -8.7), (35.9, -8.1)),
    ("C", (36.3, -4.5), (36.4, 8.2), (35.3, 11.3)),
    ("C", (34.4, 14.0), (32.5, 16.4), (30.1, 17.9)),
    ("C", (28.7, 18.7), (27.2, 19.3), (25.6, 19.6)),
    ("C", (23.3, 20.0), (20.8, 19.7), (18.5, 19.7)),
    ("C", (14.5, 19.7), (4.0, 19.8), (0, 19.8))]
N64_SHELL = [("M", (0, -50.7)),
    ("C", (10.5, -50.7), (20.8, -50.7), (31.3, -50.7)),
    ("C", (36.0, -50.7), (41.1, -51.3), (45.7, -50.7)),
    ("C", (46.9, -50.5), (48.2, -49.9), (49.2, -49.3)),
    ("C", (52.0, -47.9), (50.9, -46.9), (52.5, -45.5)),
    ("C", (56.5, -41.8), (59.1, -40.8), (61.7, -35.4)),
    ("C", (67.4, -23.9), (71.3, 13.6), (70.4, 26.9)),
    ("C", (70.1, 31.1), (69.5, 35.5), (67.8, 39.4)),
    ("C", (65.7, 44.5), (61.8, 48.3), (56.6, 50.4)),
    ("C", (55.4, 50.9), (53.6, 51.9), (52.2, 51.6)),
    ("C", (51.0, 51.4), (50.1, 50.0), (49.3, 49.1)),
    ("C", (46.4, 46.1), (44.1, 42.6), (42.2, 39.0)),
    ("C", (38.8, 32.5), (36.3, 22.5), (28.9, 19.3)),
    ("C", (25.2, 17.7), (8.2, 18.3), (0, 18.3))]
# The Ultimate 2 seen from its top edge, the pad tipped away (the back towards the viewer, mirrored so the left
# hand stays left): the shell from the back with the grips' ends below
U2_TOP_SHELL = [("M", (0, -22.4)),
    ("C", (20, -22.4), (45, -22.4), (55, -22.0)),
    ("C", (64.6, -21.4), (70.6, -13.2), (73.0, -3.4)),
    ("C", (75.0, 6.0), (74.4, 21.6), (70.6, 29.4)),
    ("C", (67.6, 35.0), (59.0, 34.6), (55.0, 28.0)),
    ("C", (50.6, 20.6), (45.0, 14.4), (37.0, 14.1)),
    ("L", (0, 14.1))]


def size(what, page):
    return {"what": what, "source": SITE + page, "author": "8BitDo", "retrieved": "2026-10-24"}


def traced(what, path):
    return {"what": what, "source": SITE + "images/" + path, "author": "8BitDo", "terms": "product photo",
            "use": "measured geometry only (trace.py outline, positions on a grid); no pixels shipped",
            "retrieved": "2026-10-24"}


def seen(what, path):
    return {"what": what, "source": SITE + "images/" + path, "author": "8BitDo", "terms": "product photo",
            "use": "reference only, not traced", "retrieved": "2026-10-24"}


def estimate(what, source="this POC"):
    return {"what": what, "source": source, "note": "an estimate", "retrieved": "2026-10-24"}


def side_of(x):
    return "left" if x < 0 else "right"


def tab(view, x0, x1, top, rise, color, control=None):
    """A shoulder button seen from the front: its top, @p rise above the case's top edge from @p x0 (the inner end)
    to @p x1, rounding down at the outer end (the bumper curve of eightbitdo.top_shoulders)"""
    w = x1 - x0
    f = lambda x, y: f"{num(x)},{num(y)}"  # noqa: E731
    d = (f"M{f(x0, top + 1.2)}C{f(x0 + w * 0.1, top - rise * 0.9)} {f(x0 + w * 0.4, top - rise)} "
         f"{f(x0 + w * 0.65, top - rise * 0.96)}C{f(x0 + w * 0.88, top - rise * 0.9)} {f(x1, top - rise * 0.4)} "
         f"{f(x1 + 0.12 * w / abs(w), top + 1.6)}Z")
    style.shaped(view, d, color, control, layer="shoulders")


def key(view, x, y, w, h, color, control, label, legend=None, ink=None, size=None, side=None, r=None):
    """A rectangular key (a shoulder from the top, a back button) with its printed name"""
    style.shaped(view, rounded_d(x - w / 2, y - h / 2, w, h, r if r is not None else min(w, h) * 0.3), color,
                 control, rounded_d(x - w / 2 - 0.4, y - h / 2 - 0.4, w + 0.8, h + 0.8,
                                    (r if r is not None else min(w, h) * 0.3) + 0.4))
    if legend:
        style.label(view, legend, x, y, size or min(h * 0.42, 3.2), ink, control, opacity=0.85)
    view.spot(control, label, ("rect", x - w / 2 - 0.4, y - h / 2 - 0.4, w + 0.8, h + 0.8), side or side_of(x))


def pixel_heart_d(cx, cy, s):
    """8BitDo's pixel heart (their Home / screenshot mark) as a five-row block outline, @p s the pixel"""
    rows = [(-2, -1, 1, 2), (-3, 3), (-3, 3), (-2, 2), (-1, 1)]
    d = []
    for i, row in enumerate(rows):
        y = cy + (i - 2.5) * s
        for a, b in zip(row[::2], row[1::2]):
            d.append(rounded_d(cx + a * s, y, (b - a) * s, s, 0))
    return "".join(d)


def pixel_key(view, cx, cy, r, colors, control, label, side=None):
    style.button(view, cx, cy, r, colors["small"], control)
    style.glyph(view, pixel_heart_d(cx, cy, r * 0.2), colors["glyph"], control, fill=True, opacity=0.8)
    view.spot(control, label, ("circle", cx, cy, r + 0.4), side or side_of(cx))


# -- Ultimate 2 Wireless -----------------------------------------------------------------------------------------

def ultimate2():
    pic = Picture("8bitdo-ultimate-2", "8BitDo Ultimate 2 Wireless controller, white (front and top edge)")
    pic.sources = [
        size("size 147 x 103 x 61.3 mm, 246 g", "ultimate-2-wireless-controller/"),
        traced("front view in the dock (white): the control positions and sizes",
               "2025/ultimate-2-wireless/ultimate-2-wireless-white.png"),
        traced("front view of the Ultimate 2C (the same shell, its outline within 0.5 mm of the Ultimate's): the "
               "outline, ULTIMATE_SHELL of eightbitdo.py", "2024/ultimate-2c-wireless/purple.png"),
        seen("back-top view: LB over LT, the small L4 / R4 keys in the inner corner between them, the port",
             "2025/ultimate-2-wireless/ultimate-2-wireless-white.png"),
        seen("the printed letters (Xbox order: Y top, X left, B right, A bottom) and the button colours "
             "(mapping view)", "2025/ultimate-2-wireless/mapping.jpg"),
        estimate("the top-edge view's outline and the PL / PR back buttons on the grips' backs (positions to about "
                 "3 mm); the light rings round the sticks drawn unlit; the square key taken as Capture (share), the "
                 "star key as misc.1")]
    colors = {"body": "#e3e4e6", "plate": "#e3e4e6", "trigger": "#d9dadc", "bumper": "#dedfe1", "button": "#27282b",
              "small": "#e9eaec", "stick": "#2a2b2e", "dpad": "#2a2b2e", "arrow": "#7d8085", "glyph": "#8b8e93",
              "ring": "#d3d5d9"}
    inks = {"north": "#c9b45f", "west": "#6e97c2", "east": "#c27470", "south": "#7fae80"}
    front = pic.view("front", oy=70 + 52.6 * 8.6)
    for sx in (-1, 1):  # the bumpers' tops behind the case; they are drawn and hot in the top-edge view
        tab(front, sx * 24.5, sx * 54.5, -49.7, 2.7, colors["bumper"])
    body(front, ULTIMATE_SHELL, colors["body"])
    for cx, cy in ((-44.2, -27.2), (20.1, -3.3)):  # the light rings, unlit: frosted, a shade off the shell
        ring = front.defs.linear([(0, shade(colors["ring"], 1.03)), (1, shade(colors["ring"], 0.97))])
        front.add("case", f'<path d="{circle_d(cx, cy, 11.6)}" fill="{ring}" stroke="{style.rim_of(colors["body"])}" '
                          f'stroke-width="0.25"/>')
    sticks(front, ((-44.2, -27.2), (20.1, -3.3)), 8.4, colors, 9.9)
    dpad(front, -20.7, -2.7, 25.6, 8.6, colors, well=14.4, dimple=True)
    face(front, 42.3, -25.7, 10.6, 5.0, colors["button"], inks, letters=XBOX_LETTERS)
    round_key(front, -19.5, -39.3, 3.3, colors, "view", "-", "-")
    pixel_key(front, 0, -39.3, 4.3, colors, "guide", "Home", side="right")
    round_key(front, 19.5, -39.3, 3.3, colors, "menu", "+", "+")
    style.button(front, -10.2, -28.7, 3.3, colors["small"], "share")
    style.glyph(front, rounded_d(-11.6, -30.1, 2.8, 2.8, 0.5), colors["glyph"], "share", width=0.3)
    front.spot("share", "Capture", ("circle", -10.2, -28.7, 3.7), "left")
    star_button(front, 10.2, -28.7, 3.3, colors, "misc.1", "Star")
    front.add("case", f'<path d="{circle_d(0, -28.6, 0.5)}" fill="{shade(colors["plate"], 0.55)}"/>')
    style.pill(front, 0, -16.8, 6.2, 3.0, colors["small"], "profile")
    front.spot("profile", "Profile", ("rect", -3.5, -18.7, 7.0, 3.8), "right")
    for y in (-5.5, -2.5, 0.5):
        front.add("case", f'<path d="{circle_d(0, y, 0.45)}" fill="{shade(colors["plate"], 0.55)}"/>')

    # The top edge: the back of the shell, LB over LT and the small L4 in their inner corner; PL / PR on the grips
    s = 6.0
    top = pic.view("top", oy=70 + 104.2 * 8.6 + 36 + 22.4 * s, scale=s)
    style.body(top, mirrored_d(U2_TOP_SHELL), shade(colors["body"], 0.97))
    top.add("case", f'<path d="M-25,-9H25" stroke="{shade(colors["body"], 0.78)}" stroke-width="0.35"/>')
    top.add("case", f'<path d="{rounded_d(-5, -4.6, 10, 3.2, 1.6)}" fill="{shade(colors["body"], 0.45)}"/>')
    ink = shade(colors["body"], 0.62)
    for sx, b, t, k, bn, tn, kn in ((-1, "lb", "lt", "paddle.p4", "LB", "LT", "L4"),
                                     (1, "rb", "rt", "paddle.p2", "RB", "RT", "R4")):
        x0, x1 = sorted((sx * 29.4, sx * 63.5))
        key(top, (x0 + x1) / 2, -6.2, x1 - x0, 14.4, colors["bumper"], b, bn, legend=bn, ink=ink, size=3.4,
            r=4.6)
        tx0, tx1 = sorted((sx * 43.0, sx * 63.5))
        style.shaped(top, rounded_d(tx0, 1.4, tx1 - tx0, 21.4, 5.0), colors["trigger"], t,
                     rounded_d(tx0 - 0.4, 1.0, tx1 - tx0 + 0.8, 22.2, 5.4))
        style.label(top, tn, (tx0 + tx1) / 2, 13.0, 3.4, ink, t, opacity=0.85)
        top.spot(t, tn, ("rect", tx0 - 0.4, 1.0, tx1 - tx0 + 0.8, 22.2), side_of(sx),
                 analog={"kind": "trigger", "fill": "down"})
        key(top, sx * 36.0, 1.4, 12.0, 9.4, colors["bumper"], k, kn, legend=kn, ink=ink, size=2.6, r=2.0)
    for sx, control, name in ((-1, "paddle.p3", "PL"), (1, "paddle.p1", "PR")):
        key(top, sx * 53.0, 27.0, 8.0, 4.6, shade(colors["body"], 0.9), control, name, legend=name, ink=ink,
            size=2.4)
    return pic


# -- Lite 2 ------------------------------------------------------------------------------------------------------

def lite2():
    pic = Picture("8bitdo-lite-2", "8BitDo Lite 2, turquoise (front and top edge)")
    pic.sources = [
        size("size 120 x 65.95 x 35.45 mm", "lite2/"),
        traced("front view (turquoise): the outline (depth 65.0 mm traced against 65.95 mm), the control positions "
               "and sizes", "2022/lite2/lite2-2.png"),
        traced("top-edge view: L over L2 at each end, the sticks' height", "2022/lite2/lite2-2.png"),
        estimate("the top-edge view drawn mirrored (the left hand left, as the front view)")]
    colors = {"body": "#5bb8b9", "plate": "#5bb8b9", "trigger": "#dcdcd9", "bumper": "#e3e3e0", "button": "#e8e8e5",
              "small": "#e8e8e5", "stick": "#e6e6e3", "dpad": "#e8e8e5", "arrow": "#a9abad", "glyph": "#9da0a3"}
    front = pic.view("front", oy=40 + 34.2 * 8.6)
    for sx in (-1, 1):
        tab(front, sx * 31.5, sx * 48.0, -32.7, 1.6, colors["bumper"])
    body(front, LITE2_SHELL, colors["body"])
    sticks(front, ((-37.5, -7.4), (20.3, 11.4)), 6.8, colors, 7.6)
    dpad(front, -19.0, 11.5, 18.0, 6.2, colors)
    face(front, 38.1, -7.2, 7.6, 3.3, colors["button"], "#9fa2a5")
    round_key(front, -18.6, -17.9, 2.9, colors, "view", "-", "-")
    round_key(front, 18.9, -17.9, 2.9, colors, "menu", "+", "+")
    # the S / D mode slider between them: a switch, not an input
    front.add("case", f'<path d="{rounded_d(-3.6, -19.4, 7.2, 3.0, 1.5)}" fill="{shade(colors["body"], 0.6)}"/>')
    front.add("case", f'<path d="{circle_d(-1.9, -17.9, 1.35)}" fill="{colors["small"]}"/>')
    for letter, x in (("S", -5.4), ("D", 5.4)):
        style.label(front, letter, x, -17.9, 1.6, shade(colors["body"], 0.7), None)
    star_button(front, -37.9, 19.4, 2.8, colors, "share", "Star")
    pixel_key(front, 38.1, 19.4, 2.8, colors, "guide", "Home")

    s = 8.6
    top = pic.view("top", oy=40 + 67.5 * s + 50 + 13.6 * s, scale=s)
    for cx in (-37.5, 20.3):  # the sticks standing up out of the face
        top.add("case", f'<path d="{rounded_d(cx - 2.4, -11.5, 4.8, 5, 0.6)}" fill="#c9c9c6"/>')
        style.body(top, rounded_d(cx - 6.8, -13.6, 13.6, 2.6, 1.2), colors["stick"], width=0.25)
    style.body(top, rounded_d(-60, -6.9, 120, 13.8, 6.4), shade(colors["body"], 0.97))
    top.add("case", f'<path d="{rounded_d(-3.2, -0.9, 6.4, 1.8, 0.9)}" fill="{shade(colors["body"], 0.45)}"/>')
    ink = "#a3a5a7"
    for sx, b, t, bn, tn in ((-1, "lb", "lt", "L", "L2"), (1, "rb", "rt", "R", "R2")):
        x0, x1 = sorted((sx * 32.8, sx * 59.0))
        style.shaped(top, rounded_d(x0 + 1.2, -0.6, x1 - x0 - 2.4, 13.4, 5.2), colors["trigger"], t,
                     rounded_d(x0 + 0.8, -1.0, x1 - x0 - 1.6, 14.2, 5.6))
        style.label(top, tn, (x0 + x1) / 2, 8.2, 3.0, ink, t, opacity=0.85)
        top.spot(t, tn, ("rect", x0 + 0.8, 3.0, x1 - x0 - 1.6, 10.2), side_of(sx))
        key(top, (x0 + x1) / 2, -2.6, x1 - x0, 5.4, colors["bumper"], b, bn, legend=bn, ink=ink, size=2.8, r=2.2)
    return pic


# -- Micro -------------------------------------------------------------------------------------------------------

def micro():
    pic = Picture("8bitdo-micro", "8BitDo Micro, blue (drawn at twice the scale)")
    pic.sources = [
        size("size 72 x 40.7 x 14.1 mm", "micro/"),
        traced("front view (blue): the outline (depth 38.6 mm traced against 40.7 mm with the shoulders), the "
               "control positions and sizes", "2023/micro/micro-blue.png"),
        seen("top-edge view: L outside L2 on each end", "2023/micro/micro-blue.png"),
        {"what": "the scale: twice the pads' 8.6 px per mm, as the Zero 2", "source": "this POC",
         "retrieved": "2026-10-24"}]
    colors = {"body": "#5f79cf", "plate": "#5f79cf", "trigger": "#dcdcda", "bumper": "#e3e3e1", "button": "#e9e9e7",
              "small": "#5a73c6", "dpad": "#e9e9e7", "arrow": "#b2b4b7", "glyph": "#c9d2ee"}
    view = pic.view("front", oy=768, scale=17.2)
    ink = "#9fa2a6"
    for sx, b, t, bn, tn in ((-1, "lb", "lt", "L", "L2"), (1, "rb", "rt", "R", "R2")):
        tab(view, sx * 10.4, sx * 16.0, -18.8, 2.0, colors["trigger"], t)
        view.spot(t, tn, ("rect", min(sx * 10.4, sx * 16.0), -21.2, 5.6, 2.8), side_of(sx))
        tab(view, sx * 16.6, sx * 31.0, -18.8, 2.0, colors["bumper"], b)
        view.spot(b, bn, ("rect", min(sx * 16.6, sx * 31.0), -21.2, 14.4, 2.8), side_of(sx))
    body(view, MICRO_SHELL, colors["body"])
    dpad(view, -18.9, 0.1, 18.3, 6.4, colors)
    face(view, 19.1, 0.1, 7.5, 3.35, colors["button"], ink)
    for control, label, x, mark in (("view", "-", -7.3, "-"), ("menu", "+", 7.3, "+")):
        style.button(view, x, -8.5, 2.1, colors["small"], control)
        s = 2.1 * 0.48
        d = f"M{num(x - s)},-8.5H{num(x + s)}" + (f"M{num(x)},{num(-8.5 - s)}V{num(-8.5 + s)}" if mark == "+" else "")
        style.glyph(view, d, colors["glyph"], control, width=0.28)
        style.glyph(view, circle_d(x, -8.5, 1.75), colors["glyph"], control, width=0.18, opacity=0.6)
        view.spot(control, label, ("circle", x, -8.5, 2.5), side_of(x))
    view.add("case", f'<path d="{circle_d(0, -8.6, 0.35)}" fill="{shade(colors["body"], 0.4)}"/>')
    style.button(view, -4.5, 8.5, 3.0, colors["small"], "share")
    style.glyph(view, star_d(-4.5, 8.6, 1.8), colors["glyph"], "share", fill=True, opacity=0.85)
    style.glyph(view, circle_d(-4.5, 8.5, 2.6), colors["glyph"], "share", width=0.18, opacity=0.6)
    view.spot("share", "Star", ("circle", -4.5, 8.5, 3.4), "left")
    style.button(view, 4.5, 8.5, 3.0, colors["small"], "guide")
    style.glyph(view, pixel_heart_d(4.5, 8.5, 0.55), colors["glyph"], "guide", fill=True, opacity=0.85)
    style.glyph(view, circle_d(4.5, 8.5, 2.6), colors["glyph"], "guide", width=0.18, opacity=0.6)
    view.spot("guide", "Home", ("circle", 4.5, 8.5, 3.4), "right")
    return pic


# -- 64 Bluetooth ------------------------------------------------------------------------------------------------

def n64():
    pic = Picture("8bitdo-64", "8BitDo 64 Bluetooth controller, Classic Grey (front and the back triggers)")
    pic.sources = [
        size("size 140.8 x 104 x 60.5 mm, 225 g; \"the original L, R and Z buttons\"", "64-controller/"),
        traced("front view (grey on black): the outline (depth 103.1 mm traced against 104 mm), the control "
               "positions, sizes and colours", "2024/64-controller/64-wireless-controller.jpg"),
        seen("details: the C buttons on their raised disc, Start in its well, the octagonal stick gate, the side "
             "shoulder stacked over Z", "2024/64-controller/detail4.jpg"),
        estimate("the back triggers: Z on the left as on the N64, ZR on the right as SDL_GameControllerDB maps the "
                 "pad (lefttrigger b8, righttrigger b9); drawn as a strip under the pad",
                 "SDL_GameControllerDB (gamecontrollerdb.txt, 8BitDo 64)")]
    colors = {"body": "#b9babc", "plate": "#b9babc", "trigger": "#a9aaad", "bumper": "#acadb0", "stick": "#c4c5c7",
              "dpad": "#56585c", "arrow": "#8b8e92", "small": "#c3c4c6", "glyph": "#8e9195", "c": "#d8a33c",
              "a": "#3f55a0", "b": "#3f8a52", "start": "#c2453f"}
    view = pic.view("front", oy=60 + 52.8 * 8.6)
    for sx, control, name in ((-1, "lb", "L"), (1, "rb", "R")):
        x0, x1 = sorted((sx * 32.0, sx * 50.0))
        tab(view, sx * 32.0, sx * 50.0, -50.6, 2.2, colors["bumper"], control)
        view.spot(control, name, ("rect", x0, -53.2, x1 - x0, 3.4), side_of(sx))
    body(view, N64_SHELL, colors["body"])
    # the stick: concave grooved cap in its octagonal gate
    cx, cy = -40.6, -26.4
    view.add("case", f'<path d="{circle_d(cx, cy, 12.6)}" fill="{shade(colors["body"], 1.02)}" '
                     f'stroke="{style.rim_of(colors["body"])}" stroke-width="0.22"/>')
    octagon = polygon_d([(cx + 11.2 * c, cy + 11.2 * s) for c, s in
                         ((1, 0.414), (0.414, 1), (-0.414, 1), (-1, 0.414), (-1, -0.414), (-0.414, -1),
                          (0.414, -1), (1, -0.414))])
    view.add("case", f'<path d="{octagon}" fill="{shade(colors["body"], 0.78)}" stroke="{shade(colors["body"], 0.6)}" '
                     f'stroke-width="0.25" stroke-linejoin="round"/>')
    style.stick(view, cx, cy, 8.9, colors["stick"], "lstick.x", skirt=9.9)
    for rr in (2.2, 4.0, 5.7):
        style.glyph(view, circle_d(cx, cy, rr), shade(colors["stick"], 0.8), "lstick.x", width=0.25, opacity=0.8)
    common.stick_spots(view, "lstick", "Stick", cx, cy, 9.3, 7.0, "left")
    dpad(view, -17.9, -1.4, 25.6, 8.5, colors, well=14.2, dimple=True)
    # the C buttons on their raised disc: four small arrow buttons, misc.1-4 (up, down, left, right)
    ccx, ccy = 40.9, -26.1
    disc = view.defs.linear([(0, shade(colors["body"], 1.04)), (1, shade(colors["body"], 0.96))])
    view.add("case", f'<path d="{circle_d(ccx, ccy, 12.6)}" fill="{disc}" stroke="{style.rim_of(colors["body"])}" '
                     f'stroke-width="0.25"/>')
    for control, name, dx, dy, direction in (("misc.1", "C-Up", 0, -9.2, "up"), ("misc.2", "C-Down", 0, 9.2, "down"),
                                             ("misc.3", "C-Left", -9.7, 0, "left"),
                                             ("misc.4", "C-Right", 9.7, 0, "right")):
        x, y = ccx + dx, ccy + dy
        style.button(view, x, y, 4.6, colors["c"], control)
        style.glyph(view, style.arrow_d(x, y, 3.0, direction), shade(colors["c"], 0.78), control, fill=True)
        view.spot(control, name, ("circle", x, y, 5.0), "right")
    style.recess(view, 0.3, -25.8, 7.6, colors["body"], depth=0.9)
    style.button(view, 0.3, -25.8, 5.8, colors["start"], "menu")
    style.label(view, "START", 0.3, -25.8, 2.2, shade(colors["start"], 0.75), "menu")
    view.spot("menu", "Start", ("circle", 0.3, -25.8, 6.2), "right")
    for control, letter, x, y in (("west", "B", 17.4, -13.1), ("south", "A", 28.7, -1.6)):
        cap = colors[letter.lower()]
        style.button(view, x, y, 6.0, cap, control)
        style.label(view, letter, x, y, 5.6, shade(cap, 0.72), control)
        view.spot(control, letter, ("circle", x, y, 6.4), "right")
    star_button(view, -11.3, -40.3, 3.1, colors, "share", "Star")
    round_key(view, 0, -40.3, 3.1, colors, "view", "-", "-")
    pixel_key(view, 11.3, -40.3, 3.1, colors, "guide", "Home")

    # the back: Z (left) and ZR (right) under the side shoulders
    panel = rounded_d(-56, 58.0, 112, 13.0, 6.5)
    style.body(view, panel, shade(colors["body"], 0.93), width=0.3)
    for control, name, x in (("lt", "Z", -36.0), ("rt", "ZR", 36.0)):
        key(view, x, 64.5, 26.0, 7.2, colors["trigger"], control, name, legend=name, ink=shade(colors["body"], 0.6),
            size=3.4)
    return pic


# -- Arcade Stick (Switch / PC) -----------------------------------------------------------------------------------

ARCADE_BUTTONS = (  # Vewlix rows, the first column set low; the names the Switch version prints
    ("west", "Y", 1.1, -9.0, -1), ("north", "X", 34.0, -23.0, -1), ("rb", "R", 70.0, -23.0, -1),
    ("lb", "L", 106.0, -23.0, -1), ("south", "B", -5.8, 29.0, 1), ("east", "A", 27.1, 15.9, 1),
    ("rt", "ZR", 63.1, 16.0, 1), ("lt", "ZL", 99.0, 16.0, 1))


def arcade():
    pic = Picture("8bitdo-arcade-stick", "8BitDo Arcade Stick for Switch / Windows, top view")
    pic.sources = [
        size("size 303 x 203 x 111.5 mm, 2100 g", "arcade-stick/"),
        traced("top view: the outline (depth 202.5 mm traced against 203 mm), the panels, the buttons, the stick, "
               "the keys", "2020/arcade-stick/arcade-stick.png"),
        seen("the mapping view: P1 / P2 macro buttons, the X-input names", "2020/arcade-stick/mapping.png"),
        {"what": "the scale: 5.6 px per mm (303 mm does not fit 8.6 on the canvas)", "source": "this POC",
         "retrieved": "2026-10-24"},
        estimate("the two mode dials, the pair key and the BT / 2.4G slider drawn without hotspots (switches, not "
                 "inputs); P1 / P2 as quiet misc.1 / misc.2 (macro keys the pad may not report)")]
    colors = {"body": "#d8d5cd", "panel": "#dedbd3", "black": "#2b2b2d", "red": "#b8433d", "ball": "#232426",
              "small": "#e4e1d9", "glyph": "#8c8a84", "ink": "#a8524b", "green": "#79b37a", "yellow": "#d7c45a",
              "blue": "#5b8fc8"}
    view = pic.view("front", oy=768, scale=5.6)
    shell = [("M", (0, -101.5)), ("C", (50, -101.5), (100, -101.5), (146.8, -101.5)),
             ("C", (149.6, -101.4), (151.4, -98.4), (151.5, -95.0)),
             ("L", (151.5, 95.0)), ("C", (151.4, 98.6), (148.6, 100.9), (145.0, 100.9)), ("L", (0, 100.9))]
    body(view, shell, colors["body"])
    rim = shade(colors["body"], 0.82)
    style.flat(view, rounded_d(-147.5, -97.5, 295, 193.5, 3.2), shade(colors["body"], 1.02))
    view.add("case", f'<path d="{rounded_d(-147.5, -97.5, 295, 193.5, 3.2)}" fill="none" stroke="{rim}" '
                     f'stroke-width="0.4"/>')
    for x, y, w, h in ((-141.5, -90.0, 100.0, 20.3), (-141.5, -69.2, 100.0, 19.6), (-141.5, -48.4, 100.0, 99.6),
                       (-40.0, -90.0, 180.5, 40.6), (-141.5, 52.6, 282.0, 38.0)):
        style.body(view, rounded_d(x, y, w, h, 1.2), colors["panel"], width=0.35)
    view.add("case", f'<path d="{rounded_d(-40.0, -48.4, 180.5, 100.0, 1.0)}" fill="{colors["black"]}"/>')
    for x in (-108.0, -79.0, -61.5):  # the cells of the top row
        view.add("case", f'<path d="M{num(x)},-90V-69.7" stroke="{rim}" stroke-width="0.35"/>')
    # the stick: ball top over the round gate plate, the four directions as its hotspots
    cx, cy = -90.5, 1.0
    style.body(view, circle_d(cx, cy, 39.6), shade(colors["panel"], 1.02), width=0.4)
    view.add("case", f'<path d="{circle_d(cx, cy, 5.5)}" fill="{shade(colors["panel"], 0.6)}"/>')
    ball = view.defs.radial([(0, "#4a4b4e"), (0.55, colors["ball"]), (1, "#141516")], cx=0.42, cy=0.38, r=0.62)
    view.add("controls", f'<path d="{circle_d(cx + 0.4, cy + 1.2, 17.6)}" fill="{style.GAP}" fill-opacity="0.35"/>',
             "dpad")
    view.add("controls", f'<path d="{circle_d(cx, cy, 17.2)}" fill="{ball}"/>', "dpad")
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.glyph(view, style.arrow_d(cx + dx * 26, cy + dy * 26, 3.6, direction), colors["glyph"], "dpad",
                    fill=True, opacity=0.7)
        view.spot(f"dpad.{direction}", "Stick", ("circle", cx + dx * 23, cy + dy * 23, 8.0), "left", group="dpad")
    for control, name, x, y, row in ARCADE_BUTTONS:
        style.button(view, x, y, 15.0, colors["red"], control, gap=0.9)
        style.glyph(view, circle_d(x, y, 11.6), shade(colors["red"], 0.86), control, width=0.5)
        view.spot(control, name, ("circle", x, y, 15.6), "right")
    for control, name, x, y, row in ARCADE_BUTTONS:
        style.label(view, name, x - 11.2, y + row * 18.2, 4.4, colors["ink"], None)
    for control, name, x in (("misc.1", "P1", 85.5), ("misc.2", "P2", 120.4)):
        view.add("case", f'<path d="{circle_d(x, -69.6, 15.6)}" fill="{shade(colors["panel"], 1.03)}" '
                         f'stroke="{rim}" stroke-width="0.35"/>')
        style.button(view, x, -69.6, 13.2, "#2e2f31", control, gap=0.8)
        view.spot(control, name, ("circle", x, -69.6, 13.8), "right", quiet=True)
        style.label(view, name, x - 13.0, -87.0, 3.6, colors["ink"], None)
    # the top-left block: two mode dials, pair (not an input), Star, Home; slider, Select, Start
    for x in (-132.0, -111.0):
        style.body(view, circle_d(x, -79.6, 7.0), "#38393b", width=0.3)
        view.add("case", f'<path d="M{num(x - 4.4)},{num(-84.0)}L{num(x + 4.4)},{num(-75.2)}" stroke="{colors["ink"]}" '
                         f'stroke-width="0.9" stroke-linecap="round"/>')
    style.body(view, circle_d(-90.8, -79.6, 5.8), colors["green"], width=0.3)
    style.button(view, -71.0, -79.6, 5.8, colors["yellow"], "share")
    style.glyph(view, star_d(-71.0, -79.4, 3.3), shade(colors["yellow"], 0.8), "share", fill=True)
    view.spot("share", "Star", ("circle", -71.0, -79.6, 6.2), "left")
    style.button(view, -51.0, -79.6, 5.8, colors["blue"], "guide")
    style.glyph(view, pixel_heart_d(-51.0, -79.6, 1.1), shade(colors["blue"], 0.75), "guide", fill=True)
    view.spot("guide", "Home", ("circle", -51.0, -79.6, 6.2), "left")
    view.add("case", f'<path d="{rounded_d(-97.6, -61.6, 15.0, 6.2, 3.1)}" fill="{shade(colors["panel"], 0.75)}"/>')
    view.add("case", f'<path d="{circle_d(-94.4, -58.5, 2.6)}" fill="{colors["small"]}"/>')
    for x, y in ((-132.0, -61.0), (-129.0, -61.0), (-132.0, -58.0), (-129.0, -58.0)):
        view.add("case", f'<path d="{circle_d(x, y, 0.7)}" fill="{shade(colors["panel"], 0.6)}"/>')
    for control, name, x in (("view", "Select", -70.0), ("menu", "Start", -51.6)):
        style.pill(view, x, -58.5, 15.4, 6.2, colors["small"], control)
        view.spot(control, name, ("rect", x - 8.1, -62.0, 16.2, 7.0), "left")
    return pic


SETS = {
    "8bitdo-ultimate-2": ultimate2,
    "8bitdo-lite-2": lite2,
    "8bitdo-micro": micro,
    "8bitdo-64": n64,
    "8bitdo-arcade-stick": arcade,
}
