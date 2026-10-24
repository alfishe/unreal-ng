"""Fighting-game devices: an arcade stick (Vewlix layout), a Saturn-style six-button pad, a six-button fight pad.

arcade-stick: the HORI Fighting Stick α (SPF-013, 2022, sold for PS5 / PS4 / PC; the Xbox version has the same case)
stands for every eight-button Vewlix stick (HORI Real Arcade Pro, Qanba, Mayflash F300 / F500, Victrix): a box case
with a front wrist ledge, a ball-top lever on the left, eight 30 mm buttons in two slightly curved rows of four, the
start-side keys in a panel at the back. Size 410 x 300 x 120 mm (HORI via playstation.com); every position and size
is read on the maker's orthographic top view at 2.71 px per mm (the case spans the maker's 410 mm and comes out
304 mm deep, against the maker's 300). Drawn at 3.8 px per mm (the case would not fit the canvas at the pads' 8.6).
The artwork print of the panel is left out (a plain muted blue panel), as are the wordmarks and the PS logo (D5).

saturn-pad: the Sega Saturn control pad (the Japanese / model 2 shell, the one Retro-Bit's USB pads and HORI's Saturn
pads copy), 155 x 86 mm (dimensions.com, model 2). The outline is traced on a near-top photo of the model 2 pad
(the cable masked out, the left half mirrored; the traced depth 86.7 mm against 86). The six buttons follow
eightbitdo.m30's canonical names (A B C = south east rb, X Y Z = west north lb), L / R the triggers.

fight-pad: the HORI Fighting Commander OCTA (SPF-023, 2022, PS5 / PS4 / PC; an Xbox version shares the shell): six
face buttons in two rows of three, an eight-way disc D-pad, one left stick in an octagonal gate, the touchpad.
170 mm wide (HORI via playstation.com); outline and positions traced on HORI's top view at 6.47 px per mm. HORI lists
the depth as 90 mm; the top and the bottom view both give 107-113 mm for the 170 mm width, so the plan here follows the
views. The face carries PlayStation's printed labels: the top row Square, Triangle, R1 (west, north, rb), the bottom
row Cross, Circle, R2 (south, east, rt); L1 / L2 are the left shoulders. The right shoulders are taken as L3 (front)
and R3 (back), HORI's usual place for them on a pad with one stick: an assumption to check on a real pad.
"""

import math

import style
from devices import common, eightbitdo, playstation
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
PS_OLP = "https://www.playstation.com/ja-jp/local/official-products/olp-peripherals/controller/"


def traced(what, source, author, terms="product photo"):
    return {"what": what, "source": source, "author": author, "terms": terms,
            "use": "measured geometry only (trace.py outline, positions on a grid); no pixels shipped",
            "retrieved": "2026-10-24"}


def ring(view, cx, cy, r, color, width, opacity=1.0, layer="case", control=None):
    style.line(view, circle_d(cx, cy, r), color, width, control=control, opacity=opacity, layer=layer)


def bars_glyph(view, cx, cy, size, color, control):
    """Three bars (Options)"""
    w = size * 0.8
    d = "".join(f"M{num(cx - w / 2)},{num(cy + dy * size * 0.3)}H{num(cx + w / 2)}" for dy in (-1, 0, 1))
    style.glyph(view, d, color, control, width=size * 0.13)


def rays_glyph(view, cx, cy, size, color, control):
    """Three rays (Create / Share)"""
    d = ""
    for i in (-1, 0, 1):
        a = math.radians(-90 + i * 32)
        d += (f"M{num(cx + size * 0.35 * math.cos(a))},{num(cy + size * 0.5 + size * 0.35 * math.sin(a))}"
              f"L{num(cx + size * 0.85 * math.cos(a))},{num(cy + size * 0.5 + size * 0.85 * math.sin(a))}")
    style.glyph(view, d, color, control, width=size * 0.13)


# -- arcade stick -----------------------------------------------------------------------------------------------

S_ALPHA = 2.7146  # px per mm on HORI's top view (1600 x 900, the case 243..1356 px = 410 mm)


def ref(px, py):
    """A point of HORI's top view in mm from the case centre (799.5, 451 px)"""
    return (px - 799.5) / S_ALPHA, (py - 451) / S_ALPHA


def arcade_button(view, cx, cy, colors, control, label, side):
    """A 30 mm push button from above: the bezel flange, the plunger with its shallow dish, the panel's print ring"""
    ring(view, cx, cy, 19.0, colors["print"], 0.5, opacity=0.35)
    style.button(view, cx, cy, 16.4, colors["flange"], control, gap=0.5)
    plunger = view.defs.linear([(0, shade(colors["plunger"], 1.05)), (1, shade(colors["plunger"], 0.95))])
    view.add("controls", f'<path d="{circle_d(cx, cy, 12.2)}" fill="{plunger}" '
                         f'stroke="{style.rim_of(colors["plunger"])}" stroke-width="0.3"/>', control)
    dish = view.defs.linear([(0, shade(colors["plunger"], 0.9)), (1, shade(colors["plunger"], 1.04))])
    view.add("controls", f'<path d="{circle_d(cx, cy, 9.6)}" fill="{dish}"/>', control)
    view.spot(control, label, ("circle", cx, cy, 16.8), side)


def small_round(view, cx, cy, r, colors, control, label, side, well=True):
    if well:
        view.add("case", f'<path d="{circle_d(cx, cy, r + 1.3)}" fill="{shade(colors["console"], 0.8)}"/>')
    style.button(view, cx, cy, r, colors["key"], control, gap=0.4)
    view.spot(control, label, ("circle", cx, cy, r + 0.5), side)


def arcade_stick():
    pic = Picture("arcade-stick", "Arcade stick, Vewlix layout (HORI Fighting Stick α as measured)")
    pic.sources = [
        {"what": "size 410 x 300 x 120 mm (with the lever), 2.7 kg", "source": PS_OLP + "fighting-stick-alpha/",
         "author": "Sony Interactive Entertainment / HORI", "retrieved": "2026-10-24"},
        traced("top view: the case, the ledge, the lid frame and panel, every control's position and size",
               "https://gmedia.playstation.com/is/image/SIEPDC/fighting-stick-alpha-04-ja-06apr22", "HORI"),
        {"what": "the 30 mm buttons (flange about 33 mm, plunger 24 mm) and the 35 mm ball top",
         "source": "Sanwa OBSF-30 and JLF / LB-35 dimensions as the arcade parts shops list them; checked on the "
                   "top view (the flange 33-35 mm, the ball 34.6 mm)", "retrieved": "2026-10-24"},
        {"what": "the same eight-button Vewlix layout on the HORI Real Arcade Pro, Qanba, Mayflash F300 / F500 and "
                 "Victrix sticks the set stands for; their cases differ (an estimate that the layout is what "
                 "identifies the controls)", "source": "this POC", "retrieved": "2026-10-24"}]
    colors = {"case": "#2c2d30", "ledge": "#28292c", "frame": "#323337", "panel": "#2f3a4e", "console": "#2a2b2e",
              "well": "#212225", "flange": "#202124", "plunger": "#34353a", "ball": "#26272a", "key": "#2b2c30",
              "print": "#9fb3d6", "glyph": "#a9adb4", "slider": "#3a3b3f"}
    scale = 3.8
    view = pic.view("front", oy=768 - 0.2 * scale, scale=scale)
    left, right = ref(251, 0)[0], ref(1347, 0)[0]
    top, ledge_top = ref(0, 40)[1], ref(0, 770)[1]
    ledge_l, ledge_r, bottom = ref(243, 0)[0], ref(1356, 0)[0], ref(0, 863)[1]
    r = 13.0
    case = (f"M{num(left + r)},{num(top)}H{num(right - r)}C{num(right - r * 0.45)},{num(top)} {num(right)},"
            f"{num(top + r * 0.45)} {num(right)},{num(top + r)}V{num(ledge_top + 2)}H{num(left)}V{num(top + r)}"
            f"C{num(left)},{num(top + r * 0.45)} {num(left + r * 0.45)},{num(top)} {num(left + r)},{num(top)}Z")
    style.body(view, case, colors["case"], width=0.5)
    # the wrist ledge at the front: its top face, then its front face
    face_y = ref(0, 800)[1]
    style.body(view, rounded_d(ledge_l, ledge_top, ledge_r - ledge_l, bottom - ledge_top, 1.6), colors["ledge"],
               width=0.5)
    style.flat(view, rounded_d(ledge_l + 0.4, face_y, ledge_r - ledge_l - 0.8, bottom - face_y - 0.4, 1.2),
               shade(colors["ledge"], 0.9))
    for px in (330, 1270):
        x = ref(px, 0)[0]
        style.line(view, f"M{num(x)},{num(ledge_top + 0.4)}V{num(bottom - 0.4)}", shade(colors["ledge"], 0.7), 0.5)
    # the lid: its frame, the panel with the notch for the key panel at the back
    fx = ref(1270, 0)[0]
    fy0, fy1 = ref(0, 80)[1], ref(0, 745)[1]
    style.body(view, rounded_d(-fx, fy0, 2 * fx, fy1 - fy0, 2.0), colors["frame"], width=0.4)
    px_, py0, py1 = ref(1243, 0)[0], ref(0, 110)[1], ref(0, 720)[1]
    nx, ny = ref(1095, 0)[0], ref(0, 228)[1]
    panel = (f"M{num(-px_)},{num(py0)}H{num(-nx)}V{num(ny - 3)}Q{num(-nx)},{num(ny)} {num(-nx + 3)},{num(ny)}"
             f"H{num(nx - 3)}Q{num(nx)},{num(ny)} {num(nx)},{num(ny - 3)}V{num(py0)}H{num(px_)}V{num(py1)}"
             f"H{num(-px_)}Z")
    style.body(view, panel, colors["panel"], width=0.35)
    style.sheen(view, panel, cy=0.1, r=0.9, opacity=0.04)
    # the key panel: a well round a plate
    wx, wy0, wy1 = ref(1070, 0)[0], ref(0, 62)[1], ref(0, 200)[1]
    style.flat(view, rounded_d(-wx, wy0, 2 * wx, wy1 - wy0, 2.0), colors["well"])
    cx_, cy0, cy1 = ref(1046, 0)[0], ref(0, 73)[1], ref(0, 178)[1]
    style.body(view, rounded_d(-cx_, cy0, 2 * cx_, cy1 - cy0, 1.6), colors["console"], width=0.35)

    # the lever: the ball top over its dust washer, the print ring round it
    lx, ly = ref(562, 400)
    ring(view, lx, ly, 22.0, colors["print"], 0.6, opacity=0.4)
    view.add("case", f'<path d="{circle_d(lx, ly, 20.2)}" fill="{shade(colors["panel"], 0.7)}"/>')
    view.add("controls", f'<path d="{circle_d(lx, ly + 0.8, 17.6)}" fill="{style.GAP}" fill-opacity="0.45"/>',
             "dpad.up")
    ball = view.defs.radial([(0, shade(colors["ball"], 1.18)), (0.55, colors["ball"]), (1, shade(colors["ball"], 0.8))],
                            cx=0.42, cy=0.38, r=0.62)
    view.add("controls", f'<path d="{circle_d(lx, ly, 17.3)}" fill="{ball}" stroke="{style.rim_of(colors["ball"])}" '
                         f'stroke-width="0.3"/>', "dpad.up")
    for control, x, y, w, h in (("dpad.up", lx - 9, ly - 29, 18, 12), ("dpad.down", lx - 9, ly + 17, 18, 12),
                                ("dpad.left", lx - 29, ly - 9, 12, 18), ("dpad.right", lx + 17, ly - 9, 12, 18)):
        view.spot(control, "Lever", ("rect", x, y, w, h), "left", group="dpad")

    # the eight buttons: Vewlix rows (the top row Square Triangle R1 L1, the bottom row Cross Circle R2 L2)
    for control, label, px, py in (("west", "Square", 797, 351), ("north", "Triangle", 886, 320),
                                   ("rb", "R1", 982, 331), ("lb", "L1", 1071, 362),
                                   ("south", "Cross", 787, 457), ("east", "Circle", 877, 426),
                                   ("rt", "R2", 972, 436), ("lt", "L2", 1066, 465)):
        arcade_button(view, *ref(px, py), colors, control, label, "right")

    # Options on the panel's right wing, its mark under it
    ox, oy = ref(1168, 168)
    ring(view, ox, oy, 18.4, colors["print"], 0.5, opacity=0.35)
    style.button(view, ox, oy, 11.8, colors["flange"], "menu", gap=0.5)
    view.add("controls", f'<path d="{circle_d(ox, oy, 9.0)}" fill="{shade(colors["plunger"], 0.96)}"/>', "menu")
    bars_glyph(view, *ref(1168, 220), 4.0, colors["glyph"], "menu")
    view.spot("menu", "Options", ("circle", ox, oy, 12.4), "right")

    # the key panel: Home (no logo), Create, Function, the touchpad, L3 / R3, the two sliders, two LEDs
    hx, hy = ref(590, 140)
    common.neutral_guide(view, hx, hy, 8.2, colors["key"], colors["glyph"], "guide")
    view.spot("guide", "Home", ("circle", hx, hy, 8.8), "left")
    small_round(view, *ref(643, 146), 5.6, colors, "view", "Create", "left")
    rays_glyph(view, *ref(643, 118), 3.2, colors["glyph"], "view")
    small_round(view, *ref(690, 146), 5.6, colors, "misc.1", "Function", "left")
    tx0, ty0 = ref(727, 88)
    tx1, ty1 = ref(872, 162)
    style.shaped(view, rounded_d(tx0, ty0, tx1 - tx0, ty1 - ty0, 1.6), shade(colors["console"], 1.08),
                 "touchpad.click", rounded_d(tx0 - 0.5, ty0 - 0.5, tx1 - tx0 + 1, ty1 - ty0 + 1, 2.0), stroke=0.3)
    view.spot("touchpad.click", "Touchpad", ("rect", tx0, ty0, tx1 - tx0, ty1 - ty0), "right")
    for control, label, px in (("lstick.click", "L3", 908), ("rstick.click", "R3", 956)):
        x, y = ref(px, 146)
        small_round(view, x, y, 5.6, colors, control, label, "right")
        style.label(view, label, x, y - 9.2, 2.6, colors["glyph"], control, opacity=0.8)
        view.add("case", f'<path d="{rounded_d(x - 1.0, ref(0, 103)[1], 2.0, 2.0, 0.3)}" '
                         f'fill="{shade(colors["console"], 1.5)}"/>')
    for px, py, w in ((1007, 111, 7.5), (1005, 148, 9.5)):
        x, y = ref(px, py)
        style.flat(view, rounded_d(x - w / 2, y - 2.4, w, 4.8, 0.8), colors["well"])
        style.flat(view, rounded_d(x - w / 2 + 0.6, y - 1.8, 3.0, 3.6, 0.6), colors["slider"])
    return pic


# -- Saturn pad -------------------------------------------------------------------------------------------------

SATURN_SHELL = [("M", (0, -36.9)),
    ("C", (1.5, -36.9), (3.1, -36.3), (4.3, -36.3)),
    ("C", (6.3, -36.3), (8.6, -36.5), (10.6, -36.8)),
    ("C", (24.3, -38.6), (29.9, -42.9), (44.3, -36.8)),
    ("C", (63.5, -28.6), (75.1, -1.7), (77.0, 17.7)),
    ("C", (77.9, 26.3), (77.6, 35.6), (70.5, 41.6)),
    ("C", (65.2, 46.0), (58.2, 47.8), (51.5, 46.0)),
    ("C", (46.1, 44.6), (41.7, 40.4), (37.2, 37.4)),
    ("C", (29.3, 32.0), (18.5, 26.8), (9.3, 24.7)),
    ("C", (6.5, 24.1), (2.8, 23.6), (0, 23.6))]


def saturn_dpad(view, cx, cy, colors):
    """The Saturn's D-pad: a round disc with a raised cross, in a round well"""
    style.recess(view, cx, cy, 17.4, colors["body"], depth=0.82)
    disc = view.defs.radial([(0, shade(colors["dpad"], 0.94)), (0.85, colors["dpad"]), (1, shade(colors["dpad"], 1.08))],
                            r=0.5)
    view.add("controls", f'<path d="{circle_d(cx, cy + 0.4, 15.4)}" fill="{style.GAP}" fill-opacity="0.45"/>',
             "dpad")
    view.add("controls", f'<path d="{circle_d(cx, cy, 15.0)}" fill="{disc}" stroke="{style.rim_of(colors["dpad"])}" '
                         f'stroke-width="0.25"/>', "dpad")
    style.shaped(view, style.cross_d(cx, cy, 25.0, 8.6, r=1.6), shade(colors["dpad"], 1.1), "dpad", stroke=0.25)
    dish = view.defs.radial([(0, shade(colors["dpad"], 0.86)), (1, shade(colors["dpad"], 1.1))], r=0.5)
    view.add("controls", f'<path d="{circle_d(cx, cy, 2.4)}" fill="{dish}"/>', "dpad")
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.glyph(view, style.arrow_d(cx + dx * 9.0, cy + dy * 9.0, 1.9, direction), colors["arrow"], "dpad",
                    fill=True, opacity=0.8)
    common.dpad_spots(view, cx, cy, 25.0, 8.6, "left", prefix="dpad.")


def saturn_pad():
    pic = Picture("saturn-pad", "Saturn-style six-button pad (Sega Saturn control pad, Japanese / model 2 shell)")
    pic.sources = [
        {"what": "size 155 x 86 x 45.7 mm (model 2)", "source": "https://www.dimensions.com/element/"
         "sega-saturn-controller-model-2", "author": "Dimensions.com", "retrieved": "2026-10-24"},
        {"what": "near-top view of the model 2 pad: the outline (cable masked, left half mirrored, mirror overlap "
                 "0.977), the control positions and sizes on a 5 mm grid", "source": COMMONS
         + "Sega_Saturn_Controller_-_Type_2.png", "author": "Alphathon", "licence": "CC BY-SA 3.0",
         "use": "measured geometry only; no pixels shipped", "retrieved": "2026-10-24"},
        {"what": "the colours of the Japanese grey pad (light grey shell, dark A B C, pale blue X Y Z, blue Start "
                 "and L / R), the D-pad disc", "source": COMMONS + "Sega-Saturn-JP-Mk1-Controller.jpg",
         "author": "Evan-Amos", "licence": "CC BY-SA 3.0", "use": "reference only, not traced",
         "retrieved": "2026-10-24"},
        {"what": "the model 2 shell in three-quarter view (the shoulders at the back corners)", "source": COMMONS
         + "Sega-Saturn-Controller-NA-Mk-II-FL.jpg", "author": "Evan-Amos", "licence": "public domain",
         "use": "reference only", "retrieved": "2026-10-24"},
        {"what": "the photo is not a true plan (a slight tilt): positions to about 1.5 mm; the shoulders' extent "
                 "is an estimate", "source": "this POC", "retrieved": "2026-10-24"}]
    colors = {"body": "#a3a5a9", "plate": "#a3a5a9", "trigger": "#7f90b4", "bumper": "#7f90b4", "dpad": "#3d3e42",
              "arrow": "#8c8f94", "abc": "#44454a", "xyz": "#a4b3cd", "start": "#56689e", "ink_abc": "#8d9096",
              "ink_xyz": "#6f7c95"}
    view = pic.view("front", oy=768 - 3.3 * 8.6)
    eightbitdo.top_shoulders(view, colors, ("L", "R", "", ""), -36.5, 22.0, 58.0, triggers=False,
                             ids=("lt", "rt", "", ""), rise=0.7)
    d = mirrored_d(SATURN_SHELL)
    style.body(view, d, colors["body"])
    style.sheen(view, d)
    # the moulded edge of the top face, a few millimetres inside the outline
    inner = [(cmd, *[(x * 0.95, y * 0.93 - 0.9) for x, y in pts]) for cmd, *pts in SATURN_SHELL]
    style.line(view, mirrored_d(inner), shade(colors["body"], 0.86), 0.35, opacity=0.8)
    saturn_dpad(view, -41.4, 0.3, colors)
    style.pill(view, 0, 12.8, 12.0, 6.2, colors["start"], "menu")
    view.spot("menu", "Start", ("rect", -6.4, 9.3, 12.8, 7.0), "right")
    for control, letter, x, y, r, cap, ink in (
            ("west", "X", 23.9, -4.6, 5.6, colors["xyz"], colors["ink_xyz"]),
            ("north", "Y", 35.4, -12.5, 5.6, colors["xyz"], colors["ink_xyz"]),
            ("lb", "Z", 48.2, -18.1, 5.6, colors["xyz"], colors["ink_xyz"]),
            ("south", "A", 30.1, 13.5, 7.3, colors["abc"], colors["ink_abc"]),
            ("east", "B", 44.0, 3.5, 7.3, colors["abc"], colors["ink_abc"]),
            ("rb", "C", 60.0, -3.2, 7.3, colors["abc"], colors["ink_abc"])):
        style.button(view, x, y, r, cap, control)
        dish = view.defs.linear([(0, shade(cap, 0.94)), (1, shade(cap, 1.04))])
        view.add("controls", f'<path d="{circle_d(x, y, r * 0.78)}" fill="{dish}"/>', control)
        style.label(view, letter, x, y, r * 0.95, ink, control, face="sans")
        view.spot(control, letter, ("circle", x, y, r + 0.4), "right")
    return pic


# -- Fight pad --------------------------------------------------------------------------------------------------

OCTA_SHELL = [("M", (0, -44.3)),
    ("C", (6.4, -44.3), (59.6, -45.0), (61.8, -44.0)),
    ("C", (64.6, -42.7), (67.7, -40.3), (69.3, -37.6)),
    ("C", (69.9, -36.7), (70.1, -35.7), (70.6, -34.8)),
    ("C", (72.3, -32.4), (74.7, -30.5), (76.0, -27.7)),
    ("C", (84.3, -9.7), (87.1, 17.8), (83.3, 37.2)),
    ("C", (82.2, 42.7), (81.0, 48.5), (77.6, 53.0)),
    ("C", (74.5, 57.2), (70.2, 60.2), (64.9, 59.8)),
    ("C", (44.9, 58.4), (50.4, 38.3), (40.8, 33.1)),
    ("C", (34.2, 29.6), (11.7, 30.9), (0, 30.9))]


def octagon_d(cx, cy, r):
    return polygon_d([(cx + r * math.cos(math.radians(22.5 + 45 * i)), cy + r * math.sin(math.radians(22.5 + 45 * i)))
                      for i in range(8)])


def octa_shoulders(view, colors):
    """L2 / R3 the raised keys at the back, L1 / L3 the wide shoulders over the case's top edge"""
    for sx, front, back, fname, bname, side in ((-1, "lb", "lt", "L1", "L2", "left"),
                                                (1, "lstick.click", "rstick.click", "L3", "R3", "right")):
        f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
        key = (f"M{f(20.9, -41.8)}L{f(21.4, -48.0)}C{f(21.5, -48.7)} {f(22.1, -49.0)} {f(22.8, -49.0)}"
               f"L{f(38.4, -48.6)}C{f(39.3, -48.6)} {f(39.9, -48.0)} {f(40.1, -47.2)}L{f(41.0, -41.8)}Z")
        style.shaped(view, key, colors["key2"], back, layer="shoulders")
        a, b = sorted((21 * sx, 40.5 * sx))
        view.spot(back, bname, ("rect", a, -49.2, b - a, 5.2), side)
        bumper = (f"M{f(20.8, -44.4)}L{f(59.8, -44.8)}C{f(63.8, -44.6)} {f(67.4, -41.9)} {f(69.3, -37.8)}"
                  f"C{f(69.8, -36.6)} {f(69.0, -35.0)} {f(67.6, -34.6)}C{f(62.0, -33.4)} {f(52.0, -32.8)} "
                  f"{f(42.0, -32.8)}L{f(22.2, -32.8)}C{f(21.3, -32.8)} {f(20.8, -33.4)} {f(20.8, -34.2)}Z")
        style.shaped(view, bumper, colors["bumper"], front, gap_d=bumper, stroke=0.3)
        style.line(view, f"M{f(23, -41.6)}L{f(58.5, -41.9)}", shade(colors["bumper"], 1.12), 0.3, control=front,
                   opacity=0.6, layer="controls")
        a, b = sorted((21.5 * sx, 68.8 * sx))
        view.spot(front, fname, ("rect", a, -44.6, b - a, 11.6), side)


def fight_pad():
    pic = Picture("fight-pad", "Six-button fight pad (HORI Fighting Commander OCTA as measured)")
    pic.sources = [
        {"what": "width 170 mm (HORI gives W 170 x D 90 x H 48 mm, 230 g; the 90 mm depth does not match the views, "
                 "which give 107-113 mm: the plan follows the views)", "source": PS_OLP + "fighting-commander-octa/",
         "author": "Sony Interactive Entertainment / HORI", "retrieved": "2026-10-24"},
        traced("top view: the outline (cable masked, left half mirrored, the shoulders above the case cut at 5.6 mm, "
               "mirror overlap 1.000), every control's position and size on a 5 mm grid",
               "https://gmedia.playstation.com/is/image/SIEPDC/fighting-commander-octa-02-ja-06apr22", "HORI"),
        {"what": "the bottom view (the plan's proportions, 170 x 113 mm)",
         "source": "https://gmedia.playstation.com/is/image/SIEPDC/fighting-commander-octa-08-ja-06apr22",
         "author": "HORI", "use": "reference only", "retrieved": "2026-10-24"},
        {"what": "the right shoulders as L3 (front) and R3 (back): HORI's usual assignment on its one-stick pads; "
                 "an assumption, not read on the device", "source": "this POC", "retrieved": "2026-10-24"}]
    colors = {"body": "#2e2f32", "bumper": "#37383c", "key2": "#313236", "button": "#27282b", "small": "#2a2b2e",
              "pad": "#2b2c2f", "dpad": "#2a2b2e", "arrow": "#80838a", "stick": "#2a2b2e", "glyph": "#9ea1a7",
              "label": "#8e9197", "led": "#5a5c61"}
    inks = {"north": "#5ea89a", "east": "#c27c8a", "south": "#7d9ccb", "west": "#b997c2"}
    view = pic.view("front", oy=768 - 7.8 * 8.6)
    octa_shoulders(view, colors)
    d = mirrored_d(OCTA_SHELL)
    style.body(view, d, colors["body"])
    style.sheen(view, d)
    # the touchpad, Create and Options beside it
    style.shaped(view, rounded_d(-29.8, -38.2, 59.6, 34.6, 3.2), colors["pad"], "touchpad.click",
                 rounded_d(-30.3, -38.7, 60.6, 35.6, 3.6), stroke=0.3)
    view.spot("touchpad.click", "Touchpad", ("rect", -29.8, -38.2, 59.6, 34.6), "right")
    for control, label, x, glyph in (("view", "Create", -34.7, rays_glyph), ("menu", "Options", 34.9, bars_glyph)):
        style.pill(view, x, -25.6, 4.6, 9.0, colors["small"], control)
        glyph(view, x, -18.6 if glyph is bars_glyph else -20.3, 2.6, colors["glyph"], control)
        view.spot(control, label, ("rect", x - 2.7, -30.5, 5.4, 9.8), "left" if x < 0 else "right")
    # the eight-way D-pad: a disc with a cross on it, in a raised collar
    cx, cy = -55.3, -3.3
    collar = view.defs.linear([(0, shade(colors["body"], 1.05)), (1, shade(colors["body"], 0.95))])
    view.add("case", f'<path d="{circle_d(cx, cy, 15.0)}" fill="{collar}" stroke="{style.rim_of(colors["body"])}" '
                     f'stroke-width="0.3"/>')
    style.recess(view, cx, cy, 13.6, colors["body"], depth=0.7)
    disc = view.defs.radial([(0, shade(colors["dpad"], 0.92)), (0.8, colors["dpad"]), (1, shade(colors["dpad"], 1.1))],
                            r=0.5)
    view.add("controls", f'<path d="{circle_d(cx, cy, 12.8)}" fill="{disc}" stroke="{style.rim_of(colors["dpad"])}" '
                         f'stroke-width="0.25"/>', "dpad")
    style.shaped(view, style.cross_d(cx, cy, 22.6, 7.4, r=1.4), shade(colors["dpad"], 1.12), "dpad", stroke=0.25)
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.glyph(view, style.arrow_d(cx + dx * 8.0, cy + dy * 8.0, 1.7, direction), colors["arrow"], "dpad",
                    fill=True, opacity=0.8)
    common.dpad_spots(view, cx, cy, 22.6, 7.4, "left", prefix="dpad.")
    # the left stick in its octagonal gate
    sx_, sy_ = -31.3, 17.2
    view.add("case", f'<path d="{circle_d(sx_, sy_, 14.3)}" fill="{collar}" stroke="{style.rim_of(colors["body"])}" '
                     f'stroke-width="0.3"/>')
    gate = octagon_d(sx_, sy_, 11.6)
    view.add("case", f'<path d="{gate}" fill="{shade(colors["body"], 0.62)}" stroke="{shade(colors["body"], 0.5)}" '
                     f'stroke-width="0.25"/>')
    style.stick(view, sx_, sy_, 7.4, colors["stick"], "lstick.x", skirt=9.0)
    view.spot("lstick.x", "Stick", ("circle", sx_, sy_, 7.8), "left",
              analog={"kind": "stick", "pair": "lstick.y", "radius": 4.0}, group="lstick")
    # Home (no logo), three LEDs, the mute and function keys
    common.neutral_guide(view, 0, 6.2, 5.4, colors["small"], colors["glyph"], "guide")
    view.spot("guide", "Home", ("circle", 0, 6.2, 5.9), "right")
    for x in (-10.0, 0, 10.0):
        view.add("case", f'<path d="{circle_d(x, 15.6, 0.6)}" fill="{colors["led"]}"/>')
    for control, label, x in (("misc.1", "Mute", -9.6), ("misc.2", "Function", 9.6)):
        style.pill(view, x, 23.5, 10.4, 5.0, colors["small"], control, gap=0.55)
        view.spot(control, label, ("rect", x - 5.6, 20.7, 11.2, 5.6), "left" if x < 0 else "right")
    # six face buttons in two rows: Square Triangle R1 over Cross Circle R2
    marks = {"north": "triangle", "east": "circle", "south": "cross", "west": "square"}
    for control, label, x, y in (("west", "Square", 30.3, 4.4), ("north", "Triangle", 43.8, -7.5),
                                 ("rb", "R1", 60.6, -15.1), ("south", "Cross", 41.8, 18.8),
                                 ("east", "Circle", 55.2, 6.8), ("rt", "R2", 72.4, -1.0)):
        style.button(view, x, y, 6.4, colors["button"], control, gap=0.55)
        if control in marks:
            playstation.symbols(marks[control])(view, x, y, 6.4, control, inks[control])
        else:
            style.label(view, label, x, y, 4.4, colors["label"], control)
        view.spot(control, label, ("circle", x, y, 6.8), "right")
    return pic


SETS = {
    "arcade-stick": arcade_stick,
    "saturn-pad": saturn_pad,
    "fight-pad": fight_pad,
}
