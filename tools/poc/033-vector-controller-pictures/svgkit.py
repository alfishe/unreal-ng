"""SVG primitives for the controller pictures: gradients by content, colour shades, text as outlines, path helpers.

QSvgRenderer draws SVG Tiny 1.2: paths, gradients and opacity, no filters and no clip paths. Text is converted to
outlines with fontTools (the macOS system fonts, as POC 031), so a picture never depends on the fonts where it runs.
Units are millimetres of the device; each view is placed on the 2048 x 1536 canvas by a transform.
"""

import re

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTCollection, TTFont

FONTS = {
    "sans": ("/System/Library/Fonts/Helvetica.ttc", 0),
    "sans-bold": ("/System/Library/Fonts/Helvetica.ttc", 1),
    "rounded": ("/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf", None),
}

_fonts = {}
_NUMBER = re.compile(r"-?\d+\.\d+")


def num(value):
    """A coordinate to 0.01 mm (a tenth of a design pixel at 9 px per mm)"""
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def font(name):
    if name not in _fonts:
        path, index = FONTS[name]
        _fonts[name] = TTCollection(path)[index] if index is not None else TTFont(path)
    return _fonts[name]


def text_path(text, x, y, size, face_name="sans-bold", anchor="middle"):
    """The outlines of @p text with its baseline at (x, y); @p size is the em in mm"""
    face = font(face_name)
    cmap = face.getBestCmap()
    scale = size / face["head"].unitsPerEm
    width = sum(face["hmtx"][cmap[ord(c)]][0] for c in text if ord(c) in cmap) * scale
    if anchor == "middle":
        x -= width / 2
    elif anchor == "end":
        x -= width
    glyphs = face.getGlyphSet()
    parts = []
    for char in text:
        glyph = cmap.get(ord(char))
        if glyph is None:
            continue
        pen = SVGPathPen(glyphs)
        glyphs[glyph].draw(TransformPen(pen, (scale, 0, 0, -scale, x, y)))
        parts.append(pen.getCommands())
        x += face["hmtx"][glyph][0] * scale
    return _NUMBER.sub(lambda m: num(float(m.group())), " ".join(p for p in parts if p))


def cap_height(size, face_name="sans-bold"):
    face = font(face_name)
    os2 = face["OS/2"]
    height = getattr(os2, "sCapHeight", 0) or face["head"].unitsPerEm * 0.72
    return height * size / face["head"].unitsPerEm


def rgb(color):
    color = color.lstrip("#")
    return tuple(int(color[i:i + 2], 16) for i in (0, 2, 4))


def hexrgb(r, g, b):
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(c))) for c in (r, g, b))


def shade(color, factor):
    """@p color lighter (factor > 1: towards white by factor - 1) or darker (< 1: scaled)"""
    r, g, b = rgb(color)
    if factor >= 1:
        f = factor - 1
        return hexrgb(*(c + (255 - c) * f for c in (r, g, b)))
    return hexrgb(*(c * factor for c in (r, g, b)))


def mix(a, b, t):
    ra, rb = rgb(a), rgb(b)
    return hexrgb(*(x + (y - x) * t for x, y in zip(ra, rb)))


class Defs:
    """Gradients by content: the same gradient is defined once"""

    def __init__(self, prefix="g"):
        self._items = {}
        self._prefix = prefix

    def _stops(self, stops):
        return "".join(f'<stop offset="{o}" stop-color="{c}"' + (f' stop-opacity="{a}"' if a != 1 else "") + "/>"
                       for o, c, a in (s if len(s) == 3 else (*s, 1) for s in stops))

    def linear(self, stops, x1=0, y1=0, x2=0, y2=1):
        key = ("l", tuple(stops), x1, y1, x2, y2)
        if key not in self._items:
            ident = f"{self._prefix}{len(self._items)}"
            self._items[key] = (ident, f'<linearGradient id="{ident}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}">'
                                       f"{self._stops(stops)}</linearGradient>")
        return f"url(#{self._items[key][0]})"

    def radial(self, stops, cx=0.5, cy=0.5, r=0.5, fx=None, fy=None):
        key = ("r", tuple(stops), cx, cy, r, fx, fy)
        if key not in self._items:
            ident = f"{self._prefix}{len(self._items)}"
            focus = (f' fx="{fx}"' if fx is not None else "") + (f' fy="{fy}"' if fy is not None else "")
            self._items[key] = (ident, f'<radialGradient id="{ident}" cx="{cx}" cy="{cy}" r="{r}"{focus}>'
                                       f"{self._stops(stops)}</radialGradient>")
        return f"url(#{self._items[key][0]})"

    def svg(self):
        return "<defs>" + "".join(item for _, item in self._items.values()) + "</defs>"


def circle_d(cx, cy, r):
    """A circle as a path (two arcs), so it can join a compound path"""
    return (f"M{num(cx - r)},{num(cy)}A{num(r)},{num(r)} 0 1 0 {num(cx + r)},{num(cy)}"
            f"A{num(r)},{num(r)} 0 1 0 {num(cx - r)},{num(cy)}Z")


def rounded_d(x, y, w, h, r):
    r = max(0.0, min(r, w / 2, h / 2))
    return (f"M{num(x + r)},{num(y)}H{num(x + w - r)}A{num(r)},{num(r)} 0 0 1 {num(x + w)},{num(y + r)}"
            f"V{num(y + h - r)}A{num(r)},{num(r)} 0 0 1 {num(x + w - r)},{num(y + h)}H{num(x + r)}"
            f"A{num(r)},{num(r)} 0 0 1 {num(x)},{num(y + h - r)}V{num(y + r)}A{num(r)},{num(r)} 0 0 1 "
            f"{num(x + r)},{num(y)}Z")


def mirrored_d(half, mirror_y=False):
    """A closed outline symmetric about x = 0 from its right half: @p half starts at the top centre point
    ("M", (x, y)) and goes clockwise to the bottom centre with ("C", c1, c2, p) and ("L", p) segments. The left half
    is the right half reversed and mirrored. @p mirror_y flips the whole outline top to bottom (a back view of a pad
    turned over towards the viewer)."""
    fy = -1 if mirror_y else 1
    pts = [half[0][1]]
    segs = []
    for seg in half[1:]:
        segs.append(seg)
        pts.append(seg[-1])

    def p(point, sx=1):
        return f"{num(point[0] * sx)},{num(point[1] * fy)}"

    out = [f"M{p(pts[0])}"]
    for seg in segs:
        if seg[0] == "C":
            out.append(f"C{p(seg[1])} {p(seg[2])} {p(seg[3])}")
        else:
            out.append(f"L{p(seg[1])}")
    for i in range(len(segs) - 1, -1, -1):
        seg = segs[i]
        start = pts[i]
        if seg[0] == "C":
            out.append(f"C{p(seg[2], -1)} {p(seg[1], -1)} {p(start, -1)}")
        else:
            out.append(f"L{p(start, -1)}")
    return "".join(out) + "Z"


def polygon_d(points):
    return "M" + "L".join(f"{num(x)},{num(y)}" for x, y in points) + "Z"
