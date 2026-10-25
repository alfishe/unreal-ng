"""8BitDo pads: Ultimate (2.4G / Bluetooth), Pro 2, SN30 Pro+, SN30 Pro / SF30 Pro, SN30 / SF30, M30, Zero 2.

Sizes are 8BitDo's (product pages, "Dimension / Weight", retrieved 2026-10-23): Ultimate Bluetooth 147 x 104 x
61.5 mm, Pro 2 153.6 x 100.6 x 64.5 mm, SN30 Pro 144 x 63.5 x 33 mm, SN30 2.4G 144 x 63.5 x 26.2 mm, M30 147 x 74 x
31 mm, Zero 2 73 x 36.5 x 14.7 mm. The outlines are measured with trace.py on 8BitDo's orthographic front views (the
colourway shots of the product pages, cut out on a transparent ground): the silhouette, the left half mirrored about
the best symmetry axis, scaled to the maker's width, cubic Beziers within 0.25 mm. Each traced outline's depth comes
out within 1 % of the maker's figure, which checks the views are true plans. The control positions and sizes are read
on the same views on a 5 mm grid (about 0.5 mm). The Ultimate's own front view stands in its dock, so its outline is
the Ultimate 2C's (the same shell: the two outlines agree within 0.5 mm where both show). 8BitDo prints
Nintendo-positional letters (X top, A right, B bottom, Y left); the hotspots keep the canonical positional names
(north, east, south, west), the legends show what is printed. The wordmark and the 8BitDo logo on the Home button are
left out (D5).
"""

import math

import style
from devices import common
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
SITE = "https://www.8bitdo.com/"
POSITIONAL = {"north": "X", "east": "A", "south": "B", "west": "Y"}

# The right halves of the outlines (trace.py; the shoulder buttons above the case top cut off), in mm from the
# symmetry axis and from the middle of the pad's depth
ULTIMATE_SHELL = [("M", (0, -49.7)),
    ("C", (3.8, -49.7), (9.8, -49.8), (14.4, -49.7)),
    ("C", (18.3, -49.6), (22.3, -49.2), (26.2, -49.4)),
    ("C", (32.9, -49.9), (40.8, -50.2), (47.4, -49.7)),
    ("C", (48.3, -49.6), (49.1, -49.3), (49.9, -49.0)),
    ("C", (52.6, -47.6), (56.2, -46.0), (58.3, -43.6)),
    ("C", (59.0, -42.7), (58.8, -41.6), (59.4, -40.7)),
    ("C", (60.1, -39.6), (61.2, -38.8), (61.9, -37.7)),
    ("C", (63.9, -35.1), (65.3, -32.1), (66.4, -29.0)),
    ("C", (69.3, -20.4), (70.2, -11.2), (71.3, -2.3)),
    ("C", (72.6, 8.1), (73.9, 18.4), (73.5, 28.9)),
    ("C", (73.4, 32.0), (72.8, 35.4), (71.9, 38.3)),
    ("C", (70.5, 42.9), (67.6, 46.8), (63.3, 49.0)),
    ("C", (61.6, 50.0), (58.7, 51.8), (56.7, 51.5)),
    ("C", (55.5, 51.4), (54.5, 50.3), (53.6, 49.6)),
    ("C", (45.9, 42.8), (38.9, 24.5), (33.4, 21.1)),
    ("C", (31.2, 19.7), (28.2, 19.6), (25.6, 19.4)),
    ("C", (20.1, 19.0), (14.5, 19.4), (8.9, 19.4)),
    ("C", (7.0, 19.4), (5.1, 19.4), (3.2, 19.4)),
    ("C", (2.5, 19.4), (1.8, 19.4), (1.0, 19.4)),
    ("C", (0.7, 19.4), (-0.3, 19.4), (0, 19.4))]
PRO2_SHELL = [("M", (0, -47.3)),
    ("C", (5.8, -47.3), (53.1, -48.0), (55.5, -46.8)),
    ("C", (57.5, -45.9), (56.7, -44.7), (58.0, -43.6)),
    ("C", (58.6, -43.0), (59.5, -42.6), (60.2, -42.1)),
    ("C", (63.9, -39.2), (67.0, -35.3), (69.0, -31.1)),
    ("C", (74.3, -20.1), (77.3, 16.4), (76.8, 29.4)),
    ("C", (76.7, 32.8), (76.1, 36.3), (74.9, 39.5)),
    ("C", (73.5, 43.1), (71.1, 46.3), (67.7, 48.3)),
    ("C", (66.6, 49.0), (65.3, 49.3), (64.4, 50.3)),
    ("C", (63.6, 51.2), (56.8, 51.3), (56.2, 50.3)),
    ("C", (55.4, 49.3), (54.5, 49.0), (53.5, 48.3)),
    ("C", (51.4, 46.8), (49.8, 44.7), (48.6, 42.5)),
    ("C", (42.9, 31.9), (45.4, 24.5), (34.9, 15.2)),
    ("C", (33.5, 13.9), (31.8, 12.3), (30.0, 11.7)),
    ("C", (28.5, 11.2), (22.8, 13.9), (17.9, 12.6)),
    ("C", (13.1, 11.3), (10.1, 6.9), (8.6, 6.4)),
    ("C", (7.0, 6.0), (4.8, 6.3), (3.2, 6.3)),
    ("C", (2.5, 6.3), (1.8, 6.3), (1.2, 6.3)),
    ("C", (0.8, 6.3), (-0.4, 6.3), (0, 6.3))]
SN30PRO_SHELL = [("M", (0, -29.3)),
    ("C", (12.9, -29.3), (25.7, -29.3), (38.5, -29.3)),
    ("C", (42.1, -29.3), (50.1, -29.9), (53.2, -29.2)),
    ("C", (55.7, -28.7), (56.7, -27.8), (57.3, -25.5)),
    ("C", (57.9, -25.1), (58.5, -24.9), (59.2, -24.4)),
    ("C", (61.6, -22.8), (63.9, -20.5), (65.7, -18.1)),
    ("C", (75.6, -4.1), (73.6, 15.3), (59.4, 25.7)),
    ("C", (54.4, 29.3), (51.5, 29.4), (46.2, 31.4)),
    ("C", (44.8, 32.0), (37.0, 32.6), (35.8, 31.6)),
    ("C", (35.2, 31.2), (27.4, 28.5), (26.7, 28.5)),
    ("C", (25.7, 28.5), (24.8, 28.9), (23.9, 29.1)),
    ("C", (21.9, 29.6), (19.7, 29.6), (17.7, 29.2)),
    ("C", (15.5, 28.7), (13.4, 27.7), (11.6, 26.4)),
    ("C", (10.9, 25.9), (10.3, 25.1), (9.5, 24.8)),
    ("C", (7.5, 24.2), (2.2, 24.7), (0, 24.7))]
SN30_SHELL = [("M", (0, -29.1)),
    ("C", (13.3, -29.1), (26.6, -29.1), (39.9, -29.1)),
    ("C", (43.1, -29.1), (51.1, -29.7), (53.8, -28.9)),
    ("C", (57.7, -27.9), (56.3, -26.2), (58.4, -24.6)),
    ("C", (61.9, -22.0), (64.9, -19.1), (67.3, -15.4)),
    ("C", (76.3, -1.1), (71.6, 18.9), (57.0, 27.5)),
    ("C", (48.3, 32.6), (37.4, 33.4), (28.2, 29.0)),
    ("C", (25.7, 27.8), (23.2, 25.5), (20.5, 25.0)),
    ("C", (16.7, 24.2), (6.6, 24.8), (2.1, 24.8)),
    ("C", (0.9, 24.8), (-0.2, 24.8), (0, 24.8))]
M30_SHELL = [("M", (0, -31.9)),
    ("C", (6.5, -31.9), (12.9, -32.3), (19.4, -33.3)),
    ("C", (20.2, -33.4), (21.0, -33.8), (21.8, -33.9)),
    ("C", (24.4, -34.2), (51.9, -34.1), (53.2, -33.6)),
    ("C", (54.6, -33.1), (54.6, -31.8), (55.8, -31.1)),
    ("C", (67.0, -23.9), (72.0, -9.9), (73.2, 2.9)),
    ("C", (74.0, 11.9), (73.5, 25.0), (66.3, 31.6)),
    ("C", (58.0, 39.3), (45.3, 38.1), (35.5, 34.6)),
    ("C", (30.9, 33.0), (26.6, 30.5), (22.1, 28.7)),
    ("C", (17.5, 26.8), (12.9, 25.5), (7.9, 24.8)),
    ("C", (6.2, 24.6), (4.6, 24.5), (2.9, 24.5)),
    ("C", (2.3, 24.5), (0.7, 24.5), (0, 24.5))]
ZERO2_SHELL = [("M", (0, -16.7)),
    ("C", (3.5, -16.7), (24.7, -17.1), (26.3, -16.4)),
    ("C", (28.8, -15.3), (30.9, -13.0), (32.4, -10.8)),
    ("C", (34.1, -8.4), (35.6, -5.6), (36.1, -2.7)),
    ("C", (37.8, 5.7), (33.0, 14.2), (25.0, 17.2)),
    ("C", (20.9, 18.7), (16.3, 18.2), (12.0, 18.2)),
    ("C", (8.3, 18.2), (3.7, 18.2), (0, 18.2))]


def size(what, page):
    return {"what": what, "source": SITE + page, "author": "8BitDo", "retrieved": "2026-10-23"}


def traced(what, path):
    return {"what": what, "source": SITE + "images/" + path, "author": "8BitDo", "terms": "product photo",
            "use": "measured geometry only (trace.py outline, positions on a grid); no pixels shipped",
            "retrieved": "2026-10-23"}


def photo(what, file, author, licence):
    return {"what": what, "source": COMMONS + file, "author": author, "licence": licence,
            "use": "reference only, not traced", "retrieved": "2026-10-23"}


def scaled(segments, sx, sy, dy=0.0):
    return [(cmd, *[(x * sx, y * sy + dy) for x, y in pts]) for cmd, *pts in segments]


def top_shoulders(view, colors, names, top, x0, x1, triggers=True, ids=("lb", "rb", "lt", "rt"), rise=1.0):
    """Bumpers (and triggers behind them) along the top edge from x0 to x1 on each side, @p rise their height
    against the pads' usual 5.4 mm (a front view shows only the top of them); @p ids the controls the bumpers and
    triggers are (the M30's only shoulders are its L / R triggers)"""
    lb, rb, lt, rt = names
    for sx, bump, trig, side, bname, tname in ((-1, ids[0], ids[2], "left", lb, lt),
                                               (1, ids[1], ids[3], "right", rb, rt)):
        f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
        w = x1 - x0
        if triggers:
            t = top - 5.4 * rise + 1.2
            trigger = (f"M{f(x0 + w * 0.18, t + 2)}C{f(x0 + w * 0.26, t - 2.6)} {f(x0 + w * 0.5, t - 3.6)} "
                       f"{f(x0 + w * 0.66, t - 3.5)}C{f(x0 + w * 0.82, t - 3.3)} {f(x0 + w * 0.94, t - 1.6)} "
                       f"{f(x0 + w * 0.97, t + 2)}Z")
            style.shaped(view, trigger, colors["trigger"], trig, layer="shoulders")
            a, b = sorted(((x0 + w * 0.22) * sx, (x0 + w * 0.95) * sx))
            view.spot(trig, tname, ("rect", a, t - 3.7, b - a, 3.6), side, analog={"kind": "trigger", "fill": "down"})
        bumper = (f"M{f(x0, top + 1.2)}C{f(x0 + w * 0.12, top - 4.6 * rise)} {f(x0 + w * 0.5, top - 5.4 * rise)} "
                  f"{f(x0 + w * 0.72, top - 5 * rise)}C{f(x0 + w * 0.9, top - 4.6 * rise)} {f(x1, top - 2 * rise)} "
                  f"{f(x1 + 0.6, top + 1.6)}Z")
        style.shaped(view, bumper, colors["bumper"], bump, layer="shoulders")
        a, b = sorted(((x0 + 1) * sx, (x1 - 0.5) * sx))
        view.spot(bump, bname, ("rect", a, top - 5.4 * rise - 0.2, b - a, 5.4 * rise + 0.6), side)


def body(view, shell, color, sheen=True):
    d = mirrored_d(shell)
    style.body(view, d, color)
    if sheen:
        style.sheen(view, d)
    return d


def dpad(view, cx, cy, span, arm, colors, well=None, dimple=False, cross_well=None):
    if well:
        style.recess(view, cx, cy, well, colors["plate"], depth=0.8)
    if cross_well:  # a recess following the cross, a margin around it
        view.add("case", f'<path d="{style.cross_d(cx, cy, span + cross_well, arm + cross_well, r=1.8)}" '
                         f'fill="{shade(colors["plate"], 0.86)}" stroke="{shade(colors["plate"], 0.72)}" '
                         f'stroke-width="0.25"/>')
    style.shaped(view, style.cross_d(cx, cy, span, arm, r=1.0), colors["dpad"], "dpad",
                 style.cross_d(cx, cy, span + 0.9, arm + 0.9, r=1.3))
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.glyph(view, style.arrow_d(cx + dx * span * 0.33, cy + dy * span * 0.33, arm * 0.24, direction),
                    colors["arrow"], "dpad", fill=True, opacity=0.8)
    if dimple:
        dish = view.defs.radial([(0, shade(colors["dpad"], 0.8)), (1, shade(colors["dpad"], 1.06))], r=0.5)
        view.add("controls", f'<path d="{circle_d(cx, cy, arm * 0.28)}" fill="{dish}"/>', "dpad")
    common.dpad_spots(view, cx, cy, span, arm, "left", prefix="dpad.")


def face(view, cx, cy, offset, r, caps, inks, letters=POSITIONAL, wells=None, plate=None):
    ox, oy = offset if isinstance(offset, tuple) else (offset, offset)
    if wells:  # each button in its own shallow round well
        for dx, dy in ((0, -oy), (0, oy), (-ox, 0), (ox, 0)):
            view.add("case", f'<path d="{circle_d(cx + dx, cy + dy, wells)}" fill="{shade(plate, 0.9)}" '
                             f'stroke="{shade(plate, 0.76)}" stroke-width="0.22"/>')
    labels = {c: {"cap": caps[c] if isinstance(caps, dict) else caps, "text": t, "name": t,
                  "ink": inks[c] if isinstance(inks, dict) else inks} for c, t in letters.items()}
    common.face_diamond(view, cx, cy, offset, r, labels)


def sticks(view, positions, r, colors, well, collar=None):
    for name, label, (cx, cy), side in (("lstick", "LS", positions[0], "left"), ("rstick", "RS", positions[1], "right")):
        if collar:  # the raised ring round the well
            ring = view.defs.linear([(0, shade(colors["plate"], 1.05)), (1, shade(colors["plate"], 0.95))])
            view.add("case", f'<path d="{circle_d(cx, cy, collar)}" fill="{ring}" '
                             f'stroke="{style.rim_of(colors["plate"])}" stroke-width="0.25"/>')
        style.recess(view, cx, cy, well, colors["plate"], depth=0.62)
        style.stick(view, cx, cy, r, colors["stick"], f"{name}.x", skirt=r + 1.6)
        common.stick_spots(view, name, label, cx, cy, r + 0.4, 6.0, side)


def star_d(cx, cy, r):
    pts = []
    for i in range(10):
        a = math.radians(-90 + i * 36)
        rr = r if i % 2 == 0 else r * 0.45
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return polygon_d(pts)


def star_button(view, cx, cy, r, colors, control, label):
    style.button(view, cx, cy, r, colors["small"], control)
    style.glyph(view, star_d(cx, cy + 0.1, r * 0.55), colors["glyph"], control, width=0.3)
    view.spot(control, label, ("circle", cx, cy, r + 0.4), "left" if cx < 0 else "right")


def round_key(view, cx, cy, r, colors, control, label, mark):
    """A small round key with its printed mark ("-", "+")"""
    style.button(view, cx, cy, r, colors["small"], control)
    s = r * 0.42
    d = f"M{num(cx - s)},{num(cy)}H{num(cx + s)}" + (f"M{num(cx)},{num(cy - s)}V{num(cy + s)}" if mark == "+" else "")
    style.glyph(view, d, colors["glyph"], control, width=0.3)
    view.spot(control, label, ("circle", cx, cy, r + 0.4), "left" if cx < 0 else "right")


def home_button(view, cx, cy, r, colors, side="right"):
    common.neutral_guide(view, cx, cy, r, colors["small"], colors["glyph"], "guide")
    view.spot("guide", "Home", ("circle", cx, cy, r + 0.4), side)


def system_pair(view, y, xs, w, h, colors, labels=("-", "+"), tray=None):
    """Select / Start (or - / +) as two pills at @p xs, in a tray (centre x, width, height) when given"""
    if tray:
        tx, tw, th = tray
        view.add("case", f'<path d="{rounded_d(tx - tw / 2, y - th / 2, tw, th, th / 2)}" '
                         f'fill="{shade(colors["plate"], 0.86)}" stroke="{shade(colors["plate"], 0.7)}" '
                         f'stroke-width="0.25"/>')
    for control, label, px in (("view", labels[0], xs[0]), ("menu", labels[1], xs[1])):
        style.pill(view, px, y, w, h, colors["small"], control)
        view.spot(control, label, ("rect", px - w / 2 - 0.4, y - h / 2 - 0.4, w + 0.8, h + 0.8),
                  "left" if px < 0 else "right")


def leds(view, points, colors):
    for x, y in points:
        view.add("case", f'<path d="{circle_d(x, y, 0.45)}" fill="{shade(colors["plate"], 0.5)}"/>')


def back_pair(view, y, colors, ink):
    """The two back buttons as a strip under the pad (an inset): left hand's PL as paddle.p3, right hand's PR as
    paddle.p1 (the project's P1 / P2 right, P3 / P4 left convention)"""
    panel = rounded_d(-36, y - 6.2, 72, 12.4, 6.2)
    style.body(view, panel, shade(colors["body"], 0.92), width=0.3)
    for control, name, x, side in (("paddle.p3", "PL", -17, "left"), ("paddle.p1", "PR", 17, "right")):
        style.pill(view, x, y, 16, 6.0, colors["button"] if "button" in colors else colors["small"], control)
        style.label(view, name, x, y, 2.9, ink, control, opacity=0.85)
        view.spot(control, name, ("rect", x - 8.4, y - 3.4, 16.8, 6.8), side)


# -- Ultimate (2.4G / Bluetooth) ---------------------------------------------------------------------------------

def ultimate():
    pic = Picture("8bitdo-ultimate", "8BitDo Ultimate Bluetooth / 2.4G controller, black")
    pic.sources = [
        size("size 147 x 104 x 61.5 mm", "ultimate-bluetooth-controller/"),
        traced("front view in the dock: the control positions and sizes, the upper outline",
               "2022/ultimate-bluetooth-controller/ultimate-bluetooth-black.png"),
        traced("front view of the Ultimate 2C (the same shell): the outline", "2025/ultimate-2-bluetooth/"
               "ultimate-2c-bluetooth.jpg"),
        {"what": "the letters and the back buttons (mapping view)", "source": SITE
         + "images/2022/ultimate-bluetooth-controller/mapping.png", "author": "8BitDo", "use": "reference only",
         "retrieved": "2026-10-23"}]
    colors = {"body": "#3c3e42", "plate": "#3c3e42", "trigger": "#333538", "bumper": "#383a3e", "button": "#232427",
              "small": "#2b2c2f", "stick": "#2a2b2e", "dpad": "#26272a", "arrow": "#8f9297", "glyph": "#a9acb1"}
    view = pic.view("front", oy=768 - 8 * 8.6)
    top_shoulders(view, colors, ("LB", "RB", "LT", "RT"), -49.7, 24.5, 54.5, rise=0.5)
    body(view, ULTIMATE_SHELL, colors["body"])
    sticks(view, ((-46.9, -26.7), (19.4, -3.7)), 8.6, colors, 11.4)
    dpad(view, -22.7, -2.7, 23, 8.0, colors, well=13.6)
    face(view, 43.4, -25.6, 10.0, 4.9, colors["button"], "#d4d6da")
    home_button(view, 0, -37.5, 4.2, colors)
    round_key(view, -11.5, -25.6, 3.0, colors, "view", "-", "-")
    star_button(view, 0, -25.6, 3.2, colors, "share", "Star")
    round_key(view, 11.5, -25.6, 3.0, colors, "menu", "+", "+")
    style.pill(view, 0, -13.5, 6.0, 3.0, colors["small"], "profile")
    view.spot("profile", "Profile", ("rect", -3.4, -15.4, 6.8, 3.8), "right")
    leds(view, ((0, -4.2), (0, -1.7), (0, 0.8)), colors)
    back_pair(view, 66, colors, "#d4d6da")
    return pic


# -- Pro 2 and SN30 Pro+ ------------------------------------------------------------------------------------------

def pro_family(family, title, colors, caps, inks, paddles, sources):
    pic = Picture(family, title)
    pic.sources = sources
    view = pic.view("front", oy=768 - (8 if paddles else 0) * 8.6)
    top_shoulders(view, colors, ("L", "R", "L2", "R2"), -47.3, 32.5, 58.5, rise=0.6)
    body(view, PRO2_SHELL, colors["body"], sheen=False)
    # the lighter face plate: the SN30 Pro's outline, as wide as the Pro 2's plate (143 mm) and a little shallower
    plate = body(view, scaled(SN30PRO_SHELL, 143 / 144, 0.94, -19.4), colors["plate"])
    del plate
    dpad(view, -43.2, -24.4, 25.2, 8.8, colors, dimple=True)
    face(view, 41.3, -24.8, (12.0, 10.9), 5.25, caps, inks)
    system_pair(view, -24.9, (-7.0, 7.0), 11.2, 3.6, colors, tray=(0, 30, 8.6))
    leds(view, ((-2.9, -8.1), (0, -8.1), (2.9, -8.1)), colors)
    sticks(view, ((-21.8, -2.3), (21.8, -2.3)), 8.9, colors, 11.2, collar=13.4)
    style.pill(view, 0, -2.3, 6.6, 4.0, colors["pill"], "profile")
    view.spot("profile", "Profile", ("rect", -3.7, -4.7, 7.4, 4.8), "right")
    star_button(view, -43.2, 0.4, 2.9, colors, "share", "Star")
    home_button(view, 41.2, 0.4, 3.1, colors)
    if paddles:
        back_pair(view, 66, colors, "#d4d6da")
    return pic


def pro2():
    colors = {"plate": "#a6a7a9", "body": "#8b8c8f", "trigger": "#7d7e81", "bumper": "#9c9d9f", "button": "#252629",
              "small": "#97989b", "pill": "#9a9b9e", "stick": "#2d2e31", "dpad": "#2a2b2e", "arrow": "#6c6f74",
              "glyph": "#5d6065"}
    inks = {"north": "#d495b6", "west": "#d6d8db", "east": "#82c2b4", "south": "#d48a9c"}
    return pro_family("8bitdo-pro-2", "8BitDo Pro 2, Gray Edition", colors, colors["button"], inks, True, [
        size("size 153.6 x 100.6 x 64.5 mm", "pro2/"),
        traced("front view: the outline, the control positions and sizes", "2021/pro2/pro2_gray.png"),
        photo("the two greys of the Gray Edition (plate and grips)", "8BitDo_Pro_2_Bluetooth_gamepad.jpg", "Kyu3a",
              "CC BY-SA 4.0"),
        {"what": "the face plate's edge: the SN30 Pro outline of this set scaled to 143 x 59.6 mm; an estimate from "
                 "the edge line in the front view", "source": "devices/eightbitdo.py", "retrieved": "2026-10-23"},
        {"what": "the letters and the back buttons (mapping view)", "source": SITE + "images/2021/pro2/mapping.png",
         "author": "8BitDo", "use": "reference only", "retrieved": "2026-10-23"}])


def sn30proplus():
    colors = {"plate": "#bdbdbf", "body": "#a9a9ac", "trigger": "#98989b", "bumper": "#b0b0b3", "button": "#8b84bf",
              "small": "#a9a9ac", "pill": "#acacaf", "stick": "#2d2e31", "dpad": "#2a2b2e", "arrow": "#6c6f74",
              "glyph": "#6d7075"}
    caps = {"north": "#aca6d4", "west": "#aca6d4", "east": "#6d63b0", "south": "#6d63b0"}
    return pro_family("8bitdo-sn30-pro-plus", "8BitDo SN30 Pro+, SN Edition", colors, caps, "#ecebf2", False, [
        {"what": "size about 153 x 100 x 64 mm: the shell of the Pro 2 (its successor, the same body with back "
                 "buttons added); the Pro 2's measured outline and layout", "source": SITE + "pro2/",
         "retrieved": "2026-10-23"},
        traced("the Pro 2 front view the outline is measured on", "2021/pro2/pro2_gray.png"),
        photo("the SN Edition colours (grey body, lavender and purple buttons)", "8BitDo_SN30_gamepad_HS_DSC_2704.jpg",
              "Hayden Schiff", "CC BY 4.0")])


# -- SN30 Pro / SF30 Pro, SN30 / SF30 --------------------------------------------------------------------------------

SN_COLOURS = {"plate": "#bebec0", "body": "#bebec0", "trigger": "#a6a6a9", "bumper": "#b3b3b6", "small": "#5a5b5f",
              "stick": "#2b2c2f", "dpad": "#2a2b2e", "arrow": "#6c6f74", "glyph": "#c6c8cc", "dish": "#aeadb6"}
SN_CAPS = {"north": "#aaa4d2", "west": "#aaa4d2", "east": "#6a60ae", "south": "#6a60ae"}
SF_CAPS = {"north": "#4e68a8", "west": "#4f8d5c", "east": "#b14a42", "south": "#c4a23e"}  # the SFC colours, muted
SN_INKS = {"north": "#f1f0f6", "west": "#f1f0f6", "east": "#ecebf2", "south": "#ecebf2"}


def sn30pro(colourway="sn"):
    caps = SN_CAPS if colourway == "sn" else SF_CAPS
    if colourway == "sn":
        pic = Picture("8bitdo-sn30-pro", "8BitDo SN30 Pro, SN Edition")
        pic.sources = [
            size("size 144 x 63.5 x 33 mm", "sn30-pro-g-classic-or-sn30-pro-sn"),
            traced("front view: the outline, the control positions and sizes", "2018/sn30pro/sn30pro_gray.png"),
            photo("the SN Edition colours", "8BitDo_SN30_gamepad_HS_DSC_2704.jpg", "Hayden Schiff", "CC BY 4.0")]
    else:
        pic = Picture("8bitdo-sf30-pro", "8BitDo SF30 Pro (Super Famicom colours)")
        pic.sources = [
            size("size 144 x 63.5 x 33 mm (the SN30 Pro's body)", "sn30-pro-g-classic-or-sn30-pro-sn"),
            traced("front view of the SN30 Pro (the same body): the outline, the positions and sizes",
                   "2018/sn30pro/sn30pro_gray.png"),
            {"what": "button colours: the Super Famicom's red / yellow / blue / green, muted", "source": "the SF30 "
             "Pro retail colourway; an estimate of the shades", "retrieved": "2026-10-23"}]
    colors = dict(SN_COLOURS)
    view = pic.view("front", oy=768)
    top_shoulders(view, colors, ("L", "R", "L2", "R2"), -29.3, 26, 57, rise=0.45)
    body(view, SN30PRO_SHELL, colors["plate"])
    dpad(view, -42.3, -6.5, 24.9, 8.4, colors, cross_well=3.6)
    face(view, 42.3, -6.35, (13.35, 10.15), 5.0, caps, SN_INKS, wells=6.3, plate=colors["plate"])
    system_pair(view, -6.9, (-10.7, 3.6), 11.3, 4.0, colors, labels=("Select", "Start"), tray=(-3.55, 29.5, 8.5))
    sticks(view, ((-20.75, 14.1), (20.75, 14.1)), 8.8, colors, 11.4, collar=13.6)
    star_button(view, -42.3, 18.4, 3.0, colors, "share", "Star")
    home_button(view, 42.3, 18.4, 3.2, colors)
    return pic


def sn30(colourway="sn"):
    caps = SN_CAPS if colourway == "sn" else SF_CAPS
    family, title = (("8bitdo-sn30", "8BitDo SN30, SN Edition") if colourway == "sn"
                     else ("8bitdo-sf30", "8BitDo SF30 (Super Famicom colours)"))
    pic = Picture(family, title)
    pic.sources = [
        size("size 144 x 63.5 x 26.2 mm", "sn30-wireless-for-original-snes-sfc"),
        traced("front view (SN30 2.4G): the outline, the control positions and sizes",
               "2020/sn30-2.4g/sn30-2.4g-black.png"),
        photo("the SNES pad the SN30 copies: the face buttons' dish", "SNES-Controller-Flat.jpg", "Evan-Amos",
              "public domain"),
        photo("the SF30 (the same body)", "8BitDo_SF30_Bluetooth_Gamepad.jpg", "StroggPicker", "CC BY-SA 4.0")]
    colors = dict(SN_COLOURS)
    view = pic.view("front", oy=768)
    top_shoulders(view, colors, ("L", "R", "", ""), -29.1, 27, 56.6, triggers=False, rise=0.5)
    body(view, SN30_SHELL, colors["plate"])
    # the SNES's right lobe: a darker concave dish holding the four buttons
    view.add("case", f'<path d="{circle_d(42.5, 0.4, 24.5)}" fill="{colors["dish"]}" '
                     f'stroke="{shade(colors["dish"], 0.8)}" stroke-width="0.25"/>')
    style.recess(view, -42.3, 0.15, 14.4, colors["plate"], depth=0.9)
    dpad(view, -42.3, 0.15, 23.7, 8.6, colors)
    face(view, 40.6, 0.4, (13.8, 10.4), 4.7, caps, SN_INKS)
    system_pair(view, 5.0, (-11.3, 3.7), 11.6, 3.8, colors, labels=("Select", "Start"), tray=(-3.9, 33, 8.2))
    home_button(view, 0, 18.8, 1.9, colors)
    return pic


# -- M30 ---------------------------------------------------------------------------------------------------------

def m30():
    pic = Picture("8bitdo-m30", "8BitDo M30 (2.4G / Bluetooth), black")
    pic.sources = [
        size("size 147 x 74 x 31 mm", "m30-2-4g/"),
        traced("front view: the outline, the control positions and sizes",
               "2023/m30-bluetooth/m30-bluetooth-black.png"),
        photo("the six buttons in two slanted rows, the round D-pad", "8BitDo_M30.png", "Llapissera", "CC BY-SA 3.0"),
        {"what": "the six face buttons on the canonical names as SDL_GameControllerDB maps the M30 (A south, B east, "
                 "C right shoulder, X west, Y north, Z left shoulder; L / R the triggers); an assumption to check on "
                 "a real pad", "source": "SDL_GameControllerDB (gamecontrollerdb.txt, 8BitDo M30)",
         "retrieved": "2026-10-23"}]
    colors = {"body": "#3a3c40", "plate": "#3a3c40", "trigger": "#313236", "bumper": "#36383c", "button": "#26272a",
              "small": "#2b2c2f", "dpad": "#2a2b2e", "arrow": "#8c8f94", "glyph": "#a9acb1", "accent": "#5a74b8",
              "dish": "#45474b"}
    view = pic.view("front", oy=768)
    top_shoulders(view, colors, ("L", "R", "", ""), -33.9, 21.5, 54.6, triggers=False, ids=("lt", "rt", "", ""),
                  rise=0.7)
    body(view, M30_SHELL, colors["body"])
    # the two dishes: round under the D-pad, wide under the six buttons
    for d in (circle_d(-38.3, -3.6, 24.3), circle_d(38, -4.6, 26)):
        fill = view.defs.linear([(0, shade(colors["dish"], 1.03)), (1, shade(colors["dish"], 0.96))])
        view.add("case", f'<path d="{d}" fill="{fill}" stroke="{shade(colors["body"], 0.8)}" stroke-width="0.25"/>')
    cx, cy = -38.3, -3.6
    disc = view.defs.radial([(0, shade(colors["dpad"], 0.92)), (0.8, colors["dpad"]), (1, shade(colors["dpad"], 1.1))],
                            r=0.5)
    view.add("controls", f'<path d="{circle_d(cx, cy, 15.9)}" fill="{disc}" stroke="{style.rim_of(colors["dpad"])}" '
                         f'stroke-width="0.25"/>', "dpad")
    style.glyph(view, style.cross_d(cx, cy, 25.4, 7.4, r=1.0), shade(colors["dpad"], 1.25), "dpad", width=0.35)
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.glyph(view, style.arrow_d(cx + dx * 8.4, cy + dy * 8.4, 1.7, direction), colors["arrow"], "dpad",
                    fill=True, opacity=0.8)
    common.dpad_spots(view, cx, cy, 25.4, 8.4, "left", prefix="dpad.")
    # two rows of three, slanted up to the right: the upper row smaller, as the Mega Drive's
    rows = (("south", "A", 25.4, 8.8, 5.6), ("east", "B", 39.1, 1.4, 5.6), ("rb", "C", 54.0, -3.0, 5.6),
            ("west", "X", 20.4, -6.8, 4.5), ("north", "Y", 33.1, -14.0, 4.5), ("lb", "Z", 46.3, -17.3, 4.5))
    for control, letter, x, y, r in rows:
        style.button(view, x, y, r, colors["button"], control)
        style.label(view, letter, x, y, r * 0.95, "#c9cbcf", control)
        view.spot(control, letter, ("circle", x, y, r + 0.4), "right")
    style.pill(view, 0, -3.7, 11.6, 4.4, colors["accent"], "menu")
    view.spot("menu", "Start", ("rect", -6.2, -6.3, 12.4, 5.2), "right")
    # three small round keys in a row under Start: heart, minus, home
    star_free = {"small": colors["small"], "glyph": colors["glyph"]}
    style.button(view, -9.0, 11.4, 3.6, colors["small"], "share")
    view.spot("share", "Heart", ("circle", -9.0, 11.4, 4.0), "left")
    round_key(view, 0, 11.4, 3.6, star_free, "view", "-", "-")
    home_button(view, 9.0, 11.4, 3.6, colors)
    return pic


# -- Zero 2 ------------------------------------------------------------------------------------------------------

def zero2():
    pic = Picture("8bitdo-zero-2", "8BitDo Zero 2, blue (drawn at twice the scale)")
    pic.sources = [
        size("size 73 x 36.5 x 14.7 mm, 20 g", "zero2/"),
        traced("front view: the outline, the control positions and sizes, the colours", "2019/zero2/zero2-blue.png"),
        {"what": "the scale: twice the pads' 8.6 px per mm, so the keychain pad stays readable in the dialog",
         "source": "this POC", "retrieved": "2026-10-23"}]
    colors = {"plate": "#62b4ba", "body": "#62b4ba", "trigger": "#d9d9d6", "bumper": "#e1e1de", "small": "#e3e2dd",
              "stick": "#2b2c2f", "dpad": "#e3e2dd", "arrow": "#b0b2b4", "glyph": "#9a9c9f", "button": "#e3e2dd"}
    view = pic.view("front", oy=768, scale=17.2)
    top_shoulders(view, colors, ("L", "R", "", ""), -16.7, 7.7, 26.5, triggers=False, rise=0.4)
    body(view, ZERO2_SHELL, colors["body"])
    dpad(view, -19.1, 0.7, 20.2, 6.8, colors)
    face(view, 19.0, 0.7, 7.5, 3.7, colors["button"], "#a3a5a8")
    for control, label, x in (("view", "Select", -4.0), ("menu", "Start", 4.0)):
        style.button(view, x, 9.0, 3.0, colors["small"], control)
        view.spot(control, label, ("circle", x, 9.0, 3.4), "left" if x < 0 else "right")
    return pic
