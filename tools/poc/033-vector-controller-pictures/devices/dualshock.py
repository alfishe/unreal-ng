"""The DualShock 4 (PlayStation 4) and the generic PlayStation-like pad drawn on its shape.

Sony's sizes: DualShock 4 about 162 x 98 x 52 mm, DualShock 3 about 157 x 95 x 55 mm. The outline is measured with
trace.py on an orthographic layout drawing of the DS4 (Tokyoship, CC BY 3.0; 162 x 100.6 mm when scaled to the width,
the depth within 3 % of Sony's), the control positions and sizes read on the same drawing on a 5 mm grid and checked
against a top-view photo: the wings curving down into the grips, which lean outwards and end in round tips narrower
than their roots, the deep U between them starting under the sticks, the stick housings standing out of its top, the
D-pad as four separate keys on a round dish, the face buttons on a second dish, the PS button between the sticks, the
touchpad between Share and Options, the light bar along the top edge. The generic pad is the same outline at the
DualShock 3's size (157 mm; the DS3 layout drawing on Commons is a three-quarter view, not a plan) with the DS3's
plain centre and its two small keys instead of the touchpad.
"""

import math

import style
from devices import common
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
LAYOUT = COMMONS + "Dualshock_4_Layout.svg"
# The right half of the outline, measured with trace.py on the orthographic layout drawing (scaled to 162 mm, the
# left half mirrored, cubic Beziers within 0.3 mm; the shoulder buttons above the case top cut off at 4 mm)
SHELL = [("M", (0, -45.5)),
         ("C", (7.9, -45.5), (15.9, -45.7), (23.8, -45.5)),
         ("C", (27.5, -45.4), (31.1, -44.9), (34.7, -45.1)),
         ("C", (36.0, -45.1), (37.1, -45.9), (38.3, -46.0)),
         ("C", (41.2, -46.3), (57.9, -46.5), (59.8, -45.6)),
         ("C", (61.5, -44.8), (62.3, -42.9), (63.3, -41.4)),
         ("C", (65.5, -38.1), (67.7, -34.7), (69.1, -31.0)),
         ("C", (74.7, -16.5), (78.4, 6.6), (80.1, 22.3)),
         ("C", (80.5, 26.0), (81.2, 30.0), (81.0, 33.8)),
         ("C", (80.8, 36.0), (80.3, 38.2), (79.3, 40.2)),
         ("C", (77.5, 43.9), (74.5, 47.1), (70.7, 48.8)),
         ("C", (67.1, 50.5), (63.0, 51.0), (59.0, 50.2)),
         ("C", (49.3, 48.2), (46.9, 40.6), (44.0, 32.3)),
         ("C", (42.6, 28.4), (41.2, 24.5), (39.9, 20.6)),
         ("C", (39.3, 18.8), (38.9, 16.4), (37.2, 15.4)),
         ("C", (35.6, 14.5), (34.1, 16.2), (32.7, 16.9)),
         ("C", (28.1, 19.0), (22.2, 19.0), (17.7, 16.5)),
         ("C", (16.5, 15.9), (15.4, 14.6), (14.1, 14.3)),
         ("C", (11.5, 13.5), (5.7, 14.2), (2.7, 14.2)),
         ("C", (2.1, 14.2), (0.6, 14.2), (0, 14.2))]
DPAD, FACE = (-50.2, -21.5), (50.2, -21.5)
DISH = 18.75
STICKS = ((-25.6, 0.5), (25.6, 0.5))
GUIDE = (0, 1.3)
SYMBOLS = (("north", "Triangle", "triangle"), ("east", "Circle", "circle"), ("south", "Cross", "cross"),
           ("west", "Square", "square"))
# The DS4's printed symbol colours, muted (the owner's taste: restrained)
SYMBOL_INK = {"triangle": "#6aa596", "circle": "#c27474", "cross": "#8199c4", "square": "#b98bb3"}


def shoulders(view, colors, names):
    """L1 / R1 over the case top between x 39 and 59 mm (as the layout drawing), L2 / R2 just showing behind them"""
    l1, r1, l2, r2 = names
    for sx, trig, bump, side, tname, bname in ((-1, "lt", "lb", "left", l2, l1), (1, "rt", "rb", "right", r2, r1)):
        f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
        trigger = (f"M{f(41, -47.6)}C{f(42.6, -52.4)} {f(46.4, -53.6)} {f(49.8, -53.6)}"
                   f"C{f(53.2, -53.6)} {f(56.8, -52.6)} {f(58.4, -47.6)}Z")
        bumper = (f"M{f(37.6, -44.6)}C{f(39.4, -48.8)} {f(44.6, -49.9)} {f(49.6, -49.9)}"
                  f"C{f(54.6, -49.9)} {f(59.4, -49)} {f(60.8, -45.8)}L{f(60.4, -43.6)}Z")
        style.shaped(view, trigger, colors["shoulder2"], trig, layer="shoulders")
        style.shaped(view, bumper, colors["shoulder"], bump, layer="shoulders")
        x0, x1 = sorted((41.4 * sx, 58 * sx))
        view.spot(trig, tname, ("rect", x0, -53.8, x1 - x0, 3.8), side, analog={"kind": "trigger", "fill": "down"})
        x0, x1 = sorted((38.6 * sx, 60.6 * sx))
        view.spot(bump, bname, ("rect", x0, -50.1, x1 - x0, 4.4), side)


def dish(view, cx, cy, colors, ticks):
    """The raised round dish under the D-pad or the face buttons, with the small printed direction marks"""
    fill = view.defs.linear([(0, shade(colors["dish"], 1.04)), (1, shade(colors["dish"], 0.95))])
    view.add("case", f'<path d="{circle_d(cx, cy + 0.3, DISH)}" fill="{style.GAP}" fill-opacity="0.35"/>')
    view.add("case", f'<path d="{circle_d(cx, cy, DISH)}" fill="{fill}" stroke="{style.rim_of(colors["dish"])}" '
                     f'stroke-width="0.25"/>')
    if ticks:
        for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
            view.add("legends", f'<path d="{style.arrow_d(cx + dx * 15.9, cy + dy * 15.9, 1.3, direction)}" '
                                f'fill="{colors["mark"]}" fill-opacity="0.7"/>')


def dpad_key(view, cx, cy, direction, colors):
    """A separate D-pad key: square-ended outside, pointed towards the centre, the gap in the middle"""
    a = math.radians({"up": 0, "right": 90, "down": 180, "left": 270}[direction])
    pts = [(-4.5, -13.2), (4.5, -13.2), (4.6, -6.8), (0, -3.2), (-4.6, -6.8)]
    rot = [(cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)) for x, y in pts]
    mx, my = sum(p[0] for p in rot) / 5, sum(p[1] for p in rot) / 5
    grow = [(mx + (x - mx) * 1.1, my + (y - my) * 1.1) for x, y in rot]
    control = f"dpad.{direction}"
    style.shaped(view, polygon_d(rot), colors["key"], control, polygon_d(grow), stroke=0.25)
    xs, ys = [p[0] for p in rot], [p[1] for p in rot]
    view.spot(control, "D-pad", ("rect", min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)), "left",
              group="dpad")


def symbol(name, color):
    def draw(view, x, y, r, control):
        s = r * 0.5
        if name == "triangle":
            d = polygon_d([(x, y - s * 1.05), (x + s * 0.95, y + s * 0.62), (x - s * 0.95, y + s * 0.62)])
        elif name == "circle":
            d = circle_d(x, y, s * 0.92)
        elif name == "cross":
            d = (f"M{num(x - s)},{num(y - s)}L{num(x + s)},{num(y + s)}"
                 f"M{num(x + s)},{num(y - s)}L{num(x - s)},{num(y + s)}")
        else:
            d = rounded_d(x - s * 0.82, y - s * 0.82, s * 1.64, s * 1.64, 0.15)
        style.glyph(view, d, color, control, width=0.42)
    return draw


def pad(pic, colors, inks, names, centre, scale=8.6):
    """The shared DS4-shaped pad; @p centre draws the part between the wings (touchpad or panel); @p scale in px
    per mm of the drawing (a smaller one draws the same outline at a smaller pad's size)"""
    view = pic.view("front", oy=768 + 1.25 * scale, scale=scale)
    shoulders(view, colors, names["shoulders"])
    body = mirrored_d(SHELL)
    style.body(view, body, colors["body"])
    style.sheen(view, body)
    centre(view)
    dish(view, *DPAD, colors, ticks=True)
    dish(view, *FACE, colors, ticks=False)
    for direction in ("up", "right", "down", "left"):
        dpad_key(view, *DPAD, direction, colors)
    labels = {c: {"cap": colors["button"], "name": n, "draw": symbol(s, inks[s])} for c, n, s in SYMBOLS}
    common.face_diamond(view, *FACE, 11.9, 5.0, labels)
    for name, label, (cx, cy), side in (("lstick", "L3", STICKS[0], "left"), ("rstick", "R3", STICKS[1], "right")):
        style.recess(view, cx, cy, 14.4, colors["body"], depth=0.62)
        style.stick(view, cx, cy, 9.6, colors["stick"], f"{name}.x", skirt=11.6)
        common.stick_spots(view, name, label, cx, cy, 10.0, 6.0, side)
    # the PS button without its logo (D5)
    common.neutral_guide(view, *GUIDE, 3.7, colors["button"], colors["mark"], "guide")
    view.spot("guide", names["guide"], ("circle", *GUIDE, 4.1), "right")
    return view


def system_pills(view, colors, names, y, x):
    for control, label, px in (("view", names["view"], -x), ("menu", names["menu"], x)):
        style.pill(view, px, y, 4.6, 8.4, colors["small"], control)
        view.spot(control, label, ("rect", px - 2.7, y - 4.6, 5.4, 9.2), "left" if px < 0 else "right")


def dualshock4():
    pic = Picture("dualshock-4", "DualShock 4 wireless controller (PS4), Jet Black")
    pic.sources = [
        {"what": "size about 162 x 98 x 52 mm (W x D x H)", "source": "Sony DualShock 4 specifications as quoted by "
         "retail listings and dimensions.com (dualshock-4)", "retrieved": "2026-10-23"},
        {"what": "outline (trace.py: silhouette, left half mirrored, Beziers within 0.3 mm, scaled to 162 mm) and the "
                 "control positions and sizes (read on a 5 mm grid)", "source": LAYOUT, "author": "Tokyoship",
         "licence": "CC BY 3.0", "use": "measured geometry only; no pixels or paths of the drawing are shipped",
         "retrieved": "2026-10-23"},
        {"what": "top view: the check of the proportions and of the control positions",
         "source": COMMONS + "Sony_DualShock_4_wireless_controller_for_PlayStation_4_(black)_-_top_view.jpg",
         "author": "ITEagle Europe - Sebastiaan Broekhoven", "licence": "CC BY-SA 4.0",
         "use": "reference only, not traced", "retrieved": "2026-10-23"},
        {"what": "the light bar on the top edge, the shoulders", "source": COMMONS
         + "Sony_DualShock_4_wireless_controller_for_PlayStation_4_(black)_-_rear_view.jpg",
         "author": "ITEagle Europe - Sebastiaan Broekhoven", "licence": "CC BY-SA 4.0", "use": "reference only",
         "retrieved": "2026-10-23"},
        {"what": "three-quarter view, the face button symbols' colours", "source": COMMONS + "DualShock_4.jpg",
         "author": "Evan-Amos", "licence": "public domain", "use": "reference only", "retrieved": "2026-10-23"}]
    colors = {"body": "#3a3c40", "dish": "#35373b", "shoulder": "#36383c", "shoulder2": "#313236",
              "key": "#2a2b2e", "button": "#26272a", "stick": "#2b2c30", "small": "#2c2d31", "pad": "#303236",
              "mark": "#9a9da3", "glyph": "#a9acb1"}
    names = {"shoulders": ("L1", "R1", "L2", "R2"), "guide": "PS", "view": "Share", "menu": "Options"}

    def centre(view):
        pad_d = rounded_d(-26.2, -44.9, 52.4, 28.8, 3.2)
        view.add("controls", f'<path d="{pad_d}" fill="{style.GAP}" fill-opacity="0.4" '
                             f'transform="translate(0 0.35)"/>', "touchpad.click")
        style.shaped(view, pad_d, colors["pad"], "touchpad.click", stroke=0.25)
        view.spot("touchpad.click", "Touchpad", ("rect", -26.2, -44.9, 52.4, 28.8), "right")
        # the light bar along the top edge, seen just over the touchpad
        style.line(view, "M-20,-45.15H20", "#7f93b8", 0.6, opacity=0.6, layer="highlights")
        for row, y in enumerate((-10.2, -8.1, -6.0)):  # the speaker grille
            for i in range(5 - row):
                x = (i - (4 - row) / 2) * 2.85
                view.add("case", f'<path d="{circle_d(x, y, 0.62)}" fill="{shade(colors["body"], 0.5)}"/>')
        system_pills(view, colors, names, -37.4, 33.0)

    pad(pic, colors, SYMBOL_INK, names, centre)
    return pic


def generic_like():
    pic = Picture("generic-playstation-like", "Generic pad (PlayStation-like layout)")
    pic.sources = [
        {"what": "the silhouette and layout the DualShock 3 and 4 share: the DS4's measured outline and positions "
                 "drawn at the DS3's width (157 of 162 mm; DS3 about 157 x 95 x 55 mm)",
         "source": "the dualshock-4 set of this POC, measured on " + LAYOUT + " (Tokyoship, CC BY 3.0)",
         "retrieved": "2026-10-23"},
        {"what": "the DS3 front (three-quarter) layout: Select / Start between the dishes, the PS button between the "
                 "sticks", "source": COMMONS + "Dualshock3_Layout.svg", "author": "Tokyoship", "licence": "CC BY 3.0",
         "use": "reference only", "retrieved": "2026-10-23"},
        {"what": "the DualShock 3 layout (Select / Start between the dishes, no touchpad)", "source": COMMONS + "DualShock_3.jpg",
         "author": "Evan-Amos", "licence": "public domain", "use": "reference only, not traced",
         "retrieved": "2026-10-23"}]
    colors = {"body": "#585b60", "dish": "#53565b", "shoulder": "#505358", "shoulder2": "#4a4d52",
              "key": "#3a3c40", "button": "#35373b", "stick": "#36383c", "small": "#3a3c40", "mark": "#a3a6ab"}
    ink = "#d3d5d9"
    names = {"shoulders": ("L1", "R1", "L2", "R2"), "guide": "PS", "view": "Create", "menu": "Options"}

    def centre(view):
        # the DS3 arrangement: a plain centre, two small keys between the dishes (no touchpad, no wordmark)
        for control, label, x in (("view", names["view"], -12.6), ("menu", names["menu"], 12.6)):
            style.pill(view, x, -24, 6.8, 3.2, colors["small"], control)
            view.spot(control, label, ("rect", x - 3.8, -26, 7.6, 4.0), "left" if x < 0 else "right")

    pad(pic, colors, {s: ink for _, _, s in SYMBOLS}, names, centre, scale=8.6 * 157 / 162)
    return pic
