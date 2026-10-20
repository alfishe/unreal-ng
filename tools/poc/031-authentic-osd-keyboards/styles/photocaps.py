"""Key cap materials, each a function of the canvas (for its gradients) and the key's rectangle in mm.

A material draws the cap's form: its colour, its light and shade. The specular highlight (the sheen on a glossy
top, the dome on a rubber key) goes to the canvas's own `highlights` layer, which the picture draws at a set
opacity and an app can turn down or leave out.

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
    body = d.linear([(0, shade(color, 1.07)), (0.5, color), (1, shade(color, 0.86))])
    dome = d.radial([(0, "#ffffff", 0.22), (0.6, "#ffffff", 0.05), (1, "#ffffff", 0)], cx=0.42, cy=0.3, r=0.75)
    rim = d.linear([(0, "#000000", 0), (0.8, "#000000", 0), (1, "#000000", 0.2)])
    side = d.linear([(0, shade(color, 0.62)), (1, shade(color, 0.42))])
    canvas.add("highlights", f'<path d="{rounded(x + 0.4, y + 0.3, w - 0.8, h - 0.9, radius * 0.85)}" fill="{dome}"/>')
    return "\n".join([
        soft_shadow(x, y, w, h + skirt, radius, 1.2, dx=0.35, dy=0.7, opacity=0.55, steps=5),
        f'<path d="{rounded(x - 0.25, y + 0.5, w + 0.5, h + skirt - 0.4, radius)}" fill="{side}"/>' if skirt else "",
        f'<path d="{rounded(x, y, w, h, radius)}" fill="{body}"/>',
        f'<path d="{rounded(x, y, w, h, radius)}" fill="{rim}"/>',
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
        f'<path d="{rounded(tx, ty, tw, th, top_radius)}" fill="none" stroke="{shade(color, 1.4)}" '
        f'stroke-width="0.1" stroke-opacity="0.45"/>',
        f'<path d="{rounded(x, y, w, h, radius)}" fill="none" stroke="#000" stroke-width="0.14" stroke-opacity="0.7"/>',
    ]
    canvas.add("highlights", f'<path d="{rounded(tx, ty, tw, th, top_radius)}" fill="{sheen}"/>')
    return "\n".join(parts), (tx, ty, tw, th)


def d_shape(x, y, w, h, top, bottom):
    """A rounded rectangle with small back corners (@p top) and large front ones (@p bottom): the Next's matte top"""
    top, bottom = min(top, w / 2, h / 2), min(bottom, w / 2, h / 2)
    return (f"M{x + top:.3f},{y:.3f}H{x + w - top:.3f}A{top:.3f},{top:.3f} 0 0 1 {x + w:.3f},{y + top:.3f}"
            f"V{y + h - bottom:.3f}A{bottom:.3f},{bottom:.3f} 0 0 1 {x + w - bottom:.3f},{y + h:.3f}"
            f"H{x + bottom:.3f}A{bottom:.3f},{bottom:.3f} 0 0 1 {x:.3f},{y + h - bottom:.3f}"
            f"V{y + top:.3f}A{top:.3f},{top:.3f} 0 0 1 {x + top:.3f},{y:.3f}Z")


def l_shape(points, inset, top_inset, top, bottom, front=None):
    """The Next's ENTER: the inverted L of @p points (leg top left, leg top right, bar bottom right, bar bottom left,
    bar top left, leg bottom left), inset (by @p front at the front, @p inset if not given); the bar's front corners
    rounded by @p bottom"""
    (lx, ly), (rx, _), (_, by), (bx, _), (_, my), _ = points
    lx, rx, bx = lx + inset, rx - inset, bx + inset
    ly, by, my = ly + top_inset, by - (inset if front is None else front), my + top_inset
    return (f"M{lx + top:.3f},{ly:.3f}H{rx - top:.3f}A{top:.3f},{top:.3f} 0 0 1 {rx:.3f},{ly + top:.3f}"
            f"V{by - bottom:.3f}A{bottom:.3f},{bottom:.3f} 0 0 1 {rx - bottom:.3f},{by:.3f}H{bx + bottom:.3f}"
            f"A{bottom:.3f},{bottom:.3f} 0 0 1 {bx:.3f},{by - bottom:.3f}V{my + top:.3f}"
            f"A{top:.3f},{top:.3f} 0 0 1 {bx + top:.3f},{my:.3f}H{lx - top:.3f}"
            f"A{top:.3f},{top:.3f} 0 0 0 {lx:.3f},{my - top:.3f}V{ly + top:.3f}"
            f"A{top:.3f},{top:.3f} 0 0 1 {lx + top:.3f},{ly:.3f}Z")


def polygon(points):
    return "M" + " L".join(f"{px:.3f},{py:.3f}" for px, py in points) + "Z"


def next_cap(canvas, x, y, w, h, color, shape=None, unit=19.0, top_inset=0.25, frame=None, gloss=0.22, sheen=0.07,
             relief=(1.12, 0.86), rim=0.35):
    """The Next's two-tone key: a glossy black frame over the whole cell, a low matte top shaped like a D on its back
    (flat back edge, rounded front) set low in the frame; the glossy strip above it carries the E-mode legends.
    The Spectrum+ / 128 key is the same shape with other finishes: a satin @p frame and ledge, a convex glossy pad
    (@p sheen its highlight), the pad starting @p top_inset u down the key; @p relief the top's light at its back
    and front, @p rim its edge's. Returns the svg and the top's rectangle
    (for an L, the bar's)"""
    d = canvas.defs
    gap = 0.35
    inset, top_inset = 0.08 * unit, top_inset * unit
    fx, fy, fw, fh = x + gap, y + gap, w - 2 * gap, h - 2 * gap
    frame = d.linear(frame or [(0, "#2a2c30"), (0.12, "#1a1b1e"), (0.85, "#121315"), (1, "#0a0b0c")])
    gloss = d.linear([(0, "#ffffff", gloss), (0.05, "#ffffff", round(gloss / 4.4, 3)), (0.4, "#ffffff", 0)])
    top = d.linear([(0, shade(color, relief[0])), (0.25, color), (1, shade(color, relief[1]))])
    sheen = d.radial([(0, "#ffffff", sheen), (1, "#ffffff", 0)], cx=0.4, cy=0.2, r=0.8)
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
    canvas.add("highlights", f'<path d="{matte}" fill="{sheen}"/>')
    return "\n".join([
        f'<path d="{outer}" fill="{frame}"/>',
        f'<path d="{outer}" fill="{gloss}"/>',
        f'<path d="{outer}" fill="none" stroke="#4a4d52" stroke-width="0.12" stroke-opacity="0.55"/>',
        f'<path d="{drop}" fill="#000" fill-opacity="0.55"/>',
        f'<path d="{matte}" fill="{top}"/>',
        f'<path d="{matte}" fill="{lip}"/>',
        f'<path d="{matte}" fill="none" stroke="{shade(color, 1.35)}" stroke-width="0.1" stroke-opacity="{rim}"/>',
    ]), face


def amstrad(canvas, x, y, w, h, color, shape=None, unit=19.0, skirt=0.27, shadow=0.55):
    """The Amstrad +2 / +3 key (and the Datacorder's buttons): a sculpted typewriter cap seen from above and a little
    in front, the top face a little narrower than the base, slightly dished, the front skirt showing below it as a
    darker band (@p skirt of the key's height). Returns the svg and the top face's rectangle (for an L, the bar's)"""
    d = canvas.defs
    gap, side, back = 0.4, 1.1, 0.6
    front = skirt * (unit if shape else h)
    body = d.linear([(0, shade(color, 0.9)), (0.6, shade(color, 0.72)), (0.75, shade(color, 0.55)), (1, shade(color, 0.4))])
    flanks = d.linear([(0, "#ffffff", 0.02), (0.06, "#ffffff", 0), (0.94, "#000000", 0), (1, "#000000", 0.28)],
                      x1=0, y1=0, x2=1, y2=0)
    top = d.linear([(0, shade(color, 1.05)), (0.35, shade(color, 1.0)), (1, shade(color, 0.95))])
    dish = d.radial([(0, "#000000", 0), (0.7, "#000000", 0), (1, "#000000", 0.16)], cx=0.5, cy=0.45, r=0.72)
    sheen = d.radial([(0, "#ffffff", 0.05), (1, "#ffffff", 0)], cx=0.35, cy=0.15, r=0.9)
    if shape:
        outer = l_shape(shape, gap, gap, 1.0, 1.0)
        face_path = l_shape(shape, gap + side, gap + back, 1.4, 1.4, front=gap + front)
        (_, _), (rx, _), (_, by), (bx, _), (_, my), _ = shape
        face = (bx + gap + side, my + gap + back, rx - bx - 2 * (gap + side), by - my - 2 * gap - back - front)
        shadow_svg = f'<path d="{l_shape(shape, gap - 0.6, gap - 0.2, 1.4, 1.4, front=gap - 1.0)}" fill="#000" ' \
                     f'fill-opacity="{shadow * 0.6}"/>'
    else:
        ox, oy, ow, oh = x + gap, y + gap, w - 2 * gap, h - 2 * gap
        outer = rounded(ox, oy, ow, oh, 1.0)
        face = (ox + side, oy + back, ow - 2 * side, oh - back - front)
        face_path = rounded(*face, 1.4)
        shadow_svg = soft_shadow(ox, oy, ow, oh, 1.0, 1.0, dx=0.3, dy=0.8, opacity=shadow, steps=4)
    canvas.add("highlights", f'<path d="{face_path}" fill="{sheen}"/>')
    return "\n".join([
        shadow_svg,
        f'<path d="{outer}" fill="{body}"/>',
        f'<path d="{outer}" fill="{flanks}"/>',
        f'<path d="{outer}" fill="none" stroke="#000" stroke-width="0.14" stroke-opacity="0.6"/>',
        f'<path d="{face_path}" fill="{top}"/>',
        f'<path d="{face_path}" fill="{dish}"/>',
        f'<path d="{face_path}" fill="none" stroke="{shade(color, 1.25)}" stroke-width="0.12" stroke-opacity="0.3"/>',
    ]), face


def tint(color, factors):
    """@p color with each channel scaled by its factor: a warm shadow on aged plastic darkens the blue most. A
    neutral plastic (the grey caps) takes the factors' mean, a warm one all of their spread"""
    color = color.lstrip("#")
    warmth = min(1.0, max(0.0, (int(color[0:2], 16) - int(color[4:6], 16)) / 50))
    mean = sum(factors) / 3
    factors = [mean + (f - mean) * warmth for f in factors]
    return "#" + "".join(f"{min(255, round(int(color[i:i + 2], 16) * f)):02x}" for i, f in zip((0, 2, 4), factors))


def soviet(canvas, x, y, w, h, color, shape=None, side=0.14, back=0.04, front=0.3, depth=None):
    """A Soviet sculpted cap (the MS 7004 family's, the Pentagon 2+'s) seen from above: a flat top face, about two
    thirds of the key, nearly flush with its back; round it the cap's sides, the narrow left and right ones turning
    darker towards their base, the tall front slope a little darker than the top (its legend is printed on it).
    @p side, @p back, @p front the sides' share of the key, of @p depth mm at most (a two-row key keeps a one-row
    slope). The sides are one moulded surface, so it is drawn as one: a body darkening towards the front with the
    left and right falloff laid over it, no seams at the corners. The top face is a shallow cylinder, its edge a soft
    rounded turn; the cap stands in a soft contact shadow. Matte: no highlight. Returns the svg and the top face's rectangle (for an L, the bar's)"""
    d = canvas.defs
    gap = 0.45
    # the sides as the photo shows them, against the top face: the front slope a warmer, darker tan, the left side
    # (away from the light) much darker, the right one between
    front_c, left_c, right_c = (tint(color, k) for k in ((0.88, 0.865, 0.82), (0.71, 0.655, 0.555),
                                                         (0.835, 0.74, 0.63)))
    body = d.linear([(0, left_c), (side, front_c), (1 - side, front_c), (1, right_c)], x1=0, y1=0, x2=1, y2=0)
    flanks = d.linear([(0, "#000000", 0.06), (0.3, "#000000", 0.0), (1, "#000000", 0.08)])
    top = d.linear([(0, shade(color, 1.02)), (0.45, shade(color, 1.0)), (1, shade(color, 0.975))])
    edge = d.linear([(0, shade(color, 0.95)), (0.7, shade(color, 0.85)), (1, tint(color, (0.8, 0.76, 0.68)))])
    if shape:  # the L: an inset L for the top face
        outer = l_shape(shape, gap, gap, 0.8, 0.8)
        unit = shape[1][0] - shape[0][0]
        sd, b, f = side * unit, back * unit, front * unit
        face_path = l_shape(shape, gap + sd, gap + b, 1.2, 1.2, front=gap + f)
        (_, _), (rx, _), (_, by), (bx, _), (_, my), _ = shape
        face = (bx + gap + sd, my + gap + b, rx - bx - 2 * (gap + sd), by - my - 2 * gap - b - f)
        parts = [f'<path d="{l_shape(shape, gap - 0.5, gap - 0.1, 1.0, 1.0, front=gap - 0.9)}" fill="#000" '
                 f'fill-opacity="0.22"/>',
                 f'<path d="{l_shape(shape, gap - 0.2, gap, 0.8, 0.8, front=gap - 0.45)}" fill="#000" '
                 f'fill-opacity="0.25"/>']
    else:
        ox, oy, ow, oh = x + gap, y + gap, w - 2 * gap, h - 2 * gap
        sd, b, f = side * min(ow, oh), back * min(oh, depth or oh), front * min(oh, depth or oh)
        face = (ox + sd, oy + b, ow - 2 * sd, oh - b - f)
        outer = rounded(ox, oy, ow, oh, 0.8)
        face_path = rounded(*face, 1.6)
        parts = [soft_shadow(ox, oy, ow, oh, 0.8, 0.9, dx=0.25, dy=0.55, opacity=0.5, steps=4)]
    parts += [
        f'<path d="{outer}" fill="{body}"/>',
        f'<path d="{outer}" fill="{flanks}"/>',
        f'<path d="{outer}" fill="none" stroke="#000" stroke-width="0.12" stroke-opacity="0.4"/>',
        f'<path d="{face_path}" fill="{top}"/>',
        f'<path d="{face_path}" fill="none" stroke="{edge}" stroke-width="0.3" stroke-opacity="0.8"/>',
    ]
    return "\n".join(parts), face
