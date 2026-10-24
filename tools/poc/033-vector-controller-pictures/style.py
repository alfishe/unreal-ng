"""The house style of the controller pictures: matte, restrained, lit softly from above.

Rules (the owner, POC 031): a body's shading stays within +-5 % of its colour, the sheen is its own layer at about
0.05, no dirt, wear or texture noise. Depth comes from the cut lines around a control (a thin dark gap) and the
recesses of the sticks and the D-pad, not from glossy highlights. Every function draws in millimetres of a view.
"""

from svgkit import circle_d, cap_height, mix, num, rounded_d, shade, text_path

GAP = "#0b0c0e"  # the cut around a control: the dark line between a part and the shell


def is_light(color):
    r, g, b = (int(color.lstrip("#")[i:i + 2], 16) for i in (0, 2, 4))
    return 0.299 * r + 0.587 * g + 0.114 * b > 140


def rim_of(color):
    """The outline of a part: lighter on a dark part (it reads on the dark theme), darker on a light one"""
    return shade(color, 0.72) if is_light(color) else shade(color, 1.16)


def body(view, d, color, control=None, rim=True, width=0.32, layer="case"):
    """A shell part: @p d its outline; a top-to-bottom shade of +-5 %, the outline in its rim colour"""
    fill = view.defs.linear([(0, shade(color, 1.05)), (1, shade(color, 0.95))])
    stroke = f' stroke="{rim_of(color)}" stroke-width="{num(width)}" stroke-linejoin="round"' if rim else ""
    view.add(layer, f'<path d="{d}" fill="{fill}"{stroke}/>', control)


def flat(view, d, color, control=None, opacity=1.0, layer="case"):
    extra = f' fill-opacity="{num(opacity)}"' if opacity < 1 else ""
    view.add(layer, f'<path d="{d}" fill="{color}"{extra}/>', control)


def line(view, d, color, width, control=None, opacity=1.0, layer="case"):
    extra = f' stroke-opacity="{num(opacity)}"' if opacity < 1 else ""
    view.add(layer, f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{num(width)}" '
                    f'stroke-linecap="round" stroke-linejoin="round"{extra}/>', control)


def sheen(view, d, cx=0.5, cy=0.22, r=0.75, opacity=0.05):
    """The soft light on the top of a shell: its own layer, so an app can turn it off"""
    fill = view.defs.radial([(0, "#ffffff", opacity), (1, "#ffffff", 0)], cx=cx, cy=cy, r=r)
    view.add("highlights", f'<path d="{d}" fill="{fill}"/>')


def recess(view, cx, cy, r, shell, depth=0.6, control=None):
    """A round well in the shell (a stick's or a D-pad's): darker than the shell, its upper wall in shade and its
    lower lip catching the light"""
    inner = shade(shell, depth)
    fill = view.defs.linear([(0, shade(inner, 0.85)), (0.55, inner), (1, shade(inner, 1.06))])
    view.add("case", f'<path d="{circle_d(cx, cy, r)}" fill="{fill}" stroke="{shade(shell, 0.5)}" '
                     f'stroke-width="0.25"/>', control)
    lip = view.defs.linear([(0, rim_of(shell), 0), (0.6, rim_of(shell), 0), (1, rim_of(shell), 0.55)])
    view.add("case", f'<path d="{circle_d(cx, cy, r + 0.2)}" fill="none" stroke="{lip}" stroke-width="0.35"/>',
             control)


def stick(view, cx, cy, r, color, control, skirt=None, ring=True):
    """A thumbstick from above: the cap with its grip ring and the concave centre; @p skirt the dome seen around
    the cap in the well (radius)"""
    if skirt:
        dome = view.defs.radial([(0, shade(color, 1.04)), (0.75, shade(color, 0.86)), (1, shade(color, 0.7))],
                                cx=0.5, cy=0.45, r=0.55)
        view.add("controls", f'<path d="{circle_d(cx, cy, skirt)}" fill="{dome}"/>', control)
    view.add("controls", f'<path d="{circle_d(cx, cy + 0.35, r + 0.25)}" fill="{GAP}" fill-opacity="0.45"/>',
             control)
    outer = view.defs.linear([(0, shade(color, 1.05)), (1, shade(color, 0.95))])
    view.add("controls", f'<path d="{circle_d(cx, cy, r)}" fill="{outer}" stroke="{rim_of(color)}" '
                         f'stroke-width="0.22"/>', control)
    dish = view.defs.linear([(0, shade(color, 0.9)), (1, shade(color, 1.04))])  # concave: lit at its lower side
    view.add("controls", f'<path d="{circle_d(cx, cy, r * 0.74)}" fill="{dish}"/>', control)
    if ring:
        view.add("controls", f'<path d="{circle_d(cx, cy, r * 0.74)}" fill="none" stroke="{shade(color, 0.78)}" '
                             f'stroke-width="0.2"/>', control)


def button(view, cx, cy, r, color, control, gap=0.45):
    """A round button standing in its cut-out"""
    view.add("controls", f'<path d="{circle_d(cx, cy, r + gap)}" fill="{GAP}" fill-opacity="0.55"/>', control)
    fill = view.defs.linear([(0, shade(color, 1.05)), (1, shade(color, 0.95))])
    view.add("controls", f'<path d="{circle_d(cx, cy, r)}" fill="{fill}" stroke="{rim_of(color)}" '
                         f'stroke-width="0.2"/>', control)


def shaped(view, d, color, control, gap_d=None, stroke=0.2, layer="controls"):
    """A button of any outline @p d, with its cut-out @p gap_d (the outline grown by the gap)"""
    if gap_d:
        view.add(layer, f'<path d="{gap_d}" fill="{GAP}" fill-opacity="0.55"/>', control)
    fill = view.defs.linear([(0, shade(color, 1.05)), (1, shade(color, 0.95))])
    view.add(layer, f'<path d="{d}" fill="{fill}" stroke="{rim_of(color)}" stroke-width="{num(stroke)}" '
                         f'stroke-linejoin="round"/>', control)


def pill(view, cx, cy, w, h, color, control, gap=0.4):
    shaped(view, rounded_d(cx - w / 2, cy - h / 2, w, h, h / 2), color, control,
           rounded_d(cx - w / 2 - gap, cy - h / 2 - gap, w + 2 * gap, h + 2 * gap, h / 2 + gap))


def label(view, text, cx, cy, size, color, control, face="sans-bold", opacity=1.0):
    """A legend centred on (cx, cy) by its cap height"""
    d = text_path(text, cx, cy + cap_height(size, face) / 2, size, face)
    extra = f' fill-opacity="{num(opacity)}"' if opacity < 1 else ""
    view.add("legends", f'<path d="{d}" fill="{color}"{extra}/>', control)


def glyph(view, d, color, control, width=None, fill=False, opacity=1.0):
    """A drawn legend (a PlayStation shape, an arrow): stroked, or filled"""
    extra = f' fill-opacity="{num(opacity)}"' if fill and opacity < 1 else (
        f' stroke-opacity="{num(opacity)}"' if opacity < 1 else "")
    if fill:
        view.add("legends", f'<path d="{d}" fill="{color}"{extra}/>', control)
    else:
        view.add("legends", f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{num(width)}" '
                            f'stroke-linecap="round" stroke-linejoin="round"{extra}/>', control)


def cross_d(cx, cy, span, arm, r=0.9):
    """A plus-shaped D-pad: @p span tip to tip, @p arm the width of an arm, corners rounded by @p r"""
    s, a = span / 2, arm / 2
    pts = [(-a, -s), (a, -s), (a, -a), (s, -a), (s, a), (a, a), (a, s), (-a, s), (-a, a), (-s, a), (-s, -a),
           (-a, -a)]
    outer = {0, 1, 3, 4, 6, 7, 9, 10}  # the tips' corners are rounded, the inner corners stay sharp-ish
    d = []
    n = len(pts)
    for i, (x, y) in enumerate(pts):
        rr = r if i in outer else 0.6
        px, py = pts[i - 1]
        nx, ny = pts[(i + 1) % n]

        def toward(ax, ay, bx, by, dist):
            length = ((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5
            return ax + (bx - ax) * dist / length, ay + (by - ay) * dist / length

        x1, y1 = toward(x, y, px, py, rr)
        x2, y2 = toward(x, y, nx, ny, rr)
        d.append(f"{'M' if i == 0 else 'L'}{num(cx + x1)},{num(cy + y1)}Q{num(cx + x)},{num(cy + y)} "
                 f"{num(cx + x2)},{num(cy + y2)}")
    return "".join(d) + "Z"


def arrow_d(cx, cy, size, direction):
    """A small filled triangle pointing @p direction (up, down, left, right)"""
    h = size / 2
    pts = {"up": [(0, -h), (h, h * 0.7), (-h, h * 0.7)], "down": [(0, h), (-h, -h * 0.7), (h, -h * 0.7)],
           "left": [(-h, 0), (h * 0.7, -h), (h * 0.7, h)], "right": [(h, 0), (-h * 0.7, h), (-h * 0.7, -h)]}
    return "M" + "L".join(f"{num(cx + x)},{num(cy + y)}" for x, y in pts[direction]) + "Z"


def tint(color, saturation):
    """A button colour kept at @p saturation of its own (the style guide keeps legend colours at about 60 %)"""
    r, g, b = (int(color.lstrip("#")[i:i + 2], 16) for i in (0, 2, 4))
    grey = 0.299 * r + 0.587 * g + 0.114 * b
    grey_hex = "#%02x%02x%02x" % (round(grey), round(grey), round(grey))
    return mix(grey_hex, color, saturation)
