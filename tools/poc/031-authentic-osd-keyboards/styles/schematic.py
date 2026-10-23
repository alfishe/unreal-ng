"""Phase 2: schematic. The same keyboards as clean vector diagrams: flat fills in the real colours, one outline per
part and per key, no light, no shadow, no texture. The legends are the photo's, placed on the same cap faces, so a
key reads the same in both styles."""

from styles import photo
from styles.common import Picture, glyph_shape, glyphs, legends, luminance
from styles.photocaps import polygon
from svgkit import rounded, shade, text_path, text_width

LINE = 0.3  # the outline's width in mm: a hairline at the overlay's usual size


def edge(color, strength=1.0):
    """The outline of a part of @p color: lighter on a dark part, darker on a light one"""
    if luminance(color) < 0.2:
        return shade(color, 1.0 + 0.7 * strength) if luminance(color) > 0.02 else shade("#2a2b2e", 1.0 + 0.4 * strength)
    return shade(color, 1.0 - 0.28 * strength)


def outlined(d, color, line=LINE, strength=1.0, fill=None):
    return (f'<path d="{d}" fill="{fill or color}" stroke="{edge(color, strength)}" stroke-width="{line:.3f}" '
            f'stroke-linejoin="round"/>')


def box(g, default_r=1.0):
    return g["x"], g["y"], g["w"], g["h"], g.get("r", default_r)


def case_body(pic, part):
    x, y, w, h, r = box(part.geometry, 3)
    line = LINE * 1.5
    inset = line / 2  # the outline inside the body: a body as big as the picture keeps all of it
    return outlined(rounded(x + inset, y + inset, w - line, h - line, r), part.color, line)


def case_flat(pic, part):
    """A plate, panel, well, boss, tab or hump: its outline in its colour"""
    x, y, w, h, r = box(part.geometry)
    return outlined(rounded(x, y, w, h, r), part.color, strength=0.6)


def case_line(pic, part):
    """A groove: one line across the case"""
    g = part.geometry
    return (f'<rect x="{g["x"]:.3f}" y="{g["y"] + g["h"] * 0.3:.3f}" width="{g["w"]:.3f}" '
            f'height="{max(LINE, g["h"] * 0.4):.3f}" fill="{g.get("dark", edge(part.color, 0.4))}"/>')


def case_insert(pic, part):
    g = part.geometry
    return f'<path d="{rounded(g["x"], g["y"], g["w"], g["h"], g["h"] / 2)}" fill="{part.color}"/>'


def case_slots(pic, part):
    """A grille or a field of vents: the slots as outlined bars, fewer of them (a diagram, not a texture)"""
    g = part.geometry
    x, y, w, h = g["x"], g["y"], g["w"], g["h"]
    line = edge(part.color, 0.5)
    if "slots" in g:
        n = max(1, g["slots"] // 2)
        pitch = w / n
        return "".join(f'<path d="{rounded(x + i * pitch + pitch * 0.4, y, pitch * 0.2, h, pitch * 0.1)}" '
                       f'fill="none" stroke="{line}" stroke-width="{LINE:.3f}"/>' for i in range(n))
    cols, rows = g["cols"], max(1, g["rows"] // 2)
    col, pitch = w / cols, h / rows
    return "".join(f'<path d="{rounded(x + c * col + col * 0.06, y + r * pitch + pitch * 0.35, col * 0.88, pitch * 0.3, pitch * 0.15)}" '
                   f'fill="none" stroke="{line}" stroke-width="{LINE:.3f}"/>' for c in range(cols) for r in range(rows))


def case_fins(pic, part):
    """A heatsink: its outline and every other fin as a line (the fins at full count are a texture, not a diagram)"""
    g = part.geometry
    x, y, w, h, n = g["x"], g["y"], g["w"], g["h"], g["fins"]
    parts = [outlined(rounded(x, y, w, h, g.get("r", 2)), part.color, strength=0.6)]
    n = max(2, n // 2)
    pitch = h / n
    parts += [f'<rect x="{x:.3f}" y="{y + i * pitch:.3f}" width="{w:.3f}" height="{LINE:.3f}" '
              f'fill="{edge(part.color, 0.6)}"/>' for i in range(1, n)]
    return "\n".join(parts)


def case_steps(pic, part):
    g = part.geometry
    band = g["h"] / g["steps"]
    return "".join(f'<rect x="{g["x"]:.3f}" y="{g["y"] + (i + 1) * band - LINE:.3f}" width="{g["w"]:.3f}" '
                   f'height="{LINE:.3f}" fill="{edge(part.color, 0.4)}"/>' for i in range(g["steps"]))


def case_teeth(pic, part):
    g = part.geometry
    return f'<rect x="{g["x"]:.3f}" y="{g["y"]:.3f}" width="{g["w"]:.3f}" height="{g["h"]:.3f}" fill="{part.color}"/>'


def case_mesh(pic, part):
    x, y, w, h, r = box(part.geometry, 0.6)
    return outlined(rounded(x, y, w, h, r), part.color, strength=0.6)


def case_led(pic, part):
    g = part.geometry
    return f'<circle cx="{g["x"]:.3f}" cy="{g["y"]:.3f}" r="{g["r"]:.3f}" fill="{part.color}"/>'


def case_logo(pic, part):
    """A word: flat in its colour, the moulded ones (outline, emboss, gloss) too, so a diagram reads them"""
    g = dict(part.geometry)
    for effect in ("outline", "emboss", "gloss", "edge"):
        g.pop(effect, None)
    flat = photo.case_logo(pic, type(part)(part.kind, g, part.color, part.extra))
    if part.geometry.get("gloss") or part.geometry.get("outline") or part.geometry.get("emboss"):
        size = g.get("size")
        squeeze, spacing, face = g.get("squeeze", 1.0), g.get("spacing", 0.0), g.get("face", "sans-bold")
        if "width" in g:
            natural = text_width(g["text"], size or 1, face) + spacing * (len(g["text"]) - 1)
            if size:
                squeeze = g["width"] / natural
            else:
                size = (g["width"] - spacing * (len(g["text"]) - 1)) / text_width(g["text"], 1, face)
        d = text_path(g["text"], g["x"], g["y"], size, face, g.get("anchor", "start"), spacing, squeeze)
        return (f'<path d="{d}" fill="{part.color}" stroke="{edge(part.color, 1.6)}" stroke-width="{LINE * 0.8:.3f}" '
                f'stroke-linejoin="round"/>')
    return flat


def case_datacorder(pic, part):
    """The +2's deck: the lid, the window with the two spindles, the band with its rainbow and name"""
    g = part.geometry
    x, y, w, h = g["x"], g["y"], g["w"], g["h"]
    wx, wy, ww, wh = g["window"]
    bx, by, bw, bh = g["band"]
    parts = [outlined(rounded(x, y, w, h, 0.8), part.color, strength=0.6),
             outlined(rounded(wx, wy, ww, wh, 0.5), g.get("inside") or shade(part.color, 0.7), strength=0.6)]
    cy = wy + wh * 0.62
    for sx in g.get("spindles", []):
        parts.append(f'<circle cx="{wx + sx:.3f}" cy="{cy:.3f}" r="4.2" fill="#141414"/>')
    parts.append(f'<path d="{rounded(bx, by, bw, bh, 0.5)}" fill="{shade(part.color, 0.62)}"/>')
    rainbow = g["rainbow"]
    sy, sh = by + bh * 0.25, bh * 0.5
    for i, colour in enumerate(rainbow):
        parts.append(f'<rect x="{bx + 1.0:.3f}" y="{sy + i * sh / len(rainbow):.3f}" width="{bw - 2.0:.3f}" '
                     f'height="{sh / len(rainbow) + 0.02:.3f}" fill="{colour}"/>')
    size = (bw - 6.0) / text_width(g["text"], 1, "bold-italic")
    parts.append(glyphs(g["text"], bx + bw / 2, sy + sh * 0.5 + size * 0.36, size, "bold-italic", "#2a3036"))
    return "\n".join(parts)


def case_buttons(pic, part):
    """The Datacorder's piano keys: outlined caps, the symbol on the cap or on the case above it"""
    g = part.geometry
    x, y, w, h, gap = g["x"], g["y"], g["w"], g["h"], g["gap"]
    on_case = g.get("on_case", False)
    for i, symbol in enumerate(g["symbols"]):
        bx = x + i * (w + gap)
        pic.add("caps", outlined(rounded(bx, y, w, h, 0.8), part.color))
        colour = "#e03030" if symbol == "dot" else "#f2f2f2"
        size = 2.4 if symbol == "dot" else 3.4 if on_case else 4.0
        cx, cy = (bx + w / 2, y - 3.2) if on_case else (bx + w / 2, y + h * 0.45)
        pic.add("legends", glyph_shape(symbol, cx, cy, size, colour), "symbol")
    return ""


def case_fddpanel(pic, part):
    """The +3's panel: the plate, the outlined 128K, the rainbow lines, FDD and its caption"""
    g = part.geometry
    x, y, w, h = g["x"], g["y"], g["w"], g["h"]
    parts = [outlined(rounded(x, y, w, h, 0.6), part.color, strength=0.6)]
    big = text_path("128K", x + w * 0.53, y + h * 0.4, g["size"], "sans", "middle", 0.4, 1.0)
    parts.append(f'<path d="{big}" fill="none" stroke="{g["ink"]}" stroke-width="0.45" stroke-linejoin="round"/>')
    lines = g["rainbow"]
    top, band = y + h * 0.7, h * 0.17
    pitch = band / len(lines)
    for i, colour in enumerate(lines):
        lx = x + w * (0.02 + 0.012 * (len(lines) - 1 - i))
        parts.append(f'<rect x="{lx:.3f}" y="{top + i * pitch:.3f}" width="{x + w - lx:.3f}" '
                     f'height="{pitch * 0.55:.3f}" fill="{colour}"/>')
    fdd = text_path("FDD", x + w * 0.47, top + band * 0.92, band * 1.25, "black", "middle", 0.3, 1.0)
    parts.append(f'<path d="{fdd}" fill="{part.color}" stroke="#4a4c50" stroke-width="0.35" stroke-linejoin="round"/>')
    parts.append(glyphs("FLOPPY DISC DRIVE", x + w * 0.47, y + h * 0.965, 3.0, "arial-bold", "#e8e8e8"))
    return "\n".join(parts)


CASE = {"body": case_body, "plate": case_flat, "panel": case_flat, "well": case_flat, "boss": case_flat,
        "tab": case_flat, "hump": case_flat, "groove": case_line, "insert": case_insert, "grille": case_slots,
        "vents": case_slots, "fins": case_fins, "steps": case_steps, "teeth": case_teeth, "mesh": case_mesh,
        "led": case_led, "logo": case_logo, "stripe": photo.case_stripe, "print": photo.case_print,
        "ring": photo.case_ring, "datacorder": case_datacorder, "buttons": case_buttons, "fddpanel": case_fddpanel}


def face_of(keyboard, key):
    """The cap's top face as the photo draws it: the legends sit where they sit in the photo"""
    return photo.cap(Picture(keyboard), key)[1]


def key_outline(key):
    x, y, w, h = key.rect
    if key.shape:
        return polygon(key.shape)
    return rounded(x, y, w, h, min(1.2, w * 0.08, h * 0.08))


def cap(pic, key):
    """A key: its outline filled with the cap's colour; a key with a top face (hard caps, the Soviet and PC ones)
    gets the face as a second, fainter outline, so the board still reads as keys at a glance"""
    face = face_of(pic.keyboard, key)
    parts = [outlined(key_outline(key), key.color)]
    x, y, w, h = key.rect
    fx, fy, fw, fh = face
    if key.cap != "mech" and not key.shape and (fx - x > 0.5 or fy - y > 0.5) and fw > 2 and fh > 2:
        parts.append(f'<path d="{rounded(fx, fy, fw, fh, min(1.0, fw * 0.08))}" fill="none" '
                     f'stroke="{edge(key.color, 0.45)}" stroke-width="{LINE * 0.7:.3f}"/>')
    return "\n".join(parts), face


def draw(keyboard, model):
    pic = Picture(keyboard)
    for part in model.case:
        pic.add("case", CASE[part.kind](pic, part))
    for key in model.keys:
        svg, face = cap(pic, key)
        pic.add("caps", svg)
        legends(pic, key, face)
    return pic.svg()
