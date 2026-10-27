"""Other makers' pads: the Valve Steam Controller (2015), the Google Stadia controller, the Amazon Luna controller.

Sizes: Stadia 163 x 105 x 65 mm (Google's figure, iFixit and retail listings), Luna 156.4 x 107.6 x 58.5 mm (Amazon's
figure via iFixit and Woot listings). Valve published no size for the 2015 Steam Controller; its width is taken as
160 mm (6.3 in, the figure retail listings carry) and marked as an estimate; the traced depth then comes out at 99 mm.
The outlines are measured with trace.py on near-orthographic views (the silhouette, the left half mirrored about the
best symmetry axis, scaled to the width, cubic Beziers within 0.3 mm); the control positions and sizes are read on the
same views on a 5 mm grid (about 1 mm). Platform logos are left out (D5): the Steam, Stadia and Luna buttons are
neutral rings.
"""

import math

import style
from devices import common
from devices.eightbitdo import top_shoulders
from devices.xbox import paddle
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
RETRIEVED = "2026-10-24"


def ref(what, file, author, licence, use="reference only, not traced", note=None):
    entry = {"what": what, "source": COMMONS + file, "author": author, "licence": licence, "use": use,
             "retrieved": RETRIEVED}
    if note:
        entry["note"] = note
    return entry


def traced(what, file, author, licence):
    return ref(what, file, author, licence,
               use="measured geometry only (trace.py outline, positions on a 5 mm grid); no pixels shipped")


def smooth_half(points, tension=1.0):
    """A right half outline (mirrored_d format) through @p points: a Catmull-Rom spline as cubic Beziers"""
    half = [("M", points[0])]
    for i in range(len(points) - 1):
        p0, p1, p2 = points[max(0, i - 1)], points[i], points[i + 1]
        p3 = points[min(len(points) - 1, i + 2)]
        c1 = tuple(b + (c - a) * tension / 6 for a, b, c in zip(p0, p1, p2))
        c2 = tuple(c - (d - b) * tension / 6 for b, c, d in zip(p1, p2, p3))
        half.append(("C", c1, c2, p2))
    return half


def inset_half(shell, boundary, bottom):
    """The part of a shell's right half inside the polyline @p boundary (x by y, top to bottom) and above @p bottom:
    a face plate that follows the shell's top and ends where the grips begin"""
    def limit(y):
        for (xa, ya), (xb, yb) in zip(boundary, boundary[1:]):
            if ya <= y <= yb:
                return xa + (xb - xa) * (y - ya) / (yb - ya)
        return boundary[0][0] if y < boundary[0][1] else boundary[-1][0]
    pts = [(min(x, limit(y)), y) for x, y in shell_points(shell) if y <= bottom and y < boundary[-1][1] + 0.01]
    pts.append((boundary[-1][0], bottom))
    pts.append((0, bottom))
    left = [(-x, y) for x, y in reversed(pts)]
    return polygon_d(pts + left)


def round_poly_d(points, r):
    """A closed polygon with its corners rounded by @p r (a quadratic through each corner)"""
    out = []
    n = len(points)
    for i, (x, y) in enumerate(points):
        px, py = points[i - 1]
        nx, ny = points[(i + 1) % n]

        def toward(bx, by):
            length = math.hypot(bx - x, by - y)
            k = min(r, length / 2) / length
            return x + (bx - x) * k, y + (by - y) * k

        a, b = toward(px, py), toward(nx, ny)
        out.append(f"{'M' if i == 0 else 'L'}{num(a[0])},{num(a[1])}Q{num(x)},{num(y)} {num(b[0])},{num(b[1])}")
    return "".join(out) + "Z"


def letters_face(view, cx, cy, offset, r, cap, inks, letters=None):
    """Xbox-positional face buttons (A south, B east, X west, Y north) with their printed letters"""
    letters = letters or {"north": "Y", "west": "X", "east": "B", "south": "A"}
    labels = {c: {"cap": cap, "text": t, "name": t, "ink": inks[c] if isinstance(inks, dict) else inks}
              for c, t in letters.items()}
    common.face_diamond(view, cx, cy, offset, r, labels)


def small_key(view, cx, cy, r, color, control, label, side=None):
    style.button(view, cx, cy, r, color, control)
    view.spot(control, label, ("circle", cx, cy, r + 0.4), side or ("left" if cx < 0 else "right"))


def dots_glyph(view, cx, cy, r, color, control, n=3):
    """The "..." of an Options key"""
    for i in range(n):
        x = cx + (i - (n - 1) / 2) * r * 0.55
        view.add("legends", f'<path d="{circle_d(x, cy, r * 0.13)}" fill="{color}"/>', control)


def bars_glyph(view, cx, cy, r, color, control):
    w = r * 0.9
    d = "".join(f"M{num(cx - w / 2)},{num(cy + k * r * 0.3)}H{num(cx + w / 2)}" for k in (-1, 0, 1))
    style.glyph(view, d, color, control, width=r * 0.14)


def capture_glyph(view, cx, cy, r, color, control):
    """Four corner brackets round a point (a screenshot key)"""
    s, t = r * 0.5, r * 0.22
    d = ""
    for sx in (-1, 1):
        for sy in (-1, 1):
            d += (f"M{num(cx + sx * s)},{num(cy + sy * (s - t))}V{num(cy + sy * s)}"
                  f"H{num(cx + sx * (s - t))}")
    style.glyph(view, d, color, control, width=r * 0.13)
    view.add("legends", f'<path d="{circle_d(cx, cy, r * 0.12)}" fill="{color}"/>', control)


# -- Steam Controller (2015) ---------------------------------------------------------------------------------------

# The right half of the outline (trace.py on Kenming Wang's cut-out front view, 160 mm wide; the shoulder buttons
# above the top line belong to the shell's top here), mm from the centre line and from the pad's top
STEAM_SHELL = [("M", (0, -50.0)),
    ("C", (10.8, -50.0), (33.9, -49.5), (44.0, -43.3)),
    ("C", (53.9, -37.4), (69.9, -5.8), (74.5, 5.5)),
    ("C", (76.7, 10.9), (78.7, 16.6), (79.6, 22.5)),
    ("C", (80.4, 27.8), (80.0, 33.8), (77.5, 38.7)),
    ("C", (75.9, 41.8), (73.3, 46.4), (70.1, 48.0)),
    ("C", (67.9, 49.1), (65.5, 48.6), (63.3, 48.0)),
    ("C", (52.3, 44.7), (44.4, 34.2), (37.3, 25.7)),
    ("C", (34.5, 22.4), (32.2, 17.7), (28.0, 15.8)),
    ("C", (22.5, 13.3), (9.2, 15.2), (2.8, 15.2)),
    ("C", (2.2, 15.2), (0.6, 15.2), (0, 15.2))]
STEAM_PADS = ((-37.8, -26.2), (37.8, -26.2))
STEAM_PAD_R = 16.7
STEAM_STICK = (-16.8, -10.9)
STEAM_FACE = (18.8, -10.6)
STEAM_GUIDE = (0, -32.0)
# The matte face plate between the trackpads (they cover its upper corners)
STEAM_PLATE = [(-51, -40.2), (51, -40.2), (53.5, -33), (41, -9), (27, 3.7), (-27, 3.7), (-41, -9), (-53.5, -33)]
# The grip buttons on the underside, in front coordinates (x as held, y down): a long blade along the inner side of
# each grip, the right hand's as paddle.p1, the left hand's as paddle.p3
STEAM_GRIPS = (("paddle.p1", "Right grip", [(46, 8), (52, 18), (57, 28), (61, 38)], (54, 23), "right"),
               ("paddle.p3", "Left grip", [(-46, 8), (-52, 18), (-57, 28), (-61, 38)], (-54, 23), "left"))


def trackpad(view, cx, cy, r, colors, control):
    """A round trackpad: a shallow disc in its cut, a thin rim"""
    view.add("controls", f'<path d="{circle_d(cx, cy, r + 0.55)}" fill="{style.GAP}" fill-opacity="0.55"/>', control)
    fill = view.defs.linear([(0, shade(colors["pad"], 0.97)), (1, shade(colors["pad"], 1.04))])  # concave
    view.add("controls", f'<path d="{circle_d(cx, cy, r)}" fill="{fill}" stroke="{style.rim_of(colors["pad"])}" '
                         f'stroke-width="0.25"/>', control)


def shell_points(shell, steps=24):
    """Points along the right half of an outline (mirrored_d format)"""
    pts = [shell[0][1]]
    for seg in shell[1:]:
        if seg[0] == "L":
            pts.append(seg[1])
            continue
        p0, p1, p2, p3 = pts[-1], seg[1], seg[2], seg[3]
        for i in range(1, steps + 1):
            t = i / steps
            mt = 1 - t
            pts.append(tuple(mt ** 3 * a + 3 * mt * mt * t * b + 3 * mt * t * t * c + t ** 3 * d
                             for a, b, c, d in zip(p0, p1, p2, p3)))
    return pts


def edge_band(shell, x0, x1, depth, sx=1, end_taper=0.6):
    """A band along the shell's top edge from x0 to x1 (a bumper seen from the front: it is the top of the shell
    there), @p depth mm deep, its ends tapering"""
    edge = [(x, y) for x, y in shell_points(shell) if x0 <= x <= x1 and y < 0]
    n = len(edge)
    inner = []
    for i, (x, y) in enumerate(reversed(edge)):
        k = i / max(1, n - 1)
        taper = min(1.0, min(k, 1 - k) / 0.2) if end_taper else 1.0
        inner.append((x - 0.6 * taper, y + depth * (0.35 + 0.65 * taper)))
    pts = edge + inner
    return polygon_d([(x * sx, y) for x, y in pts])


def steam_front(view, colors):
    body = mirrored_d(STEAM_SHELL)
    style.body(view, body, colors["body"])
    style.sheen(view, body)
    # the bumpers are the shell's top corners seen from the front; the triggers behind them show in the back view
    for sx, control, label, side in ((-1, "lb", "LB", "left"), (1, "rb", "RB", "right")):
        band = edge_band(STEAM_SHELL, 30, 60, 3.6, sx)
        style.shaped(view, band, colors["bumper"], control, stroke=0.25)
        style.line(view, band, style.GAP, 0.3, control, opacity=0.5, layer="controls")
        a, b = sorted((31 * sx, 54 * sx))
        view.spot(control, label, ("rect", a, -48.8, b - a, 6.4), side)
    plate = round_poly_d(STEAM_PLATE, 3.0)
    style.body(view, plate, colors["plate"], width=0.28)

    # left trackpad: the D-pad cross embossed on it; its hotspots are the cross arms and the touch area
    lx, ly = STEAM_PADS[0]
    trackpad(view, lx, ly, STEAM_PAD_R, colors, "misc.1")
    cross = style.cross_d(lx, ly, 26.0, 6.4, r=2.2)
    style.glyph(view, cross, shade(colors["pad"], 0.7), "misc.1", width=0.35)
    style.glyph(view, cross, shade(colors["pad"], 1.18), "misc.1", width=0.12, opacity=0.6)
    view.spot("misc.1", "Left pad", ("circle", lx, ly, STEAM_PAD_R + 0.4), "left")
    common.dpad_spots(view, lx, ly, 26.0, 6.4, "left", prefix="dpad.")

    # right trackpad: an analog surface, reported as the right stick
    rx, ry = STEAM_PADS[1]
    trackpad(view, rx, ry, STEAM_PAD_R, colors, "rstick.x")
    common.stick_spots(view, "rstick", "Right pad", rx, ry, STEAM_PAD_R + 0.4, STEAM_PAD_R * 0.7, "right")

    sx, sy = STEAM_STICK
    style.recess(view, sx, sy, 11.4, colors["plate"], depth=0.62)
    style.stick(view, sx, sy, 7.4, colors["stick"], "lstick.x", skirt=9.2)
    common.stick_spots(view, "lstick", "Stick", sx, sy, 7.8, 5.0, "left")

    letters_face(view, *STEAM_FACE, (8.2, 7.4), 4.0, colors["button"], colors["inks"])
    common.neutral_guide(view, *STEAM_GUIDE, 5.0, colors["button"], colors["glyph"], "guide")
    view.spot("guide", "Steam", ("circle", *STEAM_GUIDE, 5.4), "right")
    for control, label, x, direction in (("view", "Back", -11.6, "left"), ("menu", "Start", 11.6, "right")):
        style.pill(view, x, -30.4, 8.6, 4.2, colors["button"], control)
        style.glyph(view, style.arrow_d(x, -30.4, 1.9, direction), colors["glyph"], control, fill=True, opacity=0.85)
        view.spot(control, label, ("rect", x - 4.7, -32.9, 9.4, 5.0), "left" if x < 0 else "right")


def steam_back(view, colors):
    """The pad turned over towards the viewer (triggers at the bottom, the right hand still right): the shell, the
    battery cover, the two grip buttons"""
    for sx in (-1, 1):
        f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
        trigger = (f"M{f(29, 46)}C{f(31, 54)} {f(42, 57.5)} {f(49, 56.6)}C{f(54, 56)} {f(56.5, 51.5)} {f(55.5, 44)}Z")
        control = "rt" if sx > 0 else "lt"
        style.shaped(view, trigger, colors["trigger"], control, layer="shoulders")
        a, b = sorted((31 * sx, 55 * sx))
        view.spot(control, control.upper(), ("rect", a, 47, b - a, 10), "right" if sx > 0 else "left",
                  analog={"kind": "trigger", "fill": "down"})
    body = mirrored_d(STEAM_SHELL, mirror_y=True)
    style.body(view, body, shade(colors["body"], 0.96))
    # the battery cover and the USB port
    view.add("case", f'<path d="{round_poly_d([(-22, -10), (22, -10), (25, 12), (-25, 12)], 3)}" '
                     f'fill="{shade(colors["body"], 0.88)}" stroke="{shade(colors["body"], 0.7)}" '
                     f'stroke-width="0.3"/>')
    view.add("case", f'<path d="{rounded_d(-4.5, 41.5, 9, 2.6, 1.3)}" fill="{shade(colors["body"], 0.5)}"/>')
    for control, name, points, centre, side in STEAM_GRIPS:
        paddle(view, [(x, -y) for x, y in points], colors["paddle"], control)
        view.spot(control, name, ("circle", centre[0], -centre[1], 7.0), side)


def steam_controller():
    pic = Picture("steam-controller", "Valve Steam Controller (2015), front and back")
    pic.sources = [
        {"what": "width 160 mm (6.3 in, as retail listings carry it; Valve published no size): an estimate; the "
                 "traced outline gives 99 mm deep", "source": "moveonup.csd.auth.gr/products/steam-controller "
                 "(retail listing)", "retrieved": RETRIEVED},
        traced("front view cut out on a transparent ground: the outline, the trackpads, stick, buttons and the "
               "face plate", "Steam_Controller_(25483452893).png", "Kenming Wang", "CC BY-SA 2.0"),
        ref("top view: the check of the proportions and of the button positions",
            "Steam_Controller_(top)_(24525157630).jpg", "Les Orchard", "CC BY 2.0"),
        ref("the back: the battery cover, the two grip buttons along the inner side of the grips",
            "Steam_Controller_given_away_during_the_Steam_Dev_Days_2016_event_(steam-controller-from-steam-dev-day-"
            "in-2016-v0-xsnkezxoyccf3).webp", "Willow", "CC BY-SA 4.0",
            note="the back view is drawn on the front outline turned over; the grip buttons' length and position are "
                 "estimates (about 3 mm)")]
    colors = {"body": "#333437", "plate": "#3d3e42", "pad": "#37383b", "trigger": "#2c2d30", "bumper": "#313235",
              "button": "#242528", "stick": "#2b2c2f", "glyph": "#a4a7ac", "paddle": "#2e2f32",
              "inks": {"north": "#cfae52", "west": "#6c9ccc", "east": "#c46257", "south": "#76a862"}}
    front_scale, back_scale = 7.6, 4.0
    front = pic.view("front", oy=110 + 50 * front_scale, scale=front_scale)
    steam_front(front, colors)
    back = pic.view("back", oy=110 + 100 * front_scale + 40 + 50 * back_scale, scale=back_scale)
    steam_back(back, colors)
    return pic


# -- Google Stadia controller ----------------------------------------------------------------------------------------

# trace.py on the Premiere Edition photo (the lit left half mirrored, 163 mm wide; the black bumpers are left out of
# the silhouette), mm from the centre line and from the shell's top
STADIA_SHELL = [("M", (0, -50.0)),
                ("C", (14.2, -49.7), (40.9, -52.2), (53.0, -46.0)),
                ("C", (58.8, -43.0), (62.9, -37.9), (65.8, -32.2)),
                ("C", (72.2, -19.5), (84.7, 31.4), (80.5, 44.3)),
                ("C", (78.5, 50.6), (72.6, 55.3), (66.0, 55.9)),
                ("C", (42.2, 57.8), (49.0, 23.0), (33.6, 16.6)),
                ("C", (27.3, 13.9), (11.2, 16.0), (3.6, 15.5)),
                ("C", (2.9, 15.4), (0.8, 15.4), (0, 15.5))]
STADIA_DPAD = (-46.5, -25.4)
STADIA_FACE = (46.3, -25.5)
STADIA_STICKS = ((-26.3, -5.8), (26.3, -5.8))
STADIA_GUIDE = (0, -3.2)


def stadia_front(view, colors):
    top_shoulders(view, colors, ("L1", "R1", "L2", "R2"), -47.0, 35, 60, rise=0.55)
    body = mirrored_d(STADIA_SHELL)
    style.body(view, body, colors["body"])
    style.sheen(view, body)

    cx, cy = STADIA_DPAD
    style.shaped(view, style.cross_d(cx, cy, 26.6, 9.0, r=1.6), colors["button"], "dpad",
                 style.cross_d(cx, cy, 27.6, 10.0, r=2.0))
    common.dpad_spots(view, cx, cy, 26.6, 9.0, "left", prefix="dpad.")

    for (x, y), name, label in zip(STADIA_STICKS, ("lstick", "rstick"), ("Left stick", "Right stick")):
        style.recess(view, x, y, 12.0, colors["body"], depth=0.9)  # the raised white collar's inner wall
        style.stick(view, x, y, 8.4, colors["stick"], f"{name}.x", skirt=9.8)
        view.add("controls", f'<path d="{circle_d(x, y + 0.5, 9.4)}" fill="none" stroke="{colors["collar"]}" '
                             f'stroke-width="0.9" stroke-opacity="0.9"/>', f"{name}.x")
        common.stick_spots(view, name, label, x, y, 9.0, 5.5, "left" if x < 0 else "right")

    letters_face(view, *STADIA_FACE, (10.8, 10.3), 4.7, colors["button"], colors["ink"])
    common.neutral_guide(view, *STADIA_GUIDE, 5.3, colors["guide"], colors["glyph"], "guide")
    view.spot("guide", "Stadia", ("circle", *STADIA_GUIDE, 5.7), "right")

    for control, label, x, glyph in (("view", "Options", -19.0, dots_glyph), ("menu", "Menu", 19.0, bars_glyph)):
        style.pill(view, x, -36.8, 8.4, 4.4, colors["button"], control)
        glyph(view, x, -36.8, 3.0, colors["ink"], control)
        view.spot(control, label, ("rect", x - 4.6, -39.2, 9.2, 4.8), "left" if x < 0 else "right")
    small_key(view, -12.0, -25.6, 2.9, colors["button"], "misc.1", "Assistant")
    assistant_glyph(view, -12.0, -25.6, 2.9, colors["ink"], "misc.1")
    small_key(view, 12.0, -25.6, 2.9, colors["button"], "share", "Capture")
    capture_glyph(view, 12.0, -25.6, 2.9, colors["ink"], "share")


def assistant_glyph(view, cx, cy, r, color, control):
    """Four dots of falling size (the Assistant key), drawn plain"""
    for dx, dy, k in ((-0.32, -0.12, 0.2), (0.22, -0.34, 0.13), (0.3, 0.1, 0.11), (-0.02, 0.36, 0.09)):
        view.add("legends", f'<path d="{circle_d(cx + dx * r, cy + dy * r, k * r)}" fill="{color}"/>', control)


def stadia():
    pic = Picture("stadia", "Google Stadia controller, Clearly White")
    pic.sources = [
        {"what": "size 163 x 105 x 65 mm (Google's figure)", "source": "https://www.ifixit.com/Device/"
                 "Google_Stadia_Controller (and retail listings)", "retrieved": RETRIEVED},
        traced("near top-down photo of the white controller: the outline (the lit left half mirrored), the D-pad, "
               "sticks, face buttons, the four small keys and the Stadia button",
               "Stadia-controller-and-Chromecast-Ultra-from-Premiere-Edition.jpg", "Y2kcrazyjoker4", "CC BY-SA 4.0"),
        ref("exploded view: the top cover's openings, the check of the button sizes",
            "Exploded-view-drawing_Google_Stadia_Controller_(transparent_bg).png", "zerforschung / Ubahnverleih", "CC0",
            note="the bumpers' and triggers' extent above the shell is read on the photo (about 1 mm)"),
    ]
    colors = {"body": "#e6e5e1", "bumper": "#2b2c2f", "trigger": "#26272a", "button": "#26272a",
              "stick": "#2a2b2f", "collar": "#c8643f", "guide": "#eeede9", "glyph": "#8e8f93", "ink": "#d9dadc"}
    view = pic.view("front", oy=768 - 1 * 8.6)
    stadia_front(view, colors)
    return pic


# -- Amazon Luna controller ------------------------------------------------------------------------------------------

# Read on Montero3456's photo (its left grip runs off the frame, so the right half is measured: the silhouette's row
# extents at 18.24 px/mm, 156.4 mm wide); the gap between the grips, where the ground's shadow joins the shell, is
# read off the left grip by eye; below the centre line the photo is foreshortened (the grips lie lower than the face),
# so that part is stretched to Amazon's 107.6 mm depth
LUNA_SHELL = smooth_half([(0, -49.0), (18, -48.9), (30, -48.5), (40, -47.8), (46.2, -46.6), (50.2, -44.0),
                          (54.2, -38.0), (57.6, -32.0), (60.6, -26.0), (63.3, -20.0), (65.8, -14.0), (68.0, -8.0),
                          (70.0, -2.0), (71.6, 4.0), (73.4, 10.5), (75.2, 17.0), (76.6, 23.4), (77.7, 29.8),
                          (78.2, 36.0), (77.9, 42.5), (76.6, 48.0), (73.8, 53.2), (69.6, 56.6), (64.4, 57.6),
                          (60.0, 56.0), (56.1, 52.8), (51.5, 47.4), (47.7, 41.6), (43.8, 34.4), (40.0, 26.6),
                          (36.6, 21.0), (33.0, 18.9), (24.0, 18.2), (0, 18.0)])
LUNA_PLATE = [(53.0, -42.0), (57.4, -27.0), (55.6, -16.0), (49.4, -6.0), (42.0, 4.0), (35.4, 13.0), (33.0, 18.0)]
LUNA_LSTICK = (-40.8, -31.6)
LUNA_RSTICK = (21.6, -7.8)
LUNA_DPAD = (-21.6, -5.6)
LUNA_FACE = (40.8, -29.1)
LUNA_GUIDE = (0, -34.9)


def luna_front(view, colors):
    top_shoulders(view, colors, ("LB", "RB", "LT", "RT"), -47.6, 32, 51, rise=0.45)
    body = mirrored_d(LUNA_SHELL)
    style.body(view, body, colors["grip"])
    style.body(view, inset_half(LUNA_SHELL, LUNA_PLATE, 18.0), colors["plate"], width=0.26)
    style.sheen(view, body)

    for (x, y), name, label in ((LUNA_LSTICK, "lstick", "Left stick"), (LUNA_RSTICK, "rstick", "Right stick")):
        style.recess(view, x, y, 11.6, colors["plate"], depth=0.72)
        style.stick(view, x, y, 8.4, colors["stick"], f"{name}.x", skirt=9.8)
        view.add("controls", f'<path d="{circle_d(x, y + 0.5, 9.3)}" fill="none" stroke="{colors["collar"]}" '
                             f'stroke-width="0.9" stroke-opacity="0.9"/>', f"{name}.x")
        common.stick_spots(view, name, label, x, y, 9.0, 5.5, "left" if x < 0 else "right")

    cx, cy = LUNA_DPAD
    style.recess(view, cx, cy, 15.2, colors["plate"], depth=0.78)
    style.shaped(view, style.cross_d(cx, cy, 25.0, 8.8, r=1.8), colors["dpad"], "dpad",
                 style.cross_d(cx, cy, 25.9, 9.7, r=2.2))
    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):  # the short ridge on each arm
        x, y, h = cx + dx * 8.2, cy + dy * 8.2, 1.4
        d = (f"M{num(x - h * abs(dy))},{num(y - h * abs(dx))}L{num(x + h * abs(dy))},{num(y + h * abs(dx))}")
        style.glyph(view, d, shade(colors["dpad"], 1.5), "dpad", width=0.45, opacity=0.7)
    common.dpad_spots(view, cx, cy, 25.0, 8.8, "left", prefix="dpad.")

    letters_face(view, *LUNA_FACE, (10.8, 9.3), 4.6, colors["button"], colors["ink"])
    style.recess(view, *LUNA_GUIDE, 6.9, colors["plate"], depth=0.85)
    common.neutral_guide(view, *LUNA_GUIDE, 5.9, colors["button"], colors["glyph"], "guide")
    view.spot("guide", "Luna", ("circle", *LUNA_GUIDE, 6.4), "right")
    small_key(view, -13.8, -35.1, 2.9, colors["button"], "view", "Action")
    style.glyph(view, circle_d(-13.8, -35.1, 1.15), colors["ink"], "view", width=0.32)
    small_key(view, 13.8, -35.1, 2.9, colors["button"], "menu", "Menu")
    bars_glyph(view, 13.8, -35.1, 2.9, colors["ink"], "menu")
    small_key(view, 0, -23.3, 2.9, colors["button"], "misc.1", "Microphone", side="right")
    mic_glyph(view, 0, -23.3, 2.9, colors["ink"], "misc.1")
    view.add("case", f'<path d="{circle_d(0, -43.5, 0.45)}" fill="{shade(colors["plate"], 0.6)}"/>')  # mic hole
    view.add("case", f'<path d="{rounded_d(-1.6, 17.0, 3.2, 1.0, 0.5)}" fill="{style.GAP}" fill-opacity="0.7"/>')


def mic_glyph(view, cx, cy, r, color, control):
    s = r * 0.42
    d = (f"M{num(cx - s * 0.75)},{num(cy - s * 0.1)}A{num(s * 0.75)},{num(s * 0.75)} 0 0 0 {num(cx + s * 0.75)},"
         f"{num(cy - s * 0.1)}M{num(cx)},{num(cy + s * 0.65)}V{num(cy + s * 1.0)}")
    view.add("legends", f'<path d="{rounded_d(cx - s * 0.38, cy - s * 1.05, s * 0.76, s * 1.3, s * 0.38)}" '
                        f'fill="{color}"/>', control)
    style.glyph(view, d, color, control, width=r * 0.11)


def luna():
    pic = Picture("luna", "Amazon Luna controller")
    pic.sources = [
        {"what": "size 156.4 x 107.6 x 58.5 mm (Amazon's figure)", "source": "https://www.ifixit.com/Device/"
                 "Amazon_Luna_Controller (and the Woot listing)", "retrieved": RETRIEVED},
        traced("near top-down photo of the controller on a worktop: the outline (right half), the plate's edge, "
               "both sticks, the D-pad, face buttons, the small keys and the Luna button", "Amazon_Luna_Controller.jpg",
               "Montero3456", "CC0"),
        {"what": "the press render: the colours of the grips, plate and the stick collars", "source":
            "https://www.aftvnews.com/wp-content/uploads/2020/09/Amazon-Luna-Game-Controller.jpg (Amazon press "
            "image)", "use": "reference only, not traced", "retrieved": RETRIEVED,
         "note": "the photo is foreshortened below the centre line and the gap between the grips is read by eye: "
                 "the grips' inner edge and the depth are estimates (about 2 mm)"},
    ]
    colors = {"grip": "#3a3c41", "plate": "#2b2c2f", "bumper": "#313236", "trigger": "#2b2c30", "button": "#232427",
              "dpad": "#2c2e33", "stick": "#8b8c8f", "collar": "#6e5f9c", "glyph": "#77797e", "ink": "#a8aaae"}
    view = pic.view("front", oy=768 - 3.8 * 8.6)
    luna_front(view, colors)
    return pic


SETS = {
    "steam-controller": steam_controller,
    "stadia": stadia,
    "luna": luna,
}
