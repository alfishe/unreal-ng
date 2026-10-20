"""Phase 1: photorealistic. Real colours, lit materials, soft shadows, all within what QSvgRenderer draws."""

from styles.common import Picture, glyph_shape, glyphs, legend_origin
from styles.photocaps import amstrad, hard, next_cap, rubber, soviet
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


def case_fins(pic, part):
    """A finned heatsink seen from above: fins across, each lit on its rear edge, dark gaps between"""
    g = part.geometry
    d = pic.defs
    x, y, w, h, n = g["x"], g["y"], g["w"], g["h"], g["fins"]
    pitch = h / n
    fin = d.linear([(0, shade(part.color, 1.3)), (0.3, shade(part.color, 1.08)), (1, shade(part.color, 0.8))])
    end = d.linear([(0, "#000000", 0), (0.7, "#000000", 0.1), (1, "#000000", 0.5)], x1=0, y1=0, x2=1, y2=0)
    parts = [f'<path d="{rounded(x, y, w, h, g.get("r", 2))}" fill="#050506"/>']
    for i in range(n):
        parts.append(f'<rect x="{x:.3f}" y="{y + i * pitch + pitch * 0.12:.3f}" width="{w:.3f}" '
                     f'height="{pitch * 0.52:.3f}" fill="{fin}"/>')
    parts.append(f'<path d="{rounded(x, y, w, h, g.get("r", 2))}" fill="{end}"/>')
    return "\n".join(parts)


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
    if g.get("stroke"):  # printed as an outline (the 128's "128K")
        d = text_path(g["text"], *args, kw["anchor"], spacing, squeeze)
        return (f'<path d="{d}" fill="none" stroke="{part.color}" stroke-width="{g["stroke"]}" '
                f'stroke-linejoin="round"/>')
    if g.get("outline"):  # moulded into the case: a double contour, lit above and shaded below
        d = text_path(g["text"], *args, kw["anchor"], spacing, squeeze)
        return "\n".join(f'<path d="{d}" transform="translate(0,{dy})" fill="none" stroke="{colour}" '
                          f'stroke-width="{width}" stroke-opacity="{opacity}" stroke-linejoin="round"/>'
                          for dy, colour, width, opacity in [(0.25, "#000000", 0.8, 0.6), (-0.15, "#4a4a48", 0.45, 0.5),
                                                             (0, part.color, 0.45, 1)])
    if g.get("emboss"):  # thin letters moulded in relief: a shadow under each stroke, a faint lit top
        return "\n".join([
            glyphs(g["text"], g["x"] + 0.1, g["y"] + 0.25, size, face, "#000", opacity=0.45, **kw),
            glyphs(g["text"], g["x"], g["y"] - 0.12, size, face, "#ffffff", opacity=0.07, **kw),
            glyphs(g["text"], *args, part.color, **kw),
        ])
    if not g.get("gloss"):
        return glyphs(g["text"], *args, part.color, **kw)
    fill = pic.defs.linear([(0, "#3c3e42"), (0.35, part.color), (0.7, "#0a0a0b"), (1, "#26282b")])
    return "\n".join([
        glyphs(g["text"], g["x"], g["y"] + 0.35, size, face, "#000", opacity=0.6, **kw),
        glyphs(g["text"], g["x"], g["y"] - 0.2, size, face, "#8a8d92", opacity=0.45, **kw),
        glyphs(g["text"], *args, fill, **kw),
    ])


def case_teeth(pic, part):
    """A castellated edge (the Amstrad's rear and front): small notches cut into the case's edge, @p pitch apart"""
    g = part.geometry
    x, y, w, h, pitch = g["x"], g["y"], g["w"], g["h"], g["pitch"]
    notch = pitch * 0.45
    parts = []
    for i in range(int((w - notch) / pitch) + 1):
        nx = x + i * pitch
        parts.append(f'<rect x="{nx:.3f}" y="{y:.3f}" width="{notch:.3f}" height="{h:.3f}" fill="{part.color}"/>')
        parts.append(f'<rect x="{nx + notch:.3f}" y="{y:.3f}" width="0.22" height="{h:.3f}" fill="#ffffff" '
                     f'fill-opacity="0.12"/>')
    return "\n".join(parts)


def case_datacorder(pic, part):
    """The +2's cassette deck seen from above: a lid of the case's plastic set into the case, its clear window over
    the cassette well (the two black spindles, the grey plate between them), the dark band with DATACORDER in dark
    italics over rainbow stripes, the microphone hole under it"""
    g = part.geometry
    d = pic.defs
    x, y, w, h = g["x"], g["y"], g["w"], g["h"]
    wx, wy, ww, wh = g["window"]
    bx, by, bw, bh = g["band"]
    lid = d.linear([(0, shade(part.color, 1.04)), (0.1, part.color), (1, shade(part.color, 0.96))])
    inside = g.get("inside")  # the well's plastic: the lid's, darker, unless the deck sets its own
    well = d.linear([(0, shade(inside, 0.9)), (1, shade(inside, 1.05))] if inside else
                    [(0, shade(part.color, 0.62)), (1, shade(part.color, 0.78))])
    glare = d.linear([(0, "#ffffff", 0.0), (0.3, "#ffffff", 0.04), (0.45, "#ffffff", 0.0)], x1=0, y1=0, x2=1, y2=1)
    parts = [
        f'<path d="{rounded(x - 0.5, y - 0.5, w + 1.0, h + 1.0, 1.2)}" fill="#000" fill-opacity="0.45"/>',
        f'<path d="{rounded(x, y, w, h, 0.8)}" fill="{lid}"/>',
        f'<path d="{rounded(wx, wy, ww, wh, 0.5)}" fill="{well}"/>',
        f'<rect x="{wx:.3f}" y="{wy:.3f}" width="{ww:.3f}" height="1.2" fill="#000" fill-opacity="0.35"/>',
    ]
    lw, lh = g["label"]
    cy = wy + wh * 0.62
    parts.append(f'<path d="{rounded(wx + ww / 2 - lw / 4, cy - lh / 4, lw / 2, lh / 2, 0.6)}" '
                 f'fill="{g.get("plate") or shade(part.color, 1.3)}"/>')
    for sx in g.get("spindles", []):
        cx = wx + sx
        parts.append(f'<circle cx="{cx + 0.3:.3f}" cy="{cy + 0.5:.3f}" r="4.4" fill="#000" fill-opacity="0.4"/>')
        parts.append(f'<circle cx="{cx:.3f}" cy="{cy:.3f}" r="4.2" fill="#141414"/>')
        parts.append(f'<circle cx="{cx - 0.6:.3f}" cy="{cy - 0.6:.3f}" r="0.6" fill="#5a5a5a"/>')
    parts += [
        f'<path d="{rounded(wx, wy, ww, wh, 0.5)}" fill="none" stroke="#000" stroke-width="0.3" stroke-opacity="0.5"/>',
        f'<path d="{rounded(bx, by, bw, bh, 0.5)}" fill="{shade(part.color, 0.62)}"/>',
    ]
    pic.add("highlights", f'<path d="{rounded(wx, wy, ww, wh, 0.5)}" fill="{glare}"/>')
    rainbow = g["rainbow"]  # the stripes run across the band behind the letters, red at the top
    sy, sh = by + bh * 0.25, bh * 0.5
    for i, colour in enumerate(rainbow):
        parts.append(f'<rect x="{bx + 1.0:.3f}" y="{sy + i * sh / len(rainbow):.3f}" width="{bw - 2.0:.3f}" '
                     f'height="{sh / len(rainbow) + 0.02:.3f}" fill="{colour}"/>')
    text = g["text"]
    size = (bw - 6.0) / text_width(text, 1, "bold-italic")
    baseline = sy + sh * 0.5 + size * 0.36
    dpath = text_path(text, bx + bw / 2, baseline, size, "bold-italic", "middle", 0.0, 1.0)
    parts.append(f'<path d="{dpath}" fill="#2a3036" stroke="#15171a" stroke-width="0.25" stroke-linejoin="round"/>')
    mx, my = g["mic"]
    parts.append(f'<circle cx="{mx:.3f}" cy="{my:.3f}" r="0.9" fill="#050505"/>')
    return "\n".join(parts)


def case_buttons(pic, part):
    """The Datacorder's piano keys: dark Amstrad caps in a row, each with its symbol on the cap (the +2) or printed
    on the case above it (the +2A)"""
    g = part.geometry
    x, y, w, h, gap = g["x"], g["y"], g["w"], g["h"], g["gap"]
    on_case = g.get("on_case", False)
    for i, symbol in enumerate(g["symbols"]):
        bx = x + i * (w + gap)
        svg, face = amstrad(pic, bx, y, w, h, part.color, skirt=0.42)
        pic.add("caps", svg)
        colour = "#e03030" if symbol == "dot" else "#f2f2f2"
        size = 2.4 if symbol == "dot" else 3.4 if on_case else 4.0
        fx, fy, fw, fh = face
        cx, cy = (bx + w / 2, y - 3.2) if on_case else (fx + fw * 0.5, fy + fh * 0.5)
        pic.add("legends", glyph_shape(symbol, cx, cy, size, colour))
    return ""


def case_fddpanel(pic, part):
    """The +3's label panel over the disc drive: a flat black plate, the big outlined 128K, the thin rainbow lines
    running out to its right edge with FDD in dark outline over them, FLOPPY DISC DRIVE under them"""
    g = part.geometry
    d = pic.defs
    x, y, w, h = g["x"], g["y"], g["w"], g["h"]
    plate = d.linear([(0, shade(part.color, 1.12)), (0.15, part.color), (1, shade(part.color, 0.9))])
    parts = [
        f'<path d="{rounded(x - 0.5, y - 0.5, w + 1.0, h + 1.0, 1.0)}" fill="#000" fill-opacity="0.5"/>',
        f'<path d="{rounded(x, y, w, h, 0.6)}" fill="{plate}"/>',
        f'<path d="{rounded(x, y, w, h, 0.6)}" fill="none" stroke="{shade(part.color, 1.6)}" stroke-width="0.2" '
        f'stroke-opacity="0.4"/>',
    ]
    big = text_path("128K", x + w * 0.53, y + h * 0.4, g["size"], "sans", "middle", 0.4, 1.0)
    parts.append(f'<path d="{big}" fill="none" stroke="{g["ink"]}" stroke-width="0.45" stroke-linejoin="round"/>')
    lines = g["rainbow"]
    top, band = y + h * 0.7, h * 0.17
    pitch = band / len(lines)
    for i, colour in enumerate(lines):  # each line starts further left going down: the band's slanted left end
        ly = top + i * pitch
        lx = x + w * (0.02 + 0.012 * (len(lines) - 1 - i))
        parts.append(f'<rect x="{lx:.3f}" y="{ly:.3f}" width="{x + w - lx:.3f}" height="{pitch * 0.55:.3f}" '
                     f'fill="{colour}"/>')
    fdd = text_path("FDD", x + w * 0.47, top + band * 0.92, band * 1.25, "black", "middle", 0.3, 1.0)
    parts.append(f'<path d="{fdd}" fill="{part.color}" fill-opacity="0.85" stroke="#4a4c50" stroke-width="0.35" '
                 f'stroke-linejoin="round"/>')
    parts.append(glyphs("FLOPPY DISC DRIVE", x + w * 0.47, y + h * 0.965, 3.0, "arial-bold", "#e8e8e8"))
    return "\n".join(parts)


def case_panel(pic, part):
    """A flat plate recessed into the case (the Pentagon 2+'s name plate): flat, a thin dark rim, no sheen"""
    g = part.geometry
    x, y, w, h, r = g["x"], g["y"], g["w"], g["h"], g.get("r", 0.6)
    return "\n".join([
        f'<path d="{rounded(x - 0.5, y - 0.5, w + 1.0, h + 1.0, r + 0.5)}" fill="{shade(part.color, 0.6)}"/>',
        f'<path d="{rounded(x, y, w, h, r)}" fill="{part.color}"/>',
        f'<path d="{rounded(x, y, w, h, r)}" fill="none" stroke="{shade(part.color, 1.5)}" stroke-width="0.12" '
        f'stroke-opacity="0.3"/>',
    ])



def case_boss(pic, part):
    """A raised flat moulding of the case's plastic (the Scorpion's logo plaque, its ribs): matte, its top edge
    faintly lit, a narrow shadow under its front edge"""
    g = part.geometry
    x, y, w, h, r = g["x"], g["y"], g["w"], g["h"], g.get("r", 0.6)
    lip = min(0.35, h * 0.3)
    return "\n".join([
        f'<path d="{rounded(x + 0.1, y + 0.3, w, h + 0.4, r)}" fill="#000" fill-opacity="0.35"/>',
        f'<path d="{rounded(x, y, w, h, r)}" fill="{part.color}"/>',
        f'<rect x="{x + r:.3f}" y="{y:.3f}" width="{w - 2 * r:.3f}" height="{lip:.3f}" fill="#ffffff" '
        f'fill-opacity="0.06"/>',
    ])


def case_vents(pic, part):
    """A field of vent slots across the case (the Quorum's top): @p rows slots front to back in each of @p cols
    columns, each a dark groove with a faintly lit front lip"""
    g = part.geometry
    x, y, w, h, cols, rows = g["x"], g["y"], g["w"], g["h"], g["cols"], g["rows"]
    pitch, col = h / rows, w / cols
    slot, gap = pitch * 0.32, min(1.2, col * 0.06)
    dark, lip = shade(part.color, 0.62), shade(part.color, 1.06)
    parts = []
    for c in range(cols):
        sx, sw = x + c * col + gap / 2, col - gap
        for r in range(rows):
            sy = y + r * pitch + (pitch - slot) / 2
            parts.append(f'<path d="{rounded(sx, sy, sw, slot, slot / 2)}" fill="{dark}"/>')
            parts.append(f'<rect x="{sx + slot / 2:.3f}" y="{sy + slot:.3f}" width="{sw - slot:.3f}" height="0.25" '
                         f'fill="{lip}"/>')
    return "\n".join(parts)


def case_steps(pic, part):
    """Ledges stepping down to the front (the Quorum's raised rear onto its keyboard deck): @p steps bands, each a
    faint lit top and a shadow line under its front edge"""
    g = part.geometry
    x, y, w, h, n = g["x"], g["y"], g["w"], g["h"], g["steps"]
    band = h / n
    parts = []
    for i in range(n):
        by = y + i * band
        parts.append(f'<rect x="{x:.3f}" y="{by:.3f}" width="{w:.3f}" height="{band * 0.25:.3f}" fill="#ffffff" '
                     f'fill-opacity="0.05"/>')
        parts.append(f'<rect x="{x:.3f}" y="{by + band - 0.5:.3f}" width="{w:.3f}" height="0.5" fill="#000" '
                     f'fill-opacity="0.18"/>')
    return "\n".join(parts)


def case_ring(pic, part):
    """A printed ring (the Quorum's emblem)"""
    g = part.geometry
    return (f'<circle cx="{g["x"]:.3f}" cy="{g["y"]:.3f}" r="{g["r"]:.3f}" fill="none" stroke="{part.color}" '
            f'stroke-width="{g["width"]:.3f}"/>')

CASE = {"ring": case_ring, "vents": case_vents, "steps": case_steps, "boss": case_boss, "panel": case_panel, "fddpanel": case_fddpanel, "teeth": case_teeth, "datacorder": case_datacorder, "buttons": case_buttons, "body": case_body, "plate": case_plate, "stripe": case_stripe, "logo": case_logo, "well": case_well,
        "groove": case_groove, "insert": case_insert, "grille": case_grille, "tab": case_tab, "led": case_led,
        "hump": case_hump, "fins": case_fins}


def lit(pic, key):
    """The key's colour as the photo shows it: the light falls off from the top left across the board, and no two caps
    of an old keyboard have quite the same tint (a fixed spread per key, ±1.5 %)"""
    W, H = pic.keyboard["size"]
    x, y, w, h = key.rect
    falloff = 1.03 - 0.06 * (x + w / 2) / W - 0.04 * (y + h / 2) / H
    spread = (sum(map(ord, key.id)) * 37 % 31 - 15) / 1000
    return shade(key.color, falloff + spread)


def cap(pic, key):
    x, y, w, h = key.rect
    if key.cap == "rubber":
        return rubber(pic, x, y, w, h, key.color, skirt=pic.keyboard.get("skirt", 0.0)), (x, y, w, h)
    if key.cap == "hard":
        return hard(pic, x, y, w, h, key.color)
    if key.cap == "next":
        return next_cap(pic, x, y, w, h, key.color, key.shape)
    if key.cap == "plus":  # the Spectrum+ / 128 key: a satin block, the convex glossy pad in front
        return next_cap(pic, x, y, w, h, key.color, key.shape, top_inset=0.31, gloss=0.03, sheen=0.3, relief=(1.05, 0.92), rim=0.15,
                        frame=[(0, "#2c2d30"), (0.1, "#202124"), (0.9, "#18191b"), (1, "#0e0f10")])
    if key.cap == "mech":  # a low black cap of a Soviet mechanical switch, its sticker on the top face
        return hard(pic, x, y, w, h, key.color, inset=(0.9, 0.6, 0.9, 1.3), radius=1.0, top_radius=1.2, gloss=0.05)
    if key.cap == "soviet":
        return soviet(pic, x, y, w, h, lit(pic, key), key.shape, depth=pic.keyboard.get("depth"))
    if key.cap == "amstrad":
        return amstrad(pic, x, y, w, h, key.color, key.shape)
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
