"""8BitDo pads: Ultimate (2.4G / Bluetooth), Pro 2, SN30 Pro+, SN30 Pro / SF30 Pro, SN30 / SF30, M30, Zero 2.

Sizes are 8BitDo's (product pages, "Dimension / Weight", retrieved 2026-10-23): Ultimate Bluetooth 147 x 104 x
61.5 mm, Pro 2 153.6 x 100.6 x 64.5 mm, SN30 Pro 144 x 63.5 x 33 mm, SN30 2.4G 144 x 63.5 x 26.2 mm, M30 147 x 74 x
31 mm, Zero 2 73 x 36.5 x 14.7 mm. Layouts are measured by eye on openly licensed photos and 8BitDo's own product
shots (references only, nothing traced; positions to about 2 mm). 8BitDo prints Nintendo-positional letters (X top,
A right, B bottom, Y left); the hotspots keep the canonical positional names (north, east, south, west), the legends
show what is printed. The wordmark and the 8BitDo logo on the Home button are left out (D5).
"""

import math

import style
from devices import common, xbox
from model import Picture
from svgkit import circle_d, mirrored_d, num, polygon_d, rounded_d, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
SITE = "https://www.8bitdo.com/"
POSITIONAL = {"north": "X", "east": "A", "south": "B", "west": "Y"}


def size(what, page):
    return {"what": what, "source": SITE + page, "author": "8BitDo", "retrieved": "2026-10-23"}


def photo(what, file, author, licence):
    return {"what": what, "source": COMMONS + file, "author": author, "licence": licence,
            "use": "reference only, not traced", "retrieved": "2026-10-23"}


def press(what, path):
    return {"what": what, "source": SITE + "images/" + path, "author": "8BitDo", "terms": "product photo",
            "use": "reference only, not traced, not shipped", "retrieved": "2026-10-23"}


def scaled(segments, sx, sy):
    return [(cmd, *[(x * sx, y * sy) for x, y in pts]) for cmd, *pts in segments]


def stadium_d(w, h, cy=0.0):
    return rounded_d(-w / 2, cy - h / 2, w, h, h / 2)


def top_shoulders(view, colors, names, top, x0, x1, triggers=True, ids=("lb", "rb", "lt", "rt"), rise=1.0):
    """Bumpers (and triggers behind them) along a flat top edge from x0 to x1 on each side; @p ids the controls the
    bumpers and triggers are (the M30's only shoulders are its L / R triggers)"""
    lb, rb, lt, rt = names
    for sx, bump, trig, side, bname, tname in ((-1, ids[0], ids[2], "left", lb, lt),
                                               (1, ids[1], ids[3], "right", rb, rt)):
        f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
        w = x1 - x0
        if triggers:
            trigger = (f"M{f(x0 + w * 0.18, top - 1.5)}C{f(x0 + w * 0.26, top - 8)} {f(x0 + w * 0.5, top - 9.4)} "
                       f"{f(x0 + w * 0.66, top - 9.2)}C{f(x0 + w * 0.82, top - 9)} {f(x0 + w * 0.94, top - 6.4)} "
                       f"{f(x0 + w * 0.97, top - 1.5)}Z")
            style.shaped(view, trigger, colors["trigger"], trig, layer="shoulders")
            a, b = sorted(((x0 + w * 0.22) * sx, (x0 + w * 0.95) * sx))
            view.spot(trig, tname, ("rect", a, top - 9.6, b - a, 5.6), side, analog={"kind": "trigger", "fill": "down"})
        bumper = (f"M{f(x0, top + 0.4)}C{f(x0 + w * 0.15, top - 4.6 * rise)} {f(x0 + w * 0.5, top - 5.4 * rise)} "
                  f"{f(x0 + w * 0.72, top - 5 * rise)}C{f(x0 + w * 0.9, top - 4.6 * rise)} {f(x1, top - 2 * rise)} "
                  f"{f(x1 + 1.2 * rise, top + 3 * rise)}L{f(x1 - 6 * rise, top + 4 * rise)}Z")
        style.shaped(view, bumper, colors["bumper"], bump, layer="shoulders")
        a, b = sorted(((x0 + 1) * sx, (x1 + 0.5) * sx))
        view.spot(bump, bname, ("rect", a, top - 5.6 * rise, b - a, 4.8 * rise), side)


def dpad(view, cx, cy, span, arm, colors, well=None, dimple=False):
    if well:
        style.recess(view, cx, cy, well, colors["plate"], depth=0.8)
    style.shaped(view, style.cross_d(cx, cy, span, arm, r=1.0), colors["dpad"], "dpad",
                 style.cross_d(cx, cy, span + 0.9, arm + 0.9, r=1.3))
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.glyph(view, style.arrow_d(cx + dx * span * 0.33, cy + dy * span * 0.33, arm * 0.24, direction),
                    colors["arrow"], "dpad", fill=True, opacity=0.8)
    if dimple:
        dish = view.defs.radial([(0, shade(colors["dpad"], 0.8)), (1, shade(colors["dpad"], 1.06))], r=0.5)
        view.add("controls", f'<path d="{circle_d(cx, cy, arm * 0.28)}" fill="{dish}"/>', "dpad")
    common.dpad_spots(view, cx, cy, span, arm, "left", prefix="dpad.")


def face(view, cx, cy, offset, r, caps, inks, letters=POSITIONAL):
    labels = {c: {"cap": caps[c] if isinstance(caps, dict) else caps, "text": t, "name": t,
                  "ink": inks[c] if isinstance(inks, dict) else inks} for c, t in letters.items()}
    common.face_diamond(view, cx, cy, offset, r, labels)


def sticks(view, positions, r, colors, well):
    for name, label, (cx, cy), side in (("lstick", "LS", positions[0], "left"), ("rstick", "RS", positions[1], "right")):
        style.recess(view, cx, cy, well, colors["plate"], depth=0.62)
        style.stick(view, cx, cy, r, colors["stick"], f"{name}.x", skirt=r + 1.6)
        common.stick_spots(view, name, label, cx, cy, r + 0.4, 6.0, side)


def star_d(cx, cy, r):
    pts = []
    for i in range(10):
        a = math.radians(-90 + i * 36)
        rr = r if i % 2 == 0 else r * 0.45
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return polygon_d(pts)


def star_button(view, cx, cy, r, colors, control, label):
    style.button(view, cx, cy, r, colors["small"], control)
    style.glyph(view, star_d(cx, cy + 0.1, r * 0.55), colors["glyph"], control, width=0.3)
    view.spot(control, label, ("circle", cx, cy, r + 0.4), "left" if cx < 0 else "right")


def home_button(view, cx, cy, r, colors, side="right"):
    common.neutral_guide(view, cx, cy, r, colors["small"], colors["glyph"], "guide")
    view.spot("guide", "Home", ("circle", cx, cy, r + 0.4), side)


def system_pair(view, y, x, w, h, colors, labels=("-", "+"), angle=0.0, tray=None):
    if tray:
        view.add("case", f'<path d="{rounded_d(-tray[0] / 2, y - tray[1] / 2, tray[0], tray[1], tray[1] / 2)}" '
                         f'fill="{shade(colors["plate"], 0.86)}" stroke="{shade(colors["plate"], 0.7)}" '
                         f'stroke-width="0.25"/>')
    for control, label, px in (("view", labels[0], -x), ("menu", labels[1], x)):
        a = -angle
        if angle:
            c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
            corners = [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)]
            view.add("controls", f'<g transform="rotate({num(a)} {num(px)} {num(y)})">', control)
            style.pill(view, px, y, w, h, colors["small"], control)
            view.add("controls", "</g>", control)
            pts = [(px + x0 * c - y0 * s, y + x0 * s + y0 * c) for x0, y0 in corners]
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            view.spot(control, label, ("rect", min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)),
                      "left" if px < 0 else "right")
        else:
            style.pill(view, px, y, w, h, colors["small"], control)
            view.spot(control, label, ("rect", px - w / 2 - 0.4, y - h / 2 - 0.4, w + 0.8, h + 0.8),
                      "left" if px < 0 else "right")


def back_pair(view, y, colors, ink):
    """The two back buttons as a strip under the pad (an inset): left hand's PL as paddle.p3, right hand's PR as
    paddle.p1 (the project's P1 / P2 right, P3 / P4 left convention)"""
    panel = rounded_d(-36, y - 6.2, 72, 12.4, 6.2)
    style.body(view, panel, shade(colors["body"], 0.92), width=0.3)
    for control, name, x, side in (("paddle.p3", "PL", -17, "left"), ("paddle.p1", "PR", 17, "right")):
        style.pill(view, x, y, 16, 6.0, colors["button"] if "button" in colors else colors["small"], control)
        style.label(view, name, x, y, 2.9, ink, control, opacity=0.85)
        view.spot(control, name, ("rect", x - 8.4, y - 3.4, 16.8, 6.8), side)


# -- Ultimate (2.4G / Bluetooth) ---------------------------------------------------------------------------------

def ultimate():
    pic = Picture("8bitdo-ultimate", "8BitDo Ultimate Bluetooth / 2.4G controller, black")
    pic.sources = [
        size("size 147 x 104 x 61.5 mm", "ultimate-bluetooth-controller/"),
        press("layout and letters (mapping view), the back buttons", "2022/ultimate-bluetooth-controller/mapping.png"),
        press("the pad from above", "2022/ultimate-bluetooth-controller/01-l.jpg"),
        photo("the shell of the family (Ultimate 2C, same body)", "8BitDo_Ultimate_2C_gamepad_HS_DSC_2699.jpg",
              "Hayden Schiff", "CC BY 4.0"),
        {"what": "outline: the Xbox Series shell of this POC scaled to 147 x 104 mm (the Ultimate follows that "
                 "shape); an estimate", "source": "devices/xbox.py", "retrieved": "2026-10-23"}]
    colors = {"body": "#3c3e42", "plate": "#3c3e42", "trigger": "#333538", "bumper": "#383a3e", "button": "#232427",
              "small": "#28292c", "stick": "#2a2b2e", "dpad": "#26272a", "arrow": "#8f9297", "glyph": "#a9acb1"}
    sx, sy = 147 / 157.2, 104 / 105.0
    view = pic.view("front", oy=768 - 8 * 8.6)
    top = xbox.TOP * sy
    top_shoulders(view, colors, ("LB", "RB", "LT", "RT"), top + 1.4, 24, 66)
    body = mirrored_d(scaled(xbox.SHELL, sx, sy))
    style.body(view, body, colors["body"])
    style.sheen(view, body)
    sticks(view, ((-40, -21.4), (20.6, 3.6)), 8.8, colors, 11.8)
    dpad(view, -20.6, 3.6, 23, 7.8, colors, well=13.4)
    face(view, 39.6, -21.6, 9.6, 4.9, colors["button"], "#d4d6da")
    home_button(view, 0, -36.4, 4.4, colors)
    for i in range(4):
        view.add("case", f'<path d="{circle_d(-3.3 + i * 2.2, -29.6, 0.45)}" fill="{shade(colors["body"], 0.55)}"/>')
    system_pair(view, -24.6, 11.4, 3.4, 3.4, colors)
    star_button(view, 0, -13.4, 2.6, colors, "profile", "Star")
    back_pair(view, 66, colors, "#d4d6da")
    return pic


# -- Pro 2 and SN30 Pro+ ------------------------------------------------------------------------------------------

PRO_PLATE_W, PRO_PLATE_H = 150.0, 60.0


def pro_grips_d(sx):
    f = lambda x, y: f"{num(x * sx)},{num(y)}"  # noqa: E731
    return (f"M{f(48, 14)}C{f(60, 14)} {f(74, 12)} {f(75.4, 18)}C{f(76.6, 34)} {f(73.6, 52)} {f(68, 62)}"
            f"C{f(63.6, 70)} {f(52, 71)} {f(47.6, 63)}C{f(44.4, 57)} {f(43, 46)} {f(39, 36)}"
            f"C{f(36, 31)} {f(30, 29.4)} {f(22, 29.4)}L{f(22, 14)}Z")


def pro_family(family, title, colors, caps, inks, paddles, sources):
    pic = Picture(family, title)
    pic.sources = sources
    view = pic.view("front", oy=768 - (11 if paddles else 5) * 8.6)
    top = -PRO_PLATE_H / 2
    top_shoulders(view, colors, ("L", "R", "L2", "R2"), top + 0.6, 30, 70)
    for sx in (-1, 1):  # the darker body with the grips under the plate
        style.body(view, pro_grips_d(sx), colors["body"], width=0.3)
    style.body(view, rounded_d(-40, 10, 80, 21.6, 9), colors["body"], width=0.3)
    plate = stadium_d(PRO_PLATE_W, PRO_PLATE_H)
    style.body(view, plate, colors["plate"])
    style.sheen(view, plate)
    for x in (-24.6, 24.6):  # the stick bulges at the plate's lower edge
        bulge = circle_d(x, 19.4, 15.2)
        style.body(view, bulge, colors["plate"], width=0.25)
    dpad(view, -46.5, -5.6, 25.4, 8.4, colors, dimple=True)
    face(view, 45, -5.6, 11.4, 5.5, caps, inks)
    sticks(view, ((-24.6, 19.4), (24.6, 19.4)), 9.6, colors, 12.6)
    system_pair(view, -5.8, 7.6, 11.4, 3.8, colors, tray=(31, 6.6))
    for i in range(3):
        view.add("case", f'<path d="{circle_d(-2.4 + i * 2.4, 12.4, 0.45)}" fill="{shade(colors["plate"], 0.5)}"/>')
    style.pill(view, 0, 18.6, 7.0, 3.8, colors["pill"], "profile")
    view.spot("profile", "Profile", ("rect", -3.9, 16.3, 7.8, 4.6), "right")
    star_button(view, -47, 14.6, 3.6, colors, "share", "Star")
    home_button(view, 47, 14.6, 3.8, colors)
    if paddles:
        back_pair(view, 80, colors, "#d4d6da")
    return pic


def pro2():
    colors = {"plate": "#a6a7a9", "body": "#7b7c7f", "trigger": "#6f7073", "bumper": "#97989a", "button": "#252629",
              "small": "#97989b", "pill": "#9a9b9e", "stick": "#2d2e31", "dpad": "#2a2b2e", "arrow": "#6c6f74",
              "glyph": "#5d6065"}
    inks = {"north": "#d495b6", "west": "#d6d8db", "east": "#82c2b4", "south": "#d48a9c"}
    return pro_family("8bitdo-pro-2", "8BitDo Pro 2, Gray Edition", colors, colors["button"], inks, True, [
        size("size 153.6 x 100.6 x 64.5 mm", "pro2/"),
        photo("layout, sizes and colours (measured by eye, near-front view)", "8BitDo_Pro_2_Bluetooth_gamepad.jpg",
              "Kyu3a", "CC BY-SA 4.0"),
        press("the letters and the back buttons (mapping view)", "2021/pro2/mapping.png")])


def sn30proplus():
    colors = {"plate": "#bdbdbf", "body": "#a3a3a6", "trigger": "#98989b", "bumper": "#b0b0b3", "button": "#8b84bf",
              "small": "#a9a9ac", "pill": "#acacaf", "stick": "#2d2e31", "dpad": "#2a2b2e", "arrow": "#6c6f74",
              "glyph": "#6d7075"}
    caps = {"north": "#aca6d4", "west": "#aca6d4", "east": "#6d63b0", "south": "#6d63b0"}
    return pro_family("8bitdo-sn30-pro-plus", "8BitDo SN30 Pro+, SN Edition", colors, caps, "#ecebf2", False, [
        {"what": "size about 152 x 100 x 63 mm: the shape of the Pro 2 (its successor), without the back buttons; "
                 "an estimate (no 8BitDo page left)", "source": SITE + "pro2/", "retrieved": "2026-10-23"},
        photo("the SN Edition colours (grey body, lavender and purple buttons)", "8BitDo_SN30_gamepad_HS_DSC_2704.jpg",
              "Hayden Schiff", "CC BY 4.0")])


# -- SN30 Pro / SF30 Pro, SN30 / SF30 --------------------------------------------------------------------------------

SN_W, SN_H = 144.0, 63.5
SN_COLOURS = {"plate": "#bebec0", "body": "#bebec0", "trigger": "#a6a6a9", "bumper": "#b3b3b6", "small": "#5a5b5f",
              "stick": "#2b2c2f", "dpad": "#2a2b2e", "arrow": "#6c6f74", "glyph": "#c6c8cc", "dish": "#a9a8b2"}
SN_CAPS = {"north": "#aaa4d2", "west": "#aaa4d2", "east": "#6a60ae", "south": "#6a60ae"}
SF_CAPS = {"north": "#4e68a8", "west": "#4f8d5c", "east": "#b14a42", "south": "#c4a23e"}  # the SFC colours, muted


def sn_body(view, colors, sticks_on):
    top = -SN_H / 2
    top_shoulders(view, colors, ("L", "R", "L2", "R2"), top + 0.6, 30, 64, triggers=sticks_on)
    body = stadium_d(SN_W, SN_H)
    style.body(view, body, colors["plate"])
    style.sheen(view, body)
    # the SNES's right lobe: a darker concave dish holding the four buttons
    view.add("case", f'<path d="{circle_d(46, -4, 19)}" fill="{colors["dish"]}" stroke="{shade(colors["dish"], 0.8)}" '
                     f'stroke-width="0.25"/>')
    style.recess(view, -46, -4, 15.4, colors["plate"], depth=0.9)
    return top


def sn_family(family, title, caps, sticks_on, sources):
    colors = dict(SN_COLOURS)
    pic = Picture(family, title)
    pic.sources = sources
    view = pic.view("front", oy=768 - 4 * 8.6)
    if sticks_on:
        for x in (-18.6, 18.6):  # the stick housings stand out of the lower edge
            style.body(view, circle_d(x, 18.4, 14.6), colors["plate"], width=0.25)
    sn_body(view, colors, sticks_on)
    if sticks_on:
        for x in (-18.6, 18.6):
            style.body(view, circle_d(x, 18.4, 14.6), colors["plate"], rim=False)
    dpad(view, -46, -4, 23, 7.8, colors)
    face(view, 46, -4, 9.6, 5.0, caps, {c: "#f1f0f6" if c in ("north", "west") else "#ecebf2" for c in caps})
    system_pair(view, -1.0 if sticks_on else 2.0, 8.6, 8.6, 3.0, colors, labels=("Select", "Start"), angle=25)
    if sticks_on:
        sticks(view, ((-18.6, 18.4), (18.6, 18.4)), 8.4, colors, 10.8)
        star_button(view, -33.6, 21.6, 2.6, colors, "share", "Star")
        home_button(view, 33.6, 21.6, 2.8, colors)
        for i in range(4):
            view.add("case", f'<path d="{circle_d(-3.3 + i * 2.2, 27.4, 0.45)}" fill="{shade(colors["plate"], 0.5)}"/>')
    else:
        home_button(view, 0, 14.6, 2.6, colors)
    return pic


def sn30pro(colourway="sn"):
    if colourway == "sn":
        return sn_family("8bitdo-sn30-pro", "8BitDo SN30 Pro, SN Edition", SN_CAPS, True, [
            size("size 144 x 63.5 x 33 mm", "sn30-pro-g-classic-or-sn30-pro-sn"),
            photo("layout, colours (three-quarter views corrected to a front view by eye)",
                  "8BitDo_SN30_gamepad_HS_DSC_2704.jpg", "Hayden Schiff", "CC BY 4.0"),
            photo("the same pad, the other side", "8BitDo_SN30_gamepad_HS_DSC_2705.jpg", "Hayden Schiff",
                  "CC BY 4.0")])
    return sn_family("8bitdo-sf30-pro", "8BitDo SF30 Pro (Super Famicom colours)", SF_CAPS, True, [
        size("size 144 x 63.5 x 33 mm (the SN30 Pro's body)", "sn30-pro-g-classic-or-sn30-pro-sn"),
        photo("layout (the SN30 Pro, same body)", "8BitDo_SN30_gamepad_HS_DSC_2704.jpg", "Hayden Schiff",
              "CC BY 4.0"),
        {"what": "button colours: the Super Famicom's red / yellow / blue / green, muted", "source": "the SF30 Pro "
         "retail colourway; an estimate of the shades", "retrieved": "2026-10-23"}])


def sn30(colourway="sn"):
    common_sources = [
        size("size 144 x 63.5 x 26.2 mm", "sn30-wireless-for-original-snes-sfc"),
        press("the pad from above", "2020/sn30-2.4g/01-l.jpg"),
        photo("the SN body and colours (the SN30 Pro, same plate)", "8BitDo_SN30_gamepad_HS_DSC_2704.jpg",
              "Hayden Schiff", "CC BY 4.0")]
    if colourway == "sn":
        return sn_family("8bitdo-sn30", "8BitDo SN30, SN Edition", SN_CAPS, False, common_sources)
    return sn_family("8bitdo-sf30", "8BitDo SF30 (Super Famicom colours)", SF_CAPS, False, common_sources)


# -- M30 ---------------------------------------------------------------------------------------------------------

M30_HALF = [("M", (0, -30)),
            ("C", (20, -30), (44, -34.4), (58, -33.6)),
            ("C", (68.6, -33), (73.6, -24), (73.5, -9)),
            ("C", (73.4, 8), (70, 26), (60, 34)),
            ("C", (52, 40.6), (40, 39), (32, 33)),
            ("C", (24, 27.6), (14, 25.4), (0, 25.4))]


def m30():
    pic = Picture("8bitdo-m30", "8BitDo M30 (2.4G / Bluetooth), black")
    pic.sources = [
        size("size 147 x 74 x 31 mm", "m30-2-4g/"),
        photo("layout, the six buttons in two slanted rows, the round D-pad", "8BitDo_M30.png", "Llapissera",
              "CC BY-SA 3.0"),
        press("the pad from above", "2023/m30-bluetooth/m30-l.jpg"),
        {"what": "the six face buttons on the canonical names as SDL_GameControllerDB maps the M30 (A south, B east, "
                 "C right shoulder, X west, Y north, Z left shoulder; L / R the triggers); an assumption to check on "
                 "a real pad", "source": "SDL_GameControllerDB (gamecontrollerdb.txt, 8BitDo M30)",
         "retrieved": "2026-10-23"}]
    colors = {"body": "#3a3c40", "plate": "#3a3c40", "trigger": "#313236", "bumper": "#36383c", "button": "#26272a",
              "small": "#2b2c2f", "dpad": "#2a2b2e", "arrow": "#8c8f94", "glyph": "#a9acb1", "accent": "#5a74b8"}
    view = pic.view("front", oy=768 - 2 * 8.6)
    top_shoulders(view, colors, ("L", "R", "", ""), -31.4, 34, 66, triggers=False, ids=("lt", "rt", "", ""))
    body = mirrored_d(M30_HALF)
    style.body(view, body, colors["body"])
    style.sheen(view, body)
    # the round D-pad: a dished disc with the cross moulded in
    cx, cy = -44, -3
    style.recess(view, cx, cy, 17.2, colors["body"], depth=0.8)
    disc = view.defs.radial([(0, shade(colors["dpad"], 0.92)), (0.8, colors["dpad"]), (1, shade(colors["dpad"], 1.1))],
                            r=0.5)
    view.add("controls", f'<path d="{circle_d(cx, cy, 15.4)}" fill="{disc}" stroke="{style.rim_of(colors["dpad"])}" '
                         f'stroke-width="0.25"/>', "dpad")
    style.glyph(view, style.cross_d(cx, cy, 22, 7.6, r=1.0), shade(colors["dpad"], 1.25), "dpad", width=0.35)
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.glyph(view, style.arrow_d(cx + dx * 7.6, cy + dy * 7.6, 1.7, direction), colors["arrow"], "dpad",
                    fill=True, opacity=0.8)
    common.dpad_spots(view, cx, cy, 24, 8.4, "left", prefix="dpad.")
    # two rows of three, slanted up to the right
    rows = (("south", "A", 26, 9.6), ("east", "B", 39.6, 5.6), ("rb", "C", 53.2, 1.6),
            ("west", "X", 23.4, -4.4), ("north", "Y", 37, -8.4), ("lb", "Z", 50.6, -12.4))
    for control, letter, x, y in rows:
        r = 5.5
        style.button(view, x, y, r, colors["button"], control)
        style.label(view, letter, x, y, r * 1.0, "#c9cbcf", control)
        view.spot(control, letter, ("circle", x, y, r + 0.4), "right")
    style.pill(view, 0, -4.4, 8.0, 3.4, colors["accent"], "menu")
    view.spot("menu", "Start", ("rect", -4.4, -6.5, 8.8, 4.2), "right")
    # three small round keys in a row under Start: minus, heart, home
    for control, label, x in (("view", "-", -7.6), ("share", "Heart", 0)):
        style.button(view, x, 8.6, 2.5, colors["small"], control)
        view.spot(control, label, ("circle", x, 8.6, 2.9), "left" if x < 0 else "right")
    home_button(view, 7.6, 8.6, 2.6, colors)
    return pic


# -- Zero 2 ------------------------------------------------------------------------------------------------------

def zero2():
    pic = Picture("8bitdo-zero-2", "8BitDo Zero 2, blue (drawn at twice the scale)")
    pic.sources = [
        size("size 73 x 36.5 x 14.7 mm, 20 g", "zero2/"),
        press("layout and colourways (yellow, blue, pink)", "2019/zero2/01-l.jpg"),
        {"what": "the scale: twice the pads' 8.6 px per mm, so the keychain pad stays readable in the dialog",
         "source": "this POC", "retrieved": "2026-10-23"}]
    colors = {"plate": "#5e9dbd", "body": "#5e9dbd", "trigger": "#4f88a4", "bumper": "#5592b0", "small": "#3e4246",
              "stick": "#2b2c2f", "dpad": "#2c2e31", "arrow": "#7c8086", "glyph": "#c6c8cc", "button": "#e3e1da"}
    view = pic.view("front", oy=768 + 3 * 17.2, scale=17.2)
    w, h = 73.0, 36.5
    top_shoulders(view, colors, ("L", "R", "L2", "R2"), -h / 2 + 0.8, 18, 31, triggers=False, rise=0.45)
    body = stadium_d(w, h)
    style.body(view, body, colors["body"])
    style.sheen(view, body)
    dpad(view, -22.4, -0.6, 12.6, 4.4, colors)
    face(view, 22.4, -0.6, 5.0, 2.6, colors["button"], "#6a6c70")
    system_pair(view, 3.2, 5.0, 4.6, 1.8, colors, labels=("Select", "Start"), angle=25)
    return pic
