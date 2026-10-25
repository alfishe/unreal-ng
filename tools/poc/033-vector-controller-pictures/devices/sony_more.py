"""More PlayStation pads: the DualShock 3 (PS3, the Sixaxis shares its shell) and the DualSense Edge.

DualShock 3: Sony's size about 157 x 95 x 55 mm. No orthographic drawing of the DS3 was found (the Tokyoship DS3 layout
on Commons is a three-quarter view), so the outline is measured with trace.py's fitting on a near-top-view photo
(phrawr, CC BY 2.0): the silhouette of the dark pad on a light cloth, the right half (the left one carries the shadow)
mirrored about the centre line of the controls (the sticks and the PS button, not the silhouette's best overlap,
which the shadow pulls aside), scaled to 157 mm. The photo's depth comes out 102 mm, 7 % more than Sony's 95 mm (a
hand-held camera, a little tilted): the outline and the positions are scaled in depth to Sony's figure, an estimate
marked in the provenance. A second near-top photo (Tillion_23, CC0) agrees within about 1 mm on the positions. The
control positions are found on the same photo (Hough circles for the sticks, the face buttons and the PS button, the
D-pad and the small keys read on a 5 mm grid): the D-pad as four separate keys in a cross-shaped recess and the face
buttons on a cross-shaped plate, each on a round dish of the wing, the sticks low in raised housings that stand out of
the U between the grips, Select (a small bar) and Start (a triangle) between the dishes, the PS button between the
sticks. The Sixaxis (no rumble) has the same shell and controls.

DualSense Edge: the DualSense shell and layout of devices/playstation.py (the same body: Sony gives the Edge's size as
the DualSense's, 160 x 66 x 106 mm), in the Edge's two-tone: a white shell, a black centre face and touchpad, black
D-pad, face buttons and PS button, the stick caps in the black housings of the swappable modules (a ring round each
stick), the two Fn buttons under the sticks. The back view (the pad turned over towards the viewer, so the right
hand's back button stays on the right, as the Elite 2's) shows the two back buttons in their black bays under the
grips. Positions of the Edge's own parts are read on a near-orthographic top photo (Sonson2, CC0) scaled to the
DualSense's width, the back buttons on Sony's back view (an estimate to about 2 mm).
"""

import math

import style
from devices import common, dualshock, playstation
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"

# -- DualShock 3 ---------------------------------------------------------------------------------------------------

# The right half of the outline (trace.py's fit, see the module comment), the shoulder housings cut at their top,
# the small bumps of the LED window on the top edge smoothed out; mm from the centre line and the depth's middle
DS3_SHELL = [("M", (0, -32.7)),
             ("C", (10, -32.7), (27.0, -31.5), (31.5, -33.2)),
             ("C", (33.0, -35.2), (32.6, -38.6), (34.2, -41.0)),
             ("C", (35.0, -42.4), (36.2, -44.4), (38.6, -44.6)),
             ("C", (44.9, -44.7), (52.0, -44.8), (57.0, -44.4)),
             ("C", (61.1, -42.6), (60.6, -37.0), (62.3, -33.9)),
             ("C", (62.9, -32.7), (64.5, -32.1), (65.5, -31.3)),
             ("C", (69.2, -28.4), (72.1, -24.4), (73.7, -20.0)),
             ("C", (74.8, -16.8), (74.7, -13.8), (74.9, -10.5)),
             ("C", (75.3, -5.1), (76.0, 0.4), (76.4, 5.8)),
             ("C", (76.8, 10.5), (77.1, 15.3), (77.6, 20.1)),
             ("C", (78.7, 30.2), (80.4, 42.7), (68.0, 46.6)),
             ("C", (63.7, 47.9), (59.1, 47.8), (54.8, 46.2)),
             ("C", (48.2, 43.6), (44.9, 36.2), (40.9, 30.7)),
             ("C", (39.9, 29.2), (38.9, 27.4), (37.4, 26.4)),
             ("C", (33.5, 25.6), (29.5, 25.7), (25.3, 25.6)),
             ("C", (19.5, 25.5), (14.4, 22.6), (11.0, 19.0)),
             ("C", (9.2, 17.3), (6.0, 16.7), (0, 16.7))]
DS3_DPAD, DS3_FACE = (-49.7, -13.2), (49.7, -13.2)
DS3_STICKS = ((-25.3, 9.5), (25.3, 9.5))
DS3_GUIDE = (0, -3.9)
DS3_KEYS_Y = -12.6


def ds3_shoulders(view, colors):
    """From above only the top of L1 / R1 shows over the shoulder housings, L2 / R2 a sliver behind them"""
    for sx, trig, bump, side, tname, bname in ((-1, "lt", "lb", "left", "L2", "L1"), (1, "rt", "rb", "right", "R2",
                                                                                     "R1")):
        f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
        trigger = (f"M{f(40.4, -45.4)}C{f(41.4, -48.6)} {f(44.4, -48.9)} {f(48, -48.9)}"
                   f"C{f(51.6, -48.9)} {f(54.4, -48.6)} {f(55.4, -45.4)}Z")
        bumper = (f"M{f(37.6, -43.8)}C{f(38.2, -46.6)} {f(41.6, -47.5)} {f(47.6, -47.5)}"
                  f"C{f(53.6, -47.5)} {f(57.4, -46.6)} {f(58.4, -43.8)}Z")
        style.shaped(view, trigger, colors["shoulder2"], trig, layer="shoulders")
        style.shaped(view, bumper, colors["shoulder"], bump, layer="shoulders")
        x0, x1 = sorted((41 * sx, 55 * sx))
        view.spot(trig, tname, ("rect", x0, -49.1, x1 - x0, 2.4), side, analog={"kind": "trigger", "fill": "down"})
        x0, x1 = sorted((38 * sx, 58 * sx))
        view.spot(bump, bname, ("rect", x0, -47.6, x1 - x0, 3.8), side)


def ellipse_d(cx, cy, rx, ry):
    return (f"M{num(cx - rx)},{num(cy)}A{num(rx)},{num(ry)} 0 1 0 {num(cx + rx)},{num(cy)}"
            f"A{num(rx)},{num(ry)} 0 1 0 {num(cx - rx)},{num(cy)}Z")


def ds3_dish(view, cx, cy, colors, ticks):
    """The wing's round top under the D-pad or the face buttons (a low ridge round it), with the cross-shaped plate
    the keys stand in and, round the D-pad, the four small direction marks"""
    d = ellipse_d(cx, cy, 24.4, 22.6)
    fill = view.defs.linear([(0, shade(colors["dish"], 1.04)), (1, shade(colors["dish"], 0.96))])
    view.add("case", f'<path d="{d}" fill="{fill}" stroke="{style.rim_of(colors["dish"])}" stroke-width="0.25"/>')
    plate = style.cross_d(cx, cy, 40.2, 14.4, r=1.6)
    view.add("case", f'<path d="{plate}" fill="{colors["plate"]}" stroke="{shade(colors["plate"], 0.7)}" '
                     f'stroke-width="0.25"/>')
    if ticks:
        for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
            view.add("legends", f'<path d="{style.arrow_d(cx + dx * 17.8, cy + dy * 17.8, 1.4, direction)}" '
                                f'fill="{colors["mark"]}" fill-opacity="0.7"/>')


def ds3_dpad_key(view, cx, cy, direction, colors):
    """A separate D-pad key: square-ended outside, pointed towards the centre"""
    a = math.radians({"up": 0, "right": 90, "down": 180, "left": 270}[direction])
    pts = [(-4.3, -13.6), (4.3, -13.6), (4.4, -6.9), (0, -3.0), (-4.4, -6.9)]
    rot = [(cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)) for x, y in pts]
    mx, my = sum(p[0] for p in rot) / 5, sum(p[1] for p in rot) / 5
    grow = [(mx + (x - mx) * 1.1, my + (y - my) * 1.1) for x, y in rot]
    control = f"dpad.{direction}"
    style.shaped(view, polygon_d(rot), colors["key"], control, polygon_d(grow), stroke=0.25)
    xs, ys = [p[0] for p in rot], [p[1] for p in rot]
    view.spot(control, "D-pad", ("rect", min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)), "left",
              group="dpad")


def dualshock3():
    pic = Picture("dualshock-3", "DualShock 3 wireless controller (PS3), Black")
    pic.sources = [
        {"what": "size about 157 x 95 x 55 mm (W x D x H), 192 g", "source": "Sony DualShock 3 (CECHZC2) "
         "specifications as quoted by the instruction manual and retail listings", "retrieved": "2026-10-24"},
        {"what": "outline (trace.py's fit: the right half mirrored about the controls' centre line, Beziers within "
                 "0.3 mm, scaled to 157 mm) and the control positions and sizes (Hough circles, a 5 mm grid)",
         "source": COMMONS + "DualShock_3_(8092015935).jpg", "author": "phrawr from Canada", "licence": "CC BY 2.0",
         "use": "measured geometry only; no pixels shipped", "retrieved": "2026-10-24",
         "note": "a near-top photo: its depth (102 mm) is scaled to Sony's 95 mm, an estimate of the camera's tilt"},
        {"what": "second near-top view: the check of the positions, the D-pad keys and the Start triangle",
         "source": COMMONS + "PlayStation_3_DualShock_3_Controller_(53279128907).jpg", "author": "Tillion_23",
         "licence": "CC0", "use": "reference only, not traced", "retrieved": "2026-10-24"},
        {"what": "the shoulders (L1 / L2 stacked, the housings' tops), the stick housings",
         "source": COMMONS + "DualShock_3.jpg", "author": "Evan-Amos", "licence": "public domain",
         "use": "reference only", "retrieved": "2026-10-24"},
        {"what": "the shoulder buttons seen from the front, the top markings",
         "source": COMMONS + "DualShock3WhiteTopMarkings.jpg", "author": "Bollinger", "licence": "public domain",
         "use": "reference only", "retrieved": "2026-10-24"}]
    colors = {"body": "#393b3f", "dish": "#3c3e42", "plate": "#2d2f32", "shoulder": "#36383c",
              "shoulder2": "#303135", "key": "#2a2b2e", "button": "#27282b", "stick": "#2b2c30", "small": "#2c2d31",
              "collar": "#3e4044", "mark": "#9a9da3", "glyph": "#a9acb1"}
    view = pic.view("front", oy=768 + 1.2 * 8.6)
    ds3_shoulders(view, colors)
    body = mirrored_d(DS3_SHELL)
    style.body(view, body, colors["body"])
    style.sheen(view, body)
    ds3_dish(view, *DS3_DPAD, colors, ticks=True)
    ds3_dish(view, *DS3_FACE, colors, ticks=False)
    for direction in ("up", "right", "down", "left"):
        ds3_dpad_key(view, *DS3_DPAD, direction, colors)
    labels = {c: {"cap": colors["button"], "name": n, "draw": dualshock.symbol(s, dualshock.SYMBOL_INK[s])}
              for c, n, s in dualshock.SYMBOLS}
    common.face_diamond(view, *DS3_FACE, (14.0, 12.3), 5.0, labels)
    # the sticks in their raised housings
    for name, label, (cx, cy), side in (("lstick", "L3", DS3_STICKS[0], "left"),
                                        ("rstick", "R3", DS3_STICKS[1], "right")):
        style.body(view, circle_d(cx, cy, 16.4), colors["collar"], width=0.28)
        style.recess(view, cx, cy, 13.6, colors["collar"], depth=0.62)
        style.stick(view, cx, cy, 11.2, colors["stick"], f"{name}.x", skirt=12.6)
        common.stick_spots(view, name, label, cx, cy, 11.6, 6.0, side)
    # Select (a small bar) and Start (a triangle pointing right); the PS button without its logo (D5)
    sx, sy = -15.1, DS3_KEYS_Y
    style.shaped(view, rounded_d(sx - 4.1, sy - 2.2, 8.2, 4.4, 0.9), colors["small"], "view",
                 rounded_d(sx - 4.5, sy - 2.6, 9.0, 5.2, 1.2))
    view.spot("view", "Select", ("rect", sx - 4.5, sy - 2.6, 9.0, 5.2), "left")
    tx, ty = 15.1, DS3_KEYS_Y
    tri = [(tx - 4.8, ty - 2.6), (tx + 4.8, ty), (tx - 4.8, ty + 2.6)]
    big = [(tx - 5.3, ty - 3.1), (tx + 5.6, ty), (tx - 5.3, ty + 3.1)]
    style.shaped(view, polygon_d(tri), colors["small"], "menu", polygon_d(big))
    view.spot("menu", "Start", ("rect", tx - 5.3, ty - 3.1, 10.9, 6.2), "right")
    view.add("case", f'<path d="{circle_d(*DS3_GUIDE, 5.7)}" fill="{shade(colors["body"], 0.7)}" '
                     f'stroke="{style.rim_of(colors["body"])}" stroke-width="0.2"/>')
    common.neutral_guide(view, *DS3_GUIDE, 4.5, colors["button"], colors["mark"], "guide")
    view.spot("guide", "PS", ("circle", *DS3_GUIDE, 4.9), "right")
    return pic



# -- DualSense Edge ------------------------------------------------------------------------------------------------

TOP = playstation.TOP
EDGE_FN = ((-29.5, 18.0), (29.5, 18.0))
# The back buttons seen from behind (x as from the front, y from the front's top edge): a lever in a bay under
# each grip's upper half; (control, name, bay centre, side)
EDGE_BACK = (("paddle.p3", "Left back", (-54.0, 26.0), "left"), ("paddle.p1", "Right back", (54.0, 26.0), "right"))


def touchpad_d():
    """The DualSense's touchpad (devices/playstation.py draws it inline)"""
    return (f"M{num(-38)},{num(TOP + 1.4)}H{num(38)}C{num(39.6)},{num(TOP + 1.4)} {num(40.4)},{num(TOP + 2.4)} "
            f"{num(40.2)},{num(TOP + 4)}L{num(37.4)},{num(-20.5)}C{num(37.2)},{num(-18.4)} {num(35.8)},{num(-17.6)} "
            f"{num(33.6)},{num(-17.6)}C{num(12)},{num(-16.4)} {num(-12)},{num(-16.4)} {num(-33.6)},{num(-17.6)}"
            f"C{num(-35.8)},{num(-17.6)} {num(-37.2)},{num(-18.4)} {num(-37.4)},{num(-20.5)}L{num(-40.2)},"
            f"{num(TOP + 4)}C{num(-40.4)},{num(TOP + 2.4)} {num(-39.6)},{num(TOP + 1.4)} {num(-38)},{num(TOP + 1.4)}Z")


def edge_front(view, colors):
    playstation.shoulders(view, colors)
    body = mirrored_d(playstation.SHELL)
    style.body(view, body, colors["body"])
    style.body(view, mirrored_d(playstation.FACE_HALF), colors["face"], width=0.25)
    style.sheen(view, body)
    pad = touchpad_d()
    view.add("controls", f'<path d="{pad}" fill="{style.GAP}" fill-opacity="0.4" transform="translate(0 0.35)"/>',
             "touchpad.click")
    style.shaped(view, pad, colors["pad"], "touchpad.click", stroke=0.25)
    view.spot("touchpad.click", "Touchpad", ("rect", -37, TOP + 2, 74, 32), "right")
    for sx in (-1, 1):
        style.line(view, f"M{num(sx * 40.9)},{num(TOP + 5)}L{num(sx * 38.2)},{num(-21)}", "#8ea3c4", 0.5,
                   opacity=0.55, layer="case")
    for control, label, x in (("view", "Create", -44.5), ("menu", "Options", 44.5)):
        style.pill(view, x, -43.5, 3.2, 6.4, colors["small"], control)
        view.spot(control, label, ("rect", x - 2.4, -47.2, 4.8, 7.6), "left" if x < 0 else "right")
    for i in (-1, 0, 1):  # Create: three rays above its button
        a = math.radians(-90 + i * 32)
        x0, y0 = -44.5 + 1.6 * math.cos(a), -47.4 + 1.6 * math.sin(a)
        x1, y1 = -44.5 + 2.8 * math.cos(a), -47.4 + 2.8 * math.sin(a)
        style.glyph(view, f"M{num(x0)},{num(y0)}L{num(x1)},{num(y1)}", colors["mark"], "view", width=0.35)
    for i in (-1, 0, 1):  # Options: three bars above its button
        style.glyph(view, f"M{num(43.2)},{num(-48.6 + i * 0.9)}H{num(45.8)}", colors["mark"], "menu", width=0.35)
    for direction in ("up", "right", "down", "left"):
        playstation.dpad_key(*playstation.DPAD, direction, colors, view)
    labels = {c: {"cap": colors["key"], "name": n, "draw": (lambda s: (lambda v, x, y, r, ctl: playstation.symbols(s)(
        v, x, y, r, ctl, colors["ink"])))(s)} for c, n, s in (("north", "Triangle", "triangle"),
                                                              ("east", "Circle", "circle"),
                                                              ("south", "Cross", "cross"),
                                                              ("west", "Square", "square"))}
    common.face_diamond(view, *playstation.FACE, 12.4, 5.6, labels)
    # the swappable stick modules: a housing ring round each well, the cap with its collar
    for name, label, (cx, cy), side in (("lstick", "L3", playstation.STICKS[0], "left"),
                                        ("rstick", "R3", playstation.STICKS[1], "right")):
        style.body(view, circle_d(cx, cy, 14.6), colors["module"], width=0.25)
        style.recess(view, cx, cy, 13.0, colors["face"], depth=0.62)
        style.stick(view, cx, cy, 10.6, colors["stick"], f"{name}.x", skirt=12.0)
        common.stick_spots(view, name, label, cx, cy, 11.0, 6.5, side)
    # the Fn buttons: ribbed bars standing out of the bottom edge under the sticks
    for (x, y), control, label, side in ((EDGE_FN[0], "misc.2", "Fn (left)", "left"),
                                         (EDGE_FN[1], "misc.3", "Fn (right)", "right")):
        style.shaped(view, rounded_d(x - 5.6, y - 2.6, 11.2, 5.2, 1.4), colors["key"], control,
                     rounded_d(x - 6.1, y - 3.1, 12.2, 6.2, 1.8), stroke=0.25)
        for i in (-1, 0, 1):
            style.glyph(view, f"M{num(x - 4.2)},{num(y + i * 1.3)}H{num(x + 4.2)}", shade(colors["key"], 0.7),
                        control, width=0.4)
        view.spot(control, label, ("rect", x - 6.1, y - 3.1, 12.2, 6.2), side)
    for y in (-10.6, -8.6):
        for i in range(6):
            view.add("case", f'<path d="{circle_d(-6.25 + i * 2.5, y, 0.55)}" fill="{shade(colors["face"], 0.45)}"/>')
    style.pill(view, 0, 2.6, 9.6, 6.0, shade(colors["face"], 0.85), "guide")
    view.spot("guide", "PS", ("rect", -5.2, -0.6, 10.4, 6.4), "right")
    style.pill(view, 0, 11.4, 8.4, 2.4, colors["mute"], "misc.1")
    view.spot("misc.1", "Mute", ("rect", -4.6, 9.8, 9.2, 3.2), "right")


def edge_back(view, colors):
    """The pad turned over towards the viewer: the triggers at the bottom, the right hand still on the right"""
    for sx in (-1, 1):
        f = lambda x, y: f"{num(x * sx)},{num(-y)}"  # noqa: E731
        trigger = (f"M{f(48, TOP)}C{f(52, TOP - 6.4)} {f(61, TOP - 8.2)} {f(67, TOP - 7.4)}"
                   f"C{f(72, TOP - 6.6)} {f(75.5, TOP - 3.2)} {f(76.5, TOP + 1)}L{f(62, TOP + 2)}Z")
        bumper = (f"M{f(43, TOP + 2)}C{f(48, TOP - 2.6)} {f(58, TOP - 4.6)} {f(66, TOP - 3.6)}"
                  f"C{f(73, TOP - 2.6)} {f(78.6, TOP + 3)} {f(79.6, TOP + 10.5)}L{f(70, TOP + 12)}Z")
        style.shaped(view, trigger, colors["shoulder2"], None, layer="shoulders")
        style.shaped(view, bumper, colors["shoulder"], None, layer="shoulders")
    body = mirrored_d(playstation.SHELL, mirror_y=True)
    style.body(view, body, shade(colors["body"], 0.97))
    style.sheen(view, body)
    # the black centre of the back: the USB-C port's cable lock housing at the top edge, the module release
    view.add("case", f'<path d="{rounded_d(-13, 43.6, 26, 6.4, 2.4)}" fill="{colors["face"]}"/>')
    view.add("case", f'<path d="{rounded_d(-4.2, 45.6, 8.4, 2.4, 1.2)}" fill="{shade(colors["face"], 0.5)}"/>')
    view.add("case", f'<path d="{rounded_d(-5, -14, 10, 4, 2)}" fill="{shade(colors["body"], 0.8)}"/>')
    for sx in (-1, 1):
        # the trigger stops: a three-position slider beside each trigger
        view.add("case", f'<path d="{rounded_d(sx * 40 - 2, 27, 4, 9, 2)}" fill="{colors["face"]}"/>')
        view.add("case", f'<path d="{rounded_d(sx * 40 - 1.4, 27.6, 2.8, 3.4, 1.4)}" fill="{colors["module"]}"/>')
    for control, name, (cx, cy), side in EDGE_BACK:
        sx = 1 if cx > 0 else -1
        bay = (f"M{num(cx - 6.4 * sx)},{num(-cy + 9)}C{num(cx - 7.6 * sx)},{num(-cy + 2)} {num(cx - 5.6 * sx)},"
               f"{num(-cy - 8)} {num(cx + 0.6 * sx)},{num(-cy - 11)}C{num(cx + 6.4 * sx)},{num(-cy - 9)} "
               f"{num(cx + 7.6 * sx)},{num(-cy + 1)} {num(cx + 5.8 * sx)},{num(-cy + 9)}Z")
        view.add("case", f'<path d="{bay}" fill="{colors["face"]}" stroke="{style.rim_of(colors["face"])}" '
                         f'stroke-width="0.3"/>')
        lever = (f"M{num(cx - 3.4 * sx)},{num(-cy + 5.6)}C{num(cx - 4.4 * sx)},{num(-cy)} {num(cx - 2.6 * sx)},"
                 f"{num(-cy - 6.4)} {num(cx + 0.8 * sx)},{num(-cy - 8)}C{num(cx + 3.6 * sx)},{num(-cy - 6)} "
                 f"{num(cx + 4.4 * sx)},{num(-cy)} {num(cx + 3.0 * sx)},{num(-cy + 5.6)}Z")
        style.shaped(view, lever, colors["key"], control, stroke=0.3)
        view.spot(control, name, ("circle", cx, -cy - 1, 8.0), side)


def dualsense_edge():
    pic = Picture("dualsense-edge", "DualSense Edge wireless controller (PS5), front and back")
    pic.sources = [
        {"what": "size 160 x 66 x 106 mm, 325 g: the DualSense's shell (devices/playstation.py) with the Edge's parts",
         "source": "Sony DualSense Edge specifications (playstation.com, dualsense-edge-wireless-controller) and "
                   "retail listings", "retrieved": "2026-10-24"},
        {"what": "front: the two-tone (white shell, black face and touchpad, black buttons and shoulders), the stick "
                 "modules' rings, the Fn buttons under the sticks, standing out of the bottom edge",
         "source": COMMONS + "DualSense_Edge_Controller.jpg", "author": "Sonson2", "licence": "CC0",
         "use": "reference only, not traced", "retrieved": "2026-10-24",
         "note": "the Fn buttons measured to about 1 mm on the near-top photo scaled by the stick spacing"},
        {"what": "back: the two back buttons in their bays under the grips, the trigger stop sliders, the cable lock "
                 "housing round the USB-C port", "source": "Sony DualSense Edge product images (playstation.com)",
         "use": "reference only", "retrieved": "2026-10-24",
         "note": "drawn as the pad turned over towards the viewer (triggers at the bottom), so the right hand's back "
                 "button stays on the right; positions and shapes estimated to about 2 mm"}]
    colors = {"body": "#dcdddb", "face": "#2b2c30", "shoulder": "#323337", "shoulder2": "#2c2d31", "pad": "#303135",
              "key": "#2a2b2f", "ink": "#b4b7bc", "stick": "#2c2d31", "small": "#d6d7d5", "mark": "#8a8d92",
              "module": "#3a3b40", "mute": "#4a4c51"}
    front_scale, back_scale = 7.2, 4.0
    front = pic.view("front", oy=104 + 61 * front_scale, scale=front_scale)
    edge_front(front, colors)
    back = pic.view("back", oy=104 + 118 * front_scale + 30 + 57 * back_scale, scale=back_scale)
    edge_back(back, colors)
    return pic


SETS = {"dualshock-3": dualshock3, "dualsense-edge": dualsense_edge}
