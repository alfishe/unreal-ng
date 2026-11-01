"""SEGA's six-button pad, the retro pad the emulated machines take (TS-Config, the Next, a ZX-bus Sega port).

- `md6-pad`: the SEGA Six Button Control Pad (Mega Drive MK-1653 / Genesis "6 Button Arcade Pad", the Japanese
  Fighting Pad 6B has the same shell): the D-pad on a round dish, Start in the middle, A B C over X Y Z on a second
  dish, Mode on the top right edge. Replaces the line drawing of `tools/controllers/make_fallback_images.py`; the
  control names are the retro ones that set used (up / down / left / right, a b c x y z, start, mode).

The outline is measured with trace.py on Retro-Bit's product shot of its licensed MK-1653 reissue seen from above
(the silhouette, the left half mirrored, overlap 0.993), scaled to 140 mm: the width SEGA's FightingPad 6B reissue
of the same shell is sold with (140 x 70 x 29 mm); the traced depth, 72 mm, agrees. The control positions are read on
the same view on a 5 mm grid (about 0.5 mm; the shot looks down a little, so the depth may be short by a millimetre).
Evan-Amos's public-domain photo of an original MK-1653 checks the layout. The SEGA wordmark is left out (D5); the
START print and the button letters stay.
"""

import style
from model import Picture
from devices.nintendo_retro import rot_pill_d
from svgkit import circle_d, mirrored_d, num, shade

RETRIEVED = "2026-10-31"

MD6_SHELL = [("M", (-0.0, -35.0)),
             ("C", (3.3, -35.0), (6.4, -35.3), (9.6, -35.0)),
             ("C", (11.6, -34.8), (13.5, -34.3), (15.4, -34.0)),
             ("C", (33.2, -31.3), (50.1, -23.8), (61.4, -9.3)),
             ("C", (66.6, -2.5), (70.0, 5.3), (70.0, 14.0)),
             ("C", (70.0, 22.1), (66.3, 29.6), (59.3, 34.0)),
             ("C", (53.5, 37.7), (46.0, 38.3), (39.9, 35.0)),
             ("C", (37.3, 33.6), (35.1, 31.7), (32.6, 30.3)),
             ("C", (28.7, 28.0), (24.7, 26.0), (20.4, 24.6)),
             ("C", (16.1, 23.2), (11.8, 22.1), (7.3, 21.6)),
             ("C", (5.8, 21.4), (4.3, 21.3), (2.8, 21.2)),
             ("C", (2.2, 21.2), (0.6, 21.2), (-0.0, 21.2))]


def md6():
    pic = Picture("md6-pad", "SEGA Six Button Control Pad (Mega Drive MK-1653 / Genesis 6 Button Arcade Pad), black")
    pic.sources = [
        {"what": "width 140 mm: SEGA's FightingPad 6B reissue of the same shell (140 x 70 x 29 mm, a retailer's "
                 "listing); the traced view gives 72 mm deep at that width",
         "source": "https://wafuu.com/collections/all-product/products/fightingpad-6b-gaming-controller-durable-design",
         "retrieved": RETRIEVED, "note": "an estimate"},
        {"what": "view from above: the outline, the control positions and sizes",
         "source": "https://cdn.shopify.com/s/files/1/0078/2575/2141/products/sega-genesis-6-button-arcade-pad"
                   "accessories-video-game-controller-newretrofix-games-308454.png",
         "author": "Retro-Bit (product shot of the licensed reissue, RB-SGA-001)", "licence": "product photo",
         "use": "measured geometry only (trace.py outline, positions on a grid); no pixels shipped",
         "retrieved": RETRIEVED},
        {"what": "the original pad: the layout, the Mode key on the top right edge",
         "source": "https://commons.wikimedia.org/wiki/File:Sega-Genesis-6But-Cont.jpg", "author": "Evan-Amos",
         "licence": "public domain", "use": "reference only, not traced", "retrieved": RETRIEVED}]
    colors = {"body": "#2e2f32", "dish": "#1f2022", "lip": "#4a4c50", "dpad": "#2a2b2e", "arrow": "#56585c",
              "abc": "#26272a", "xyz": "#6c6e72", "start": "#77797d", "key": "#38393c", "bridge": "#333437",
              "ink": "#4c4e52", "ink_light": "#55575b", "legend": "#c3c5c9"}
    view = pic.view("front", oy=768)
    # Mode: a small key on the sloping top right edge, only its top out of the case
    style.shaped(view, rot_pill_d(50.3, -21.2, 8.0, 3.0, 36), colors["key"], "mode", layer="shoulders")
    view.spot("mode", "Mode", ("circle", 50.3, -21.2, 4.4), "right")
    d = mirrored_d(MD6_SHELL)
    style.body(view, d, colors["body"])
    style.sheen(view, d)
    # the raised bridge on the top edge where the cord comes out
    bridge = (f"M{num(-26.0)},{num(-31.8)}C{num(-14.0)},{num(-35.4)} {num(14.0)},{num(-35.4)} {num(26.0)},{num(-31.8)}"
              f"L{num(22.6)},{num(-28.6)}C{num(10.0)},{num(-31.0)} {num(-10.0)},{num(-31.0)} "
              f"{num(-22.6)},{num(-28.6)}Z")
    view.add("case", f'<path d="{bridge}" fill="{colors["bridge"]}" stroke="{style.rim_of(colors["body"])}" '
                     f'stroke-width="0.25"/>')
    # two glossy dishes: round under the D-pad, wider under the six buttons; a lighter lip on their lower edge
    for cx, cy, r in ((-35.6, -2.3, 23.0), (34.4, -2.0, 25.0)):
        view.add("case", f'<path d="{circle_d(cx, cy, r)}" fill="{colors["dish"]}" '
                         f'stroke="{style.rim_of(colors["body"])}" stroke-width="0.3"/>')
        lip = view.defs.linear([(0, colors["lip"], 0), (0.6, colors["lip"], 0), (1, colors["lip"], 0.55)])
        view.add("case", f'<path d="{circle_d(cx, cy, r - 1.4)}" fill="none" stroke="{lip}" stroke-width="1.4"/>')
    # the D-pad: a disc with the cross moulded in, arrows in the corners
    cx, cy = -35.4, -4.5
    disc = view.defs.radial([(0, shade(colors["dpad"], 0.94)), (0.82, colors["dpad"]),
                             (1, shade(colors["dpad"], 1.12))], r=0.5)
    view.add("controls", f'<path d="{circle_d(cx, cy, 15.4)}" fill="{disc}" stroke="{style.rim_of(colors["dpad"])}" '
                         f'stroke-width="0.25"/>', "dpad")
    span, arm = 23.6, 7.6
    style.shaped(view, style.cross_d(cx, cy, span, arm, r=1.2), shade(colors["dpad"], 1.1), "dpad")
    dish = view.defs.radial([(0, shade(colors["dpad"], 0.8)), (1, shade(colors["dpad"], 1.1))], r=0.5)
    view.add("controls", f'<path d="{circle_d(cx, cy, 2.6)}" fill="{dish}"/>', "dpad")
    for direction, dx, dy in (("up", 0, -1), ("down", 0, 1), ("left", -1, 0), ("right", 1, 0)):
        style.glyph(view, style.arrow_d(cx + dx * span * 0.36, cy + dy * span * 0.36, 1.6, direction),
                    colors["arrow"], "dpad", fill=True, opacity=0.8)
    s, a = span / 2, arm / 2
    for control, label, x, y, w, h in (("up", "Up", cx - a, cy - s, arm, s - a),
                                       ("down", "Down", cx - a, cy + a, arm, s - a),
                                       ("left", "Left", cx - s, cy - a, s - a, arm),
                                       ("right", "Right", cx + a, cy - a, s - a, arm)):
        view.spot(control, label, ("rect", x, y, w, h), "left", group="directions")
    # Start in the middle, START printed under it
    style.pill(view, 0, -7.5, 11.6, 4.4, colors["start"], "start")
    view.spot("start", "Start", ("rect", -6.2, -9.9, 12.4, 4.8), "right")
    style.label(view, "START", 0, -2.6, 2.6, colors["legend"], None)
    # A B C large and dark, X Y Z smaller and grey above them; the letters moulded in, a shade off the cap
    for control, letter, x, y, r, cap, ink in (("a", "A", 23.6, 7.2, 5.9, colors["abc"], colors["ink"]),
                                               ("b", "B", 36.0, 0.4, 5.9, colors["abc"], colors["ink"]),
                                               ("c", "C", 49.7, -3.3, 5.9, colors["abc"], colors["ink"]),
                                               ("x", "X", 18.8, -7.5, 4.3, colors["xyz"], colors["ink_light"]),
                                               ("y", "Y", 30.3, -12.8, 4.3, colors["xyz"], colors["ink_light"]),
                                               ("z", "Z", 42.2, -15.8, 4.3, colors["xyz"], colors["ink_light"])):
        style.button(view, x, y, r, cap, control)
        style.label(view, letter, x, y, r * 0.9, ink, control)
        view.spot(control, letter, ("circle", x, y, r + 0.4), "right")
    return pic


SETS = {
    "md6-pad": md6,
}
