"""The Xbox pads: the Series X|S controller, the One S controller, the Elite Series 1 and 2 with their back views.

The Series and the One S share one shell drawn at the Series controller's 153 x 102 mm plan (Microsoft's 6.02 x 4.01
x 2.40 in, as the retail listings quote it). Each Elite has its own shell traced (trace.py) on a straight top view
scaled to the same width, its controls measured on that view. Control positions and sizes are measured on
near-orthographic photos scaled to that width (provenance.json names them): the offset layout, the left stick high,
the D-pad and the right stick low, mirrored about the centre line.
"""

import math

import style
from devices import common
from model import Picture
from svgkit import circle_d, mirrored_d, num, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
SIZE = {"what": "plan 153 x 102 mm, height 61 mm (6.02 x 4.01 x 2.40 in)",
        "source": "retail listings of the Xbox Wireless Controller (pcrichard.com QAU-00090, nfm.com 64696412)",
        "retrieved": "2026-10-23"}

TOP = -48.0  # the shell's top edge at the centre line; the grips end at +53.5
SHELL = [("M", (0, TOP)),
         ("C", (18, TOP), (38, TOP - 0.6), (53, TOP + 0.4)),
         ("C", (66, TOP + 1.2), (74.5, TOP + 6.5), (76.6, TOP + 17)),
         ("C", (78.2, -20), (78.6, -4), (77.2, 12)),
         ("C", (76, 28), (74.6, 40), (70.4, 48)),
         ("C", (66.6, 55), (55, 57), (50.4, 51.5)),
         ("C", (46.6, 47), (43.6, 37), (40, 31.5)),
         ("C", (35, 27.3), (20, 27), (0, 27))]
LSTICK, RSTICK, DPAD, FACE = (-43, -23.2), (22.5, 3.8), (-22.5, 3.8), (42.5, -23.6)
LETTERS = {"north": ("Y", "#e2b23a"), "west": ("X", "#3f7fd0"), "east": ("B", "#d0453a"), "south": ("A", "#5aa848")}


def _p(sx, x, y, flip=False):
    return f"{num(x * sx)},{num(-y if flip else y)}"


def bumper_d(sx=1, flip=False):
    """A bumper round the top corner, drawn under the shell: only its outer band shows"""
    f = lambda x, y: _p(sx, x, y, flip)  # noqa: E731
    return (f"M{f(20, TOP + 1.5)}C{f(30, TOP - 2.6)} {f(46, TOP - 4.4)} {f(58, TOP - 3.6)}"
            f"C{f(69, TOP - 2.8)} {f(77, TOP + 2.6)} {f(79, TOP + 11.5)}C{f(79.4, TOP + 13.5)} {f(79.2, TOP + 15.5)} "
            f"{f(78.6, TOP + 17)}L{f(70, TOP + 17)}L{f(24, TOP + 6)}Z")


def trigger_d(sx=1, flip=False):
    """A trigger's top, behind the bumper, as a slightly raised view shows it"""
    f = lambda x, y: _p(sx, x, y, flip)  # noqa: E731
    return (f"M{f(36, TOP - 1)}C{f(40, TOP - 6.6)} {f(50, TOP - 8.4)} {f(58, TOP - 8)}"
            f"C{f(65.5, TOP - 7.6)} {f(71, TOP - 4.6)} {f(73.5, TOP)}L{f(60, TOP + 2)}Z")


def shoulders(view, colors, names):
    for sx, trig, side in ((-1, "lt", "left"), (1, "rt", "right")):
        style.shaped(view, trigger_d(sx), colors["trigger"], trig, layer="shoulders")
        x0, x1 = sorted((38 * sx, 72 * sx))
        view.spot(trig, names[trig], ("rect", x0, TOP - 8.4, x1 - x0, 7.4), side,
                  analog={"kind": "trigger", "fill": "down"})
    for sx, bump, side in ((-1, "lb", "left"), (1, "rb", "right")):
        style.shaped(view, bumper_d(sx), colors["bumper"], bump, layer="shoulders")
        x0, x1 = sorted((30 * sx, 76 * sx))
        view.spot(bump, names[bump], ("rect", x0, TOP - 3.6, x1 - x0, 5.4), side)


def shell(view, colors, flip=False):
    d = mirrored_d(SHELL, mirror_y=flip)
    style.body(view, d, colors["body"])
    style.sheen(view, d, cy=0.78 if flip else 0.22)
    return d


def sticks(view, colors, collar=None, at=(LSTICK, RSTICK)):
    for name, label, (cx, cy), side in (("lstick", "LS", at[0], "left"), ("rstick", "RS", at[1], "right")):
        style.recess(view, cx, cy, 12.4, colors["body"], depth=colors.get("well", 0.55))
        if collar:  # the Elite's metal shaft collar shows round the cap
            view.add("controls", f'<path d="{circle_d(cx, cy, 11.6)}" fill="{collar}" stroke="{shade(collar, 0.7)}" '
                                 f'stroke-width="0.25"/>', f"{name}.x")
            style.stick(view, cx, cy, 10.1, colors["stick"], f"{name}.x")
        else:
            style.stick(view, cx, cy, 10.3, colors["stick"], f"{name}.x", skirt=11.8)
        common.stick_spots(view, name, label, cx, cy, 10.6, 6.5, side)


def ticks16(view, colors, cx, cy, r0=15.6, r1=16.6):
    """The Elite 2's sixteen ticks round the D-pad's dish"""
    for i in range(16):
        a = math.pi * 2 * i / 16
        style.line(view, f"M{num(cx + r0 * math.cos(a))},{num(cy + r0 * math.sin(a))}"
                         f"L{num(cx + r1 * math.cos(a))},{num(cy + r1 * math.sin(a))}",
                   shade(colors["body"], 1.35), 0.22, opacity=0.6)


def dpad(view, colors, kind, at=DPAD, ticks=True):
    """@p ticks: the Elite 2 marks the dish round either of its D-pads, the Elite 1 does not"""
    cx, cy = at
    if kind == "cross":  # One S: a plus standing straight on the shell
        span, arm = 26.5, 8.8
        style.shaped(view, style.cross_d(cx, cy, span, arm, r=1.3), colors["dpad"], "dpad",
                     style.cross_d(cx, cy, span + 1.0, arm + 1.0, r=1.6))
    elif kind == "hybrid":  # Series: the cross filling a dished disc, the disc's quarters as lower facets
        span, arm = 27, 9
        style.recess(view, cx, cy, 14.6, colors["body"], depth=0.55)
        style.button(view, cx, cy, 14, shade(colors["dpad"], 1.05), "dpad", gap=0.35)
        fill = view.defs.linear([(0, shade(colors["dpad"], 1.04)), (1, shade(colors["dpad"], 0.96))])
        view.add("controls", f'<path d="{style.cross_d(cx, cy, span, arm, r=1.1)}" fill="{fill}" '
                             f'stroke="{shade(colors["dpad"], 0.62)}" stroke-width="0.3" stroke-linejoin="round"/>',
                 "dpad")
    elif kind == "disc":  # an Elite's other D-pad: the faceted metal disc
        span, arm = 24, 8.4
        style.recess(view, cx, cy, 13.8, colors["body"], depth=0.62)
        if ticks:
            ticks16(view, colors, cx, cy)
        faceted_dpad(view, cx, cy, 12.8, colors["disc"], "dpad")
    else:  # the Elites: a metal cross on its dish
        span, arm = 26, 8.6
        style.recess(view, cx, cy, 14.8, colors["body"], depth=0.62)
        if ticks:
            ticks16(view, colors, cx, cy)
        style.shaped(view, style.cross_d(cx, cy, span, arm, r=1.4), colors["dpad"], "dpad",
                     style.cross_d(cx, cy, span + 1.0, arm + 1.0, r=1.7))
    common.dpad_spots(view, cx, cy, span, arm, "left", prefix="dpad.")


def face(view, colors, mono=False, at=FACE, offset=10.2, r=5.3):
    labels = {}
    for control, (letter, ink) in LETTERS.items():
        labels[control] = {"cap": colors["button"], "text": letter, "name": letter,
                           "ink": colors["letter"] if mono else style.tint(ink, 0.6)}
    common.face_diamond(view, *at, offset, r, labels)


def system(view, colors, share=True):
    gy = TOP + 10
    common.neutral_guide(view, 0, gy, 6.9, colors["guide"], colors["guide_ring"], "guide")
    view.spot("guide", "Guide", ("circle", 0, gy, 7.2), "right")
    for control, label, x, draw in (("view", "View", -12, common.view_glyph), ("menu", "Menu", 12, common.menu_glyph)):
        style.button(view, x, -22.5, 3.8, colors["small"], control)
        draw(view, x, -22.5, 4.2, colors["glyph"], control)
        view.spot(control, label, ("circle", x, -22.5, 4.2), "left" if x < 0 else "right")
    if share:
        style.pill(view, 0, -14, 8.8, 5.0, colors["small"], "share")
        common.share_glyph(view, 0, -14, 3.6, colors["glyph"], "share")
        view.spot("share", "Share", ("rect", -4.9, -17, 9.8, 6), "right")


COLORS_DARK = {"body": "#3f4145", "trigger": "#36383c", "bumper": "#3b3d41", "button": "#26272a", "small": "#2a2b2e",
               "stick": "#2d2e31", "dpad": "#333438", "guide": "#2a2b2e", "guide_ring": "#9a9da3",
               "glyph": "#c9ccd1", "letter": "#d7d9dd"}


def series():
    pic = Picture("xbox-series", "Xbox Wireless Controller (Series X|S), Carbon Black")
    pic.sources = [SIZE, {"what": "control positions and sizes, colours",
                          "source": COMMONS + "Xbox_Series_Controller_Carbon_Black.jpg",
                          "author": "UKER", "licence": "CC BY-SA 4.0", "use": "reference only, not traced",
                          "retrieved": "2026-10-23", "note": "measured to about 1 mm on a near-orthographic photo"}]
    view = pic.view("front", oy=768 - 2.5 * 8.6)
    shoulders(view, COLORS_DARK, {"lt": "LT", "rt": "RT", "lb": "LB", "rb": "RB"})
    shell(view, COLORS_DARK)
    sticks(view, COLORS_DARK)
    dpad(view, COLORS_DARK, "hybrid")
    face(view, COLORS_DARK)
    system(view, COLORS_DARK, share=True)
    return pic


def one():
    pic = Picture("xbox-one", "Xbox One S Wireless Controller, Robot White")
    pic.sources = [SIZE, {"what": "the One S layout: no Share button, a plus-shaped D-pad, white shell, black controls",
                          "source": COMMONS + "Xbox_One_S_controller_(cropped).jpg", "author": "Ratianidze",
                          "licence": "CC BY-SA 3.0", "use": "reference only, not traced", "retrieved": "2026-10-23"},
                   {"what": "the shell and the control positions shared with the Series controller",
                    "source": COMMONS + "Xbox_Series_Controller_Carbon_Black.jpg", "author": "UKER",
                    "licence": "CC BY-SA 4.0", "use": "reference only, not traced", "retrieved": "2026-10-23"}]
    colors = {"well": 0.8, "body": "#e4e5e3", "trigger": "#d2d3d1", "bumper": "#d9dad8", "button": "#25262a",
              "small": "#e9eae8",
              "stick": "#2a2b2e", "dpad": "#27282c", "guide": "#2a2b2e", "guide_ring": "#9a9da3",
              "glyph": "#3b3d42", "letter": "#d7d9dd"}
    view = pic.view("front", oy=768 - 2.5 * 8.6)
    shoulders(view, colors, {"lt": "LT", "rt": "RT", "lb": "LB", "rb": "RB"})
    shell(view, colors)
    sticks(view, colors)
    dpad(view, colors, "cross")
    face(view, colors)
    system(view, colors, share=False)
    return pic


# The Elite Series 2 has a shell of its own: traced (trace.py) on Microsoft's front render of the Series 2 scaled to
# 153 mm (the half mirrored, overlap 0.995); the grip's rubber texture smoothed out of the side and the paddle tips
# that show under the arch left out
E2_SHELL = [("M", (0, -49.0)),
            ("C", (6.9, -49.0), (13.8, -48.4), (20.6, -48.8)),
            ("C", (23.2, -49.0), (25.3, -51.5), (27.9, -51.9)),
            ("C", (34.2, -53.0), (47.9, -47.8), (52.5, -43.6)),
            ("C", (54.5, -39.5), (58.2, -36.8), (59.9, -32.1)),
            ("C", (61.2, -28.5), (62.2, -24.6), (63.3, -20.9)),
            ("C", (66.5, -10.0), (72.5, 2.0), (75.2, 16.0)),
            ("C", (76.5, 27.8), (77.3, 36.7), (72.8, 45.2)),
            ("C", (71.1, 48.4), (68.9, 50.5), (65.8, 52.3)),
            ("C", (64.3, 53.1), (62.2, 54.6), (60.5, 54.6)),
            ("C", (54.2, 54.6), (44.0, 39.0), (37.5, 30.5)),
            ("C", (35.5, 27.8), (32.0, 25.1), (26.0, 25.1)),
            ("C", (18.0, 25.1), (8.0, 25.1), (0, 25.1))]
# measured on the same render (about 0.5 mm): the sticks, the D-pad, the face diamond, Guide, View / Menu, the profile
# key; the render is taken from far off, so the controls on the top keep their spacing against the outline
E2_LSTICK, E2_RSTICK, E2_DPAD, E2_FACE, E2_GUIDE = (-39.6, -22.6), (20.3, 1.2), (-20.6, 2.6), (40.1, -22.3), -37.4
# The paddles of the right hand in front coordinates (x right as held, y down from the trigger side), measured on
# Microsoft's back render: flat steel blades, each a bar from its mount out towards the grip that breaks at a rounded
# kink, P1 into a short lip, P2 into a long leg down the handle that narrows after the kink and swells into a drop at
# its end. P3 / P4 on the left hand are their mirror images.
E2_P1 = [(20.0, -1.4, 1.0), (20.0, 4.2, 1.0), (34.0, 4.6, 3.0), (39.5, 8.6, 1.2), (42.0, 8.0, 1.2),
         (40.5, -1.0, 4.0)]  # (x, y, corner radius) round the outline


def e2_p2_d(sx, flip=False):
    """The long paddle: the bar, the rounded kink, the narrow neck, the drop at the end"""
    f = lambda x, y: _p(sx, x, y, flip)  # noqa: E731
    return (f"M{f(20.0, 7.4)}Q{f(20.0, 6.4)} {f(21.0, 6.4)}L{f(34.0, 6.4)}"
            f"C{f(38.5, 6.4)} {f(40.0, 9.0)} {f(40.2, 13.0)}"
            f"C{f(40.3, 18.0)} {f(41.0, 21.0)} {f(42.6, 24.0)}C{f(43.8, 26.3)} {f(44.4, 28.0)} {f(44.2, 29.8)}"
            f"C{f(44.0, 32.2)} {f(41.8, 33.2)} {f(39.8, 32.8)}C{f(37.4, 32.3)} {f(35.8, 29.8)} {f(35.8, 26.5)}"
            f"C{f(35.8, 22.5)} {f(36.0, 18.5)} {f(34.4, 15.5)}C{f(33.4, 13.6)} {f(31.0, 12.4)} {f(28.0, 12.4)}"
            f"L{f(21.0, 12.4)}Q{f(20.0, 12.4)} {f(20.0, 11.4)}Z")


E2_PADDLES = (("paddle.p1", "P1", 1, (31.0, 1.6), 5.0, "right"), ("paddle.p2", "P2", 1, (37.0, 17.0), 6.4, "right"),
              ("paddle.p3", "P3", -1, (-31.0, 1.6), 5.0, "left"), ("paddle.p4", "P4", -1, (-37.0, 17.0), 6.4, "left"))


def rounded_poly_d(points):
    """A closed outline through (x, y, r) corners, each rounded off with a quadratic curve of about radius r"""
    out = []
    for k, (x, y, r) in enumerate(points):
        (px, py, _), (nx, ny, _) = points[k - 1], points[(k + 1) % len(points)]
        a, b = math.hypot(px - x, py - y), math.hypot(nx - x, ny - y)
        t0, t1 = min(r, a / 2) / a, min(r, b / 2) / b
        p0 = (x + (px - x) * t0, y + (py - y) * t0)
        p1 = (x + (nx - x) * t1, y + (ny - y) * t1)
        out.append(f"{'M' if k == 0 else 'L'}{num(p0[0])},{num(p0[1])}Q{num(x)},{num(y)} {num(p1[0])},{num(p1[1])}")
    return "".join(out) + "Z"


def blade(view, d, color, control):
    """A steel paddle of outline @p d, standing out of its dark gap"""
    view.add("controls", f'<path d="{d}" fill="none" stroke="{style.GAP}" stroke-opacity="0.55" stroke-width="0.9" '
                         f'stroke-linejoin="round"/>', control)
    style.shaped(view, d, color, control)


def e2_trigger_d(sx, flip=False):
    """A trigger as the top view shows it: its upper face fills the shoulder from the outline down to the bumper"""
    f = lambda x, y: _p(sx, x, y, flip)  # noqa: E731
    return (f"M{f(20.6, -48.8)}C{f(23.2, -49.0)} {f(25.3, -51.5)} {f(27.9, -51.9)}"
            f"C{f(34.2, -53.0)} {f(47.9, -47.8)} {f(52.5, -43.6)}C{f(53.2, -42.5)} {f(53.6, -41.6)} {f(54.0, -40.6)}"
            f"L{f(53.6, -39.8)}C{f(52.5, -41.3)} {f(48.5, -45.3)} {f(44.2, -45.9)}C{f(40.0, -46.5)} {f(36.0, -47.1)} "
            f"{f(32.0, -47.1)}C{f(27.0, -47.1)} {f(23.0, -46.4)} {f(21.0, -46.1)}Z")


def e2_bumper_d(sx, flip=False):
    """The bumper: a narrow band in front of the trigger, along the top edge of the black face"""
    f = lambda x, y: _p(sx, x, y, flip)  # noqa: E731
    return (f"M{f(21.0, -46.1)}C{f(23.0, -46.4)} {f(27.0, -47.1)} {f(32.0, -47.1)}C{f(36.0, -47.1)} {f(40.0, -46.5)} "
            f"{f(44.2, -45.9)}C{f(48.5, -45.3)} {f(52.5, -41.3)} {f(53.6, -39.8)}L{f(55.5, -36.9)}"
            f"C{f(54.5, -38.2)} {f(53.5, -39.0)} {f(52.6, -39.3)}C{f(50.5, -40.6)} {f(48.5, -41.7)} {f(46.5, -42.3)}"
            f"C{f(44.0, -43.1)} {f(42.0, -43.8)} {f(39.5, -44.2)}C{f(36.0, -44.8)} {f(33.0, -45.2)} {f(30.0, -45.2)}"
            f"L{f(20.6, -45.6)}Z")


def e2_grip_d(sx, flip=False):
    """The textured rubber of a handle: the outline from the side beside the stick round the end to the arch, back up
    the diagonal seam"""
    f = lambda x, y: _p(sx, x, y, flip)  # noqa: E731
    return (f"M{f(63.3, -20.9)}C{f(66.5, -10.0)} {f(72.5, 2.0)} {f(75.2, 16.0)}C{f(76.5, 27.8)} {f(77.3, 36.7)} "
            f"{f(72.8, 45.2)}C{f(71.1, 48.4)} {f(68.9, 50.5)} {f(65.8, 52.3)}C{f(64.3, 53.1)} {f(62.2, 54.6)} "
            f"{f(60.5, 54.6)}C{f(54.2, 54.6)} {f(44.0, 39.0)} {f(37.5, 30.5)}C{f(32.5, 24.0)} {f(40.0, 10.0)} "
            f"{f(48.8, -0.2)}C{f(54.0, -7.0)} {f(59.0, -14.0)} {f(63.3, -20.9)}Z")


def e2_metal(view, colors, flip=False):
    """The triggers' tops, the bumpers in front of them and the metal strip between them along the top edge"""
    for sx in (-1, 1):
        style.shaped(view, e2_trigger_d(sx, flip), colors["trigger"], None if flip else ("lt", "rt")[sx > 0])
        style.shaped(view, e2_bumper_d(sx, flip), colors["bumper"], None if flip else ("lb", "rb")[sx > 0])
    y = 46.2 if flip else -49.0
    view.add("case", f'<path d="{rounded_d(-20.6, y, 41.2, 2.8, 0.3)}" fill="{colors["strip"]}" '
                     f'stroke="{style.rim_of(colors["strip"])}" stroke-width="0.2"/>')


def elite2(pad="cross"):
    """@p pad: "cross" or "disc", the two D-pads the Elite ships with (a set each, the user picks one)"""
    name = "xbox-elite-2" if pad == "cross" else "xbox-elite-2-disc"
    pic = Picture(name, "Xbox Elite Wireless Controller Series 2, front and back"
                  + ("" if pad == "cross" else ", the faceted D-pad"))
    pic.sources = [SIZE,
                   {"what": "the outline (trace.py, the half mirrored, overlap 0.995) and the control positions, "
                            "scaled to 153 mm; front: the faceted D-pad (nine flat facets on a 3 x 3 grid), "
                            "the cross on its dish with ticks, the stick collars, the profile key under View / Menu, "
                            "the three profile lights between the D-pad and the right stick, rubber grips, "
                            "the bumpers and the top strip",
                    "source": "https://www.xbox.com/en-US/accessories/controllers/elite-wireless-controller-series-2 "
                              "(999666_Buy-Box-Image-0_2)",
                    "author": "Microsoft", "licence": "product render",
                    "use": "measured geometry only (trace.py outline, positions); no pixels shipped",
                    "retrieved": "2026-11-01"},
                   {"what": "back: the four paddles P1-P4 (P1 / P2 on the right hand, upper / lower), the trigger "
                            "stops, the charging contacts", "source": COMMONS
                    + "Xbox_One_Elite_Wireless_Controller_Series_2_(Model_1797)_-_underside.jpg",
                    "author": "mliu92", "licence": "CC BY-SA 2.0", "use": "reference only, not traced",
                    "retrieved": "2026-11-01",
                    "note": "drawn as the pad turned over towards the viewer (triggers at the bottom), so the right "
                            "hand's paddles stay on the right; positions estimated to about 2 mm"},
                   {"what": "weight 345 g with the four paddles", "source": "retail listings (abt.com 141809)",
                    "retrieved": "2026-10-23"}]
    colors = dict(COLORS_DARK, body="#2c2d30", grip="#313235", trigger="#56595e", bumper="#3c3e42",
                  strip="#a3a6ab", dpad="#5d6066", disc="#8e9196", collar="#1d1e20", paddle="#80848a",
                  small="#232426", button="#202123", letter="#7d8085", guide="#a9acb1", guide_ring="#6f7277")
    front_scale = 7.2
    front = pic.view("front", oy=104 + 53.6 * front_scale, scale=front_scale)
    d = mirrored_d(E2_SHELL)
    style.body(front, d, colors["body"])
    for sx in (-1, 1):
        style.body(front, e2_grip_d(sx), colors["grip"], width=0.25)
    style.sheen(front, d)
    e2_metal(front, colors)
    for sx, trig, bump, side in ((-1, "lt", "lb", "left"), (1, "rt", "rb", "right")):
        x0, x1 = sorted((21 * sx, 54 * sx))
        front.spot(trig, trig.upper(), ("rect", x0, -52.6, x1 - x0, 6.0), side,
                   analog={"kind": "trigger", "fill": "down"})
        x0, x1 = sorted((20.6 * sx, 55.5 * sx))
        front.spot(bump, bump.upper(), ("rect", x0, -46.6, x1 - x0, 3.0), side)
    # the crease of the top: from each bumper down round Guide, flat under it
    crease = (f"M{num(-31.0)},{num(-44.0)}C{num(-25.0)},{num(-38.0)} {num(-21.0)},{num(-33.2)} "
              f"{num(-15.0)},{num(-33.2)}L{num(15.0)},{num(-33.2)}C{num(21.0)},{num(-33.2)} {num(25.0)},{num(-38.0)} "
              f"{num(31.0)},{num(-44.0)}")
    style.line(front, crease, shade(colors["body"], 1.45), 0.3, opacity=0.5)
    common.neutral_guide(front, 0, E2_GUIDE, 5.8, colors["guide"], colors["guide_ring"], "guide")
    front.spot("guide", "Guide", ("circle", 0, E2_GUIDE, 6.2), "right")
    sticks(front, colors, collar=colors["collar"], at=(E2_LSTICK, E2_RSTICK))
    dpad(front, colors, "elite" if pad == "cross" else "disc", at=E2_DPAD)
    face(front, colors, mono=True, at=E2_FACE, offset=10.5, r=5.2)
    for control, label, x, draw in (("view", "View", -11.3, common.view_glyph),
                                    ("menu", "Menu", 11.3, common.menu_glyph)):
        style.button(front, x, -22.3, 3.4, colors["small"], control)
        draw(front, x, -22.3, 3.8, colors["glyph"], control)
        front.spot(control, label, ("circle", x, -22.3, 3.8), "left" if x < 0 else "right")
    # the profile key under View / Menu, the pairing light between them, the three profile lights lower down
    style.pill(front, 0, -14.3, 7.4, 3.4, colors["small"], "profile")
    front.spot("profile", "Profile", ("rect", -4.2, -16.4, 8.4, 4.2), "right")
    front.add("case", f'<path d="{circle_d(0, -22.3, 0.4)}" fill="{shade(colors["body"], 0.55)}"/>')
    for i in (-1, 0, 1):
        front.add("case", f'<path d="{rounded_d(-1.7, 2.4 + i * 2.6 - 0.45, 3.4, 0.9, 0.45)}" '
                          f'fill="{shade(colors["body"], 0.55)}"/>')

    # The back, the pad turned over towards the viewer: the triggers at the bottom, the right hand still right
    back_scale = 4.0
    back = pic.view("back", oy=104 + (53.6 + 54.6) * front_scale + 30 + 54.6 * back_scale, scale=back_scale)
    d = mirrored_d(E2_SHELL, mirror_y=True)
    style.body(back, d, shade(colors["body"], 0.96))
    for sx in (-1, 1):
        style.body(back, e2_grip_d(sx, flip=True), colors["grip"], width=0.3)
        # the trigger stop: a three-position slider beside each trigger
        back.add("case", f'<path d="{rounded_d(sx * 32.2 - 2, 7, 4, 7.4, 2)}" fill="{shade(colors["body"], 0.55)}"/>')
        back.add("case", f'<path d="{rounded_d(sx * 32.2 - 1.4, 7.6, 2.8, 3.2, 1.4)}" fill="{colors["collar"]}"/>')
        # the profile slots either side of the three profile lights, under the mounts
        back.add("case", f'<path d="{rounded_d(sx * 10.6 - 2.5, 5.5, 5, 1.4, 0.7)}" '
                         f'fill="{shade(colors["body"], 0.45)}"/>')
    e2_metal(back, colors, flip=True)
    style.sheen(back, d, cy=0.78)
    # the charging contacts between the lower paddles, the three profile lights above them
    back.add("case", f'<path d="{rounded_d(-3.5, -8.6, 7, 2, 1)}" fill="{shade(colors["body"], 0.45)}"/>')
    for x in (-4.9, 0, 4.9):
        back.add("case", f'<path d="{circle_d(x, 6.2, 0.6)}" fill="{shade(colors["body"], 1.6)}"/>')
    for control, name, sx, centre, r, side in E2_PADDLES:
        d = rounded_poly_d([(sx * x, -y, rr) for x, y, rr in E2_P1]) if name in ("P1", "P3") else e2_p2_d(sx, True)
        blade(back, d, colors["paddle"], control)
        back.spot(control, name, ("circle", centre[0], -centre[1], r), side)
    return pic


# The Elite Series 1 is the Xbox One shell, not the Series one: traced (trace.py) on a straight top view of a model 1698
# scaled to 153 mm (the half mirrored, overlap 0.990, 108 mm deep), the bumpers and the trigger tips in the outline
ELITE1_SHELL = [("M", (0, -48.5)),
                ("C", (5.3, -48.5), (13.6, -48.0), (19.8, -48.5)),
                ("C", (22.3, -48.7), (24.5, -50.3), (26.7, -51.3)),
                ("C", (30.7, -53.1), (38.3, -50.9), (42.3, -49.4)),
                ("C", (48.1, -47.1), (48.8, -45.5), (52.9, -41.5)),
                ("C", (54.7, -39.6), (56.5, -38.0), (57.9, -35.9)),
                ("C", (60.8, -31.7), (61.6, -25.1), (63.6, -20.2)),
                ("C", (70.6, -3.1), (85.6, 38.1), (68.8, 52.5)),
                ("C", (67.1, 54.0), (63.8, 56.2), (61.3, 56.2)),
                ("C", (59.4, 56.2), (58.1, 52.6), (56.8, 50.7)),
                ("C", (54.9, 47.9), (49.8, 42.0), (47.3, 39.9)),
                ("C", (44.6, 37.0), (40.5, 32.4), (36.3, 31.0)),
                ("C", (33.3, 30.2), (30.3, 29.3), (27.3, 28.6)),
                ("C", (23.8, 27.8), (19.6, 28.2), (16.0, 28.2)),
                ("C", (10.7, 28.2), (5.3, 27.9), (0, 27.9))]
# measured on the same view: the sticks, the D-pad, the face diamond, Guide, View / Menu, the profile switch
E1_LSTICK, E1_RSTICK, E1_DPAD, E1_FACE, E1_GUIDE = (-42.9, -23.1), (21.0, 0.8), (-22.9, 0.8), (42.5, -22.6), -41.8
# the paddles on the back as on the Series 2, the lower one longer (P2 / P4 reach down the grip)
E1_PADDLES = (("paddle.p1", "P1", [(19, -2.6), (30, -2.6), (41, -1.6)], (31, -2.4), "right"),
              ("paddle.p2", "P2", [(20, 5.2), (31, 5.6), (37, 12), (40.5, 26)], (36, 12), "right"),
              ("paddle.p3", "P3", [(-19, -2.6), (-30, -2.6), (-41, -1.6)], (-31, -2.4), "left"),
              ("paddle.p4", "P4", [(-20, 5.2), (-31, 5.6), (-37, 12), (-40.5, 26)], (-36, 12), "left"))


def clip_to_circle(points, cx, cy, r, sides=72):
    """@p points (a convex polygon) cut by the circle as a @p sides-gon (Sutherland-Hodgman; SVG Tiny has no clip)"""
    ring = [(cx + r * math.cos(2 * math.pi * i / sides), cy + r * math.sin(2 * math.pi * i / sides))
            for i in range(sides)]
    out = points
    for i in range(sides):
        (ax, ay), (bx, by) = ring[i], ring[(i + 1) % sides]
        inside = lambda p: (bx - ax) * (p[1] - ay) - (by - ay) * (p[0] - ax) >= 0  # noqa: E731
        src, out = out, []
        for j, p in enumerate(src):
            q = src[j - 1]
            if inside(p) != inside(q):
                dx, dy = p[0] - q[0], p[1] - q[1]
                t = ((bx - ax) * (ay - q[1]) - (by - ay) * (ax - q[0])) / ((bx - ax) * dy - (by - ay) * dx)
                out.append((q[0] + t * dx, q[1] + t * dy))
            if inside(p):
                out.append(p)
        if not out:
            break
    return out


def faceted_dpad(view, cx, cy, r, color, control):
    """The Elite's other D-pad: a metal disc cut into nine flat facets on a 3 x 3 grid, the middle one square and
    level, the outer ones tilted in towards it like a shallow dish; lit from the top, so the facets facing up (the
    lower row) are the brightest"""
    view.add("controls", f'<path d="{circle_d(cx, cy, r + 0.45)}" fill="{style.GAP}" fill-opacity="0.55"/>', control)
    view.add("controls", f'<path d="{circle_d(cx, cy, r)}" fill="{color}" stroke="{style.rim_of(color)}" '
                         f'stroke-width="0.2"/>', control)
    h, far = 0.34 * r, r + 1  # the middle facet's half side; the outer facets run past the rim and are cut there
    edges = (-far, -h, h, far)
    for j in (-1, 0, 1):
        for i in (-1, 0, 1):
            x0, x1, y0, y1 = cx + edges[i + 1], cx + edges[i + 2], cy + edges[j + 1], cy + edges[j + 2]
            part = clip_to_circle([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], cx, cy, r - 0.15)
            tone = 1 + 0.2 * j + 0.05 * i
            d = "M" + "L".join(f"{num(px)},{num(py)}" for px, py in part) + "Z"
            view.add("controls", f'<path d="{d}" fill="{shade(color, tone)}" stroke="{shade(color, 0.8)}" '
                                 f'stroke-width="0.1" stroke-opacity="0.6" stroke-linejoin="round"/>', control)


def elite1(pad="disc"):
    """@p pad: "disc" or "cross", the two D-pads the Elite ships with (a set each, the user picks one)"""
    name = "xbox-elite-1" if pad == "disc" else "xbox-elite-1-cross"
    pic = Picture(name, "Xbox Elite Wireless Controller (Series 1, model 1698), front and back"
                  + ("" if pad == "disc" else ", the cross D-pad"))
    pic.sources = [
        {"what": "the outline (trace.py, the half mirrored, overlap 0.990) and the control positions: a straight top "
                 "view of a model 1698 with the faceted D-pad, scaled to 153 mm across; 108 mm deep traced",
         "source": "the owner's reference photo (2026-10-31)", "use": "measured geometry only; no pixels shipped",
         "retrieved": "2026-10-31"},
        {"what": "front: the satin metal plate round Guide, the metal bumpers, the 1 / 2 profile switch under View / "
                 "Menu; no Share, no profile lights", "source": COMMONS + "Xbox_One_Elite_Controller_(front).jpg",
         "author": "Mliu92", "licence": "CC BY-SA 3.0", "use": "reference only, not traced", "retrieved": "2026-10-31"},
        {"what": "back: the four metal paddles P1-P4 (P1 / P2 on the right hand, upper / lower), the hair-trigger "
                 "locks beside the battery cover; no USB-C port (micro-USB on the top edge)",
         "source": COMMONS + "Xbox_One_Elite_Controller_(rear).jpg", "author": "Mliu92", "licence": "CC BY-SA 3.0",
         "use": "reference only, not traced", "retrieved": "2026-10-31",
         "note": "drawn as the pad turned over towards the viewer (triggers at the bottom), so the right hand's "
                 "paddles stay on the right; positions measured on the photo scaled to 153 mm, about 2 mm"}]
    colors = dict(COLORS_DARK, body="#2b2c2f", grip="#4a4b4e", trigger="#8c8f94", bumper="#a2a5aa",
                  plate="#a8abb0", dpad="#9a9da2", disc="#c4c7cb", collar="#8a8e94", paddle="#a0a3a8", small="#232426",
                  letter="#8e9196", guide="#26272a", button="#202123")
    front_scale, back_scale = 7.2, 4.0
    front = pic.view("front", oy=104 + 53.4 * front_scale, scale=front_scale)
    d = mirrored_d(ELITE1_SHELL)
    style.body(front, d, colors["body"])
    style.sheen(front, d)
    # the metal along the top: the bumpers round the corners and the plate round Guide, one satin piece each
    for sx, bump, trig, side in ((-1, "lb", "lt", "left"), (1, "rb", "rt", "right")):
        f = lambda x, y: _p(sx, x, y)  # noqa: E731
        trigger = (f"M{f(24.5, -50.3)}C{f(26.5, -51.4)} {f(28.5, -52.6)} {f(31.4, -53.0)}"
                   f"C{f(35.2, -53.3)} {f(39.8, -50.9)} {f(42.3, -49.4)}L{f(41, -48.4)}L{f(25.5, -48.6)}Z")
        bumper = (f"M{f(23.2, -48.4)}L{f(41.6, -49.0)}C{f(48.1, -47.1)} {f(48.8, -45.5)} {f(52.9, -41.5)}"
                  f"C{f(54.7, -39.6)} {f(56.5, -38.0)} {f(57.9, -35.9)}C{f(52, -40.2)} {f(45, -45.6)} "
                  f"{f(37.8, -46.3)}L{f(23.2, -46.3)}Z")
        style.shaped(front, trigger, shade(colors["trigger"], 0.92), trig, layer="shoulders")
        style.shaped(front, bumper, colors["bumper"], bump)
        x0, x1 = sorted((25 * sx, 42 * sx))
        front.spot(trig, trig.upper(), ("rect", x0, -53.4, x1 - x0, 3.2), side,
                   analog={"kind": "trigger", "fill": "down"})
        x0, x1 = sorted((23.2 * sx, 56 * sx))
        front.spot(bump, bump.upper(), ("rect", x0, -49.2, x1 - x0, 3.4), side)
    plate = (f"M{num(-22.9)},{num(-48.4)}L{num(22.9)},{num(-48.4)}L{num(16.6)},{num(-33.6)}"
             f"Q{num(15.9)},{num(-32.4)} {num(14.6)},{num(-32.4)}L{num(-14.6)},{num(-32.4)}"
             f"Q{num(-15.9)},{num(-32.4)} {num(-16.6)},{num(-33.6)}Z")
    style.shaped(front, plate, colors["plate"], None, layer="case")
    common.neutral_guide(front, 0, E1_GUIDE, 5.6, colors["guide"], colors["guide_ring"], "guide")
    front.spot("guide", "Guide", ("circle", 0, E1_GUIDE, 6.0), "right")
    # the sticks: concave caps in metal collars
    for name, label, (cx, cy), side in (("lstick", "LS", E1_LSTICK, "left"), ("rstick", "RS", E1_RSTICK, "right")):
        style.recess(front, cx, cy, 11.6, colors["body"], depth=0.55)
        front.add("controls", f'<path d="{circle_d(cx, cy, 10.8)}" fill="{colors["collar"]}" '
                              f'stroke="{shade(colors["collar"], 0.7)}" stroke-width="0.25"/>', f"{name}.x")
        style.stick(front, cx, cy, 9.4, colors["stick"], f"{name}.x")
        common.stick_spots(front, name, label, cx, cy, 10.0, 6.0, side)
    dpad(front, colors, "disc" if pad == "disc" else "elite", at=E1_DPAD, ticks=False)
    labels = {control: {"cap": colors["button"], "text": letter, "name": letter, "ink": colors["letter"]}
              for control, (letter, _) in LETTERS.items()}
    common.face_diamond(front, *E1_FACE, 10.8, 5.3, labels)
    for control, label, x, draw in (("view", "View", -12.1, common.view_glyph),
                                    ("menu", "Menu", 12.1, common.menu_glyph)):
        style.button(front, x, -22.8, 3.3, colors["small"], control)
        draw(front, x, -22.8, 3.8, colors["glyph"], control)
        front.spot(control, label, ("circle", x, -22.8, 3.8), "left" if x < 0 else "right")
    # the profile switch: a slider between 1 and 2, printed above it; a mode switch, the host never sees it
    front.add("case", f'<path d="{rounded_d(-4.8, -15.7, 9.6, 3.0, 1.5)}" fill="{shade(colors["body"], 0.5)}"/>')
    front.add("case", f'<path d="{rounded_d(-4.3, -15.25, 4.4, 2.1, 1.05)}" fill="{colors["small"]}" '
                      f'stroke="{style.rim_of(colors["small"])}" stroke-width="0.15"/>')
    for text, x in (("1", -3.4), ("2", 3.2)):
        style.label(front, text, x, -18.4, 1.7, colors["glyph"], None, opacity=0.7)

    # The back, the pad turned over towards the viewer: the triggers at the bottom, the right hand still right
    back = pic.view("back", oy=104 + (53.4 + 56.2) * front_scale + 30 + 56.2 * back_scale, scale=back_scale)
    d = mirrored_d(ELITE1_SHELL, mirror_y=True)
    style.body(back, d, shade(colors["body"], 0.96))
    for sx in (-1, 1):
        f = lambda x, y: _p(sx, x, y, flip=True)  # noqa: E731
        # the textured rubber grip over the handle, from the side above the stick down round the end
        grip = (f"M{f(63.6, -20.2)}C{f(70.6, -3.1)} {f(85.6, 38.1)} {f(68.8, 52.5)}C{f(67.1, 54)} {f(63.8, 56.2)} "
                f"{f(61.3, 56.2)}C{f(59.4, 56.2)} {f(58.1, 52.6)} {f(56.8, 50.7)}C{f(54.9, 47.9)} {f(49.8, 42)} "
                f"{f(47.3, 39.9)}C{f(44.6, 37)} {f(40.5, 32.4)} {f(36.3, 31)}C{f(36, 18)} {f(40, 2)} {f(46, -8)}"
                f"C{f(51, -15)} {f(57, -19)} {f(63.6, -20.2)}Z")
        style.body(back, grip, colors["grip"], width=0.3)
        trigger = (f"M{f(24.5, -50.3)}C{f(26.5, -51.4)} {f(28.5, -52.6)} {f(31.4, -53.0)}"
                   f"C{f(35.2, -53.3)} {f(39.8, -50.9)} {f(42.3, -49.4)}L{f(41, -48.4)}L{f(25.5, -48.6)}Z")
        style.shaped(back, trigger, colors["trigger"], None, layer="shoulders")
        # the hair-trigger lock: a slider beside the battery cover
        back.add("case", f'<path d="{rounded_d(sx * 30.6 - 1.6, 8, 3.2, 7, 1.6)}" '
                         f'fill="{shade(colors["body"], 0.55)}"/>')
        back.add("case", f'<path d="{rounded_d(sx * 30.6 - 1.1, 8.5, 2.2, 2.8, 1.1)}" fill="{colors["collar"]}"/>')
    back.add("case", f'<path d="{rounded_d(-25.7, 5.2, 51.4, 32.9, 2.4)}" fill="{shade(colors["body"], 0.9)}" '
                     f'stroke="{shade(colors["body"], 0.6)}" stroke-width="0.3"/>')
    style.sheen(back, d, cy=0.78)
    for control, name, points, centre, side in E1_PADDLES:
        paddle(back, [(x, -y) for x, y in points], colors["paddle"], control)
        back.spot(control, name, ("circle", centre[0], -centre[1], 7.2), side)
    return pic


def paddle(view, points, color, control):
    """A metal paddle: a blade of round ends along @p points, standing out of its dark gap"""
    d = "M" + " ".join(f"{num(x)},{num(y)}" for x, y in points[:1])
    if len(points) == 3:
        d += f"Q{num(points[1][0])},{num(points[1][1])} {num(points[2][0])},{num(points[2][1])}"
    else:
        d += (f"C{num(points[1][0])},{num(points[1][1])} {num(points[2][0])},{num(points[2][1])} "
              f"{num(points[3][0])},{num(points[3][1])}")
    view.add("controls", f'<path d="{d}" fill="none" stroke="{style.GAP}" stroke-opacity="0.55" stroke-width="7.6" '
                         f'stroke-linecap="round" stroke-linejoin="round"/>', control)
    view.add("controls", f'<path d="{d}" fill="none" stroke="{shade(color, 0.72)}" stroke-width="6.8" '
                         f'stroke-linecap="round" stroke-linejoin="round"/>', control)
    view.add("controls", f'<path d="{d}" fill="none" stroke="{color}" stroke-width="6.3" '
                         f'stroke-linecap="round" stroke-linejoin="round"/>', control)
    view.add("controls", f'<path d="{d}" fill="none" stroke="{shade(color, 1.1)}" stroke-width="2.2" '
                         f'stroke-opacity="0.5" stroke-linecap="round" stroke-linejoin="round"/>', control)
