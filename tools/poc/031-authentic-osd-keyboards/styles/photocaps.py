"""Key cap materials, each a function of the canvas (for its gradients) and the key's rectangle in mm.

Light comes from above and a little to the left, as in a desk photo: a top face is lighter at its top edge, a
skirt is darker on its sides and darkest at the front, a shadow falls down and a little right.
"""

from svgkit import rounded, shade, soft_shadow, text_path


def label(text, x, y, size, face="sans", color="#fff", anchor="middle", opacity=1.0, squeeze=1.0, spacing=0.0):
    d = text_path(text, x, y, size, face, anchor, spacing, squeeze)
    if not d:
        return ""
    extra = f' fill-opacity="{opacity}"' if opacity != 1 else ""
    return f'<path d="{d}" fill="{color}"{extra}/>'


def rubber(canvas, x, y, w, h, color, radius=1.6, skirt=0.0):
    """A moulded rubber key (the 48K's): matte, a low dome, soft edges, a soft shadow on the plate; @p skirt mm of
    its front side show below the top face (a tall key seen from a little in front)"""
    d = canvas.defs
    body = d.linear([(0, shade(color, 1.18)), (0.45, color), (1, shade(color, 0.72))])
    dome = d.radial([(0, "#ffffff", 0.22), (0.6, "#ffffff", 0.05), (1, "#ffffff", 0)], cx=0.42, cy=0.3, r=0.75)
    rim = d.linear([(0, "#000000", 0), (0.8, "#000000", 0), (1, "#000000", 0.35)])
    side = d.linear([(0, shade(color, 0.62)), (1, shade(color, 0.42))])
    return "\n".join([
        soft_shadow(x, y, w, h + skirt, radius, 1.2, dx=0.35, dy=0.7, opacity=0.55, steps=5),
        f'<path d="{rounded(x - 0.25, y + 0.5, w + 0.5, h + skirt - 0.4, radius)}" fill="{side}"/>' if skirt else "",
        f'<path d="{rounded(x, y, w, h, radius)}" fill="{body}"/>',
        f'<path d="{rounded(x, y, w, h, radius)}" fill="{rim}"/>',
        f'<path d="{rounded(x + 0.4, y + 0.3, w - 0.8, h - 0.9, radius * 0.85)}" fill="{dome}"/>',
        f'<path d="{rounded(x, y, w, h, radius)}" fill="none" stroke="{shade(color, 0.45)}" stroke-width="0.12" '
        f'stroke-opacity="0.8"/>',
    ])


def hard(canvas, x, y, w, h, color, inset=(1.1, 0.7, 1.1, 1.9), radius=0.9, top_radius=1.1, gloss=0.16,
         shadow=0.6):
    """A hard plastic cap seen from above: four lit facets of the skirt and a dished top face set back from the
    front edge. Returns the svg and the top face's rectangle (where the legends go)"""
    d = canvas.defs
    l, t, r, b = inset
    tx, ty, tw, th = x + l, y + t, w - l - r, h - t - b
    o = (x, y, x + w, y + h)
    i = (tx, ty, tx + tw, ty + th)
    facets = [
        ((o[0], o[1]), (o[2], o[1]), (i[2], i[1]), (i[0], i[1]), 1.05),   # back: lit
        ((o[0], o[1]), (i[0], i[1]), (i[0], i[3]), (o[0], o[3]), 0.85),   # left
        ((o[2], o[1]), (o[2], o[3]), (i[2], i[3]), (i[2], i[1]), 0.68),   # right
        ((o[0], o[3]), (i[0], i[3]), (i[2], i[3]), (o[2], o[3]), 0.55),   # front: away from the light
    ]
    parts = [soft_shadow(x, y, w, h, radius, 1.4, dx=0.4, dy=0.9, opacity=shadow, steps=5),
             f'<path d="{rounded(x, y, w, h, radius)}" fill="{shade(color, 0.6)}"/>']
    for *points, factor in facets:
        fill = d.linear([(0, shade(color, factor * 1.05)), (1, shade(color, factor * 0.92))])
        parts.append('<path d="M' + " L".join(f"{px:.3f},{py:.3f}" for px, py in points) + f'Z" fill="{fill}"/>')
    top = d.linear([(0, shade(color, 1.2)), (0.3, shade(color, 1.05)), (1, shade(color, 0.94))])
    dish = d.radial([(0, "#000000", 0.0), (0.75, "#000000", 0.0), (1, "#000000", 0.18)], cx=0.5, cy=0.55, r=0.7)
    sheen = d.radial([(0, "#ffffff", gloss), (1, "#ffffff", 0)], cx=0.35, cy=0.12, r=0.9)
    parts += [
        f'<path d="{rounded(tx, ty, tw, th, top_radius)}" fill="{top}"/>',
        f'<path d="{rounded(tx, ty, tw, th, top_radius)}" fill="{dish}"/>',
        f'<path d="{rounded(tx, ty, tw, th, top_radius)}" fill="{sheen}"/>',
        f'<path d="{rounded(tx, ty, tw, th, top_radius)}" fill="none" stroke="{shade(color, 1.4)}" '
        f'stroke-width="0.1" stroke-opacity="0.45"/>',
        f'<path d="{rounded(x, y, w, h, radius)}" fill="none" stroke="#000" stroke-width="0.14" stroke-opacity="0.7"/>',
    ]
    return "\n".join(parts), (tx, ty, tw, th)


def d_shape(x, y, w, h, top, bottom):
    """A rounded rectangle with small back corners (@p top) and large front ones (@p bottom): the Next's matte top"""
    top, bottom = min(top, w / 2, h / 2), min(bottom, w / 2, h / 2)
    return (f"M{x + top:.3f},{y:.3f}H{x + w - top:.3f}A{top:.3f},{top:.3f} 0 0 1 {x + w:.3f},{y + top:.3f}"
            f"V{y + h - bottom:.3f}A{bottom:.3f},{bottom:.3f} 0 0 1 {x + w - bottom:.3f},{y + h:.3f}"
            f"H{x + bottom:.3f}A{bottom:.3f},{bottom:.3f} 0 0 1 {x:.3f},{y + h - bottom:.3f}"
            f"V{y + top:.3f}A{top:.3f},{top:.3f} 0 0 1 {x + top:.3f},{y:.3f}Z")


def l_shape(points, inset, top_inset, top, bottom):
    """The Next's ENTER: the inverted L of @p points (leg top left, leg top right, bar bottom right, bar bottom left,
    bar top left, leg bottom left), inset; the bar's front corners rounded by @p bottom"""
    (lx, ly), (rx, _), (_, by), (bx, _), (_, my), _ = points
    lx, rx, bx = lx + inset, rx - inset, bx + inset
    ly, by, my = ly + top_inset, by - inset, my + top_inset
    return (f"M{lx + top:.3f},{ly:.3f}H{rx - top:.3f}A{top:.3f},{top:.3f} 0 0 1 {rx:.3f},{ly + top:.3f}"
            f"V{by - bottom:.3f}A{bottom:.3f},{bottom:.3f} 0 0 1 {rx - bottom:.3f},{by:.3f}H{bx + bottom:.3f}"
            f"A{bottom:.3f},{bottom:.3f} 0 0 1 {bx:.3f},{by - bottom:.3f}V{my + top:.3f}"
            f"A{top:.3f},{top:.3f} 0 0 1 {bx + top:.3f},{my:.3f}H{lx - top:.3f}"
            f"A{top:.3f},{top:.3f} 0 0 0 {lx:.3f},{my - top:.3f}V{ly + top:.3f}"
            f"A{top:.3f},{top:.3f} 0 0 1 {lx + top:.3f},{ly:.3f}Z")


def polygon(points):
    return "M" + " L".join(f"{px:.3f},{py:.3f}" for px, py in points) + "Z"


def next_cap(canvas, x, y, w, h, color, shape=None, unit=19.0):
    """The Next's two-tone key: a glossy black frame over the whole cell, a low matte top shaped like a D on its back
    (flat back edge, rounded front) set low in the frame; the glossy strip above it carries the E-mode legends.
    Returns the svg and the matte top's rectangle (for an L, the bar's)"""
    d = canvas.defs
    gap = 0.35
    inset, top_inset = 0.08 * unit, 0.25 * unit
    fx, fy, fw, fh = x + gap, y + gap, w - 2 * gap, h - 2 * gap
    frame = d.linear([(0, "#2a2c30"), (0.12, "#1a1b1e"), (0.85, "#121315"), (1, "#0a0b0c")])
    gloss = d.linear([(0, "#ffffff", 0.22), (0.05, "#ffffff", 0.05), (0.4, "#ffffff", 0)])
    top = d.linear([(0, shade(color, 1.12)), (0.25, color), (1, shade(color, 0.86))])
    sheen = d.radial([(0, "#ffffff", 0.07), (1, "#ffffff", 0)], cx=0.4, cy=0.2, r=0.8)
    lip = d.linear([(0, "#000000", 0), (0.82, "#000000", 0), (1, "#000000", 0.35)])
    if shape:
        outer = l_shape(shape, gap, gap, 0.8, 0.8)
        matte = l_shape(shape, gap + inset, gap + top_inset, 1.0, 0.3 * unit)
        face = (shape[3][0] + gap + inset, shape[4][1] + gap + top_inset, shape[1][0] - shape[3][0] - 2 * gap - 2 * inset,
                shape[2][1] - shape[4][1] - 2 * gap - inset - top_inset)
        drop = l_shape(shape, gap + inset - 0.15, gap + top_inset - 0.1, 1.0, 0.3 * unit)
    else:
        outer = rounded(fx, fy, fw, fh, 0.8)
        mx, my, mw, mh = fx + inset, fy + top_inset, fw - 2 * inset, fh - top_inset - inset
        matte = d_shape(mx, my, mw, mh, 1.0, 0.3 * unit)
        face = (mx, my, mw, mh)
        drop = d_shape(mx - 0.15, my - 0.1, mw + 0.3, mh + 0.45, 1.1, 0.3 * unit)
    return "\n".join([
        f'<path d="{outer}" fill="{frame}"/>',
        f'<path d="{outer}" fill="{gloss}"/>',
        f'<path d="{outer}" fill="none" stroke="#4a4d52" stroke-width="0.12" stroke-opacity="0.55"/>',
        f'<path d="{drop}" fill="#000" fill-opacity="0.55"/>',
        f'<path d="{matte}" fill="{top}"/>',
        f'<path d="{matte}" fill="{sheen}"/>',
        f'<path d="{matte}" fill="{lip}"/>',
        f'<path d="{matte}" fill="none" stroke="{shade(color, 1.35)}" stroke-width="0.1" stroke-opacity="0.35"/>',
    ]), face
