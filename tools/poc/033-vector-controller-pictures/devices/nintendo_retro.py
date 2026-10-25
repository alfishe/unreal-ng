"""Nintendo's retro pads for Nintendo Switch Online, and the GameCube controller.

- `nso-nes`: the NES controller for Nintendo Switch Online (the NES pad's body, a rail with L and R on its top edge);
  also the picture of generic USB NES pads.
- `nso-snes`: the SNES controller for Nintendo Switch Online (the US SNES dog-bone with ZL / ZR beside L / R on the top
  edge); also generic USB SNES pads (iBuffalo, Retrolink and the like).
- `nso-n64`: the Nintendo 64 controller for Nintendo Switch Online (the three-pronged N64 pad; ZR, Home and Capture
  on the top edge of the centre prong, Z under it).
- `nso-genesis`: the SEGA Genesis Control Pad for Nintendo Switch Online as sold in the Americas and Europe: the large
  three-button Genesis pad (A B C, Start; Mode, Home, Capture on the top edge). The Japanese six-button Fighting Pad 6B
  has the Mega Drive six-button shape, the one `8bitdo-m30` draws.
- `gamecube`: the GameCube controller (indigo), for the Wii U / Mayflash adapters and the Switch 2 GameCube pad.

The outlines are measured with trace.py on orthographic front views: Nintendo's product shots of the NSO pads
(nintendo.com store pages) and Evan-Amos's public-domain NES photo and a cut-out of his GameCube photo; the silhouette,
the left half mirrored about the best symmetry axis (overlap 0.97-1.00), scaled to the measured width, cubic Beziers
within 0.25-0.3 mm. Each traced depth checks the view: NES 53.5 mm against 53.2, N64 152.9 against 152.6, the Genesis
pad 96.9 against an estimated 98 (dimensions.com). The control positions are read on the same views: the coloured
buttons by their colour (centre and size of each blob), the rest on a 5 mm grid (about 0.5 mm). Logos and wordmarks
(Nintendo, SUPER NINTENDO, SEGA GENESIS, GAMECUBE) are left out (D5); button letters and printed marks stay.
"""

import math

import style
from devices import common
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
NSO = "https://assets.nintendo.com/image/upload/ncom/en_US/products/accessories/nintendo-switch/controllers/"
DIMENSIONS = "https://www.dimensions.com/element/"
RETRIEVED = "2026-10-24"

# The right halves (trace.py), in mm from the symmetry axis and from the trace's top line
NES_SHELL = [("M", (0, -26.6)),
    ("C", (6.6, -26.6), (59.1, -27.2), (60.7, -26.3)),
    ("C", (61.0, -26.1), (61.3, -25.9), (61.4, -25.6)),
    ("C", (62.2, -24.0), (61.5, 9.8), (61.5, 14.5)),
    ("C", (61.5, 16.5), (62.1, 24.8), (60.9, 26.0)),
    ("C", (59.8, 27.1), (43.1, 26.6), (40.4, 26.6)),
    ("C", (31.7, 26.6), (23.0, 26.6), (14.3, 26.6)),
    ("C", (9.8, 26.6), (4.5, 26.6), (0, 26.6))]
SNES_SHELL = [("M", (0, -26.3)),
    ("C", (6.3, -26.3), (55.7, -26.8), (57.4, -26.1)),
    ("C", (60.6, -24.8), (63.6, -21.1), (65.7, -18.4)),
    ("C", (79.2, -0.1), (70.6, 25.4), (48.2, 30.6)),
    ("C", (41.8, 32.1), (34.5, 31.7), (28.5, 29.0)),
    ("C", (24.4, 27.1), (22.0, 25.2), (17.2, 25.2)),
    ("C", (13.6, 25.2), (9.9, 25.2), (6.3, 25.2)),
    ("C", (5.0, 25.2), (1.9, 25.2), (0, 25.2))]
N64_SHELL = [("M", (0, -76.0)),
    ("C", (6.3, -76.0), (23.3, -74.3), (29.3, -70.3)),
    ("C", (31.4, -68.9), (30.8, -66.1), (33.0, -65.0)),
    ("C", (33.1, -64.6), (33.0, -64.2), (32.9, -63.9)),
    ("C", (33.4, -63.2), (33.9, -62.8), (34.7, -62.6)),
    ("C", (41.1, -60.6), (49.0, -61.2), (55.9, -58.7)),
    ("C", (60.3, -57.2), (64.4, -54.6), (67.5, -51.3)),
    ("C", (70.9, -47.9), (73.3, -43.7), (74.7, -39.1)),
    ("C", (76.1, -34.7), (75.7, -30.3), (76.3, -25.7)),
    ("C", (77.2, -18.3), (79.1, -11.2), (79.7, -3.7)),
    ("C", (80.5, 5.9), (79.0, 32.3), (73.7, 40.1)),
    ("C", (72.4, 42.0), (70.4, 43.5), (68.2, 43.9)),
    ("C", (65.3, 44.5), (62.6, 43.5), (60.6, 41.4)),
    ("C", (56.0, 36.6), (51.7, 18.1), (49.8, 11.1)),
    ("C", (49.0, 8.3), (48.8, 3.7), (46.2, 2.0)),
    ("C", (43.6, 0.3), (36.9, 1.9), (33.9, 2.4)),
    ("C", (29.2, 3.1), (25.0, 5.3), (23.5, 10.2)),
    ("C", (22.2, 14.3), (21.9, 18.9), (21.1, 23.2)),
    ("C", (18.9, 35.6), (15.0, 65.0), (6.6, 74.0)),
    ("C", (5.5, 75.3), (4.0, 75.9), (2.6, 76.6)),
    ("C", (2.2, 76.8), (0.6, 76.9), (0, 76.9))]
# the L / R buttons seen behind the shell's top edge (the first trace, with the shoulders in the silhouette)
N64_SHOULDER_TOP = [(32.9, -63.9), ("C", (41.3, -63.3 - 0.4), (58.6, -63.9), (64.6, -57.4)),
                    ("C", (65.7, -56.2), (64.9, -54.3), (65.9, -52.9))]
GENESIS_SHELL = [("M", (0, -49.0)),
    ("C", (7.4, -49.0), (14.2, -48.0), (21.6, -47.1)),
    ("C", (23.2, -46.9), (24.7, -47.1), (26.3, -46.7)),
    ("C", (27.6, -46.5), (28.5, -45.8), (29.6, -45.4)),
    ("C", (32.9, -44.4), (36.3, -43.7), (39.6, -42.7)),
    ("C", (50.0, -39.4), (64.6, -33.7), (72.5, -25.9)),
    ("C", (86.6, -11.8), (84.0, 10.5), (74.7, 26.5)),
    ("C", (69.8, 35.2), (62.2, 44.5), (52.4, 47.6)),
    ("C", (51.6, 47.9), (47.6, 48.1), (46.8, 47.7)),
    ("C", (44.5, 46.5), (42.0, 46.0), (39.9, 44.3)),
    ("C", (37.4, 42.2), (36.3, 39.7), (34.9, 36.9)),
    ("C", (31.8, 30.5), (26.9, 26.1), (20.3, 23.6)),
    ("C", (14.1, 21.2), (7.6, 20.2), (1.0, 20.0)),
    ("C", (0.7, 20.0), (-0.3, 20.0), (0, 20.0))]
GAMECUBE_SHELL = [("M", (0, -45.3)),
    ("C", (4.4, -45.3), (9.0, -45.6), (13.4, -45.3)),
    ("C", (21.9, -44.5), (29.8, -41.8), (37.9, -39.6)),
    ("C", (40.8, -38.9), (43.4, -40.5), (46.5, -39.2)),
    ("C", (49.8, -37.9), (51.1, -35.5), (53.6, -33.4)),
    ("C", (54.6, -32.5), (56.0, -31.9), (57.1, -31.1)),
    ("C", (60.3, -28.8), (62.8, -25.5), (64.6, -22.0)),
    ("C", (65.5, -20.1), (66.3, -18.0), (66.6, -15.9)),
    ("C", (67.0, -13.6), (66.8, -11.2), (67.0, -8.9)),
    ("C", (68.6, 4.2), (70.0, 17.6), (70.0, 30.8)),
    ("C", (70.0, 38.3), (70.2, 58.4), (61.0, 60.6)),
    ("C", (58.7, 61.1), (56.3, 60.6), (54.5, 59.1)),
    ("C", (47.5, 53.7), (46.8, 35.0), (45.4, 26.7)),
    ("C", (45.0, 24.3), (44.9, 21.6), (43.6, 19.6)),
    ("C", (42.7, 19.2), (42.2, 19.4), (41.7, 20.3)),
    ("C", (40.6, 22.0), (40.3, 24.0), (39.6, 25.8)),
    ("C", (37.8, 30.6), (33.9, 35.0), (29.1, 36.9)),
    ("C", (15.5, 42.2), (1.7, 29.0), (5.8, 15.1)),
    ("C", (6.7, 12.0), (8.5, 8.6), (10.8, 6.3)),
    ("C", (12.4, 4.6), (14.4, 3.3), (15.4, 1.1)),
    ("C", (15.0, -1.4), (4.1, 0.4), (1.9, 0.6)),
    ("C", (1.5, 0.6), (0.3, 0.7), (0, 0.7))]


def source(what, url, author, licence, use="reference only, not traced", note=None):
    entry = {"what": what, "source": url, "author": author, "licence": licence, "use": use, "retrieved": RETRIEVED}
    if note:
        entry["note"] = note
    return entry


def traced(what, url, author="Nintendo", licence="product photo"):
    return source(what, url, author, licence, "measured geometry only (trace.py outline, positions on a grid and by "
                  "colour); no pixels shipped")


def rot_pill_d(cx, cy, length, h, angle):
    """A pill @p length x @p h centred on (cx, cy), its long axis turned @p angle degrees (clockwise, y down)"""
    a = math.radians(angle)
    ux, uy = math.cos(a), math.sin(a)
    nx, ny = -uy, ux
    s, r = length / 2 - h / 2, h / 2
    p1 = (cx - ux * s + nx * r, cy - uy * s + ny * r)
    p2 = (cx + ux * s + nx * r, cy + uy * s + ny * r)
    p3 = (cx + ux * s - nx * r, cy + uy * s - ny * r)
    p4 = (cx - ux * s - nx * r, cy - uy * s - ny * r)
    return (f"M{num(p1[0])},{num(p1[1])}L{num(p2[0])},{num(p2[1])}A{num(r)},{num(r)} 0 0 0 {num(p3[0])},{num(p3[1])}"
            f"L{num(p4[0])},{num(p4[1])}A{num(r)},{num(r)} 0 0 0 {num(p1[0])},{num(p1[1])}Z")


def rot_pill(view, cx, cy, length, h, angle, color, control, gap=0.4):
    style.shaped(view, rot_pill_d(cx, cy, length, h, angle), color, control,
                 rot_pill_d(cx, cy, length + 2 * gap, h + 2 * gap, angle))


def tray(view, points, width, color, rim):
    """A recess along a polyline through @p points, @p width wide with round ends (the Genesis button tray)"""
    d = "M" + "L".join(f"{num(x)},{num(y)}" for x, y in points)
    view.add("case", f'<path d="{d}" fill="none" stroke="{rim}" stroke-width="{num(width + 0.6)}" '
                     f'stroke-linecap="round" stroke-linejoin="round"/>')
    view.add("case", f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{num(width)}" '
                     f'stroke-linecap="round" stroke-linejoin="round"/>')


def edge_key(view, x0, x1, top, rise, color, control, label, side, radius=1.4):
    """A key on the top edge of the case (an NSO pad's L / R, ZR, Home, Capture, Mode): seen from above only its
    top shows, @p rise mm above the edge"""
    d = rounded_d(x0, top - rise, x1 - x0, rise + 3, min(radius, (x1 - x0) / 2))
    style.shaped(view, d, color, control, layer="shoulders")
    view.spot(control, label, ("rect", x0, top - rise - 0.3, x1 - x0, rise + 1.0), side)


def round_key(view, cx, cy, r, color, glyph, control, label, side, mark=None):
    style.button(view, cx, cy, r, color, control)
    if mark == "home":
        style.glyph(view, polygon_d([(cx, cy - r * 0.5), (cx + r * 0.5, cy - r * 0.05), (cx + r * 0.32, cy - r * 0.05),
                                     (cx + r * 0.32, cy + r * 0.45), (cx - r * 0.32, cy + r * 0.45),
                                     (cx - r * 0.32, cy - r * 0.05), (cx - r * 0.5, cy - r * 0.05)]),
                    glyph, control, width=r * 0.12)
    elif mark == "capture":
        style.glyph(view, rounded_d(cx - r * 0.42, cy - r * 0.42, r * 0.84, r * 0.84, r * 0.12), glyph, control,
                    width=r * 0.12)
        style.glyph(view, circle_d(cx, cy, r * 0.2), glyph, control, fill=True)
    view.spot(control, label, ("circle", cx, cy, r + 0.4), side)


def dpad_cross(view, cx, cy, span, arm, color, arrows, control="dpad", r=0.9, arrow=0.24, dimple=None):
    style.shaped(view, style.cross_d(cx, cy, span, arm, r=r), color, control,
                 style.cross_d(cx, cy, span + 0.9, arm + 0.9, r=r + 0.3))
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.glyph(view, style.arrow_d(cx + dx * span * 0.34, cy + dy * span * 0.34, arm * arrow, direction), arrows,
                    control, fill=True, opacity=0.7)
    if dimple:
        dish = view.defs.radial([(0, shade(color, 0.82)), (1, shade(color, 1.05))], r=0.5)
        view.add("controls", f'<path d="{circle_d(cx, cy, dimple)}" fill="{dish}"/>', control)
    common.dpad_spots(view, cx, cy, span, arm, "left", prefix="dpad.")


def stick_spot(view, name, label, cx, cy, r, travel, side):
    """A stick without a click (the N64 and GameCube sticks do not press)"""
    view.spot(f"{name}.x", label, ("circle", cx, cy, r), side,
              analog={"kind": "stick", "pair": f"{name}.y", "radius": travel}, group=name)


def body(view, shell, color, sheen=True):
    d = mirrored_d(shell)
    style.body(view, d, color)
    if sheen:
        style.sheen(view, d)
    return d


# -- NES ------------------------------------------------------------------------------------------------------------

def nes():
    pic = Picture("nso-nes", "Nintendo Entertainment System controller for Nintendo Switch Online (L)")
    pic.sources = [
        {"what": "size 123.4 x 53.2 x 17.5 mm (the NES controller the NSO pad copies)", "source": DIMENSIONS
         + "nes-controller", "retrieved": RETRIEVED},
        traced("top view of the NES controller: the outline, the control positions and sizes",
               COMMONS + "NES-Controller-Flat.jpg", "Evan-Amos", "public domain"),
        source("the NSO version: the rail on the top edge with L and R, the same face",
               NSO + "nintendo-entertainment-system-controllers.html/109045-nintendo-switch-online-nes-controller-"
               "left-1200x675", "Nintendo", "product photo",
               note="the rail's span and the L / R positions read on this near-front view, about 1 mm")]
    colors = {"body": "#c7c7c4", "plate": "#2c2d2f", "bar": "#a2a2a0", "tray": "#cbcbc8", "pill": "#2a2b2d",
              "bezel": "#cfcfcc", "red": "#b33f38", "ink": "#b8463e", "dpad": "#252628", "arrow": "#8f9195",
              "rail": "#303134", "key": "#a9aaa8"}
    view = pic.view("front", oy=768)
    top = -26.6
    # the NSO rail on the top edge: L and R on it, the sync key and the lights between
    style.shaped(view, rounded_d(-45.4, top - 3.2, 85.6, 6, 1.2), colors["rail"], None, layer="shoulders")
    edge_key(view, -27.7, -19.4, top - 2.4, 1.6, colors["key"], "lb", "L", "left", radius=0.8)
    edge_key(view, 13.3, 21.6, top - 2.4, 1.6, colors["key"], "rb", "R", "right", radius=0.8)
    body(view, NES_SHELL, colors["body"])
    # the black face plate and its light-grey bars
    style.flat(view, rounded_d(-56.5, -19.4, 113, 40.8, 1.6), colors["plate"])
    for y0, y1 in ((-18.0, -12.9), (-10.4, -5.2), (19.0, 21.4)):
        style.flat(view, rounded_d(-22.0, y0, 34.5, y1 - y0, 0.9), colors["bar"])
    style.flat(view, rounded_d(-22.0, -2.3, 34.5, 5.0, 0.9), colors["bar"])
    style.label(view, "SELECT", -12.8, 0.2, 2.6, colors["ink"], None)
    style.label(view, "START", 3.2, 0.2, 2.6, colors["ink"], None)
    # Select / Start: two black pills in a light-grey tray
    style.body(view, rounded_d(-22.0, 4.9, 34.5, 11.3, 1.0), colors["tray"], width=0.25)
    for control, label, x in (("view", "Select", -12.8), ("menu", "Start", 3.2)):
        style.pill(view, x, 10.8, 9.6, 3.8, colors["pill"], control)
        view.spot(control, label, ("rect", x - 5.2, 8.5, 10.4, 4.6), "left" if control == "view" else "right")
    # the D-pad in its raised grey cross-shaped frame
    cx, cy = -40.3, 3.1
    view.add("case", f'<path d="{style.cross_d(cx, cy, 24.4, 10.4, r=1.4)}" fill="{colors["bezel"]}" '
                     f'stroke="{shade(colors["bezel"], 0.72)}" stroke-width="0.25"/>')
    dpad_cross(view, cx, cy, 21.0, 7.2, colors["dpad"], colors["arrow"], r=0.6, arrow=0.3, dimple=1.6)
    # B and A in their square grey bezels
    for control, letter, x in (("south", "B", 25.3), ("east", "A", 41.2)):
        style.body(view, rounded_d(x - 6.5, 11.0 - 6.5, 13, 13, 0.9), colors["bezel"], width=0.25)
        style.button(view, x, 11.0, 4.8, colors["red"], control)
        style.label(view, letter, x + 5.0, 20.4, 2.9, colors["ink"], control)
        view.spot(control, letter, ("circle", x, 11.0, 5.2), "right")
    return pic


# -- SNES -----------------------------------------------------------------------------------------------------------

def snes():
    pic = Picture("nso-snes", "Super Nintendo Entertainment System controller for Nintendo Switch Online")
    pic.sources = [
        {"what": "width 144 mm (an estimate of dimensions.com for the SNES controller); the depth follows from the "
                 "traced front view, 57.8 mm without the shoulder buttons", "source": DIMENSIONS + "snes-controller",
         "retrieved": RETRIEVED},
        traced("front view: the outline, the control positions and sizes, the L / ZL / ZR / R keys on the top edge",
               NSO + "other-controllers/super-nintendo-entertainment-system-controller/110756-switch-nso-super-"
               "nintendo-entertainment-system-controller-front-1200x675"),
        source("the SNES pad the NSO version copies: the dish under the four buttons, the two tilted recesses",
               COMMONS + "SNES-Controller-Flat.jpg", "Evan-Amos", "public domain")]
    colors = {"body": "#c6c5ca", "dish": "#a9a8b1", "groove": "#b9b8bf", "dpad": "#4a4a4f", "arrow": "#2e2f33",
              "pill": "#696970", "x": "#b8b0dc", "a": "#5a3f99", "ink": "#7d7c86", "key": "#cfced3",
              "letter": "#7a7984"}
    view = pic.view("front", oy=768 - 2.5 * 8.6)
    top = -26.3
    # the top edge: L and R wide, ZL and ZR small beside the USB-C port
    edge_key(view, -57.8, -25.2, top, 3.4, colors["key"], "lb", "L", "left", radius=2.2)
    edge_key(view, 25.2, 57.8, top, 3.4, colors["key"], "rb", "R", "right", radius=2.2)
    edge_key(view, -18.4, -9.6, top, 2.0, colors["key"], "lt", "ZL", "left", radius=1.0)
    edge_key(view, 9.6, 18.4, top, 2.0, colors["key"], "rt", "ZR", "right", radius=1.0)
    for spot in pic.spots:
        if spot["control"] in ("lt", "rt"):
            spot["analog"] = {"kind": "trigger", "fill": "down"}
    body(view, SNES_SHELL, colors["body"])
    # the dish under the buttons and the D-pad's round recess
    view.add("case", f'<path d="{circle_d(41.0, 4.7, 26.6)}" fill="{colors["dish"]}" '
                     f'stroke="{shade(colors["dish"], 0.8)}" stroke-width="0.25"/>')
    style.recess(view, -41.7, 4.7, 15.4, colors["body"], depth=0.95)
    dpad_cross(view, -41.7, 4.7, 24.2, 8.6, colors["dpad"], colors["arrow"], r=0.9, dimple=1.8)
    # the two tilted grooves, Y-X and B-A, the buttons in them
    cx, cy, ox, oy, r = 41.0, 4.6, 13.9, 10.1, 5.4
    angle = math.degrees(math.atan2(-oy, ox))
    for (x0, y0), (x1, y1) in (((cx - ox, cy), (cx, cy - oy)), ((cx, cy + oy), (cx + ox, cy))):
        length = math.hypot(x1 - x0, y1 - y0) + 2 * r + 3.0
        view.add("case", f'<path d="{rot_pill_d((x0 + x1) / 2, (y0 + y1) / 2, length, 2 * r + 3.0, angle)}" '
                         f'fill="{colors["groove"]}" stroke="{shade(colors["dish"], 0.78)}" stroke-width="0.25"/>')
    labels = {"north": {"cap": colors["x"], "text": "", "name": "X", "draw": lambda *a: None},
              "west": {"cap": colors["x"], "text": "", "name": "Y", "draw": lambda *a: None},
              "east": {"cap": colors["a"], "text": "", "name": "A", "draw": lambda *a: None},
              "south": {"cap": colors["a"], "text": "", "name": "B", "draw": lambda *a: None}}
    common.face_diamond(view, cx, cy, (ox, oy), r, labels)
    # the letters printed on the dish beside each button, italic as on the pad
    for control, letter, x, y in (("north", "X", 49.9, -15.5), ("east", "A", 62.8, -2.0), ("west", "Y", 18.2, 10.0),
                                  ("south", "B", 31.8, 21.5)):
        style.label(view, letter, x, y, 3.3, colors["letter"], control)
    for control, label, x in (("view", "Select", -12.9), ("menu", "Start", 2.7)):
        rot_pill(view, x, 9.3, 10.6, 4.0, -42, colors["pill"], control)
        view.spot(control, label, ("rect", x - 5.0, 5.3, 10.0, 8.0), "left" if control == "view" else "right")
        style.label(view, label.upper(), x - 0.4, 17.6, 2.3, colors["letter"], control)
    return pic


# -- N64 ------------------------------------------------------------------------------------------------------------

def n64():
    pic = Picture("nso-n64", "Nintendo 64 controller for Nintendo Switch Online")
    pic.sources = [
        {"what": "size 160 x 152.6 x 66.7 mm (the N64 controller the NSO pad copies)", "source": DIMENSIONS
         + "nintendo-64-controller", "retrieved": RETRIEVED},
        traced("front view: the outline, the control positions and sizes",
               NSO + "other-controllers/n64-controller/128577-switch-nso-n64-controller-front-view-1200x675"),
        source("the keys on the top edge of the centre prong (Capture, Home, ZR)",
               NSO + "other-controllers/n64-controller/128577-switch-nso-n64-controller-front-view-angle-1200x675",
               "Nintendo", "product photo", note="positions read on a tilted view, about 1.5 mm"),
        source("the Z trigger under the centre prong (drawn as a dashed outline where it lies)",
               COMMONS + "N64-controller-annotated.jpg", "public domain", "public domain")]
    colors = {"body": "#b6b8be", "shoulder": "#8b8d94", "dish": "#a8aab1", "dpad": "#505157", "arrow": "#34353a",
              "start": "#b9403c", "b": "#3f8f57", "a": "#3a58a6", "c": "#d9b43c", "cring": "#a5a7ae",
              "well": "#6a6c72", "gate": "#55575c", "stick": "#d2d3d8", "key": "#a7a9b0", "glyph": "#5e6066",
              "ink": "#e8e8ea", "cink": "#a8862a"}
    view = pic.view("front", oy=768)
    # L / R behind the shell
    for sx, control, label, side in ((-1, "lb", "L", "left"), (1, "rb", "R", "right")):
        (x0, y0), seg1, seg2 = N64_SHOULDER_TOP
        f = lambda p: f"{num(p[0] * sx)},{num(p[1])}"  # noqa: E731
        d = (f"M{f((x0, y0))}C{f(seg1[1])} {f(seg1[2])} {f(seg1[3])}C{f(seg2[1])} {f(seg2[2])} {f(seg2[3])}"
             f"L{f((60, -46))}L{f((36, -55))}Z")
        style.shaped(view, d, colors["shoulder"], control, layer="shoulders")
        a, b = sorted((35 * sx, 64 * sx))
        view.spot(control, label, ("rect", a, -64.4, b - a, 6.0), side)
    # Capture, Home and ZR on the top edge of the centre prong
    top = -76.0
    edge_key(view, -20.5, -14.5, top + 2.0, 1.4, colors["key"], "share", "Capture", "left", radius=1.0)
    edge_key(view, 14.5, 20.5, top + 2.0, 1.4, colors["key"], "guide", "Home", "right", radius=1.0)
    edge_key(view, 21.5, 28.5, top + 4.6, 1.6, colors["key"], "rt", "ZR", "right", radius=1.0)
    pic.spots[-1]["analog"] = {"kind": "trigger", "fill": "down"}
    body(view, N64_SHELL, colors["body"])
    # the D-pad's dish, the C buttons' ring
    view.add("case", f'<path d="{circle_d(-50.0, -30.0, 16.4)}" fill="{colors["dish"]}" '
                     f'stroke="{shade(colors["body"], 0.8)}" stroke-width="0.25"/>')
    dpad_cross(view, -50.0, -30.0, 26.0, 8.8, colors["dpad"], colors["arrow"], r=1.0, dimple=2.0)
    view.add("case", f'<path d="{circle_d(53.8, -32.5, 11.4)}" fill="{colors["cring"]}" '
                     f'stroke="{shade(colors["body"], 0.78)}" stroke-width="0.25"/>')
    style.label(view, "C", 53.8, -32.5, 4.2, shade(colors["cring"], 0.8), None, face="rounded")
    for control, label, x, y, direction in (("misc.1", "C-Up", 53.8, -41.7, "up"),
                                            ("misc.2", "C-Down", 53.8, -23.3, "down"),
                                            ("misc.3", "C-Left", 44.2, -32.5, "left"),
                                            ("misc.4", "C-Right", 63.4, -32.5, "right")):
        style.button(view, x, y, 4.3, colors["c"], control)
        style.glyph(view, style.arrow_d(x, y, 3.0, direction), colors["cink"], control, fill=True, opacity=0.85)
        view.spot(control, label, ("circle", x, y, 4.7), "right")
    # B and A with the groove between them
    style.line(view, f"M30.2,-23.3L41.5,-11.8", shade(colors["body"], 0.78), 1.4)
    for control, letter, x, y, cap in (("west", "B", 30.2, -23.3, colors["b"]),
                                       ("south", "A", 41.5, -11.8, colors["a"])):
        style.button(view, x, y, 5.9, cap, control)
        style.label(view, letter, x, y, 5.0, shade(cap, 0.72), control)
        view.spot(control, letter, ("circle", x, y, 6.3), "right")
    # Start in its ring
    view.add("case", f'<path d="{circle_d(0, -21.7, 6.8)}" fill="{shade(colors["body"], 0.93)}" '
                     f'stroke="{shade(colors["body"], 0.78)}" stroke-width="0.25"/>')
    style.button(view, 0, -21.7, 5.2, colors["start"], "menu")
    style.label(view, "START", 0, -21.7, 2.4, shade(colors["start"], 0.7), "menu")
    view.spot("menu", "Start", ("circle", 0, -21.7, 5.6), "right")
    # the stick: its round well, the octagonal gate, the cap
    style.recess(view, 0, 9.8, 17.0, colors["body"], depth=0.62)
    gate = polygon_d([(9.2 * math.cos(math.radians(22.5 + 45 * i)), 9.8 + 9.2 * math.sin(math.radians(22.5 + 45 * i)))
                      for i in range(8)])
    view.add("case", f'<path d="{gate}" fill="{colors["gate"]}" stroke="{shade(colors["gate"], 0.7)}" '
                     f'stroke-width="0.25"/>')
    style.stick(view, 0, 9.8, 6.8, colors["stick"], "lstick.x")
    for k in (0.52, 0.3):
        style.line(view, circle_d(0, 9.8, 6.8 * k), shade(colors["stick"], 0.8), 0.25, "lstick.x", layer="controls")
    stick_spot(view, "lstick", "Stick", 0, 9.8, 7.2, 6.0, "left")
    # Z under the centre prong: a dashed outline where it lies
    z = rounded_d(-6.0, 26.0, 12.0, 15.0, 4.0)
    view.add("legends", f'<path d="{z}" fill="none" stroke="{colors["glyph"]}" stroke-width="0.35" '
                        f'stroke-dasharray="1.2 0.9" stroke-opacity="0.8"/>', "lt")
    style.label(view, "Z", 0, 33.5, 4.0, colors["glyph"], "lt", opacity=0.8)
    view.spot("lt", "Z", ("rect", -6.4, 25.6, 12.8, 15.8), "left")
    return pic


# -- Genesis ---------------------------------------------------------------------------------------------------------

def genesis():
    pic = Picture("nso-genesis", "SEGA Genesis Control Pad for Nintendo Switch Online (three-button)")
    pic.sources = [
        {"what": "width 165 mm: dimensions.com's estimate for the three-button Genesis pad (98 mm deep; the traced "
                 "view gives 96.9 mm at that width)", "source": DIMENSIONS + "sega-genesis-controller-3-button",
         "retrieved": RETRIEVED, "note": "an estimate"},
        traced("front view: the outline, the control positions and sizes",
               NSO + "other-controllers/sega-genesis-controller/114295-switch-nso-genesis-controller-front-view-"
               "1200x675"),
        source("the keys on the top edge (Capture, Home, Mode)", NSO + "other-controllers/sega-genesis-controller/"
               "114295-switch-nso-genesis-controller-front-view-angle-1200x675", "Nintendo", "product photo",
               note="positions read on a tilted view, about 2 mm"),
        {"what": "the three face buttons on the canonical names as the 8bitdo-m30 set and SDL_GameControllerDB name "
                 "a Sega pad: A south, B east, C the right shoulder (rb); Start menu, Mode view",
         "source": "this POC", "retrieved": RETRIEVED}]
    colors = {"body": "#2f3033", "ring": "#232426", "silver": "#8d8f94", "disc": "#1f2022", "dpad": "#3b3c40",
              "arrow": "#18191b", "mark": "#b2443c", "tray": "#202123", "button": "#2b2c2f", "ink": "#c0473f",
              "start": "#b9bbc0", "key": "#3a3b3f", "legend": "#c9cbcf", "dot": "#c9cbcf"}
    view = pic.view("front", oy=768)
    top = -49.0
    edge_key(view, -32.0, -26.0, top + 3.4, 0.9, colors["key"], "share", "Capture", "left", radius=0.8)
    edge_key(view, 26.0, 32.0, top + 3.4, 0.9, colors["key"], "guide", "Home", "right", radius=0.8)
    # Mode on the sloping top edge at the right, its top 1.2 mm out of the case
    style.shaped(view, rot_pill_d(58.6, -35.6, 12.0, 4.0, 27), colors["key"], "view", layer="shoulders")
    view.spot("view", "Mode", ("rect", 52.6, -41.0, 14.4, 6.0), "right")
    body(view, GENESIS_SHELL, colors["body"])
    # the D-pad: a gloss-black ring with its silver lower lip, a disc, the cross on it
    cx, cy = -44.3, -2.8
    view.add("case", f'<path d="{circle_d(cx, cy, 24.6)}" fill="{colors["ring"]}" '
                     f'stroke="{style.rim_of(colors["body"])}" stroke-width="0.3"/>')
    lip = view.defs.linear([(0, colors["silver"], 0), (0.55, colors["silver"], 0), (1, colors["silver"], 0.7)])
    view.add("case", f'<path d="{circle_d(cx, cy, 21.4)}" fill="none" stroke="{lip}" stroke-width="1.6"/>')
    view.add("case", f'<path d="{circle_d(cx, cy, 20.4)}" fill="{colors["disc"]}"/>')
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.flat(view, style.arrow_d(cx + dx * 22.4, cy + dy * 22.4, 2.3, direction), colors["mark"])
    disc = view.defs.radial([(0, shade(colors["dpad"], 0.96)), (0.85, colors["dpad"]), (1, shade(colors["dpad"], 1.1))],
                            r=0.5)
    view.add("controls", f'<path d="{circle_d(cx, cy, 16.8)}" fill="{disc}" stroke="{style.rim_of(colors["dpad"])}" '
                         f'stroke-width="0.25"/>', "dpad")
    dpad_cross(view, cx, cy, 28.4, 11.6, colors["dpad"], colors["arrow"], r=1.6, arrow=0.2, dimple=2.4)
    # Start, tilted, top right
    rot_pill(view, 33.2, -26.0, 14.0, 5.2, -24, colors["start"], "menu")
    view.spot("menu", "Start", ("rect", 25.4, -30.4, 15.6, 8.8), "right")
    style.label(view, "START", 30.6, -32.6, 2.7, colors["legend"], "menu")
    # the curved tray, A B C in it
    buttons = (("south", "A", 27.7, 5.1), ("east", "B", 43.5, -4.0), ("rb", "C", 61.2, -9.5))
    tray(view, [(x, y) for _, _, x, y in buttons], 21.0, colors["tray"], style.rim_of(colors["body"]))
    for control, letter, x, y in buttons:
        style.button(view, x, y, 7.3, colors["button"], control)
        style.label(view, letter, x, y, 4.4, colors["ink"], control, face="rounded")
        view.spot(control, letter, ("circle", x, y, 7.7), "right")
    style.label(view, "TRIGGER", 40.5, -16.4, 2.6, colors["legend"], None)
    return pic


# -- GameCube --------------------------------------------------------------------------------------------------------

def kidney_d(cx, cy, w, h, angle, bend):
    """A bean-shaped key (the GameCube's X / Y): a pill @p w x @p h turned @p angle, bent by @p bend mm round the A
    button (the inner edge concave)"""
    a = math.radians(angle)
    ux, uy, nx, ny = math.cos(a), math.sin(a), -math.sin(a), math.cos(a)
    s, r = w / 2 - h / 2, h / 2

    def pt(u, v):
        return f"{num(cx + ux * u + nx * v)},{num(cy + uy * u + ny * v)}"
    return (f"M{pt(-s, -r)}Q{pt(0, -r - bend)} {pt(s, -r)}A{num(r)},{num(r)} 0 0 1 {pt(s, r)}"
            f"Q{pt(0, r - bend)} {pt(-s, r)}A{num(r)},{num(r)} 0 0 1 {pt(-s, -r)}Z")


def octagon_d(cx, cy, r, turn=22.5):
    return polygon_d([(cx + r * math.cos(math.radians(turn + 45 * i)), cy + r * math.sin(math.radians(turn + 45 * i)))
                      for i in range(8)])


def gamecube():
    pic = Picture("gamecube", "Nintendo GameCube controller, indigo")
    pic.sources = [
        {"what": "width 140 mm (dimensions.com, 140 x 100 x 65 mm); the traced view at that width gives 106 mm "
                 "front to back with the grips", "source": DIMENSIONS + "gamecube-controller",
         "retrieved": RETRIEVED},
        traced("top view (a cut-out of Evan-Amos's photo): the outline, the control positions and sizes",
               COMMONS + "GameCube_controller.png", "Evan-Amos (photo), cut-out by a Commons user", "CC BY-SA 3.0"),
        source("the controls in black: the Z key on the right shoulder, the triggers", COMMONS
               + "GameCube_Controller_in_Jet_Black_(Transparent).png", "Monathephantom", "CC BY-SA 4.0")]
    colors = {"body": "#4b4d93", "lower": "#45478a", "trigger": "#3e4080", "z": "#5b4f9e", "a": "#4aa894",
              "b": "#b9423f", "xy": "#d3d3d8", "start": "#dedee2", "c": "#d9a33b", "stick": "#d2d2d6",
              "dpad": "#d0d0d4", "arrow": "#9a9ba2", "ink": "#8f9097", "aink": "#2f7564", "cink": "#a7771f"}
    view = pic.view("front", oy=768 - 7.5 * 8.6)
    # L and R triggers on the shoulders, Z on the right one, all behind the shell's top edge
    for sx, control, label, side in ((-1, "lt", "L", "left"), (1, "rt", "R", "right")):
        f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
        d = (f"M{f(30, -40.8)}C{f(35, -43.6)} {f(44, -43.6)} {f(50, -41.0)}C{f(55, -38.6)} {f(59, -34.4)} {f(61, -29)}"
             f"L{f(50, -30)}L{f(30, -36)}Z")
        style.shaped(view, d, colors["trigger"], control, layer="shoulders")
        a, b = sorted((28 * sx, 59 * sx))
        view.spot(control, label, ("rect", a, -43.6, b - a, 5.0), side, analog={"kind": "trigger", "fill": "down"})
    z = ("M40.2,-40.2C43.4,-42.4 48.2,-42.4 51.8,-40.4C53.8,-39.2 55.4,-37.6 56.6,-35.6L52.4,-34.4"
         "C49.6,-37.0 44.6,-38.2 41.2,-37.4Z")
    style.shaped(view, z, colors["z"], "rb", layer="shoulders")
    view.spot("rb", "Z", ("rect", 40.0, -42.6, 17.0, 7.0), "right")
    body(view, GAMECUBE_SHELL, colors["body"])
    # the main stick: an octagonal gate, the cap with its rings
    cx, cy = -42.4, -13.1
    view.add("case", f'<path d="{octagon_d(cx, cy, 13.0)}" fill="{shade(colors["body"], 0.72)}" '
                     f'stroke="{shade(colors["body"], 0.55)}" stroke-width="0.3"/>')
    style.stick(view, cx, cy, 9.6, colors["stick"], "lstick.x")
    for k in (0.5, 0.26):
        style.line(view, circle_d(cx, cy, 9.6 * k), shade(colors["stick"], 0.84), 0.3, "lstick.x", layer="controls")
    stick_spot(view, "lstick", "Stick", cx, cy, 10.0, 6.0, "left")
    # the C stick: yellow, its octagonal gate yellow too
    cx, cy = 22.1, 18.2
    view.add("case", f'<path d="{octagon_d(cx, cy, 10.4)}" fill="{shade(colors["c"], 0.8)}" '
                     f'stroke="{shade(colors["c"], 0.62)}" stroke-width="0.3"/>')
    style.stick(view, cx, cy, 6.6, colors["c"], "rstick.x", ring=False)
    style.label(view, "C", cx, cy, 4.2, colors["cink"], "rstick.x", face="rounded")
    stick_spot(view, "rstick", "C", cx, cy, 7.0, 4.0, "right")
    # the D-pad, small, low left
    dpad_cross(view, -23.5, 17.1, 17.0, 5.8, colors["dpad"], colors["arrow"], r=0.7, arrow=0.32, dimple=1.2)
    # A big, B small below left, X and Y bean keys round A
    style.button(view, 41.7, -13.0, 8.1, colors["a"], "south")
    style.label(view, "A", 41.7, -13.0, 4.4, colors["aink"], "south", face="rounded")
    view.spot("south", "A", ("circle", 41.7, -13.0, 8.5), "right")
    style.button(view, 25.9, -5.2, 5.2, colors["b"], "west")
    style.label(view, "B", 25.9, -5.2, 3.4, shade(colors["b"], 0.7), "west", face="rounded")
    view.spot("west", "B", ("circle", 25.9, -5.2, 5.6), "right")
    for control, letter, x, y, angle in (("north", "Y", 38.6, -27.4, -12), ("east", "X", 56.6, -15.6, 102)):
        d = kidney_d(x, y, 14.6, 8.0, angle, 1.6)
        gap = kidney_d(x, y, 15.4, 8.8, angle, 1.6)
        style.shaped(view, d, colors["xy"], control, gap)
        style.label(view, letter, x, y, 3.2, colors["ink"], control, face="rounded")
        view.spot(control, letter, ("circle", x, y, 6.4), "right")
    # Start / Pause
    style.button(view, -0.6, -11.6, 3.0, colors["start"], "menu")
    view.spot("menu", "Start", ("circle", -0.6, -11.6, 3.4), "right")
    style.label(view, "START/PAUSE", -0.6, -18.2, 2.0, shade(colors["body"], 0.7), "menu")
    return pic


SETS = {
    "nso-nes": nes,
    "nso-snes": snes,
    "nso-n64": n64,
    "nso-genesis": genesis,
    "gamecube": gamecube,
}
