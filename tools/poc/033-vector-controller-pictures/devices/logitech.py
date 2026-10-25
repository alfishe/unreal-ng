"""Logitech Gamepad F310 (the F510 and the F710 share its shell and its layout).

Logitech's 142 mm width (the F310 technical specifications: width 142 mm, depth 98 mm, height 75 mm). The outline
is trace.py on Logitech's top view product shot (cut out on a transparent ground; orthographic: the face diamond is
square, the buttons round within 2 %), the right half mirrored, scaled to 142 mm: 87.1 mm deep, which is short of
the 98 mm Logitech gives (a top view does not see the grips' ends under the pad or the triggers' travel; the
drawing follows the view). The cable and the stacked shoulder tabs are cut from the outline and drawn as parts;
every control's position and size is read on the same view on a 5 mm grid and with blob fits (about 0.5 mm). The
Logitech badge is the guide button in XInput mode; its logo is left out (D5), the printed BACK / START / MODE and the
face letters stay.
"""

import style
from devices import common
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade
from devices.xbox_more import mirror, sample, side_of

GALLERY = "https://resource.logitechg.com/content/dam/gaming/en/products/f310/"

SHELL = [("M", (0, -29.2)),
    ("C", (2.5, -29.2), (5.8, -29.2), (8.2, -29.0)),
    ("C", (22.0, -27.4), (17.1, -33.7), (23.7, -38.1)),
    ("C", (27.0, -40.3), (30.9, -40.2), (34.7, -40.7)),
    ("C", (40.0, -41.0), (47.0, -40.5), (51.9, -38.6)),
    ("C", (54.9, -37.4), (57.1, -35.8), (59.1, -33.3)),
    ("C", (68.2, -21.8), (73.9, 17.5), (69.0, 31.5)),
    ("C", (67.5, 35.7), (64.5, 39.4), (60.5, 41.5)),
    ("C", (56.7, 43.4), (52.1, 43.6), (48.1, 42.1)),
    ("C", (39.4, 38.8), (38.6, 29.4), (32.9, 25.1)),
    ("C", (31.3, 25.0), (29.6, 27.2), (28.2, 28.0)),
    ("C", (22.4, 31.1), (15.2, 29.8), (10.3, 25.7)),
    ("C", (8.9, 24.6), (6.1, 20.0), (4.8, 19.9)),
    ("C", (3.8, 19.9), (1.0, 19.9), (0, 19.9))]
# The black rubber of a grip: its inner edge, top to bottom, between the points where it leaves the outline
GRIP = [(67.5, -8.5), (66.4, -1), (65.6, 2), (64.1, 5), (62.6, 8), (61.6, 14), (60.5, 20), (60.4, 23), (60.8, 27),
        (61.4, 32), (62.0, 35), (62.8, 37.5), (63.4, 39.0)]
DPAD, FACE, STICKS = (-41.7, -9.7), (40.8, -8.7), 15.8
LETTERS = {"north": ("Y", "#d9a91e"), "west": ("X", "#3f6fc0"), "east": ("B", "#cc3b30"), "south": ("A", "#6c9a4c")}


def grip_d(sx, outline):
    lo, hi = GRIP[0][1], GRIP[-1][1]
    edge = [(x, y) for x, y in outline if x > 55 and lo <= y <= hi]
    return polygon_d([(sx * x, y) for x, y in edge + list(reversed(GRIP))])


def shoulders(view, colors):
    for sx, trig, bump, side in ((-1, "lt", "lb", "left"), (1, "rt", "rb", "right")):
        f = lambda x, y: f"{num(sx * x)},{num(y)}"  # noqa: E731
        trigger = (f"M{f(36.0, -41.0)}L{f(36.0, -43.5)}Q{f(36.2, -44.1)} {f(37.0, -44.1)}L{f(43.6, -44.1)}"
                   f"C{f(45.0, -44.0)} {f(45.8, -43.2)} {f(46.3, -41.4)}Z")
        style.shaped(view, trigger, colors["trigger"], trig, layer="shoulders")
        x0, x1 = sorted((sx * 35.6, sx * 46.6))
        view.spot(trig, trig.upper(), ("rect", x0, -44.8, x1 - x0, 2.4), side,
                  analog={"kind": "trigger", "fill": "down"})
        bumper = (f"M{f(35.7, -39.6)}L{f(35.7, -42.3)}Q{f(35.8, -42.8)} {f(36.5, -42.8)}L{f(46.0, -42.6)}"
                  f"C{f(48.0, -42.4)} {f(49.6, -41.6)} {f(50.6, -39.4)}Z")
        style.shaped(view, bumper, colors["bumper"], bump, layer="shoulders")
        x0, x1 = sorted((sx * 35.4, sx * 50.6))
        view.spot(bump, bump.upper(), ("rect", x0, -42.6, x1 - x0, 2.6), side)


def f310():
    pic = Picture("logitech-f310", "Logitech Gamepad F310 (F510 / F710 share the shell)")
    pic.sources = [
        {"what": "width 142 mm, depth 98 mm, height 75 mm",
         "source": "Logitech support, \"Logitech Gamepad F310 Technical Specifications\" (support.logi.com)",
         "author": "Logitech", "retrieved": "2026-10-24"},
        {"what": "top view: the outline (trace.py, 87.1 mm deep at 142 mm: the view does not see the grips' "
                 "ends under the pad), the shoulder tabs, the grips' rubber, every control's position, size and "
                 "colour", "source": GALLERY + "f310-gallery-1.png", "author": "Logitech", "terms": "product photo",
         "use": "measured geometry only (trace.py outline, positions on a grid); no pixels shipped",
         "retrieved": "2026-10-24"},
        {"what": "the shoulders stacked (LB over LT), the X / D switch underneath (not a control)",
         "source": GALLERY + "f310-gallery-5.png", "author": "Logitech", "terms": "product photo",
         "use": "reference only, not traced", "retrieved": "2026-10-24"},
        {"what": "the stick collars, the D-pad in its well, the printed BACK / START / MODE",
         "source": "https://commons.wikimedia.org/wiki/File:Logitech_F310,_forward.jpg", "author": "FreeMediaKid!",
         "licence": "CC BY 4.0", "use": "reference only, not traced", "retrieved": "2026-10-24"},
        {"what": "Mode (swaps the D-pad and the left stick in the pad, reports nothing): a quiet misc.1; the "
                 "trigger tabs' and the bumpers' lower ends, under the shell", "source": "this POC",
         "note": "an estimate", "retrieved": "2026-10-24"}]
    colors = {"body": "#677083", "grip": "#1e1f21", "trigger": "#202123", "bumper": "#4a4b4e", "stick": "#47484b",
              "dpad": "#2c2d30", "well": "#1a1b1d", "small": "#8f9196", "badge": "#7b8496", "ring": "#aeb3bd",
              "legend": "#a9aeb8"}
    view = pic.view("front", oy=768 - 0.4 * 8.6)
    shoulders(view, colors)
    d = mirrored_d(SHELL)
    style.body(view, d, colors["body"])
    outline = sample(SHELL)
    for sx in (-1, 1):
        style.body(view, grip_d(sx, outline), colors["grip"], width=0.25)
    style.sheen(view, d)
    # the raised discs round the D-pad and the face buttons, the stick collars
    rim = shade(colors["body"], 0.82)
    for cx, cy in (DPAD, FACE):
        style.line(view, circle_d(cx, cy, 20.8), rim, 0.35, opacity=0.8)
    for sx in (-1, 1):
        style.line(view, circle_d(sx * 20.7, STICKS, 14.4), rim, 0.35, opacity=0.8)

    for name, label, sx in (("lstick", "Left stick", -1), ("rstick", "Right stick", 1)):
        cx = sx * 20.7
        view.add("case", f'<path d="{circle_d(cx, STICKS, 11.6)}" fill="{colors["well"]}" '
                         f'stroke="{shade(colors["body"], 0.6)}" stroke-width="0.25"/>')
        style.stick(view, cx, STICKS, 10.2, colors["stick"], f"{name}.x", ring=False)
        common.stick_spots(view, name, label, cx, STICKS, 10.6, 5.5, side_of(sx))

    cx, cy = DPAD
    view.add("case", f'<path d="{circle_d(cx, cy, 12.7)}" fill="{colors["well"]}" '
                     f'stroke="{shade(colors["body"], 0.6)}" stroke-width="0.25"/>')
    style.shaped(view, style.cross_d(cx, cy, 21.8, 7.8, r=1.4), colors["dpad"], "dpad",
                 style.cross_d(cx, cy, 22.6, 8.6, r=1.7), stroke=0.3)
    common.dpad_spots(view, cx, cy, 21.8, 7.8, "left", prefix="dpad.")

    fx, fy = FACE
    for control, dx, dy in (("north", 0, -1), ("west", -1, 0), ("east", 1, 0), ("south", 0, 1)):
        letter, color = LETTERS[control]
        x, y = fx + dx * 10.6, fy + dy * 10.6
        cap = style.tint(color, 0.75)
        style.button(view, x, y, 4.6, cap, control, gap=0.4)
        style.label(view, letter, x, y, 4.4, shade(cap, 1.3), control)
        view.spot(control, letter, ("circle", x, y, 5.0), "right")

    # the badge (guide), Back / Start, Mode with its light
    style.shaped(view, rounded_d(-8.0, -20.4, 15.4, 11.9, 5.9), colors["badge"], "guide",
                 rounded_d(-8.4, -20.8, 16.2, 12.7, 6.3))
    style.glyph(view, circle_d(-0.3, -14.45, 3.6), colors["ring"], "guide", width=0.35, opacity=0.8)
    view.spot("guide", "Logitech (guide)", ("rect", -8.4, -20.8, 16.2, 12.7), "right")
    for control, label, x, y, ly, quiet in (("view", "Back", -14.8, -19.6, -15.3, False),
                                            ("menu", "Start", 14.8, -19.6, -15.3, False),
                                            ("misc.1", "Mode", -12.4, -7.0, -2.7, True)):
        style.pill(view, x, y, 8.0, 4.6, colors["small"], control)
        style.label(view, label.upper(), x, ly, 1.9, colors["legend"], control)
        view.spot(control, label, ("rect", x - 4.4, y - 2.7, 8.8, 5.4), side_of(x), quiet=quiet)
    view.add("case", f'<path d="{circle_d(-5.1, -4.9, 0.65)}" fill="{shade(colors["body"], 0.45)}"/>')
    return pic


SETS = {"logitech-f310": f310}
