"""The Nintendo Switch Pro Controller, black.

Nintendo's size: about 152 x 106 x 60 mm (width x height x depth). The layout is measured on a three-quarter
press-style photo corrected to a front view, so its positions are an estimate to about 2 mm (reference only, nothing
traced): the offset layout, the left stick high, the D-pad and the right stick low; minus, plus, capture and home
between the sticks; the four player lights at the lower edge; the smoked face plate between matte grips.
"""

import style
from devices import common
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
TOP = -46.0
SHELL = [("M", (0, TOP)),
         ("C", (18, TOP), (40, TOP - 0.8), (55, TOP + 0.6)),
         ("C", (67, TOP + 1.8), (74, TOP + 7), (75.6, TOP + 18)),
         ("C", (76.8, -18), (76.6, -4), (75.6, 9)),
         ("C", (74.4, 24), (71.4, 39), (66.2, 48.6)),
         ("C", (62, 56.4), (52.4, 57.2), (48.4, 51)),
         ("C", (45.2, 46), (43, 36.4), (39.4, 29.4)),
         ("C", (34, 23.6), (19, 23), (0, 23))]
LSTICK, RSTICK, DPAD, FACE = (-40.5, -20), (21.5, 6.5), (-22.5, 6.5), (41.5, -11.5)
LETTERS = {"north": "X", "east": "A", "south": "B", "west": "Y"}


def grip_d(sx):
    f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
    return (f"M{f(76.6, -14)}C{f(76.8, -4)} {f(76.2, 2)} {f(75.6, 9)}C{f(74.4, 24)} {f(71.4, 39)} {f(66.2, 48.6)}"
            f"C{f(62, 56.4)} {f(52.4, 57.2)} {f(48.4, 51)}C{f(45.2, 46)} {f(43, 36.4)} {f(39.4, 29.4)}"
            f"L{f(41.5, 27)}C{f(50, 17)} {f(62, 2)} {f(76.6, -14)}Z")


def shoulders(view, colors):
    for sx, trig, bump, side, tname, bname in ((-1, "lt", "lb", "left", "ZL", "L"), (1, "rt", "rb", "right", "ZR",
                                                                                   "R")):
        f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
        trigger = (f"M{f(36, TOP)}C{f(40, TOP - 6.4)} {f(50, TOP - 8.4)} {f(57, TOP - 8)}"
                   f"C{f(64.5, TOP - 7.6)} {f(70, TOP - 4.6)} {f(72.5, TOP)}L{f(60, TOP + 2)}Z")
        bumper = (f"M{f(22, TOP + 1.4)}C{f(32, TOP - 2.4)} {f(46, TOP - 4.2)} {f(57, TOP - 3.4)}"
                  f"C{f(68, TOP - 2.6)} {f(76, TOP + 2.8)} {f(78, TOP + 11.5)}L{f(70, TOP + 16)}L{f(24, TOP + 6)}Z")
        style.shaped(view, trigger, colors["trigger"], trig, layer="shoulders")
        style.shaped(view, bumper, colors["bumper"], bump, layer="shoulders")
        x0, x1 = sorted((38 * sx, 71 * sx))
        view.spot(trig, tname, ("rect", x0, TOP - 8.4, x1 - x0, 7.4), side, analog={"kind": "trigger", "fill": "down"})
        x0, x1 = sorted((28 * sx, 75 * sx))
        view.spot(bump, bname, ("rect", x0, TOP - 3.6, x1 - x0, 5.4), side)


def pro():
    pic = Picture("switch-pro", "Nintendo Switch Pro Controller, black")
    pic.sources = [
        {"what": "size about 152 x 106 x 60 mm (W x H x D)", "source": "retail listings of the Switch Pro "
         "Controller (thegoodguys.com.au 142991, jbhifi.business)", "retrieved": "2026-10-23"},
        {"what": "layout, buttons, the smoked face plate and matte grips, the player lights",
         "source": COMMONS + "Nintendo-Switch-Pro-Controller-FL.jpg", "author": "Evan-Amos",
         "licence": "public domain", "use": "reference only, not traced", "retrieved": "2026-10-23",
         "note": "a three-quarter view corrected to a front view by eye: positions to about 2 mm, an estimate"}]
    colors = {"body": "#3b3d41", "grip": "#333437", "trigger": "#303134", "bumper": "#37393c", "button": "#26272a",
              "small": "#2a2b2e", "stick": "#2b2c2f", "dpad": "#27282b", "letter": "#d2d4d8", "glyph": "#b9bcc1"}
    view = pic.view("front", oy=768 - 4 * 8.6)
    shoulders(view, colors)
    body = mirrored_d(SHELL)
    style.body(view, body, colors["body"])
    for sx in (-1, 1):
        style.body(view, grip_d(sx), colors["grip"], width=0.25)
    style.sheen(view, body)
    for name, label, (cx, cy), side in (("lstick", "LS", LSTICK, "left"), ("rstick", "RS", RSTICK, "right")):
        style.recess(view, cx, cy, 12.6, colors["body"], depth=0.6)
        style.stick(view, cx, cy, 9.4, colors["stick"], f"{name}.x", skirt=11.2)
        common.stick_spots(view, name, label, cx, cy, 9.8, 6.0, side)
    cx, cy = DPAD
    style.recess(view, cx, cy, 14.2, colors["body"], depth=0.8)
    span, arm = 24.5, 8.4
    style.shaped(view, style.cross_d(cx, cy, span, arm, r=1.2), colors["dpad"], "dpad",
                 style.cross_d(cx, cy, span + 1.0, arm + 1.0, r=1.5))
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.glyph(view, style.arrow_d(cx + dx * 8.2, cy + dy * 8.2, 1.9, direction), colors["glyph"], "dpad",
                    fill=True, opacity=0.85)
    common.dpad_spots(view, cx, cy, span, arm, "left", prefix="dpad.")
    labels = {c: {"cap": colors["button"], "text": t, "name": t, "ink": colors["letter"], "face": "sans"}
              for c, t in LETTERS.items()}
    common.face_diamond(view, *FACE, 10.0, 5.1, labels)
    # minus, plus; capture under minus, home under plus
    for control, label, x in (("view", "-", -15.5), ("menu", "+", 15.5)):
        style.button(view, x, -30, 3.2, colors["small"], control)
        bar = f"M{num(x - 1.4)},-30H{num(x + 1.4)}"
        if control == "menu":
            bar += f"M{num(x)},-31.4V-28.6"
        style.glyph(view, bar, colors["glyph"], control, width=0.45)
        view.spot(control, label, ("circle", x, -30, 3.7), "left" if x < 0 else "right")
    style.shaped(view, rounded_d(-12.8, -21.2, 5.6, 5.6, 1.0), colors["small"], "share",
                 rounded_d(-13.2, -21.6, 6.4, 6.4, 1.3))
    style.glyph(view, circle_d(-10, -18.4, 1.5), colors["glyph"], "share", width=0.35)
    view.spot("share", "Capture", ("rect", -13.4, -21.8, 6.8, 6.8), "left")
    style.button(view, 10, -18.4, 3.6, colors["small"], "guide")
    style.glyph(view, circle_d(10, -18.4, 3.0), "#8e8aa6", "guide", width=0.35, opacity=0.8)  # the ring light
    house = polygon_d([(8.6, -17.3), (8.6, -18.7), (10, -19.9), (11.4, -18.7), (11.4, -17.3)])
    style.glyph(view, house, colors["glyph"], "guide", width=0.3)
    view.spot("guide", "Home", ("circle", 10, -18.4, 4.0), "right")
    for i in range(4):  # the player lights on the lower edge
        view.add("case", f'<path d="{rounded_d(-5.6 + i * 3.2 - 0.5, 19.6, 1.0, 0.6, 0.3)}" '
                         f'fill="{shade(colors["body"], 0.55)}"/>')
    return pic
