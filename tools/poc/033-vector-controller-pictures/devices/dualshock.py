"""The DualShock 4 (PlayStation 4) and the generic PlayStation-like pad drawn on its shape.

Sony's sizes: DualShock 4 about 162 x 98 x 52 mm, DualShock 3 about 157 x 95 x 55 mm. The plan is measured on a
near-orthographic top-view photo of a DS4 scaled to 162 mm (4.81 px per mm; reference only, nothing traced): the two
long grips going down from the wings, the D-pad as four separate keys in a cross on a round dish, the face buttons on a
second dish, both sticks side by side low in the centre, the PS button between them, the touchpad between Share and
Options, the light bar along the top edge. The generic pad keeps that silhouette (the look DS3 and DS4 share) with a
plain centre and two small keys (the DS3 Select / Start) instead of the touchpad.
"""

import math

import style
from devices import common
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
TOP = -48.3
SHELL = [("M", (0, TOP)),
         ("C", (14, TOP), (26, TOP - 0.1), (32, TOP - 0.5)),
         ("C", (40, TOP - 1.3), (46, TOP - 1.7), (52, TOP - 1.7)),
         ("C", (62, TOP - 1.7), (71, -44), (72.4, -32)),
         ("C", (73.6, -20), (77, 0), (78.6, 16)),
         ("C", (79.6, 28), (80, 42), (76.4, 50)),
         ("C", (73, 57), (62, 58.6), (54, 56)),
         ("C", (46, 53.4), (42, 48), (40.4, 40)),
         ("C", (39, 32), (36, 22), (32, 16)),
         ("C", (29, 12.4), (25, 11.5), (20, 11.5)),
         ("C", (12, 11.5), (6, 11.5), (0, 11.5))]
DPAD, FACE = (-51.8, -27.4), (51.8, -27.4)
DISH = 19.4
STICKS = ((-26.6, -5.8), (26.6, -5.8))
SYMBOLS = (("north", "Triangle", "triangle"), ("east", "Circle", "circle"), ("south", "Cross", "cross"),
           ("west", "Square", "square"))
# The DS4's printed symbol colours, muted (the owner's taste: restrained)
SYMBOL_INK = {"triangle": "#6aa596", "circle": "#c27474", "cross": "#8199c4", "square": "#b98bb3"}


def shoulders(view, colors, names):
    l1, r1, l2, r2 = names
    for sx, trig, bump, side, tname, bname in ((-1, "lt", "lb", "left", l2, l1), (1, "rt", "rb", "right", r2, r1)):
        f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
        trigger = (f"M{f(40, TOP - 2.6)}C{f(44, TOP - 9)} {f(53, TOP - 10.6)} {f(58, TOP - 10.4)}"
                   f"C{f(63, TOP - 10.2)} {f(67, TOP - 7.8)} {f(68.4, TOP - 3.2)}Z")
        bumper = (f"M{f(35, TOP - 0.4)}C{f(41, TOP - 4.8)} {f(53, TOP - 5.8)} {f(60, TOP - 5.4)}"
                  f"C{f(66, TOP - 5)} {f(70.4, TOP - 2.8)} {f(71.8, TOP + 1.8)}L{f(64, TOP + 3)}Z")
        style.shaped(view, trigger, colors["shoulder2"], trig, layer="shoulders")
        style.shaped(view, bumper, colors["shoulder"], bump, layer="shoulders")
        x0, x1 = sorted((43 * sx, 67 * sx))
        view.spot(trig, tname, ("rect", x0, TOP - 10.6, x1 - x0, 5.4), side, analog={"kind": "trigger", "fill": "down"})
        x0, x1 = sorted((37 * sx, 71 * sx))
        view.spot(bump, bname, ("rect", x0, TOP - 5.8, x1 - x0, 4.6), side)


def dish(view, cx, cy, colors, ticks):
    """The raised round dish under the D-pad or the face buttons, with the small printed direction marks"""
    fill = view.defs.linear([(0, shade(colors["dish"], 1.04)), (1, shade(colors["dish"], 0.95))])
    view.add("case", f'<path d="{circle_d(cx, cy + 0.3, DISH)}" fill="{style.GAP}" fill-opacity="0.35"/>')
    view.add("case", f'<path d="{circle_d(cx, cy, DISH)}" fill="{fill}" stroke="{style.rim_of(colors["dish"])}" '
                     f'stroke-width="0.25"/>')
    if ticks:
        for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
            view.add("legends", f'<path d="{style.arrow_d(cx + dx * 16.4, cy + dy * 16.4, 1.3, direction)}" '
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


def pad(pic, colors, inks, names, centre):
    """The shared DS4-shaped pad; @p centre draws the part between the wings (touchpad or panel)"""
    view = pic.view("front", oy=768 + 1.5 * 8.6)
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
    common.face_diamond(view, *FACE, 11.6, 5.1, labels)
    for name, label, (cx, cy), side in (("lstick", "L3", STICKS[0], "left"), ("rstick", "R3", STICKS[1], "right")):
        style.recess(view, cx, cy, 13.4, colors["body"], depth=0.62)
        style.stick(view, cx, cy, 9.4, colors["stick"], f"{name}.x", skirt=11.6)
        common.stick_spots(view, name, label, cx, cy, 9.8, 6.0, side)
    # the PS button without its logo (D5)
    common.neutral_guide(view, 0, -3.8, 3.4, colors["button"], colors["mark"], "guide")
    view.spot("guide", names["guide"], ("circle", 0, -3.8, 3.8), "right")
    return view


def system_pills(view, colors, names, y, x):
    for control, label, px in (("view", names["view"], -x), ("menu", names["menu"], x)):
        style.pill(view, px, y, 3.4, 6.2, colors["small"], control)
        view.spot(control, label, ("rect", px - 2.4, y - 3.8, 4.8, 7.6), "left" if px < 0 else "right")


def dualshock4():
    pic = Picture("dualshock-4", "DualShock 4 wireless controller (PS4), Jet Black")
    pic.sources = [
        {"what": "size about 162 x 98 x 52 mm (W x D x H)", "source": "Sony DualShock 4 specifications as quoted by "
         "retail listings and dimensions.com (dualshock-4)", "retrieved": "2026-10-23"},
        {"what": "top view: outline, control positions and sizes (measured at 4.81 px per mm, about 1 mm)",
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
        pad_d = rounded_d(-28, TOP + 0.8, 56, 25.4, 3.2)
        view.add("controls", f'<path d="{pad_d}" fill="{style.GAP}" fill-opacity="0.4" '
                             f'transform="translate(0 0.35)"/>', "touchpad.click")
        style.shaped(view, pad_d, colors["pad"], "touchpad.click", stroke=0.25)
        view.spot("touchpad.click", "Touchpad", ("rect", -28, TOP + 0.8, 56, 25.4), "right")
        # the light bar along the top edge, seen just over the touchpad
        style.line(view, f"M-21,{num(TOP + 0.35)}H21", "#7f93b8", 0.7, opacity=0.6, layer="highlights")
        for row, y in enumerate((-16.6, -15.0, -13.4)):  # the speaker grille
            for i in range(5 - row):
                x = (i - (4 - row) / 2) * 1.6
                view.add("case", f'<path d="{circle_d(x, y, 0.42)}" fill="{shade(colors["body"], 0.5)}"/>')
        system_pills(view, colors, names, TOP + 6.2, 33.4)

    view = pad(pic, colors, SYMBOL_INK, names, centre)
    view.add("case", f'<path d="{rounded_d(-6.5, 5.0, 6.2, 1.4, 0.7)}" fill="{shade(colors["body"], 0.55)}"/>')
    view.add("case", f'<path d="{circle_d(4.2, 5.7, 0.9)}" fill="{shade(colors["body"], 0.55)}"/>')
    return pic


def generic_like():
    pic = Picture("generic-playstation-like", "Generic pad (PlayStation-like layout)")
    pic.sources = [
        {"what": "the silhouette and layout the DualShock 3 and 4 share: long grips, D-pad keys in a cross, the "
                 "sticks side by side low in the centre; DS3 about 157 x 95 x 55 mm, DS4 about 162 x 98 x 52 mm",
         "source": "the dualshock-4 set of this POC, measured on " + COMMONS
         + "Sony_DualShock_4_wireless_controller_for_PlayStation_4_(black)_-_top_view.jpg (CC BY-SA 4.0)",
         "retrieved": "2026-10-23"},
        {"what": "the DualShock 3 layout (Select / Start between the dishes, no touchpad)", "source": COMMONS + "DualShock_3.jpg",
         "author": "Evan-Amos", "licence": "public domain", "use": "reference only, not traced",
         "retrieved": "2026-10-23"}]
    colors = {"body": "#585b60", "dish": "#53565b", "shoulder": "#505358", "shoulder2": "#4a4d52",
              "key": "#3a3c40", "button": "#35373b", "stick": "#36383c", "small": "#3a3c40", "mark": "#a3a6ab"}
    ink = "#d3d5d9"
    names = {"shoulders": ("L1", "R1", "L2", "R2"), "guide": "PS", "view": "Create", "menu": "Options"}

    def centre(view):
        # the DS3 arrangement: a plain centre, two small keys between the dishes (no touchpad, no wordmark)
        for control, label, x in (("view", names["view"], -11), ("menu", names["menu"], 11)):
            style.pill(view, x, -30, 6.6, 3.0, colors["small"], control)
            view.spot(control, label, ("rect", x - 3.8, -31.8, 7.6, 3.6), "left" if x < 0 else "right")

    pad(pic, colors, {s: ink for _, _, s in SYMBOLS}, names, centre)
    return pic
