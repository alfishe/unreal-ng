"""The DB9 joystick: an Atari-standard stick of the Competition Pro kind, in three-quarter view.

The style guide gives joysticks a three-quarter view (device-database.md §6). The stick is the Competition Pro 5000
type (the Kempston row of device-database.md §10): a black base, a raised square platform at the front holding the
stick in its rubber boot, a red shaft and ball, two red fire buttons about an inch across at the back corners
(Sinclair User's review). Base size and heights are estimates (no maker's drawing was found); the projection keeps
widths, shows the plan at half depth and heights at full size. The one picture serves every DB9 image set: the generic
DB9 adapter, the Kempston joystick (its second button as Fire 2), the Sinclair and the Cursor joystick.
"""

import math

import style
from model import Picture
from svgkit import circle_d, num, shade

COMMONS = "https://commons.wikimedia.org/wiki/File:"
ELEVATION = 48  # degrees above the table the stick is seen from
K = math.sin(math.radians(ELEVATION))  # the plan's depth on screen
V = math.cos(math.radians(ELEVATION))  # a height on screen


def at(x, z, h):
    """Screen position (mm) of the device point x right, z back, h up"""
    return x, -z * K - h * V


def plan_rect_d(x0, z0, x1, z1, r, h):
    """A rounded rectangle of the plan (x0..x1, z0..z1, corner r) at height h, squashed by the projection"""
    ry = r * K
    left, right = x0, x1
    top, bottom = at(0, z1, h)[1], at(0, z0, h)[1]
    return (f"M{num(left + r)},{num(top)}H{num(right - r)}A{num(r)},{num(ry)} 0 0 1 {num(right)},{num(top + ry)}"
            f"V{num(bottom - ry)}A{num(r)},{num(ry)} 0 0 1 {num(right - r)},{num(bottom)}H{num(left + r)}"
            f"A{num(r)},{num(ry)} 0 0 1 {num(left)},{num(bottom - ry)}V{num(top + ry)}"
            f"A{num(r)},{num(ry)} 0 0 1 {num(left + r)},{num(top)}Z")


def ellipse_d(cx, cy, rx, ry):
    return (f"M{num(cx - rx)},{num(cy)}A{num(rx)},{num(ry)} 0 1 0 {num(cx + rx)},{num(cy)}"
            f"A{num(rx)},{num(ry)} 0 1 0 {num(cx - rx)},{num(cy)}Z")


def block(view, x0, z0, x1, z1, r, h0, h1, top, side, control=None, layer="case"):
    """A rounded box from h0 to h1: its front and side walls, then its top face"""
    upper = plan_rect_d(x0, z0, x1, z1, r, h1)
    ry = r * K
    mid = at(0, (z0 + z1) / 2, h1)[1]
    front = at(0, z0, h0)[1]
    wall_d = (f"M{num(x0)},{num(mid)}V{num(front - ry)}A{num(r)},{num(ry)} 0 0 0 {num(x0 + r)},{num(front)}"
              f"H{num(x1 - r)}A{num(r)},{num(ry)} 0 0 0 {num(x1)},{num(front - ry)}V{num(mid)}Z")
    wall = view.defs.linear([(0, shade(side, 1.04)), (1, shade(side, 0.92))])
    view.add(layer, f'<path d="{wall_d}" fill="{wall}" stroke="{style.rim_of(side)}" stroke-width="0.3"/>', control)
    style.body(view, upper, top, control=control, layer=layer, width=0.3)


def cylinder(view, cx, z, r, h0, h1, top, side, control=None, layer="controls"):
    x, y0 = at(cx, z, h0)
    _, y1 = at(cx, z, h1)
    ry = r * K
    wall = view.defs.linear([(0, shade(side, 1.04)), (0.5, side), (1, shade(side, 0.86))], x2=1, y2=0)
    view.add(layer, f'<path d="{ellipse_d(x, y0, r, ry)}" fill="{wall}"/>', control)
    view.add(layer, f'<path d="M{num(x - r)},{num(y1)}H{num(x + r)}V{num(y0)}H{num(x - r)}Z" fill="{wall}"/>', control)
    face = view.defs.linear([(0, shade(top, 1.05)), (1, shade(top, 0.95))])
    view.add(layer, f'<path d="{ellipse_d(x, y1, r, ry)}" fill="{face}" stroke="{style.rim_of(top)}" '
                    f'stroke-width="0.25"/>', control)
    return x, y1


def stick(family, prefix, fires):
    pic = Picture(family, "DB9 joystick, Competition Pro type")
    pic.sources = [
        {"what": "the layout: square base, two red fire buttons about one inch across, the stick on a raised "
                 "platform, a metal shaft with a ball top", "source": "Sinclair User review of the Competition Pro "
                 "5000, via spectrumcomputing.co.uk ZXSR 1000551", "retrieved": "2026-10-23"},
        {"what": "proportions and colours (the later red shaft and ball)",
         "source": COMMONS + "Joystick_Competition_PRO.JPG", "author": "MOS6502", "licence": "public domain",
         "use": "reference only, not traced", "retrieved": "2026-10-23"},
        {"what": "the base seen from the front, the buttons at the back corners", "source": COMMONS
         + "Competition_Pro.jpg", "licence": "public domain", "use": "reference only", "retrieved": "2026-10-23"},
        {"what": "base 124 x 136 x 44 mm, the ball 116 mm above the platform", "source": "estimate, no maker's "
         "drawing found", "retrieved": "2026-10-23"}]
    colors = {"base": "#46474b", "wall": "#3a3b3e", "platform": "#333437", "boot": "#2c2d30", "red": "#b4463b",
              "bezel": "#303134", "glyph": "#b9bcc1"}
    view = pic.view("front", oy=768 + 37.5 * 7.2, scale=7.2)
    block(view, -62, -68, 62, 68, 18, 0, 44, colors["base"], colors["wall"])
    base_top = plan_rect_d(-62, -68, 62, 68, 18, 44)
    style.sheen(view, base_top, cy=0.3)
    # the fire buttons at the back corners: a bezel, then the red cap
    names = [("fire", fires[0], -41)] + ([("fire2", fires[1], 41)] if len(fires) > 1 else [])
    caps = {}
    for x in (-41, 41):
        cylinder(view, x, 44, 16.5, 44, 49, colors["bezel"], shade(colors["bezel"], 0.9), layer="case")
    for control, label, x in names + ([("fire", None, 41)] if len(fires) == 1 else []):
        name = f"{prefix}{control}" if label else f"{prefix}fire"
        caps[x] = cylinder(view, x, 44, 12.5, 49, 55, colors["red"], shade(colors["red"], 0.85),
                           control=name if label else f"{name}-twin")
        if label:
            cx, cy = caps[x]
            view.spot(name, label, ("rect", cx - 13.5, cy - 7.5, 27, 15), "left" if x < 0 else "right")
    # the platform, the boot, the shaft, the ball
    block(view, -38, -64, 38, 10, 9, 44, 50, colors["platform"], shade(colors["platform"], 0.85))
    bx, by = at(0, -27, 50)
    _, ty = at(0, -27, 62)
    boot = (f"M{num(bx - 17)},{num(by)}A17,{num(17 * K)} 0 0 0 {num(bx + 17)},{num(by)}L{num(bx + 7.5)},{num(ty)}"
            f"A7.5,{num(7.5 * K)} 0 0 1 {num(bx - 7.5)},{num(ty)}Z")
    fill = view.defs.linear([(0, shade(colors["boot"], 1.06)), (0.5, colors["boot"]), (1, shade(colors["boot"], 0.86))],
                            x2=1, y2=0)
    view.add("controls", f'<path d="{ellipse_d(bx, by, 17, 17 * K)}" fill="{style.GAP}" fill-opacity="0.5"/>')
    view.add("controls", f'<path d="{boot}" fill="{fill}"/>')
    view.add("controls", f'<path d="{ellipse_d(bx, ty, 7.5, 7.5 * K)}" fill="{shade(colors["boot"], 0.9)}"/>')
    top = at(0, -27, 152)[1]
    shaft = view.defs.linear([(0, shade(colors["red"], 1.06)), (0.45, colors["red"]), (1, shade(colors["red"], 0.84))],
                             x2=1, y2=0)
    view.add("controls", f'<path d="M{num(bx - 5.2)},{num(top)}H{num(bx + 5.2)}V{num(ty)}H{num(bx - 5.2)}Z" '
                         f'fill="{shaft}"/>')
    ball_y = at(0, -27, 166)[1]
    ball = view.defs.radial([(0, shade(colors["red"], 1.08)), (0.7, colors["red"]), (1, shade(colors["red"], 0.82))],
                            cx=0.42, cy=0.38, r=0.62)
    view.add("controls", f'<path d="{circle_d(bx, ball_y, 18)}" fill="{ball}" stroke="{style.rim_of(colors["red"])}" '
                         f'stroke-width="0.3"/>')
    sheen = view.defs.radial([(0, "#ffffff", 0.06), (1, "#ffffff", 0)], cx=0.4, cy=0.35, r=0.5)
    view.add("highlights", f'<path d="{circle_d(bx, ball_y, 18)}" fill="{sheen}"/>')
    # the four directions round the ball, where the hand pushes it
    for control, label, dx, dy, side in (("up", "Up", 0, -27, "left"), ("down", "Down", 0, 27, "left"),
                                         ("left", "Left", -29, 0, "left"), ("right", "Right", 29, 0, "right")):
        x, y = bx + dx, ball_y + dy
        style.glyph(view, style.arrow_d(x, y, 6.0, control), colors["glyph"], f"{prefix}{control}", fill=True,
                    opacity=0.55)
        view.spot(f"{prefix}{control}", label, ("circle", x, y, 6.5), side, group="directions")
    return pic
