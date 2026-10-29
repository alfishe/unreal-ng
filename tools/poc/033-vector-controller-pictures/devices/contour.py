"""Contour Design ShuttlePRO v2: a jog / shuttle debug remote (control surfaces devices.md §4), not a pad.

Size 109 x 196 x 29 mm (retail listings: B&H 4.3 x 7.7 in, Multitronic, Managerit). The outline is measured with
trace.py on Contour's own front view (near-orthographic, transparent background; the left half mirrored about the
symmetry axis, scaled to 109 mm wide: it comes out at 200 mm deep against the listed 196, kept as traced); the
controls are read on the same view on a 5 mm grid (about 1 mm). The rows, as agraef/ShuttlePRO's example.shuttlerc
draws them: K1-K4, K5-K9 on two arcs over the ring, K14 and K15 beside it, K10 / K11 and K12 / K13 as two pairs of
long bars below. The nine top keys have clear caps over grey, the four bars are silver, K14 / K15 black rubber. The
ring is the spring-loaded shuttle (rubber, ribbed) around the jog wheel (a light disc with three finger dimples).
The wordmark is left out.

The picture is taller than the pads, so it is drawn at its own scale (6.6 px per mm) to fit the canvas.
"""

import math

import style
from model import H, Picture
from svgkit import circle_d, num, polygon_d, shade

RETRIEVED = "2026-10-28"
SCALE = 6.6  # 200 mm deep in the 1344 px between the top and bottom safe areas

# The right half, from the top centre clockwise (trace.py --width 109 on Contour's front view)
SHELL = [("M", (0.0, -100.0)),
         ("C", (9.6, -100.0), (19.4, -97.0), (27.5, -91.9)),
         ("C", (49.5, -78.2), (53.8, -55.3), (54.4, -31.2)),
         ("C", (54.6, -21.3), (54.3, -11.1), (52.8, -1.3)),
         ("C", (51.2, 9.8), (46.2, 20.0), (44.2, 31.0)),
         ("C", (41.1, 47.5), (43.0, 65.0), (34.9, 80.2)),
         ("C", (30.2, 88.9), (22.4, 95.6), (12.9, 98.4)),
         ("C", (10.3, 99.2), (7.6, 99.8), (4.8, 100.0)),
         ("C", (3.8, 100.1), (1.0, 100.2), (0.0, 100.2))]

RING = (0.0, -22.5)  # the centre of the shuttle ring and the jog wheel
RING_WELL, RING_OUTER, RING_INNER, JOG = 31.5, 29.5, 21.8, 20.6
DIMPLES = [(11.5, -164), (11.5, -50), (11.5, 92)]  # radius, angle (degrees, y down) from the ring's centre
DIMPLE_R = 5.4

KEY_W, KEY_H = 13.6, 9.6  # the clear-capped keys
# control, label, centre, callout side
KEYS = [("button.1", "K1", (-27.3, -72.0), "left"), ("button.2", "K2", (-9.5, -78.4), "left"),
        ("button.3", "K3", (9.5, -78.4), "right"), ("button.4", "K4", (27.3, -72.0), "right"),
        ("button.5", "K5", (-34.6, -49.6), "left"), ("button.6", "K6", (-19.8, -58.6), "left"),
        ("button.7", "K7", (0.0, -62.8), "right"), ("button.8", "K8", (19.8, -58.6), "right"),
        ("button.9", "K9", (34.6, -49.6), "right")]
SIDE_W, SIDE_H, SIDE_X = 7.2, 24.0, 44.0  # K14 / K15 beside the ring
BAR_W, BAR_H = 24.5, 7.0  # the silver bars: control, label, centre, angle (degrees), side
BARS = [("button.10", "K10", (-26.0, 4.2), 41, "left"), ("button.11", "K11", (26.0, 4.2), -41, "right"),
        ("button.12", "K12", (-30.4, 17.2), 37, "left"), ("button.13", "K13", (30.4, 17.2), -37, "right")]


def capsule_points(cx, cy, w, h, angle, steps=10):
    """A capsule (a rounded rectangle whose ends are half circles) of @p w x @p h centred on (cx, cy), turned by
    @p angle degrees (clockwise, y down), as a polygon"""
    r = h / 2
    a = math.radians(angle)
    ca, sa = math.cos(a), math.sin(a)
    pts = []
    for end, start in ((w / 2 - r, -90), (-(w / 2 - r), 90)):
        for i in range(steps + 1):
            t = math.radians(start + 180 * i / steps)
            x, y = end + r * math.cos(t), r * math.sin(t)
            pts.append((cx + x * ca - y * sa, cy + x * sa + y * ca))
    return pts


def pebble_points(cx, cy, w, h, angle, steps=24):
    """A pebble key (a superellipse, squarer than an ellipse) turned by @p angle degrees"""
    a = math.radians(angle)
    ca, sa = math.cos(a), math.sin(a)
    pts = []
    for i in range(steps * 4):
        t = 2 * math.pi * i / (steps * 4)
        c, s = math.cos(t), math.sin(t)
        x = w / 2 * math.copysign(abs(c) ** 0.6, c)
        y = h / 2 * math.copysign(abs(s) ** 0.6, s)
        pts.append((cx + x * ca - y * sa, cy + x * sa + y * ca))
    return pts


def arc_points(cx, cy, r, a0, a1, steps=24):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / steps)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / steps))) for i in range(steps + 1)]


def key_angle(x, y):
    """The keys follow their arcs about the ring, three quarters of the tangent's turn (as the photo shows)"""
    return 0.75 * math.degrees(math.atan2(x - RING[0], RING[1] - y))


def shuttle_front(view, colors):
    from svgkit import mirrored_d
    shell = mirrored_d(SHELL)
    style.body(view, shell, colors["body"])
    style.sheen(view, shell, cy=0.18, r=0.6)

    # The ring: its well in the shell, the ribbed rubber shuttle, the jog disc with its dimples
    cx, cy = RING
    style.recess(view, cx, cy, RING_WELL, colors["body"], depth=0.55)
    rubber = view.defs.linear([(0, shade(colors["rubber"], 1.05)), (1, shade(colors["rubber"], 0.95))])
    ring_d = circle_d(cx, cy, RING_OUTER) + circle_d(cx, cy, RING_INNER)
    for side, a0, a1 in (("shuttle.left", 90, 270), ("shuttle.right", -90, 90)):
        half = arc_points(cx, cy, RING_OUTER, a0, a1) + arc_points(cx, cy, RING_INNER, a1, a0)
        view.add("controls", f'<path d="{polygon_d(half)}" fill="{rubber}"/>', side)
        for i in range(18):  # the ribs
            t = math.radians(a0 + (a1 - a0) * (i + 0.5) / 18)
            x0, y0 = cx + (RING_INNER + 1.2) * math.cos(t), cy + (RING_INNER + 1.2) * math.sin(t)
            x1, y1 = cx + (RING_OUTER - 1.0) * math.cos(t), cy + (RING_OUTER - 1.0) * math.sin(t)
            style.line(view, f"M{num(x0)},{num(y0)}L{num(x1)},{num(y1)}", shade(colors["rubber"], 0.6), 0.55,
                       side, layer="controls")
        view.spot(side, "Shuttle " + ("left" if side.endswith("left") else "right"), ("polygon", arc_points(cx, cy, RING_OUTER, a0, a1, 12) + arc_points(cx, cy, RING_INNER, a1, a0, 8)),
                  "left" if side.endswith("left") else "right",
                  anchor=(cx + (-1 if side.endswith("left") else 1) * (RING_OUTER - 3.5), cy + 9))
    view.add("controls", f'<path d="{ring_d}" fill="none" stroke="{style.rim_of(colors["rubber"])}" '
                         f'stroke-width="0.25"/>', "shuttle.right")
    view.add("controls", f'<path d="{circle_d(cx, cy, JOG + 0.6)}" fill="{style.GAP}" fill-opacity="0.6"/>', "jog")
    disc = view.defs.linear([(0, shade(colors["jog"], 1.04)), (1, shade(colors["jog"], 0.94))])
    view.add("controls", f'<path d="{circle_d(cx, cy, JOG)}" fill="{disc}" stroke="{style.rim_of(colors["jog"])}" '
                         f'stroke-width="0.25"/>', "jog")
    for r, a in DIMPLES:
        x, y = cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))
        style.button(view, x, y, DIMPLE_R, colors["dimple"], "jog", gap=0.35)
        dish = view.defs.radial([(0, shade(colors["dimple"], 1.12)), (0.7, colors["dimple"]),
                                 (1, shade(colors["dimple"], 0.85))], cx=0.42, cy=0.38, r=0.6)
        view.add("controls", f'<path d="{circle_d(x, y, DIMPLE_R * 0.82)}" fill="{dish}"/>', "jog")
    view.spot("jog", "Jog", ("circle", cx, cy, JOG), "right", anchor=(cx + 7, cy - 12),
              analog={"kind": "encoder", "steps": 10, "radius": JOG - 4})

    # The clear-capped keys: a grey key under a lighter cap
    for control, label, (x, y), side in KEYS:
        angle = key_angle(x, y)
        style.shaped(view, polygon_d(pebble_points(x, y, KEY_W, KEY_H, angle)), colors["key"], control,
                     polygon_d(pebble_points(x, y, KEY_W + 1.0, KEY_H + 1.0, angle)))
        cap = view.defs.radial([(0, "#ffffff", 0.32), (1, "#ffffff", 0.0)], cx=0.45, cy=0.35, r=0.55)
        view.add("controls", f'<path d="{polygon_d(pebble_points(x, y - 0.6, KEY_W - 3.0, KEY_H - 3.4, angle))}" '
                             f'fill="{cap}"/>', control)
        edge = (KEY_W / 2 - 1.5) * (-1 if side == "left" else 1) if x != 0 else 0  # the callout's end: the outer side
        view.spot(control, label, ("polygon", pebble_points(x, y, KEY_W + 0.6, KEY_H + 0.6, angle, steps=4)), side,
                  anchor=(x + edge * math.cos(math.radians(angle)), y + edge * math.sin(math.radians(angle))))

    # K14 / K15: black rubber beside the ring
    for control, label, x, side in (("button.14", "K14", -SIDE_X, "left"), ("button.15", "K15", SIDE_X, "right")):
        style.shaped(view, polygon_d(capsule_points(x, RING[1], SIDE_H, SIDE_W, 90)), colors["side"], control,
                     polygon_d(capsule_points(x, RING[1], SIDE_H + 0.9, SIDE_W + 0.9, 90)))
        view.spot(control, label, ("polygon", capsule_points(x, RING[1], SIDE_H + 0.6, SIDE_W + 0.6, 90, steps=3)),
                  side)

    # K10-K13: the silver bars
    for control, label, (x, y), angle, side in BARS:
        style.shaped(view, polygon_d(capsule_points(x, y, BAR_W, BAR_H, angle)), colors["bar"], control,
                     polygon_d(capsule_points(x, y, BAR_W + 0.9, BAR_H + 0.9, angle)))
        edge = (BAR_W / 2 - 2.5) * (-1 if side == "left" else 1)
        view.spot(control, label, ("polygon", capsule_points(x, y, BAR_W + 0.6, BAR_H + 0.6, angle, steps=3)), side,
                  anchor=(x + edge * math.cos(math.radians(angle)), y + edge * math.sin(math.radians(angle))))


def shuttle_pro_v2():
    pic = Picture("shuttle-pro-v2", "Contour ShuttlePRO v2, black")
    pic.sources = [
        {"what": "size 109 x 196 x 29 mm (retail listings)",
         "source": "https://www.bhphotovideo.com/c/product/298670-REG/contour_design_00498_0_shuttlepro_v2_nle.html "
                   "(and Multitronic, Managerit listings)", "retrieved": RETRIEVED},
        {"what": "Contour's front view of the ShuttlePRO v2: the outline (trace.py, the left half mirrored), the "
                 "positions and sizes of the keys, the ring and the jog wheel on a 5 mm grid",
         "source": "https://www.contourdesign.com/product/shuttlepro-v2/ (product photo "
                   "ShuttleProV2FRONT_Originalratio_1920x2880)", "author": "Contour Design",
         "use": "measured geometry only (trace.py outline, positions on a 5 mm grid); no pixels shipped",
         "retrieved": RETRIEVED},
        {"what": "the key numbering K1-K15 by rows", "source": "https://github.com/agraef/ShuttlePRO "
                 "(example.shuttlerc)", "retrieved": RETRIEVED},
    ]
    colors = {"body": "#2c2d30", "rubber": "#1e1f21", "jog": "#d9d9d6", "dimple": "#a9abaf", "key": "#b9bcc0",
              "side": "#26272a", "bar": "#b4b6b9"}
    view = pic.view("front", oy=H / 2, scale=SCALE)
    shuttle_front(view, colors)
    return pic


SETS = {
    "shuttle-pro-v2": shuttle_pro_v2,
}
