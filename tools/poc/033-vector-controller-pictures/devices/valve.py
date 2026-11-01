"""Valve's Steam Deck: the handheld's own controls (its gamepad: steam-deck.md).

- `steam-deck`: the front at 6.4 px per mm (a 298 mm handheld across the canvas) and the back below it at half that:
  the screen in the middle; the D-pad, the left stick, the left trackpad and Steam on the left, mirrored by A B X Y,
  the right stick, the right trackpad and the quick access button; View and Menu above the sticks; L1 / L2 and
  R1 / R2 standing above the top edge. The back, turned over towards the viewer (the triggers at the bottom, the
  right hand still right), shows the paddles L4 L5 R4 R5. A trackpad shows the finger as a stick's dot and its
  pressure as a thin bar under it; the thumb on a stick lights the cap. Replaces the line drawing of
  `tools/controllers/make_fallback_images.py`; the control names and their analog kinds are kept.

Everything is measured on Valve's reference drawing of the Deck (steamdeck_2d_20220202.pdf, sheet 2, scale 1:1, the
vector paths read in millimetres): the outline (traced from the front view, the right half mirrored, overlap 0.977),
the cover glass and the display, every control's centre and size, the speaker slots, the paddles of the back view.
The Valve and Steam marks are left out (D5); the STEAM print on its button and the paddle names stay.
"""

import style
from devices import common
from model import Picture
from svgkit import circle_d, mirrored_d, num, rounded_d, shade

RETRIEVED = "2026-10-31"
CAD = "https://gitlab.steamos.cloud/SteamHardware/SteamDeck"

TOP = -56.5  # the shell's top edge; the outline from Valve's front view, 298.3 x 116.4 mm
SHELL = [("M", (0, TOP)),
         ("C", (34, TOP), (68, TOP), (102.6, TOP)),
         ("C", (108, -56.9), (113, -57.0), (117.3, -56.9)),
         ("C", (124.5, -56.7), (132.6, -57.1), (139.2, -53.6)),
         ("C", (144.2, -50.9), (147.4, -45.4), (148.8, -40.1)),
         ("C", (149.6, -37.0), (149.2, -26.7), (149.2, -22.9)),
         ("C", (149.2, -11.7), (149.2, -0.5), (149.0, 10.7)),
         ("C", (148.8, 17.8), (148.7, 25.0), (147.7, 32.0)),
         ("C", (146.4, 40.7), (143.5, 50.1), (135.4, 54.7)),
         ("C", (131.0, 57.2), (126.0, 58.4), (121.0, 59.0)),
         ("C", (113.9, 60.0), (106.7, 59.9), (99.6, 59.9)),
         ("C", (66, 59.9), (33, 59.9), (0, 59.9))]
STICK = (102.83, -33.93)  # the right one; the left mirrored
PAD = (89.23, -17.32, 33.1, 4.67)  # the right trackpad: left edge, top, side, corner radius
FACE = (131.7, -39.02)
DPAD = (-131.45, -39.02)
PILL_Y = 24.66  # Steam and quick access
SPEAKER = [(34.61, [(89.76, 92.84), (94.22, 97.30), (98.69, 101.77), (103.16, 106.24)]),
           (37.81, [(89.76, 95.07), (96.46, 99.54), (100.93, 104.00)]),
           (41.01, [(89.76, 92.84), (94.23, 97.30), (98.69, 101.77)])]
# the paddles on the back as the turned-over view shows them: (control, name, centre x, centre y)
PADDLES = (("paddle.p1", "R4", 109.3, -1.0), ("paddle.p2", "R5", 109.3, -23.85),
           ("paddle.p3", "L4", -109.3, -1.0), ("paddle.p4", "L5", -109.3, -23.85))


def _p(sx, x, y, flip=False):
    return f"{num(x * sx)},{num(-y if flip else y)}"


def bumper_d(sx=1, flip=False):
    """L1 / R1 over the top corner, drawn under the shell: its upper band shows"""
    f = lambda x, y: _p(sx, x, y, flip)  # noqa: E731
    return (f"M{f(106, TOP + 0.2)}C{f(112, TOP - 2.1)} {f(130, TOP - 2.9)} {f(138, TOP - 1.9)}"
            f"C{f(143, TOP - 1.1)} {f(146.5, TOP + 1.5)} {f(147.6, TOP + 4.5)}L{f(140, TOP + 4.5)}"
            f"L{f(106, TOP + 1)}Z")


def trigger_d(sx=1, flip=False):
    """L2 / R2 behind the bumper"""
    f = lambda x, y: _p(sx, x, y, flip)  # noqa: E731
    return (f"M{f(112, TOP)}C{f(116, TOP - 5)} {f(128, TOP - 5.7)} {f(136, TOP - 4.9)}"
            f"C{f(141, TOP - 4.3)} {f(144.5, TOP - 2)} {f(145.5, TOP + 1)}L{f(112, TOP + 1.5)}Z")


def steam_deck():
    pic = Picture("steam-deck", "Valve Steam Deck, front and back")
    pic.sources = [
        {"what": "the outline, the screen, every control's position and size, the speaker slots, the back paddles: "
                 "Steam Deck reference drawing rev. B (2022-02-02), sheet 2, front and back views at 1:1",
         "source": CAD + "/-/blob/master/steamdeck_2d_20220202.pdf", "author": "Valve Corporation",
         "licence": "CC BY-NC-SA 4.0", "use": "measured geometry only (vector paths read in mm); no lines shipped",
         "retrieved": RETRIEVED},
        {"what": "the colours: the matte dark grey shell, the dark caps with white letters",
         "source": "https://commons.wikimedia.org/wiki/File:Steam_Deck_(front).png", "author": "VulcanSphere",
         "licence": "CC BY-SA 4.0", "use": "reference only, not traced", "retrieved": RETRIEVED}]
    colors = {"body": "#2c2d30", "seam": "#3a3b3f", "glass": "#111214", "display": "#171c23", "cap": "#1d1e20",
              "stick": "#232427", "pad": "#2f3033", "trigger": "#28292c", "bumper": "#2f3034", "glyph": "#c9cbcf",
              "letter": "#dcdde0", "legend": "#9fa2a7"}
    scale, back_scale = 6.4, 3.2
    front = pic.view("front", oy=104 + (-TOP + 5.7) * scale, scale=scale)
    for sx, trig, bump, tl, bl, side in ((-1, "lt", "lb", "L2", "L1", "left"), (1, "rt", "rb", "R2", "R1", "right")):
        style.shaped(front, trigger_d(sx), colors["trigger"], trig, layer="shoulders")
        style.shaped(front, bumper_d(sx), colors["bumper"], bump, layer="shoulders")
        x0, x1 = sorted((112 * sx, 145.5 * sx))
        front.spot(trig, tl, ("rect", x0, TOP - 5.7, x1 - x0, 3.6), side, analog={"kind": "trigger", "fill": "down"})
        x0, x1 = sorted((106 * sx, 147 * sx))
        front.spot(bump, bl, ("rect", x0, TOP - 2.9, x1 - x0, 2.9), side)
    d = mirrored_d(SHELL)
    style.body(front, d, colors["body"])
    style.sheen(front, d)
    # the seams of the grips: the face plate's edge down to the screen, the two facets at the bottom corner
    for sx in (-1, 1):
        f = lambda x, y: _p(sx, x, y)  # noqa: E731
        style.line(front, f"M{f(125.4, -53)}C{f(125.6, -10)} {f(125.6, 18)} {f(124, 30)}"
                          f"C{f(121.5, 44)} {f(108, 53)} {f(87.5, 54.6)}", colors["seam"], 0.3, opacity=0.8)
        style.line(front, f"M{f(133.7, 49.6)}L{f(117.6, 35.2)}L{f(101.4, 54.0)}", colors["seam"], 0.25, opacity=0.6)
    # the screen: the cover glass, the display with a faint reflection
    front.add("case", f'<path d="{rounded_d(-86.85, -52.66, 173.7, 105.56, 1.6)}" fill="{colors["glass"]}" '
                      f'stroke="{style.rim_of(colors["body"])}" stroke-width="0.3"/>')
    front.add("case", f'<path d="{rounded_d(-75.85, -47.03, 151.7, 94.85, 0.4)}" fill="{colors["display"]}"/>')
    front.add("highlights", f'<path d="M-75.85,-47.03H-20L-48,47.82H-75.85Z" fill="#ffffff" fill-opacity="0.03"/>')
    # View and Menu above the sticks
    for control, label, sx, draw in (("view", "View", -1, common.view_glyph), ("menu", "Menu", 1, common.menu_glyph)):
        x = 115.13 * sx
        style.pill(front, x, -51.28, 9.5, 4.23, colors["cap"], control)
        draw(front, x, -51.28, 3.6, colors["glyph"], control)
        front.spot(control, label, ("rect", x - 5.2, -53.6, 10.4, 4.6), "left" if sx < 0 else "right")
    # the D-pad, A B X Y
    cx, cy = DPAD
    span, arm = 22.4, 7.2
    style.shaped(front, style.cross_d(cx, cy, span, arm, r=1.2), colors["cap"], "dpad",
                 style.cross_d(cx, cy, span + 0.9, arm + 0.9, r=1.5))
    common.dpad_spots(front, cx, cy, span, arm, "left", prefix="dpad.")
    labels = {control: {"cap": colors["cap"], "text": letter, "name": letter, "ink": colors["letter"]}
              for control, letter in (("north", "Y"), ("west", "X"), ("east", "B"), ("south", "A"))}
    common.face_diamond(front, *FACE, (8.75, 8.75), 4.1, labels)
    # the sticks in their wells; the thumb on a cap is a quiet spot over it
    for name, label, sx, side in (("lstick", "LS", -1, "left"), ("rstick", "RS", 1, "right")):
        x, y = STICK[0] * sx, STICK[1]
        style.recess(front, x, y, 14.42, colors["body"], depth=0.62)
        style.stick(front, x, y, 8.05, colors["stick"], f"{name}.x", skirt=9.31)
        common.stick_spots(front, name, label, x, y, 9.4, 6.5, side)
        front.spot(f"{name}.touch", f"{label} touch", ("circle", x, y, 8.05), side, group=name, quiet=True)
    # the trackpads: a press, a touch, the finger's position as a stick's dot, the pressure a bar under the pad
    for name, label, sx, side in (("ltouchpad", "Left trackpad", -1, "left"),
                                  ("rtouchpad", "Right trackpad", 1, "right")):
        x0, y0, size, r = PAD
        x = x0 if sx > 0 else -x0 - size
        front.add("case", f'<path d="{rounded_d(x - 0.4, y0 - 0.4, size + 0.8, size + 0.8, r + 0.4)}" '
                          f'fill="{style.GAP}" fill-opacity="0.5"/>')
        fill = front.defs.linear([(0, shade(colors["pad"], 0.96)), (1, shade(colors["pad"], 1.03))])
        front.add("controls", f'<path d="{rounded_d(x, y0, size, size, r)}" fill="{fill}" '
                              f'stroke="{style.rim_of(colors["pad"])}" stroke-width="0.2"/>', f"{name}.x")
        front.spot(f"{name}.x", label, ("rect", x, y0, size, size), side,
                   analog={"kind": "stick", "pair": f"{name}.y", "radius": size / 2 - 2.5}, group=name)
        inner = ("rect", x + 2, y0 + 2, size - 4, size - 4)
        front.spot(f"{name}.click", f"{label} press", inner, side, group=name)
        front.spot(f"{name}.touch", f"{label} touch", inner, side, group=name, quiet=True)
        bar = (x + 3, y0 + size + 1.1, size - 6, 1.2)
        front.add("case", f'<path d="{rounded_d(*bar, 0.6)}" fill="{shade(colors["body"], 0.8)}"/>')
        front.spot(f"{name}.pressure", f"{label} pressure", ("rect", *bar), side,
                   analog={"kind": "trigger", "fill": "down"}, group=name, quiet=True)
    # Steam and quick access under the trackpads, the speaker slots under them
    for control, label, sx in (("guide", "Steam", -1), ("misc.1", "···", 1)):
        x = 97.65 * sx
        style.pill(front, x, PILL_Y, 16.85, 7.1, colors["cap"], control)
        front.spot(control, label, ("rect", x - 8.8, PILL_Y - 3.9, 17.6, 7.8), "left" if sx < 0 else "right")
    style.label(front, "STEAM", -97.65, PILL_Y, 2.3, colors["glyph"], "guide")
    for dx in (-2.2, 0, 2.2):
        front.add("legends", f'<path d="{circle_d(97.65 + dx, PILL_Y, 0.75)}" fill="{colors["glyph"]}"/>', "misc.1")
    slots = "".join(rounded_d(sx * (a if sx > 0 else b) - 0.42, y - 0.85, b - a + 0.84, 1.69, 0.42)
                    for sx in (-1, 1) for y, row in SPEAKER for a, b in row)
    front.add("case", f'<path d="{slots}" fill="{shade(colors["body"], 0.45)}"/>')

    # The back, turned over towards the viewer: the triggers at the bottom, the right hand still right
    back = pic.view("back", oy=104 + (-TOP + 5.7) * scale + 59.9 * scale + 40 + 59.9 * back_scale, scale=back_scale)
    for sx in (-1, 1):
        style.shaped(back, trigger_d(sx, flip=True), colors["trigger"], None, layer="shoulders")
        style.shaped(back, bumper_d(sx, flip=True), colors["bumper"], None, layer="shoulders")
    d = mirrored_d(SHELL, mirror_y=True)
    style.body(back, d, shade(colors["body"], 0.96))
    style.sheen(back, d, cy=0.78)
    for control, name, x, y in PADDLES:
        back.add("controls", f'<path d="{rounded_d(x - 9.6, y - 9.9, 19.2, 19.8, 3.2)}" fill="{style.GAP}" '
                             f'fill-opacity="0.55"/>', control)
        style.shaped(back, rounded_d(x - 8.8, y - 9.1, 17.6, 18.2, 2.6), colors["bumper"], control)
        style.label(back, name, x + (4.6 if x < 0 else -4.6), y + 5.8, 3.0, colors["legend"], control)
        back.spot(control, name, ("rect", x - 9.6, y - 9.9, 19.2, 19.8), "left" if x < 0 else "right")
    return pic


SETS = {
    "steam-deck": steam_deck,
}
