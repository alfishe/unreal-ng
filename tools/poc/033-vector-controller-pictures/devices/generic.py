"""The generic pad: no maker's shape, the fallback for a family without a picture of its own.

A neutral twin-stick pad of the common 150 x 100 mm class in a mid grey, drawn in three variants that differ only in
the layout and the legends of the family template (devices.json `templates`, `glyphs`): Xbox-like (offset sticks,
A B X Y, with the four back buttons of the pads that have them), Nintendo-like (offset, positional B A Y X) and unknown (offset, the raw button numbers). The PlayStation-like pad
has the DualShock shape (devices/dualshock.py).
"""

import style
from devices import common
from model import Picture
from svgkit import mirrored_d, num, rounded_d, shade

TOP = -41.0
SHELL = [("M", (0, TOP)),
         ("C", (20, TOP), (40, TOP - 1.6), (55, TOP + 0.4)),
         ("C", (68, TOP + 2.2), (75.4, TOP + 10), (76.2, TOP + 24)),
         ("C", (77, 0), (75.2, 20), (70.4, 35.6)),
         ("C", (66.4, 48), (57.4, 53.6), (50, 49.6)),
         ("C", (44.4, 46.6), (41.4, 38), (37.4, 31)),
         ("C", (32.4, 25.4), (18, 24.4), (0, 24.4))]

VARIANTS = {
    "xbox": {"faces": {"south": "A", "east": "B", "west": "X", "north": "Y"}, "system": ("View", "Guide", "Menu"),
             "shoulders": ("LB", "RB", "LT", "RT"), "layout": "offset", "paddles": True},
    "nintendo": {"faces": {"south": "B", "east": "A", "west": "Y", "north": "X"}, "system": ("-", "Home", "+"),
                 "shoulders": ("L", "R", "ZL", "ZR"), "layout": "offset", "paddles": False},
    "unknown": {"faces": {"south": "1", "east": "2", "west": "3", "north": "4"}, "system": ("9", "13", "10"),
                "shoulders": ("LB", "RB", "LT", "RT"), "layout": "offset", "paddles": False},
}
COLORS = {"body": "#585b60", "trigger": "#4a4d52", "bumper": "#505358", "button": "#35373b", "small": "#3a3c40",
          "stick": "#36383c", "dpad": "#37393d", "letter": "#d3d5d9", "glyph": "#c3c6cb"}


def shoulders(view, names):
    lb, rb, lt, rt = names
    for sx, trig, bump, side, tname, bname in ((-1, "lt", "lb", "left", lt, lb), (1, "rt", "rb", "right", rt, rb)):
        f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
        trigger = (f"M{f(36, TOP)}C{f(40, TOP - 6.2)} {f(50, TOP - 8)} {f(57, TOP - 7.6)}"
                   f"C{f(64.5, TOP - 7.2)} {f(70, TOP - 4.4)} {f(72.5, TOP + 0.4)}L{f(60, TOP + 2)}Z")
        bumper = (f"M{f(22, TOP + 1.2)}C{f(32, TOP - 2.6)} {f(46, TOP - 4.4)} {f(57, TOP - 3.6)}"
                  f"C{f(68, TOP - 2.8)} {f(76, TOP + 3)} {f(78.2, TOP + 12)}L{f(70, TOP + 17)}L{f(24, TOP + 6)}Z")
        style.shaped(view, trigger, COLORS["trigger"], trig, layer="shoulders")
        style.shaped(view, bumper, COLORS["bumper"], bump, layer="shoulders")
        x0, x1 = sorted((38 * sx, 71 * sx))
        view.spot(trig, tname, ("rect", x0, TOP - 8, x1 - x0, 7.2), side, analog={"kind": "trigger", "fill": "down"})
        x0, x1 = sorted((28 * sx, 75 * sx))
        view.spot(bump, bname, ("rect", x0, TOP - 3.8, x1 - x0, 5.4), side)


def pad(family, variant):
    spec = VARIANTS[variant]
    titles = {"xbox": "Xbox-like", "nintendo": "Nintendo-like", "unknown": "unknown"}
    pic = Picture(family, f"Generic pad ({titles[variant]} layout)")
    pic.sources = [{"what": "a neutral pad of the 150 x 100 mm class: no maker's shape, the layout of the family "
                            "template", "source": "own design after the common layout of the C5 pads "
                                                  "(xbox-series, dualsense, switch-pro in this set)",
                    "retrieved": "2026-10-23"}]
    oy = 768 - (9 if spec["paddles"] else 2) * 8.6
    view = pic.view("front", oy=oy)
    shoulders(view, spec["shoulders"])
    body = mirrored_d(SHELL)
    style.body(view, body, COLORS["body"])
    style.sheen(view, body)
    if spec["layout"] == "offset":
        lstick, rstick, dpad, face = (-41, -17), (21.5, 6), (-21.5, 6), (41, -17)
    else:
        lstick, rstick, dpad, face = (-24, 8), (24, 8), (-48, -15), (48, -15)
    for name, label, (cx, cy), side in (("lstick", "LS", lstick, "left"), ("rstick", "RS", rstick, "right")):
        style.recess(view, cx, cy, 12.6, COLORS["body"], depth=0.62)
        style.stick(view, cx, cy, 9.6, COLORS["stick"], f"{name}.x", skirt=11.2)
        common.stick_spots(view, name, label, cx, cy, 10.0, 6.0, side)
    cx, cy = dpad
    style.recess(view, cx, cy, 14.0, COLORS["body"], depth=0.75)
    span, arm = 24.5, 8.4
    style.shaped(view, style.cross_d(cx, cy, span, arm, r=1.2), COLORS["dpad"], "dpad",
                 style.cross_d(cx, cy, span + 1.0, arm + 1.0, r=1.5))
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.glyph(view, style.arrow_d(cx + dx * 8.2, cy + dy * 8.2, 1.8, direction), COLORS["glyph"], "dpad",
                    fill=True, opacity=0.6)
    common.dpad_spots(view, cx, cy, span, arm, "left", prefix="dpad.")
    labels = {c: {"cap": COLORS["button"], "text": t, "name": t, "ink": COLORS["letter"]}
              for c, t in spec["faces"].items()}
    common.face_diamond(view, *face, 10.0, 5.2, labels)
    view_label, guide_label, menu_label = spec["system"]
    sy = -24 if spec["layout"] == "offset" else -26
    for control, label, x in (("view", view_label, -11.5), ("menu", menu_label, 11.5)):
        style.pill(view, x, sy, 6.4, 3.4, COLORS["small"], control)
        view.spot(control, label, ("rect", x - 3.6, sy - 2.1, 7.2, 4.2), "left" if x < 0 else "right")
    common.neutral_guide(view, 0, sy - 10, 5.6, COLORS["small"], "#9a9da3", "guide")
    view.spot("guide", guide_label, ("circle", 0, sy - 10, 6.0), "right")
    if spec["paddles"]:
        back_buttons(view)
    return pic


def back_buttons(view):
    """The four back buttons as a strip under the pad: the left hand's P3 / P4, the right hand's P1 / P2, as the pad
    has them on its back (an inset, not a view)"""
    y = 62.0
    panel = rounded_d(-46, y - 6.4, 92, 12.8, 6.4)
    style.body(view, panel, shade(COLORS["body"], 0.9), width=0.3)
    for control, name, x, side in (("paddle.p3", "P3", -32, "left"), ("paddle.p4", "P4", -15, "left"),
                                   ("paddle.p2", "P2", 15, "right"), ("paddle.p1", "P1", 32, "right")):
        style.pill(view, x, y, 13, 6.2, COLORS["button"], control)
        style.label(view, name, x, y, 3.0, COLORS["letter"], control, opacity=0.85)
        view.spot(control, name, ("rect", x - 7, y - 3.6, 14, 7.2), side)
