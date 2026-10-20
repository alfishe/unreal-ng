"""Drawing primitives for the keyboards: text as outlines, soft shadows and lit surfaces without SVG filters.

Qt's QSvgRenderer draws SVG Tiny 1.2: paths, gradients and opacity, no filters. A shadow is a stack of rounded
rectangles of falling opacity, a lit surface is a linear gradient for the light from above plus a radial sheen, and
text is converted to paths with fontTools so the picture never depends on the fonts of the machine it runs on.
Units are millimetres; the generator scales the whole picture with the viewBox.
"""

import re

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTCollection, TTFont

FONTS = {
    "sans": ("/System/Library/Fonts/Helvetica.ttc", 0),
    "sans-bold": ("/System/Library/Fonts/Helvetica.ttc", 1),
    "narrow": ("/System/Library/Fonts/Supplemental/Arial Narrow.ttf", None),
    "narrow-bold": ("/System/Library/Fonts/Supplemental/Arial Narrow Bold.ttf", None),
    "arial-bold": ("/System/Library/Fonts/Supplemental/Arial Bold.ttf", None),
    "black": ("/System/Library/Fonts/Supplemental/Arial Black.ttf", None),
    "unicode": ("/System/Library/Fonts/Supplemental/Arial Unicode.ttf", None),
    "rounded": ("/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf", None),
    "bold-italic": ("/System/Library/Fonts/Supplemental/Arial Bold Italic.ttf", None),
    "oblique-bold": ("/System/Library/Fonts/Helvetica.ttc", 3),
    "din-condensed": ("/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf", None),
}

_fonts = {}


def font(name):
    if name not in _fonts:
        path, index = FONTS[name]
        _fonts[name] = TTCollection(path)[index] if index is not None else TTFont(path)
    return _fonts[name]


def _glyph(face, char):
    cmap = face.getBestCmap()
    return cmap.get(ord(char))


def text_width(text, size, face_name="sans"):
    face = font(face_name)
    units = face["head"].unitsPerEm
    metrics = face["hmtx"]
    width = 0
    for char in text:
        glyph = _glyph(face, char) or _glyph(font("unicode"), char)
        if glyph is None:
            continue
        table = metrics if _glyph(face, char) else font("unicode")["hmtx"]
        width += table[glyph][0]
    return width * size / units


def text_path(text, x, y, size, face_name="sans", anchor="start", spacing=0.0, squeeze=1.0):
    """The outlines of @p text with its baseline at (x, y), @p size the em in mm; a char missing in the face comes
    from Arial Unicode (Cyrillic, arrows). @p squeeze narrows the glyphs (a condensed face)"""
    face = font(face_name)
    width = (text_width(text, size, face_name) + spacing * max(0, len(text) - 1)) * squeeze
    if anchor == "middle":
        x -= width / 2
    elif anchor == "end":
        x -= width
    pen_path = []
    cursor = x
    for char in text:
        use = face if _glyph(face, char) else font("unicode")
        glyph = _glyph(use, char)
        if glyph is None:
            continue
        scale = size / use["head"].unitsPerEm
        glyphs = use.getGlyphSet()
        pen = SVGPathPen(glyphs)
        glyphs[glyph].draw(TransformPen(pen, (scale * squeeze, 0, 0, -scale, cursor, y)))
        pen_path.append(pen.getCommands())
        cursor += (use["hmtx"][glyph][0] * scale + spacing) * squeeze
    return _NUMBER.sub(lambda m: f"{float(m.group()):.2f}".rstrip("0").rstrip("."), " ".join(p for p in pen_path if p))


_NUMBER = re.compile(r"-?\d+\.\d+")  # outlines to 0.01 mm: 1/20 of a pixel at 1600 px across


def rounded(x, y, w, h, r):
    r = max(0.0, min(r, w / 2, h / 2))
    return (f"M{x + r:.3f},{y:.3f}H{x + w - r:.3f}A{r:.3f},{r:.3f} 0 0 1 {x + w:.3f},{y + r:.3f}"
            f"V{y + h - r:.3f}A{r:.3f},{r:.3f} 0 0 1 {x + w - r:.3f},{y + h:.3f}H{x + r:.3f}"
            f"A{r:.3f},{r:.3f} 0 0 1 {x:.3f},{y + h - r:.3f}V{y + r:.3f}A{r:.3f},{r:.3f} 0 0 1 {x + r:.3f},{y:.3f}Z")


def soft_shadow(x, y, w, h, r, spread, dx=0.0, dy=0.0, opacity=0.5, steps=6, color="#000"):
    """A blurred shadow: @p steps rounded rectangles growing by @p spread, each faint, summing to @p opacity"""
    parts = []
    each = 1 - (1 - opacity) ** (1 / steps)
    for i in range(steps):
        grow = spread * (i + 1) / steps
        parts.append(f'<path d="{rounded(x + dx - grow, y + dy - grow, w + 2 * grow, h + 2 * grow, r + grow)}" '
                     f'fill="{color}" fill-opacity="{each:.3f}"/>')
    return "\n".join(parts)


class Defs:
    """Gradients by content: the same gradient is defined once"""

    def __init__(self):
        self._items = {}

    def linear(self, stops, x1=0, y1=0, x2=0, y2=1):
        key = ("l", tuple(stops), x1, y1, x2, y2)
        if key not in self._items:
            ident = f"g{len(self._items)}"
            body = "".join(f'<stop offset="{o}" stop-color="{c}"' + (f' stop-opacity="{a}"' if a != 1 else "") + "/>"
                           for o, c, a in (s if len(s) == 3 else (*s, 1) for s in stops))
            self._items[key] = (ident, f'<linearGradient id="{ident}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}">'
                                       f"{body}</linearGradient>")
        return f"url(#{self._items[key][0]})"

    def radial(self, stops, cx=0.5, cy=0.5, r=0.5, fx=None, fy=None):
        key = ("r", tuple(stops), cx, cy, r, fx, fy)
        if key not in self._items:
            ident = f"g{len(self._items)}"
            body = "".join(f'<stop offset="{o}" stop-color="{c}"' + (f' stop-opacity="{a}"' if a != 1 else "") + "/>"
                           for o, c, a in (s if len(s) == 3 else (*s, 1) for s in stops))
            focus = (f' fx="{fx}"' if fx is not None else "") + (f' fy="{fy}"' if fy is not None else "")
            self._items[key] = (ident, f'<radialGradient id="{ident}" cx="{cx}" cy="{cy}" r="{r}"{focus}>'
                                       f"{body}</radialGradient>")
        return f"url(#{self._items[key][0]})"

    def svg(self):
        return "<defs>" + "".join(item for _, item in self._items.values()) + "</defs>"


def shade(color, factor):
    """@p color lighter (factor > 1) or darker (< 1)"""
    color = color.lstrip("#")
    r, g, b = (int(color[i:i + 2], 16) for i in (0, 2, 4))
    if factor >= 1:
        f = factor - 1
        r, g, b = (round(c + (255 - c) * f) for c in (r, g, b))
    else:
        r, g, b = (round(c * factor) for c in (r, g, b))
    return "#%02x%02x%02x" % tuple(max(0, min(255, c)) for c in (r, g, b))
