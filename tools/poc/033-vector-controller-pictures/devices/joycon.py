"""Nintendo Switch Joy-Con: the left and the right one alone (upright, the rail with SL / SR beside the shell) and the
pair on the Joy-Con charging grip, grey.

Nintendo's size: 102 x 35.9 x 28.4 mm per Joy-Con (the shell 102 x 33.4 mm, the rail about 3 mm). The outlines are
measured on Owen1962's public-domain front views (cut out on a transparent ground) with the outline helper of the
all-pads round (trace.py's silhouette and Bezier fit on the whole contour: a Joy-Con is not mirror-symmetric), the rail
cut off at its seam, scaled at 25.0 px per mm (the photo's 102 mm height); the shell then measures 33.4 mm wide, the
retail figure less the rail. The control positions are the dark blobs of the same view (buttons, sticks, the minus /
plus keys, capture and home) in millimetres, to about 0.3 mm. The grip is measured the same way on the grip front view:
its photo is about 4 % taller than the Joy-Con view gives (the Joy-Cons in it are 106 mm tall), so it is squashed to
the Joy-Con's 102 mm before tracing (an estimate of the photo's error). The colour is the grey Joy-Con of the
references. The Home button keeps the house glyph and its ring light as on the Switch Pro set; no wordmark or logo on
the grip (D5). The single Joy-Cons are drawn at 1.5 times the pads' scale (a 36 mm wide part would be a strip).
"""

import style
from devices import common
from model import PX_PER_MM, Picture
from svgkit import circle_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
RETRIEVED = "2026-10-24"

# The shells (outline helper, 25.0 px per mm, origin at the shell's centre), clockwise from the top
LEFT_SHELL = [("M", (6.1, -51.0)),
    ("C", (7.3, -51.0), (8.8, -51.2), (10.0, -50.8)),
    ("C", (10.7, -50.6), (11.2, -50.2), (11.9, -50.1)),
    ("C", (13.3, -49.9), (14.7, -50.1), (16.1, -49.9)),
    ("C", (16.7, -49.3), (16.7, -42.3), (16.7, -40.2)),
    ("C", (16.7, -18.4), (16.7, 3.4), (16.7, 25.3)),
    ("C", (16.7, 31.6), (16.7, 38.0), (16.7, 44.3)),
    ("C", (16.7, 46.3), (16.7, 48.5), (16.5, 50.4)),
    ("C", (15.1, 51.6), (7.8, 50.9), (5.6, 50.9)),
    ("C", (-3.1, 50.9), (-11.1, 46.5), (-14.8, 38.4)),
    ("C", (-15.8, 36.1), (-16.4, 33.8), (-16.6, 31.3)),
    ("C", (-17.0, 22.9), (-16.5, 12.0), (-16.4, 3.3)),
    ("C", (-16.3, -7.6), (-16.7, -19.8), (-15.9, -30.6)),
    ("C", (-15.8, -32.1), (-14.5, -38.8), (-13.9, -40.0)),
    ("C", (-10.1, -47.3), (-1.8, -51.0), (6.1, -51.0))]
RIGHT_SHELL = [("M", (-6.1, -50.9)),
    ("C", (2.0, -50.9), (10.3, -47.1), (14.2, -39.7)),
    ("C", (14.4, -39.3), (16.0, -32.5), (16.1, -31.8)),
    ("C", (16.4, -28.2), (16.3, -24.5), (16.3, -20.9)),
    ("C", (16.4, -12.9), (16.5, -5.0), (16.5, 2.9)),
    ("C", (16.6, 9.2), (16.7, 15.4), (16.7, 21.7)),
    ("C", (16.7, 25.3), (16.9, 29.0), (16.4, 32.6)),
    ("C", (15.9, 35.8), (14.8, 38.6), (13.1, 41.3)),
    ("C", (7.8, 49.8), (0.1, 50.6), (-8.9, 50.8)),
    ("C", (-11.2, 50.8), (-13.8, 51.2), (-16.1, 50.7)),
    ("C", (-16.7, 49.5), (-16.7, 44.3), (-16.7, 42.5)),
    ("C", (-16.7, 19.9), (-16.7, -2.7), (-16.7, -25.3)),
    ("C", (-16.7, -29.9), (-16.7, -46.0), (-16.5, -49.3)),
    ("C", (-15.2, -50.5), (-13.2, -49.7), (-11.6, -49.9)),
    ("C", (-11.0, -50.0), (-10.5, -50.4), (-10.0, -50.7)),
    ("C", (-8.8, -51.1), (-7.3, -50.9), (-6.1, -50.9))]
# The grip (trace.py, 19.61 px per mm, the photo squashed to the Joy-Con's height): the right half
GRIP_HALF = [("M", (0, -50.1)),
    ("C", (3.7, -50.1), (15.0, -50.8), (17.8, -49.8)),
    ("C", (18.8, -48.5), (18.5, -47.3), (20.4, -46.6)),
    ("C", (21.9, -47.1), (20.9, -49.4), (22.3, -50.1)),
    ("C", (23.6, -50.7), (25.5, -50.1), (26.9, -50.2)),
    ("C", (27.8, -50.4), (28.5, -51.0), (29.4, -51.1)),
    ("C", (35.5, -51.8), (42.1, -50.5), (47.2, -47.2)),
    ("C", (49.8, -45.4), (52.4, -42.9), (54.0, -40.0)),
    ("C", (55.4, -37.3), (55.1, -32.6), (56.1, -29.6)),
    ("C", (56.7, -28.0), (58.0, -26.5), (58.8, -25.0)),
    ("C", (60.1, -22.4), (61.2, -19.7), (62.1, -16.9)),
    ("C", (64.4, -10.1), (65.6, -3.0), (67.0, 4.0)),
    ("C", (69.2, 15.2), (75.2, 38.6), (63.6, 46.3)),
    ("C", (61.8, 47.5), (59.9, 48.5), (57.8, 48.6)),
    ("C", (54.3, 48.8), (51.8, 46.7), (48.8, 45.8)),
    ("C", (46.4, 46.5), (44.1, 48.8), (41.4, 49.7)),
    ("C", (35.1, 52.0), (27.7, 51.3), (21.0, 50.9)),
    ("C", (19.9, 50.8), (18.7, 51.2), (17.5, 51.2)),
    ("C", (11.9, 51.2), (5.6, 51.2), (0, 51.2))]
FLAT = 16.7  # the shell's flat side (the rail's seam), from the shell's centre
RAIL = (2.9, -44.4, 40.6)  # width, top, bottom
UPPER, LOWER = (-27.7, -18.2), (12.3, 22.8)  # the rail's two buttons (SL / SR), top to bottom
GRIP_INNER = 22.4  # the Joy-Cons' flat sides on the grip, from its centre (the plate is 44.8 mm wide)

COLORS = {"body": "#6d6f73", "button": "#2b2c2f", "small": "#303134", "stick": "#2c2d30", "rail": "#2a2b2e",
          "railkey": "#35363a", "trigger": "#333437", "bumper": "#3a3b3e", "letter": "#d2d4d8",
          "glyph": "#b9bcc1", "arrow": "#8f9297", "ring": "#8e8aa6", "grip": "#3a3b3e", "plate": "#444548"}


def outline_d(segments, dx=0.0, dy=0.0):
    f = lambda p: f"{num(p[0] + dx)},{num(p[1] + dy)}"  # noqa: E731
    out = [f"M{f(segments[0][1])}"]
    for seg in segments[1:]:
        out.append(f"C{f(seg[1])} {f(seg[2])} {f(seg[3])}" if seg[0] == "C" else f"L{f(seg[1])}")
    return "".join(out) + "Z"


def sources(what_view, file):
    return [
        {"what": "size 102 x 35.9 x 28.4 mm per Joy-Con", "source": "nintendo.com Joy-Con product page and retail "
         "listings (Nintendo Switch tech specs: 102 x 239 x 13.9 mm with the Joy-Cons attached)",
         "retrieved": RETRIEVED},
        {"what": what_view, "source": COMMONS + file, "author": "Owen1962", "licence": "public domain",
         "use": "measured geometry only (outline traced, positions of the controls read as blobs); no pixels shipped",
         "retrieved": RETRIEVED}]


def _shoulders(view, dx, sx, colors, names, labels):
    """@p sx: -1 for the left Joy-Con (rail at +x), 1 for the right one (rail at -x); in the shell's own mm"""
    bump, trig = names
    f = lambda x, y: f"{num(dx - sx * x)},{num(y)}"  # noqa: E731  x measured from the rail side inwards
    # ZL / ZR: a band round the outer top corner, behind the shell
    trigger = (f"M{f(3.0, -51.0)}C{f(-2.0, -51.9)} {f(-10.8, -48.1)} {f(-14.7, -40.3)}"
               f"L{f(-13.4, -39.6)}C{f(-9.6, -47.0)} {f(-1.8, -50.6)} {f(3.0, -50.4)}Z")
    style.shaped(view, trigger, colors["trigger"], trig, layer="shoulders")
    x0, x1 = sorted((dx - sx * -15.6, dx - sx * 2.6))
    view.spot(trig, labels[1], ("rect", x0, -52.6, x1 - x0, 6.0), "left" if sx < 0 else "right",
              analog={"kind": "trigger", "fill": "down"})
    # L / R: a low band along the top towards the rail
    bumper = (f"M{f(3.6, -50.6)}C{f(6.0, -51.6)} {f(12.0, -51.6)} {f(16.0, -50.6)}L{f(16.4, -49.6)}"
              f"L{f(3.0, -49.6)}Z")
    style.shaped(view, bumper, colors["bumper"], bump, layer="shoulders")
    x0, x1 = sorted((dx - sx * 2.8, dx - sx * 16.6))
    view.spot(bump, labels[0], ("rect", x0, -53.0, x1 - x0, 4.2), "left" if sx < 0 else "right")


def rail(view, dx, sx, colors, keys):
    """The rail on the flat side with its two keys; @p keys ((control, label), (control, label)) top, bottom"""
    w, top, bottom = RAIL
    x = dx + FLAT if sx < 0 else dx - FLAT - w
    style.body(view, rounded_d(x, top, w, bottom - top, 0.6), colors["rail"], width=0.22)
    for (control, label), (y0, y1) in zip(keys, (UPPER, LOWER)):
        style.shaped(view, rounded_d(x + 0.3, y0, w - 0.6, y1 - y0, 0.8), colors["railkey"], control)
        cx, cy = x + w / 2, (y0 + y1) / 2
        style.label(view, label, cx, cy, 1.7, colors["glyph"], control, opacity=0.75)
        view.spot(control, label, ("rect", x - 0.2, y0 - 0.2, w + 0.4, y1 - y0 + 0.4),
                  "right" if sx < 0 else "left")


def left_controls(view, dx, colors, side="left"):
    """The left Joy-Con's face: minus, the stick, the four direction buttons, capture (shell at x offset @p dx)"""
    x = lambda v: dx + v  # noqa: E731
    style.shaped(view, rounded_d(x(8.9), -41.9, 5.6, 1.9, 0.5), colors["small"], "view",
                 rounded_d(x(8.5), -42.3, 6.4, 2.7, 0.9))
    view.spot("view", "-", ("rect", x(8.4), -42.5, 6.6, 3.1), side)
    cx, cy = x(1.5), -25.7
    style.recess(view, cx, cy, 8.9, colors["body"], depth=0.72)
    style.stick(view, cx, cy, 7.5, colors["stick"], "lstick.x", skirt=8.4)
    common.stick_spots(view, "lstick", "LS", cx, cy, 8.4, 5.0, side)
    cx, cy = x(1.2), 2.9
    for direction, ox, oy in (("up", 0, -7.6), ("down", 0, 7.6), ("left", -8.0, 0), ("right", 8.0, 0)):
        control = f"dpad.{direction}"
        style.button(view, cx + ox, cy + oy, 3.7, colors["button"], control)
        style.glyph(view, style.arrow_d(cx + ox, cy + oy, 2.1, direction), colors["arrow"], control, fill=True,
                    opacity=0.85)
        view.spot(control, direction.capitalize(), ("circle", cx + ox, cy + oy, 4.1), side, group="dpad")
    style.shaped(view, rounded_d(x(3.6), 19.1, 6.0, 6.0, 0.9), colors["small"], "share",
                 rounded_d(x(3.2), 18.7, 6.8, 6.8, 1.2))
    style.glyph(view, circle_d(x(6.6), 22.1, 1.7), colors["glyph"], "share", width=0.3, opacity=0.8)
    view.spot("share", "Capture", ("rect", x(3.1), 18.6, 7.0, 7.0), side)


def right_controls(view, dx, colors, side="right"):
    """The right Joy-Con's face: plus, X A B Y, the stick, home"""
    x = lambda v: dx + v  # noqa: E731
    px, py = x(-11.3), -41.0
    style.shaped(view, style.cross_d(px, py, 5.6, 1.9, r=0.45), colors["small"], "menu",
                 style.cross_d(px, py, 6.4, 2.7, r=0.7))
    view.spot("menu", "+", ("rect", px - 3.2, py - 3.2, 6.4, 6.4), side)
    labels = {c: {"cap": colors["button"], "text": t, "name": t, "ink": colors["letter"], "face": "sans"}
              for c, t in (("north", "X"), ("east", "A"), ("south", "B"), ("west", "Y"))}
    common.face_diamond(view, x(-1.2), -25.5, (8.0, 7.45), 3.7, labels, sides=side)
    cx, cy = x(-1.2), 2.6
    style.recess(view, cx, cy, 8.9, colors["body"], depth=0.72)
    style.stick(view, cx, cy, 7.5, colors["stick"], "rstick.x", skirt=8.4)
    common.stick_spots(view, "rstick", "RS", cx, cy, 8.4, 5.0, side)
    hx, hy = x(-6.7), 22.0
    style.button(view, hx, hy, 3.0, colors["small"], "guide")
    style.glyph(view, circle_d(hx, hy, 2.5), colors["ring"], "guide", width=0.45, opacity=0.8)  # the ring light
    house = polygon_d([(hx - 1.1, hy + 0.9), (hx - 1.1, hy - 0.2), (hx, hy - 1.15), (hx + 1.1, hy - 0.2),
                       (hx + 1.1, hy + 0.9)])
    style.glyph(view, house, colors["glyph"], "guide", width=0.26)
    view.spot("guide", "Home", ("circle", hx, hy, 3.5), side)


def shell(view, segments, dx, colors):
    d = outline_d(segments, dx)
    style.body(view, d, colors["body"])
    style.sheen(view, d)


# A Joy-Con alone is held sideways: the controls are named as SwitchProPad reports them (SDL's
# HandleMiniControllerStateL / R): the four keys under the thumb are the faces, SL / SR the shoulders, L / ZL (R / ZR)
# paddles, the one stick the left stick. Drawn upright, the left one's left arrow is the bottom key when held
SIDEWAYS_LEFT = {"dpad.left": "south", "dpad.down": "east", "dpad.up": "west", "dpad.right": "north",
                 "misc.1": "lb", "misc.2": "rb", "lb": "paddle.p3", "lt": "paddle.p4", "view": "menu",
                 "share": "guide", "dpad": "faces"}
SIDEWAYS_RIGHT = {"south": "west", "east": "south", "west": "north", "north": "east", "misc.1": "lb",
                  "misc.2": "rb", "rb": "paddle.p1", "rt": "paddle.p2", "rstick": "lstick"}


def left():
    pic = Picture("joy-con-l", "Nintendo Switch Joy-Con (L), grey")
    pic.sources = sources("front view of the grey pair: the outline, the rail, the control positions and sizes",
                          "Nintendo_Switch_Joy-Con_Controllers.png")
    view = pic.view("front", ox=1024 - 1.5 * 1.5 * PX_PER_MM, scale=1.5 * PX_PER_MM)
    view.rename = SIDEWAYS_LEFT
    pic.renames = "named as SwitchProPad reports a Joy-Con held sideways (not on master yet)"
    _shoulders(view, 0, -1, COLORS, ("lb", "lt"), ("L", "ZL"))
    shell(view, LEFT_SHELL, 0, COLORS)
    rail(view, 0, -1, COLORS, (("misc.1", "SL"), ("misc.2", "SR")))
    left_controls(view, 0, COLORS)
    return pic


def right():
    pic = Picture("joy-con-r", "Nintendo Switch Joy-Con (R), grey")
    pic.sources = sources("front view of the grey pair: the outline, the rail, the control positions and sizes",
                          "Nintendo_Switch_Joy-Con_Controllers.png")
    view = pic.view("front", ox=1024 + 1.5 * 1.5 * PX_PER_MM, scale=1.5 * PX_PER_MM)
    view.rename = SIDEWAYS_RIGHT
    pic.renames = "named as SwitchProPad reports a Joy-Con held sideways (not on master yet)"
    _shoulders(view, 0, 1, COLORS, ("rb", "rt"), ("R", "ZR"))
    shell(view, RIGHT_SHELL, 0, COLORS)
    rail(view, 0, 1, COLORS, (("misc.2", "SR"), ("misc.1", "SL")))
    right_controls(view, 0, COLORS)
    return pic


def grip():
    from svgkit import mirrored_d
    pic = Picture("joy-con-grip", "Nintendo Switch Joy-Con pair on the Joy-Con charging grip, grey")
    pic.sources = sources("front view of the grey pair: the Joy-Con outlines and control positions",
                          "Nintendo_Switch_Joy-Con_Controllers.png") + [
        {"what": "front view of the pair on the grip: the grip's outline (squashed by 1 / 1.04 to the Joy-Con's "
                 "102 mm height, an estimate of the photo's error), the plate's width, where the Joy-Cons sit",
         "source": COMMONS + "Nintendo_Switch_Joy-Con_Grip_Controller.png", "author": "Owen1962",
         "licence": "public domain", "use": "measured geometry only; no pixels shipped", "retrieved": RETRIEVED}]
    view = pic.view("front")
    dl, dr = -GRIP_INNER - FLAT, GRIP_INNER + FLAT
    _shoulders(view, dl, -1, COLORS, ("lb", "lt"), ("L", "ZL"))
    _shoulders(view, dr, 1, COLORS, ("rb", "rt"), ("R", "ZR"))
    grip_d = mirrored_d(GRIP_HALF)
    style.body(view, grip_d, COLORS["grip"])
    plate = rounded_d(-GRIP_INNER, -50.1, 2 * GRIP_INNER, 101.3, 1.2)
    style.body(view, plate, COLORS["plate"], width=0.25)
    style.sheen(view, plate)
    for i in range(4):  # the player lights beside each rail, on the plate's edges
        for sx in (-1, 1):
            view.add("case", f'<path d="{rounded_d(sx * (GRIP_INNER - 3.2) - 0.6, -12.6 + i * 3.0, 1.2, 1.2, 0.2)}" '
                             f'fill="{shade(COLORS["plate"], 0.7)}"/>')
    shell(view, LEFT_SHELL, dl, COLORS)
    shell(view, RIGHT_SHELL, dr, COLORS)
    left_controls(view, dl, COLORS)
    right_controls(view, dr, COLORS)
    return pic


SETS = {"joy-con-l": left, "joy-con-r": right, "joy-con-grip": grip}
