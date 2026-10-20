"""Phase 1: photorealistic. Real colours, lit materials, soft shadows, all within what QSvgRenderer draws."""

from styles.common import Picture, glyph_shape, glyphs, legend_origin
from styles.photocaps import hard, next_cap, rubber
from svgkit import rounded, shade, soft_shadow, text_path, text_width


def case_body(pic, part):
    g = part.geometry
    d = pic.defs
    x, y, w, h, r = g["x"], g["y"], g["w"], g["h"], g.get("r", 3)
    if g.get("matte"):
        fill = d.linear([(0, shade(part.color, 1.1)), (0.1, part.color), (1, shade(part.color, 0.88))])
    else:
        fill = d.linear([(0, shade(part.color, 1.25)), (0.08, shade(part.color, 1.08)), (1, shade(part.color, 0.85))])
    edge = d.linear([(0, "#ffffff", 0.25), (0.04, "#ffffff", 0), (0.96, "#000000", 0), (1, "#000000", 0.5)])
    parts = [
        soft_shadow(x, y, w, h, r, 2.5, dx=0.8, dy=1.6, opacity=0.6, steps=6),
        f'<path d="{rounded(x, y, w, h, r)}" fill="{fill}"/>',
        f'<path d="{rounded(x, y, w, h, r)}" fill="{edge}"/>',
    ]
    if g.get("rolloff"):  # the right end curves down: darker towards the edge, a highlight where it starts
        run = g["rolloff"]
        roll = d.linear([(0, "#000000", 0), (0.45, "#000000", 0.12), (1, "#000000", 0.55)], x1=0, y1=0, x2=1, y2=0)
        parts.append(f'<path d="{rounded(x + w - run, y, run, h, r)}" fill="{roll}"/>')
    return "\n".join(parts)


def case_hump(pic, part):
    """The raised rear of a case (the 48K's): matte, lit along its top, a lit front edge and its shadow on the plate"""
    g = part.geometry
    d = pic.defs
    x, y, w, h, r = g["x"], g["y"], g["w"], g["h"], g.get("r", 3)
    fill = d.linear([(0, shade(part.color, 1.2)), (0.12, part.color), (0.93, shade(part.color, 0.95)),
                     (1, shade(part.color, 1.12))])
    return "\n".join([
        f'<rect x="{x:.3f}" y="{y + h:.3f}" width="{w:.3f}" height="1.6" fill="#000" fill-opacity="0.55"/>',
        f'<rect x="{x:.3f}" y="{y + h + 1.6:.3f}" width="{w:.3f}" height="1.2" fill="#000" fill-opacity="0.25"/>',
        f'<path d="M{x:.3f},{y + h:.3f}V{y + r:.3f}A{r:.3f},{r:.3f} 0 0 1 {x + r:.3f},{y:.3f}H{x + w - r:.3f}'
        f'A{r:.3f},{r:.3f} 0 0 1 {x + w:.3f},{y + r:.3f}V{y + h:.3f}Z" fill="{fill}"/>',
    ])


def case_well(pic, part):
    """The keyboard's recess: a glossy black deck, its back rim in shadow and its front rim catching the light"""
    g = part.geometry
    d = pic.defs
    x, y, w, h, r = g["x"], g["y"], g["w"], g["h"], g.get("r", 1)
    deck = d.linear([(0, "#050607"), (0.06, part.color), (1, shade(part.color, 0.8))])
    rim = d.linear([(0, "#000000", 0.6), (0.03, "#000000", 0), (0.985, "#ffffff", 0), (1, "#ffffff", 0.2)])
    return "\n".join([
        f'<path d="{rounded(x - 0.4, y - 0.4, w + 0.8, h + 0.8, r + 0.4)}" fill="#000" fill-opacity="0.6"/>',
        f'<path d="{rounded(x, y, w, h, r)}" fill="{deck}"/>',
        f'<path d="{rounded(x, y, w, h, r)}" fill="{rim}"/>',
    ])


def case_groove(pic, part):
    """A groove cut across the case: a dark line with a lit lower lip"""
    g = part.geometry
    x, y, w, h = g["x"], g["y"], g["w"], g["h"]
    return (f'<rect x="{x:.3f}" y="{y:.3f}" width="{w:.3f}" height="{h * 0.6:.3f}" fill="#060709"/>'
            f'<rect x="{x:.3f}" y="{y + h * 0.6:.3f}" width="{w:.3f}" height="{h * 0.4:.3f}" '
            f'fill="{shade(part.color, 1.35)}" fill-opacity="0.6"/>')


def case_insert(pic, part):
    """A coloured strip set into a groove (the Next's rainbow): glossy plastic"""
    g = part.geometry
    d = pic.defs
    x, y, w, h = g["x"], g["y"], g["w"], g["h"]
    fill = d.linear([(0, shade(part.color, 1.3)), (0.4, part.color), (1, shade(part.color, 0.6))])
    fade = d.linear([(0, "#000000", 0), (0.6, "#000000", 0), (1, "#000000", 0.45)], x1=0, y1=0, x2=1, y2=0)
    return "\n".join([
        f'<path d="{rounded(x, y, w, h, h / 2)}" fill="{fill}"/>',
        f'<path d="{rounded(x, y, w, h, h / 2)}" fill="{fade}"/>',
    ])


def case_grille(pic, part):
    """A row of vent slots running front to back"""
    g = part.geometry
    x, y, w, h, n = g["x"], g["y"], g["w"], g["h"], g["slots"]
    pitch = w / n
    slot = pitch * 0.5
    parts = []
    for i in range(n):
        sx = x + i * pitch
        parts.append(f'<path d="{rounded(sx, y, slot, h, slot / 2)}" fill="#050506"/>')
        parts.append(f'<path d="{rounded(sx + slot, y + 0.2, 0.25, h - 0.4, 0.12)}" fill="#ffffff" '
                     f'fill-opacity="0.08"/>')
    return "\n".join(parts)


def case_tab(pic, part):
    """A glossy black part on the matte case"""
    g = part.geometry
    d = pic.defs
    x, y, w, h, r = g["x"], g["y"], g["w"], g["h"], g.get("r", 1)
    fill = d.linear([(0, shade(part.color, 2.2)), (0.3, part.color), (1, shade(part.color, 0.6))])
    return f'<path d="{rounded(x, y, w, h, r)}" fill="{fill}"/>'


def case_led(pic, part):
    g = part.geometry
    d = pic.defs
    fill = d.radial([(0, "#6a3030"), (0.6, part.color), (1, "#000000")], cx=0.4, cy=0.35, r=0.6)
    return f'<circle cx="{g["x"]:.3f}" cy="{g["y"]:.3f}" r="{g["r"]:.3f}" fill="{fill}"/>'



def case_plate(pic, part):
    """A flat panel set into the case (the 48K's metal keyboard plate): brushed by a soft horizontal sheen"""
    g = part.geometry
    d = pic.defs
    x, y, w, h, r = g["x"], g["y"], g["w"], g["h"], g.get("r", 1)
    fill = d.linear([(0, shade(part.color, 1.12)), (1, shade(part.color, 0.9))])
    sheen = d.linear([(0, "#ffffff", 0), (0.35, "#ffffff", part.extra.get("sheen", 0.06)), (0.7, "#ffffff", 0)],
                     x1=0, y1=0, x2=1, y2=0.3)
    return "\n".join([
        f'<path d="{rounded(x - 0.3, y - 0.3, w + 0.6, h + 0.6, r + 0.3)}" fill="#000" fill-opacity="0.5"/>',
        f'<path d="{rounded(x, y, w, h, r)}" fill="{fill}"/>',
        f'<path d="{rounded(x, y, w, h, r)}" fill="{sheen}"/>',
    ])


def case_stripe(pic, part):
    """A printed band: a polygon in one colour"""
    points = part.geometry["points"]
    return ('<path d="M' + " L".join(f"{px:.3f},{py:.3f}" for px, py in points) +
            f'Z" fill="{part.color}"/>')


def case_logo(pic, part):
    """A printed word; `width` sizes it to its measured width, `gloss` makes it glossy on a matte case (the Next's
    black-on-black wordmark reads by its sheen only)"""
    g = part.geometry
    face, spacing = g.get("face", "sans-bold"), g.get("spacing", 0.0)
    size = g.get("size")
    squeeze = g.get("squeeze", 1.0)
    if "width" in g:  # stretched to the measured width: with a size, wider glyphs; without, a bigger size
        natural = text_width(g["text"], size or 1, face) + spacing * (len(g["text"]) - 1)
        if size:
            squeeze = g["width"] / natural
        else:
            size = (g["width"] - spacing * (len(g["text"]) - 1)) / text_width(g["text"], 1, face)
    args = (g["x"], g["y"], size, face)
    kw = dict(anchor=g.get("anchor", "start"), squeeze=squeeze, spacing=spacing)
    if g.get("outline"):  # moulded into the case: a double contour, lit above and shaded below
        d = text_path(g["text"], *args, kw["anchor"], spacing, squeeze)
        return "\n".join(f'<path d="{d}" transform="translate(0,{dy})" fill="none" stroke="{colour}" '
                          f'stroke-width="{width}" stroke-opacity="{opacity}" stroke-linejoin="round"/>'
                          for dy, colour, width, opacity in [(0.25, "#000000", 0.8, 0.6), (-0.15, "#4a4a48", 0.45, 0.5),
                                                             (0, part.color, 0.45, 1)])
    if not g.get("gloss"):
        return glyphs(g["text"], *args, part.color, **kw)
    fill = pic.defs.linear([(0, "#3c3e42"), (0.35, part.color), (0.7, "#0a0a0b"), (1, "#26282b")])
    return "\n".join([
        glyphs(g["text"], g["x"], g["y"] + 0.35, size, face, "#000", opacity=0.6, **kw),
        glyphs(g["text"], g["x"], g["y"] - 0.2, size, face, "#8a8d92", opacity=0.45, **kw),
        glyphs(g["text"], *args, fill, **kw),
    ])


CASE = {"body": case_body, "plate": case_plate, "stripe": case_stripe, "logo": case_logo, "well": case_well,
        "groove": case_groove, "insert": case_insert, "grille": case_grille, "tab": case_tab, "led": case_led,
        "hump": case_hump}


def cap(pic, key):
    x, y, w, h = key.rect
    if key.cap == "rubber":
        return rubber(pic, x, y, w, h, key.color, skirt=pic.keyboard.get("skirt", 0.0)), (x, y, w, h)
    if key.cap == "hard":
        return hard(pic, x, y, w, h, key.color)
    if key.cap == "next":
        return next_cap(pic, x, y, w, h, key.color, key.shape)
    raise ValueError(f"{key.id}: cap {key.cap}")


def draw(keyboard, model):
    pic = Picture(keyboard)
    for part in model.case:
        pic.add("case", CASE[part.kind](pic, part))
    for key in model.keys:
        svg, face = cap(pic, key)
        pic.add("caps", svg)
        for legend in key.legends:
            lx, ly = legend_origin(legend, key, face)
            if legend.glyph:
                pic.add("legends", glyph_shape(legend.glyph, lx, ly, legend.size, legend.color))
                continue
            if legend.inverse:
                text = text_width(legend.text, legend.size, legend.face) * legend.squeeze
                pad = legend.size * 0.3
                width = text + 2 * pad
                left = {"start": lx - pad, "middle": lx - width / 2, "end": lx - text - pad}[legend.anchor]
                pic.add("legends", f'<rect x="{left:.3f}" y="{ly - legend.size * 0.95:.3f}" '
                                   f'width="{width:.3f}" height="{legend.size * 1.2:.3f}" fill="{legend.color}"/>')
                pic.add("legends", glyphs(legend.text, lx, ly, legend.size, legend.face, "#141517", legend.anchor,
                                          squeeze=legend.squeeze))
                continue
            if legend.swatch:
                pic.add("legends", f'<rect x="{lx - legend.size:.3f}" y="{ly - legend.size * 0.8:.3f}" '
                                   f'width="{legend.size * 2:.3f}" height="{legend.size:.3f}" fill="{legend.swatch}"/>')
            pic.add("legends", glyphs(legend.text, lx, ly, legend.size, legend.face, legend.color, legend.anchor,
                                      squeeze=legend.squeeze))
    return pic.svg()
