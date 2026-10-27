"""More Xbox pads: the Xbox 360 controller (wired and wireless share the shell) and the Xbox Adaptive Controller.

Xbox 360: Microsoft's 152 x 107 x 54 mm (5.98 x 4.21 x 2.13 in, the wired controller's spec sheet). The outline is
trace.py on FIG. 2 (top plan view) of Microsoft's design patent USD521015 (the dashed outline filled, the right half
mirrored, scaled to 152 mm: 104.7 mm deep without the triggers, which add about 2.3 mm behind the bumpers); the
control positions and sizes are read on the same drawing on a 5 mm grid and along columns through the controls
(about 0.5 mm). The grey lower shell shows along the arch and the grip ends (the second line of the drawing); the
bumpers are the bands over the top corners. The guide button's X is left out (D5); the ring of light, its quadrant
numbers and the BACK / START legends stay.

Xbox Adaptive Controller: 292 x 130 x 23 mm (Microsoft). Drawn on a top view photo that is orthographic within
1 % (its slab measures 2.22 : 1, the maker's 2.25 : 1), scaled to 292 mm, every part read on a 5 mm grid and with
blob fits; the back edge with its 19 jacks is a strip under the top view, the jacks in the order of the labels
embossed along the top edge (the edge seen through the device, so a jack sits under its label). The jacks of the
inputs on the top (A, B, View, Menu, Xbox, the D-pad) are drawn without hotspots: the top's controls carry those
names.
"""

import math

import style
from devices import common
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
PATENT = "https://patents.google.com/patent/USD521015S1/en"


def sample(half, steps=24):
    """The points along the right half of an outline (mirrored_d's format), top centre to bottom centre"""
    pts = [half[0][1]]
    for seg in half[1:]:
        p0 = pts[-1]
        if seg[0] == "L":
            pts.append(seg[1])
            continue
        _, c1, c2, p3 = seg
        for i in range(1, steps + 1):
            t = i / steps
            u = 1 - t
            pts.append(tuple(u ** 3 * a + 3 * u * u * t * b + 3 * u * t * t * c + t ** 3 * d
                             for a, b, c, d in zip(p0, c1, c2, p3)))
    return pts


def nearest(points, target):
    return min(range(len(points)), key=lambda i: (points[i][0] - target[0]) ** 2 + (points[i][1] - target[1]) ** 2)


def mirror(points):
    """A right half's points and the left half mirrored back, as one closed polygon"""
    return points + [(-x, y) for x, y in reversed(points)]


def side_of(x):
    return "left" if x < 0 else "right"


# -- Xbox 360 ------------------------------------------------------------------------------------------------------

X360_SHELL = [("M", (0, -43.9)),
    ("C", (0.9, -43.9), (2.1, -43.9), (3.0, -43.9)),
    ("C", (7.7, -43.8), (13.2, -43.8), (17.7, -45.3)),
    ("C", (19.8, -46.0), (21.4, -47.2), (23.3, -48.1)),
    ("C", (25.3, -49.1), (27.6, -49.6), (29.5, -50.7)),
    ("C", (30.4, -51.2), (30.5, -52.4), (31.4, -52.7)),
    ("C", (34.1, -53.8), (42.1, -52.2), (44.9, -51.3)),
    ("C", (47.5, -50.5), (53.3, -48.8), (54.9, -46.6)),
    ("C", (55.6, -45.7), (55.5, -44.1), (56.1, -43.0)),
    ("C", (65.1, -24.9), (82.2, 25.7), (73.9, 44.7)),
    ("C", (65.9, 62.6), (44.7, 42.0), (36.4, 35.2)),
    ("C", (32.6, 32.2), (28.7, 29.3), (24.3, 27.2)),
    ("C", (17.7, 24.1), (9.9, 24.2), (2.8, 23.9)),
    ("C", (2.2, 23.9), (1.6, 23.9), (1.0, 23.9)),
    ("C", (0.7, 23.9), (-0.3, 23.9), (0, 23.9))]
# The edge of the grey lower shell (the drawing's second line along the arch), centre to where it meets the outline
X360_BAND = [(0, 20.5), (8, 20.7), (16, 21.4), (24, 23.2), (32, 27.3), (40, 33.8), (48, 41.5), (56, 49.0)]
X360_BAND_END = (60.5, 51.8)
# The bumper's lower edge over the top corner, its inner end to where it runs into the outline
X360_BUMPER = [(30.6, -49.6), (36, -49.3), (40, -49.0), (44, -48.1), (48, -46.6), (52, -44.3), (56, -41.9),
               (58.0, -39.2)]
# The two creases of the top face, from the centre into the bumpers
X360_CREASES = ([(0, -39.9), (8, -40.0), (12, -40.1), (16, -40.8), (20, -42.8), (24, -45.3), (29, -48.6)],
                [(0, -36.6), (8, -36.6), (12, -36.9), (16, -37.8), (20, -39.6), (24, -42.5), (29, -46.2),
                 (34, -47.6), (40, -47.4), (48, -45.1), (56, -41.6)])
X360_LSTICK, X360_RSTICK, X360_DPAD, X360_FACE = (-41.8, -25.6), (19.7, -0.3), (-21.6, -0.6), (42.4, -24.6)
X360_GUIDE_Y = -24.1
X360_LETTERS = {"north": ("Y", "#d9a23a"), "west": ("X", "#3f72c0"), "east": ("B", "#c8453c"),
                "south": ("A", "#5f9f4c")}


def polyline_d(points):
    return "M" + "L".join(f"{num(x)},{num(y)}" for x, y in points)


def x360():
    pic = Picture("xbox-360", "Xbox 360 Controller (wired and wireless), white")
    pic.sources = [
        {"what": "size 152 x 107 x 54 mm (5.98 x 4.21 x 2.13 in)",
         "source": "Microsoft Xbox 360 Controller for Windows spec sheet (XBX0008), as neweggbusiness.com lists it",
         "retrieved": "2026-10-24"},
        {"what": "FIG. 2, top plan view: the outline (trace.py, the dashed line filled, the right half mirrored, "
                 "104.7 mm deep at 152 mm against 107 mm with the triggers), the grey lower shell's edge, the "
                 "bumpers, the creases, every control's position and size",
         "source": PATENT, "author": "Microsoft Corporation (inventors J. A. Hayes, M. Baseflug)",
         "licence": "US design patent drawing (public record)",
         "use": "measured geometry only (trace.py outline, positions on a grid); no pixels shipped",
         "retrieved": "2026-10-24"},
        {"what": "colours: white top shell, light grey lower shell, grey sticks and D-pad, the jewel face buttons, "
                 "the ring of light and its numbers, the BACK / START legends",
         "source": COMMONS + "Xbox_360_controller_top_(3455250267).jpg", "author": "I G", "licence": "CC0",
         "use": "reference only, not traced", "retrieved": "2026-10-24"},
        {"what": "the wired and the wireless controller share this shell (the wireless one's battery pack is "
                 "underneath, its connect button on the top edge between the bumpers, not seen from above)",
         "source": COMMONS + "Xbox-360-Wireless-Controller-White.png", "author": "Evan-Amos",
         "licence": "public domain", "use": "reference only, not traced", "retrieved": "2026-10-24"},
        {"what": "the triggers' rise behind the bumpers (2.3 mm, the spec's depth less the drawing's)",
         "source": "this POC", "note": "an estimate", "retrieved": "2026-10-24"}]
    colors = {"body": "#e3e4e2", "band": "#a9acaf", "trigger": "#d0d1cf", "bumper": "#c3c5c7", "stick": "#8b8e91",
              "dpad": "#8e9194", "small": "#e9eae8", "glyph": "#7b7e83", "guide": "#b6b9bc", "guide_ring": "#6c6f73",
              "light": "#5d6064", "legend": "#8a8d91"}
    view = pic.view("front", oy=768 - 1.0 * 8.6)
    outline = sample(X360_SHELL)

    # the triggers behind the bumpers, the bumpers as bands over the top corners
    for sx, control, name in ((-1, "lt", "LT"), (1, "rt", "RT")):
        top = [(sx * x, y - 2.3) for x, y in outline if 33 <= x <= 54 and y < -46]
        d = polygon_d(top + [(sx * 54, -46.0), (sx * 33, -50.0)])
        style.shaped(view, d, colors["trigger"], control, layer="shoulders")
        x0, x1 = sorted((sx * 33, sx * 55))
        view.spot(control, name, ("rect", x0, -55.6, x1 - x0, 5.0), side_of(sx),
                  analog={"kind": "trigger", "fill": "down"})
    band = outline[:nearest(outline, X360_BAND_END) + 1] + list(reversed(X360_BAND))
    style.body(view, mirrored_d(X360_SHELL), colors["band"])
    top = polygon_d(mirror(band))
    style.body(view, top, colors["body"], rim=False)
    style.sheen(view, top)
    for crease in X360_CREASES:
        for sx in (-1, 1):
            style.line(view, polyline_d([(sx * x, y) for x, y in crease]), shade(colors["body"], 0.82), 0.25,
                       opacity=0.7)
    for sx, control, name in ((-1, "lb", "LB"), (1, "rb", "RB")):
        edge = [(x, y) for x, y in outline if X360_BUMPER[0][0] <= x <= X360_BUMPER[-1][0] and y < -38.5]
        pts = [(sx * x, y) for x, y in edge + list(reversed(X360_BUMPER))]
        style.shaped(view, polygon_d(pts), colors["bumper"], control, stroke=0.25)
        x0, x1 = sorted((sx * 30.6, sx * 58))
        view.spot(control, name, ("rect", x0, -53.4, x1 - x0, 5.0), side_of(sx))

    # sticks: the well, the grey cap with its ring of bumps
    for name, label, (cx, cy), side in (("lstick", "LS", X360_LSTICK, "left"), ("rstick", "RS", X360_RSTICK, "right")):
        style.recess(view, cx, cy, 13.9, colors["body"], depth=0.82)
        style.stick(view, cx, cy, 9.6, colors["stick"], f"{name}.x", skirt=11.2)
        for i in range(4):
            a = math.pi / 4 + i * math.pi / 2
            view.add("controls", f'<path d="{circle_d(cx + 7.9 * math.cos(a), cy + 7.9 * math.sin(a), 0.45)}" '
                                 f'fill="{shade(colors["stick"], 0.8)}"/>', f"{name}.x")
        common.stick_spots(view, name, label, cx, cy, 10.0, 6.5, side)

    # the D-pad: a disc with a raised cross in its bezel
    cx, cy = X360_DPAD
    style.recess(view, cx, cy, 16.9, colors["body"], depth=0.86)
    style.line(view, circle_d(cx, cy, 15.0), shade(colors["body"], 0.8), 0.3)
    style.button(view, cx, cy, 12.6, shade(colors["dpad"], 1.04), "dpad", gap=0.35)
    fill = view.defs.linear([(0, shade(colors["dpad"], 1.04)), (1, shade(colors["dpad"], 0.96))])
    view.add("controls", f'<path d="{style.cross_d(cx, cy, 22.4, 7.0, r=1.6)}" fill="{fill}" '
                         f'stroke="{shade(colors["dpad"], 0.7)}" stroke-width="0.3" stroke-linejoin="round"/>', "dpad")
    common.dpad_spots(view, cx, cy, 22.4, 7.0, "left", prefix="dpad.")

    # face buttons: coloured jewel caps in their bezels, the letters lighter
    fx, fy = X360_FACE
    for control, dx, dy in (("north", 0, -1), ("west", -1, 0), ("east", 1, 0), ("south", 0, 1)):
        letter, color = X360_LETTERS[control]
        x, y = fx + dx * 11.3, fy + dy * 11.2
        cap = style.tint(color, 0.72)
        view.add("case", f'<path d="{circle_d(x, y, 6.2)}" fill="{shade(colors["body"], 0.9)}" '
                         f'stroke="{shade(colors["body"], 0.78)}" stroke-width="0.22"/>')
        style.button(view, x, y, 5.4, cap, control, gap=0.3)
        style.label(view, letter, x, y, 5.4, shade(cap, 1.32), control)
        view.spot(control, letter, ("circle", x, y, 5.8), "right")

    # Back, the guide in the ring of light, Start, on their raised plate
    gy = X360_GUIDE_Y
    plate = (rounded_d(-19.8, gy - 3.8, 39.6, 7.6, 3.8) + circle_d(0, gy, 9.3))
    view.add("case", f'<path d="{plate}" fill="{shade(colors["body"], 1.01)}" stroke="{shade(colors["body"], 0.8)}" '
                     f'stroke-width="0.25"/>')
    view.add("case", f'<path d="{circle_d(0, gy, 9.3)}" fill="{colors["light"]}" stroke="{shade(colors["light"], 0.8)}" '
                     f'stroke-width="0.2"/>')
    for i, (dx, dy) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):  # the quadrants' gaps and the numbers 1-4
        a = math.atan2(dy, dx)
        style.line(view, f"M{num(8.0 * math.cos(a + math.pi / 4))},{num(gy + 8.0 * math.sin(a + math.pi / 4))}"
                         f"L{num(9.3 * math.cos(a + math.pi / 4))},{num(gy + 9.3 * math.sin(a + math.pi / 4))}",
                   shade(colors["body"], 0.95), 0.35)
        style.label(view, str(i + 1), 11.3 * dx * 0.78, gy + 11.3 * dy * 0.78, 1.9, colors["legend"], None)
    common.neutral_guide(view, 0, gy, 7.4, colors["guide"], colors["guide_ring"], "guide")
    view.spot("guide", "Guide", ("circle", 0, gy, 7.8), "right")
    for control, label, x, direction in (("view", "Back", -15.1, "left"), ("menu", "Start", 15.1, "right")):
        style.shaped(view, rounded_d(x - 3.3, gy - 2.8, 6.6, 5.6, 2.8), colors["small"], control,
                     rounded_d(x - 3.7, gy - 3.2, 7.4, 6.4, 3.2))
        style.glyph(view, style.arrow_d(x, gy, 2.6, direction), colors["glyph"], control, fill=True)
        style.label(view, label.upper(), x, gy - 6.0, 1.9, colors["legend"], control)
        view.spot(control, label, ("rect", x - 3.7, gy - 3.2, 7.4, 6.4), side_of(x))
    return pic


# -- Xbox Adaptive Controller --------------------------------------------------------------------------------------

XAC_W, XAC_H, XAC_T = 292.0, 130.0, 23.0
# The jacks along the back edge in the order of their labels on the top edge, left to right: x in mm, the control
# (None: the top's control carries the name), the hotspot label, the label drawn under the jack
XAC_JACKS = [(-107.6, None, "D-pad left", "dl"), (-94.9, None, "D-pad down", "dd"), (-82.7, None, "D-pad up", "du"),
             (-70.4, None, "D-pad right", "dr"), (-57.7, "lstick.click", "LS", "LS"), (-44.5, "lb", "LB", "LB"),
             (-32.9, None, "Xbox", "xbox"), (-19.9, "misc.1", "X1", "X1"), (-7.5, "misc.2", "X2", "X2"),
             (4.8, None, "View", "view"), (17.5, None, "Menu", "menu"), (30.2, "lt", "LT", "LT"),
             (42.2, "rt", "RT", "RT"), (54.1, "rb", "RB", "RB"), (67.3, "rstick.click", "RS", "RS"),
             (79.6, None, "A", "A"), (92.3, None, "B", "B"), (105.0, "west", "X", "X"), (117.7, "north", "Y", "Y")]
XAC_LABEL_Y = -58.8  # the embossed labels along the top edge


def xac_icon(view, kind, x, y, size, color, control=None):
    """A jack's label: the D-pad directions as a plus with a pointer, the Xbox button as a plain ring (D5), View and
    Menu as their glyphs, the rest as text"""
    if kind.startswith("d") and len(kind) == 2:
        s = size * 0.5
        style.glyph(view, f"M{num(x - s)},{num(y)}H{num(x + s)}M{num(x)},{num(y - s)}V{num(y + s)}", color, control,
                    width=size * 0.22)
        direction = {"l": "left", "r": "right", "u": "up", "d": "down"}[kind[1]]
        dx, dy = {"left": (-1, 0), "right": (1, 0), "up": (0, -1), "down": (0, 1)}[direction]
        style.glyph(view, style.arrow_d(x + dx * s * 1.7, y + dy * s * 1.7, size * 0.45, direction), color, control,
                    fill=True)
    elif kind == "power":
        r = size * 0.4
        style.glyph(view, f"M{num(x - r * 0.62)},{num(y - r * 0.78)}A{num(r)},{num(r)} 0 1 0 {num(x + r * 0.62)},"
                          f"{num(y - r * 0.78)}M{num(x)},{num(y - r * 1.15)}V{num(y - r * 0.1)}", color, control,
                    width=size * 0.12)
    elif kind == "usb":
        s = size * 0.5
        style.glyph(view, f"M{num(x - s * 1.2)},{num(y)}H{num(x + s * 0.9)}M{num(x - s * 0.5)},{num(y)}"
                          f"L{num(x - s * 0.1)},{num(y - s * 0.6)}H{num(x + s * 0.3)}M{num(x - s * 0.2)},{num(y)}"
                          f"L{num(x + s * 0.2)},{num(y + s * 0.6)}H{num(x + s * 0.5)}", color, control,
                    width=size * 0.1)
        style.glyph(view, circle_d(x - s * 1.3, y, s * 0.22) + style.arrow_d(x + s * 1.1, y, s * 0.6, "right"),
                    color, control, fill=True)
    elif kind == "xbox":
        style.glyph(view, circle_d(x, y, size * 0.45), color, control, width=size * 0.12)
    elif kind == "view":
        common.view_glyph(view, x, y, size * 1.1, color, control)
    elif kind == "menu":
        common.menu_glyph(view, x, y, size * 1.1, color, control)
    elif kind in ("A", "B", "X", "Y"):
        style.glyph(view, circle_d(x, y, size * 0.52), color, control, width=size * 0.1)
        style.label(view, kind, x, y, size * 0.62, color, control)
    else:
        style.label(view, kind, x, y, size * 0.6, color, control)


def adaptive():
    pic = Picture("xbox-adaptive", "Xbox Adaptive Controller, top and the back edge's jacks")
    pic.sources = [
        {"what": "size 292 x 130 x 23 mm, 552 g", "source": "https://www.xbox.com/en-US/accessories/controllers/"
                                                           "xbox-adaptive-controller (and its Microsoft Store page)",
         "retrieved": "2026-10-24"},
        {"what": "top view (orthographic within 1 %: 2.22 : 1 against the maker's 2.25 : 1): the slab, the two "
                 "100 mm pads, the D-pad, Xbox, View, Menu, the profile button and its lights, the labels along "
                 "the top edge, the A / B marks; positions and sizes on a 5 mm grid and with blob fits",
         "source": COMMONS + "InclusiveGameLab_Xbox-Adaptive-Controller_4_CC-BY-SA_01.jpg",
         "author": "InclusiveGameLab", "licence": "CC BY-SA 4.0",
         "use": "measured geometry only (positions on a grid); no pixels shipped", "retrieved": "2026-10-24",
         "note": "the lab's triangle sticker between the pads is left out"},
        {"what": "the back edge: 19 jacks of 3.5 mm with their grooves and labels, the connect button, USB-C, the "
                 "5 V DC barrel",
         "source": COMMONS + "InclusiveGameLab_Xbox-Adaptive-Controller_4_CC-BY-SA_02.jpg",
         "author": "InclusiveGameLab", "licence": "CC BY-SA 4.0", "use": "reference only, not traced",
         "retrieved": "2026-10-24",
         "note": "drawn in the order of the top's labels (the edge seen through the device); spacing measured on "
                 "the top's labels, the jacks' height on the edge estimated"},
        {"what": "the USB-A ports on the two short sides (a joystick there drives the left / right stick): drawn as "
                 "slots at the ends of the strip, their place along the side estimated",
         "source": "https://learn.adafruit.com/diy-adaptive-game-controllers/adaptive-joysticks (the two USB "
                   "ports for joysticks that stand in for the thumbsticks)",
         "note": "an estimate: one port at each end", "retrieved": "2026-10-24"}]
    colors = {"body": "#e4e5e4", "pad": "#2b2c2f", "dpad": "#2a2b2e", "small": "#e9eae9", "glyph": "#4a4c50",
              "guide": "#2a2b2e", "guide_ring": "#9a9da3", "emboss": "#c4c6c7", "hole": "#1d1e20"}
    scale = 6.4
    front = pic.view("front", oy=96 + 65 * scale, scale=scale)
    slab = rounded_d(-XAC_W / 2, -XAC_H / 2, XAC_W, XAC_H, 4.0)
    style.body(front, slab, colors["body"], width=0.4)
    style.sheen(front, slab)
    # the embossed labels along the top edge (no hotspots: the jacks are on the back), the A / B marks
    style.label(front, ")))", 131.2, XAC_LABEL_Y, 3.0, colors["emboss"], None)
    xac_icon(front, "power", -132.6, XAC_LABEL_Y, 4.0, colors["emboss"])
    xac_icon(front, "usb", -119.9, XAC_LABEL_Y, 4.0, colors["emboss"])
    for x, _, _, icon in XAC_JACKS:
        xac_icon(front, icon, x, XAC_LABEL_Y, 4.0, colors["emboss"])
    for x, letter in ((-31.1, "A"), (84.1, "B")):
        xac_icon(front, letter, x, 57.1, 4.0, colors["emboss"])

    # the two big pads: A left, B right
    for control, name, cx, cy in (("south", "A", -31.3, -0.3), ("east", "B", 83.7, -0.6)):
        style.recess(front, cx, cy, 51.0, colors["body"], depth=0.8)
        style.button(front, cx, cy, 50.2, colors["pad"], control, gap=0.3)
        style.sheen(front, circle_d(cx, cy, 50.2), cy=0.3, r=0.7, opacity=0.04)
        front.spot(control, name, ("circle", cx, cy, 50.4), "right" if cx > 0 else "left")

    # the left column: Xbox, View / Menu, the profile lights and button, the D-pad in its dish
    common.neutral_guide(front, -116.0, -42.0, 6.4, colors["guide"], colors["guide_ring"], "guide")
    front.spot("guide", "Xbox", ("circle", -116.0, -42.0, 6.8), "left")
    for control, label, x, draw in (("view", "View", -127.0, common.view_glyph),
                                    ("menu", "Menu", -103.4, common.menu_glyph)):
        style.button(front, x, -22.7, 5.7, colors["small"], control, gap=0.3)
        draw(front, x, -22.7, 5.0, colors["glyph"], control)
        front.spot(control, label, ("circle", x, -22.7, 6.0), "left")
    style.button(front, -103.4, -2.2, 5.8, colors["small"], "profile", gap=0.3)
    front.spot("profile", "Profile", ("circle", -103.4, -2.2, 6.1), "left")
    for y in (-4.9, -2.0, 1.0):
        front.add("case", f'<path d="{rounded_d(-116.8, y - 0.45, 3.0, 0.9, 0.45)}" fill="{colors["glyph"]}"/>')
    front.add("case", f'<path d="{circle_d(-127.0, -5.2, 0.6)}" fill="{colors["glyph"]}"/>')
    cx, cy = -115.1, 29.9
    dish = front.defs.linear([(0, shade(colors["body"], 0.95)), (1, shade(colors["body"], 1.01))])
    front.add("case", f'<path d="{circle_d(cx, cy, 19.6)}" fill="{dish}" stroke="{shade(colors["body"], 0.86)}" '
                      f'stroke-width="0.25"/>')  # the shallow dish round the D-pad
    style.shaped(front, style.cross_d(cx, cy, 36.0, 11.8, r=2.6), colors["dpad"], "dpad",
                 style.cross_d(cx, cy, 36.8, 12.6, r=2.9))
    common.dpad_spots(front, cx, cy, 36.0, 11.8, "left", prefix="dpad.")

    # the back edge, seen through the device: the jacks under their labels; the side USB-A ports at its ends
    back = pic.view("back", oy=96 + 130 * scale + 56 + XAC_T / 2 * scale, scale=scale)
    edge = rounded_d(-XAC_W / 2, -XAC_T / 2, XAC_W, XAC_T, 3.0)
    style.body(back, edge, shade(colors["body"], 0.97), width=0.4)
    for x, control, label, icon in XAC_JACKS:
        back.add("case", f'<path d="{rounded_d(x - 1.1, -9.2, 2.2, 5.6, 1.1)}" '
                         f'fill="{shade(colors["body"], 0.86)}"/>')  # the groove over a jack
        ring = shade(colors["body"], 0.72)
        back.add("controls" if control else "case",
                 f'<path d="{circle_d(x, 0.4, 2.3)}" fill="{colors["hole"]}" stroke="{ring}" stroke-width="0.3"/>',
                 control)
        xac_icon(back, icon, x, 6.6, 3.4, colors["glyph"], control)
        if control:
            back.spot(control, label, ("rect", x - 5.4, -10.2, 10.8, 20.4), side_of(x),
                      analog={"kind": "trigger", "fill": "down"} if control in ("lt", "rt") else None)
    back.add("case", f'<path d="{circle_d(131.2, 0.4, 3.6)}" fill="{colors["hole"]}"/>')  # the connect button
    style.label(back, ")))", 131.2, 6.9, 2.6, colors["glyph"], None)
    back.add("case", f'<path d="{rounded_d(-125.6, -0.8, 7.2, 2.6, 1.3)}" fill="{colors["hole"]}"/>')  # USB-C
    back.add("case", f'<path d="{circle_d(-136.4, 0.4, 2.9)}" fill="{colors["hole"]}" '
                     f'stroke="{shade(colors["body"], 0.72)}" stroke-width="0.3"/>')  # 5 V DC
    for x, control, label in ((-XAC_W / 2, "lstick.x", "Left USB (stick)"), (XAC_W / 2, "rstick.x", "Right USB (stick)")):
        style.shaped(back, rounded_d(x - 1.6, -6.0, 3.2, 12.0, 1.2), colors["hole"], control, stroke=0.2)
        back.spot(control, label, ("rect", x - 3.0, -7.5, 6.0, 15.0), side_of(x),
                  analog={"kind": "stick", "pair": control[:-2] + ".y", "radius": 6.0})
    return pic


SETS = {"xbox-360": x360, "xbox-adaptive": adaptive}
