"""The DualSense (PlayStation 5) in its standard white and black.

Sony's size: 160 x 66 x 106 mm (width x height x depth, the largest projection excluded); the plan is 160 x 106 mm.
Control positions and sizes are measured on a near-orthographic top-view photo scaled to that width (reference
only, nothing traced): the symmetric layout, the sticks low either side of the PS button, the D-pad and the face
buttons high on the white wings, the large touchpad between them, the black centre face.
"""

import math

import style
from devices import common
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
TOP = -52.0
SHELL = [("M", (0, TOP + 1)),
         ("C", (20, TOP + 1), (37, TOP + 1.2), (45, TOP + 1.4)),
         ("C", (58, TOP - 1.6), (71.5, TOP + 1.5), (76.6, TOP + 11.5)),
         ("C", (80.4, TOP + 19.5), (80.4, -14), (79, 2)),
         ("C", (77.6, 18), (74.2, 39), (69.2, 49.5)),
         ("C", (65.6, 56), (56.2, 56.6), (52.6, 50)),
         ("C", (50, 44), (48.6, 32), (44, 25)),
         ("C", (37, 19.6), (18, 19), (0, 19))]
# The black centre face: from under the touchpad out along the wings' inner edge to the grip tips
FACE_HALF = [("M", (0, -17)),
             ("L", (36.5, -17)),
             ("C", (42.5, -13.5), (50.5, 0), (56, 15)),
             ("C", (60, 27), (62.6, 40), (63.6, 53.4)),
             ("C", (59.6, 55.6), (54.2, 54.6), (52.6, 50)),
             ("C", (50, 44), (48.6, 32), (44, 25)),
             ("C", (37, 19.6), (18, 19), (0, 19))]
STICKS = ((-31.6, -0.8), (31.6, -0.8))
DPAD, FACE = (-59, -22.5), (59, -22.5)


def shoulders(view, colors):
    for sx, trig, bump, side, tname, bname in ((-1, "lt", "lb", "left", "L2", "L1"), (1, "rt", "rb", "right", "R2",
                                                                                     "R1")):
        f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
        trigger = (f"M{f(48, TOP)}C{f(52, TOP - 6.4)} {f(61, TOP - 8.2)} {f(67, TOP - 7.4)}"
                   f"C{f(72, TOP - 6.6)} {f(75.5, TOP - 3.2)} {f(76.5, TOP + 1)}L{f(62, TOP + 2)}Z")
        bumper = (f"M{f(43, TOP + 2)}C{f(48, TOP - 2.6)} {f(58, TOP - 4.6)} {f(66, TOP - 3.6)}"
                  f"C{f(73, TOP - 2.6)} {f(78.6, TOP + 3)} {f(79.6, TOP + 10.5)}L{f(70, TOP + 12)}Z")
        style.shaped(view, trigger, colors["shoulder2"], trig, layer="shoulders")
        style.shaped(view, bumper, colors["shoulder"], bump, layer="shoulders")
        x0, x1 = sorted((50 * sx, 75 * sx))
        view.spot(trig, tname, ("rect", x0, TOP - 8.2, x1 - x0, 7.0), side,
                  analog={"kind": "trigger", "fill": "down"})
        x0, x1 = sorted((46 * sx, 78 * sx))
        view.spot(bump, bname, ("rect", x0, TOP - 4.4, x1 - x0, 5.0), side)


def dpad_key(cx, cy, direction, colors, view):
    """One of the four separate D-pad keys: square at its outer end, pointed towards the centre"""
    a = {"up": 0, "right": 90, "down": 180, "left": 270}[direction]
    pts = [(-4.9, -17.2), (4.9, -17.2), (5.1, -8.6), (0, -4.6), (-5.1, -8.6)]
    rad = math.radians(a)
    rot = [(cx + x * math.cos(rad) - y * math.sin(rad), cy + x * math.sin(rad) + y * math.cos(rad)) for x, y in pts]
    mx, my = sum(p[0] for p in rot) / 5, sum(p[1] for p in rot) / 5
    grow = [(mx + (x - mx) * 1.09, my + (y - my) * 1.09) for x, y in rot]
    control = f"dpad.{direction}"
    style.shaped(view, polygon_d(rot), colors["key"], control, polygon_d(grow), stroke=0.25)
    tip = ((rot[0][0] + rot[1][0]) / 2 * 0.55 + rot[3][0] * 0.45, (rot[0][1] + rot[1][1]) / 2 * 0.55 + rot[3][1] * 0.45)
    style.glyph(view, style.arrow_d(*tip, 2.2, direction), colors["ink"], control, fill=True, opacity=0.8)
    xs, ys = [p[0] for p in rot], [p[1] for p in rot]
    view.spot(control, "D-pad", ("rect", min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)), "left",
              group="dpad")


def symbols(name):
    """The four face glyphs, stroked inside a button of radius r"""
    def draw(view, x, y, r, control, color):
        s = r * 0.5
        if name == "triangle":
            d = polygon_d([(x, y - s * 1.05), (x + s * 0.95, y + s * 0.62), (x - s * 0.95, y + s * 0.62)])
        elif name == "circle":
            d = circle_d(x, y, s * 0.92)
        elif name == "cross":
            d = f"M{num(x - s)},{num(y - s)}L{num(x + s)},{num(y + s)}M{num(x + s)},{num(y - s)}L{num(x - s)},{num(y + s)}"
        else:
            d = rounded_d(x - s * 0.82, y - s * 0.82, s * 1.64, s * 1.64, 0.15)
        style.glyph(view, d, color, control, width=0.42)
    return draw


def dualsense():
    pic = Picture("dualsense", "DualSense wireless controller (PS5), White")
    pic.sources = [
        {"what": "size 160 x 66 x 106 mm (largest projection excluded), 280 g",
         "source": "Sony PlayStation DualSense specifications, as quoted by dimensions.com "
                   "(playstation-5-dualsense-controller) and retail listings", "retrieved": "2026-10-23"},
        {"what": "top view: control positions and sizes, the black face's outline, the touchpad",
         "source": COMMONS + "KontrolerDualSense.jpg", "author": "Xephuwu", "licence": "CC BY-SA 4.0",
         "use": "reference only, not traced", "retrieved": "2026-10-23",
         "note": "measured to about 1 mm on a near-orthographic photo"},
        {"what": "the clear D-pad keys and face buttons, the stick caps", "source": COMMONS
         + "Playstation_Dualsense_controller.jpg", "author": "Sergey Galyonkin", "licence": "CC BY-SA 2.0",
         "use": "reference only", "retrieved": "2026-10-23"}]
    colors = {"body": "#dcdddb", "face": "#2b2c30", "shoulder": "#d3d4d2", "shoulder2": "#c9cac8",
              "pad": "#e2e3e1", "key": "#cfd1d4", "ink": "#7f838a", "stick": "#2c2d31", "small": "#d6d7d5",
              "glyph": "#8a8d92"}
    view = pic.view("front", oy=768 + 1.5 * 8.6)
    shoulders(view, colors)
    body = mirrored_d(SHELL)
    style.body(view, body, colors["body"])
    face = mirrored_d(FACE_HALF)
    style.body(view, face, colors["face"], width=0.25)
    style.sheen(view, body)
    # the touchpad, its light-bar slits along the sides
    pad = (f"M{num(-38)},{num(TOP + 1.4)}H{num(38)}C{num(39.6)},{num(TOP + 1.4)} {num(40.4)},{num(TOP + 2.4)} "
           f"{num(40.2)},{num(TOP + 4)}L{num(37.4)},{num(-20.5)}C{num(37.2)},{num(-18.4)} {num(35.8)},{num(-17.6)} "
           f"{num(33.6)},{num(-17.6)}C{num(12)},{num(-16.4)} {num(-12)},{num(-16.4)} {num(-33.6)},{num(-17.6)}"
           f"C{num(-35.8)},{num(-17.6)} {num(-37.2)},{num(-18.4)} {num(-37.4)},{num(-20.5)}L{num(-40.2)},"
           f"{num(TOP + 4)}C{num(-40.4)},{num(TOP + 2.4)} {num(-39.6)},{num(TOP + 1.4)} {num(-38)},{num(TOP + 1.4)}Z")
    view.add("controls", f'<path d="{pad}" fill="{style.GAP}" fill-opacity="0.4" '
                         f'transform="translate(0 0.35)"/>', "touchpad.click")
    style.shaped(view, pad, colors["pad"], "touchpad.click", stroke=0.25)
    view.spot("touchpad.click", "Touchpad", ("rect", -37, TOP + 2, 74, 32), "right")
    for sx in (-1, 1):
        style.line(view, f"M{num(sx * 40.9)},{num(TOP + 5)}L{num(sx * 38.2)},{num(-21)}", "#8ea3c4", 0.5,
                   opacity=0.55, layer="case")
    # Create and Options: small upright pills on the wings, their marks above
    for control, label, x in (("view", "Create", -44.5), ("menu", "Options", 44.5)):
        style.pill(view, x, -43.5, 3.2, 6.4, colors["small"], control)
        view.spot(control, label, ("rect", x - 2.4, -47.2, 4.8, 7.6), "left" if x < 0 else "right")
    for i in (-1, 0, 1):  # Create: three rays above its button
        a = math.radians(-90 + i * 32)
        x0, y0 = -44.5 + 1.6 * math.cos(a), -47.4 + 1.6 * math.sin(a)
        x1, y1 = -44.5 + 2.8 * math.cos(a), -47.4 + 2.8 * math.sin(a)
        style.glyph(view, f"M{num(x0)},{num(y0)}L{num(x1)},{num(y1)}", colors["glyph"], "view", width=0.35)
    for i in (-1, 0, 1):  # Options: three bars above its button
        style.glyph(view, f"M{num(43.2)},{num(-48.6 + i * 0.9)}H{num(45.8)}", colors["glyph"], "menu", width=0.35)
    for direction in ("up", "right", "down", "left"):
        dpad_key(*DPAD, direction, colors, view)
    labels = {c: {"cap": colors["key"], "name": n, "draw": (lambda s: (lambda v, x, y, r, ctl: symbols(s)(
        v, x, y, r, ctl, colors["ink"])))(s)} for c, n, s in (("north", "Triangle", "triangle"),
                                                              ("east", "Circle", "circle"),
                                                              ("south", "Cross", "cross"),
                                                              ("west", "Square", "square"))}
    common.face_diamond(view, *FACE, 12.4, 5.6, labels)
    # the sticks in their wells on the black face
    for name, label, (cx, cy), side in (("lstick", "L3", STICKS[0], "left"), ("rstick", "R3", STICKS[1], "right")):
        style.recess(view, cx, cy, 13.2, colors["face"], depth=0.62)
        style.stick(view, cx, cy, 10.6, colors["stick"], f"{name}.x", skirt=12.0)
        common.stick_spots(view, name, label, cx, cy, 11.0, 6.5, side)
    # the speaker grille, the PS button without its logo (D5), the mute button
    for row, y in enumerate((-10.6, -8.6)):
        for i in range(6):
            x = -6.25 + i * 2.5
            view.add("case", f'<path d="{circle_d(x, y, 0.55)}" fill="{shade(colors["face"], 0.45)}"/>')
    style.pill(view, 0, 2.6, 9.6, 6.0, shade(colors["face"], 0.85), "guide")
    view.spot("guide", "PS", ("rect", -5.2, -0.6, 10.4, 6.4), "right")
    style.pill(view, 0, 11.4, 8.4, 2.4, "#c8cacd", "misc.1")
    view.spot("misc.1", "Mute", ("rect", -4.6, 9.8, 9.2, 3.2), "right")
    return pic
