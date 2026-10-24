"""What the pads share: the hotspots of a face diamond, a D-pad, a stick, shoulder pairs; the system glyphs."""

import style
from svgkit import circle_d, num, rounded_d


def face_diamond(view, cx, cy, offset, r, labels, sides="right"):
    """Four face buttons: @p labels maps north/south/west/east to (legend, colour, glyph drawer or None)"""
    for control, dx, dy in (("north", 0, -1), ("west", -1, 0), ("east", 1, 0), ("south", 0, 1)):
        x, y = cx + dx * offset, cy + dy * offset
        spec = labels[control]
        style.button(view, x, y, r, spec["cap"], control)
        if spec.get("draw"):
            spec["draw"](view, x, y, r, control)
        else:
            style.label(view, spec["text"], x, y, r * 1.12, spec["ink"], control,
                        face=spec.get("face", "sans-bold"))
        view.spot(control, spec["name"], ("circle", x, y, r + 0.4), sides)


def dpad_spots(view, cx, cy, span, arm, side="left", prefix="dpad", label="D-pad", group=None):
    """The four directions as the arms of a cross (the widget lights a rectangle per direction)"""
    s, a = span / 2, arm / 2
    for control, x, y, w, h in ((f"{prefix}up", cx - a, cy - s, arm, s - a),
                                (f"{prefix}down", cx - a, cy + a, arm, s - a),
                                (f"{prefix}left", cx - s, cy - a, s - a, arm),
                                (f"{prefix}right", cx + a, cy - a, s - a, arm)):
        view.spot(control, label, ("rect", x, y, w, h), side, group=group or prefix.rstrip(".") or "directions")


def stick_spots(view, name, label, cx, cy, r, travel, side):
    view.spot(f"{name}.x", label, ("circle", cx, cy, r), side,
              analog={"kind": "stick", "pair": f"{name}.y", "radius": travel}, group=name)
    view.spot(f"{name}.click", label, ("circle", cx, cy, r * 0.6), side, group=name)


def view_glyph(view, cx, cy, size, color, control):
    """Two overlapping windows (Xbox View)"""
    w, h = size * 0.62, size * 0.46
    d = (rounded_d(cx - w * 0.72, cy - h * 0.75, w, h, 0.25) + rounded_d(cx - w * 0.28, cy - h * 0.25, w, h, 0.25))
    style.glyph(view, d, color, control, width=size * 0.11)


def menu_glyph(view, cx, cy, size, color, control):
    """Three bars (Xbox Menu)"""
    w = size * 0.62
    d = "".join(f"M{num(cx - w / 2)},{num(cy + dy * size * 0.24)}H{num(cx + w / 2)}" for dy in (-1, 0, 1))
    style.glyph(view, d, color, control, width=size * 0.12)


def share_glyph(view, cx, cy, size, color, control):
    """An arrow out of a tray (Xbox Share)"""
    s = size
    d = (f"M{num(cx)},{num(cy + s * 0.18)}V{num(cy - s * 0.42)}M{num(cx - s * 0.22)},{num(cy - s * 0.2)}"
         f"L{num(cx)},{num(cy - s * 0.42)}L{num(cx + s * 0.22)},{num(cy - s * 0.2)}"
         f"M{num(cx - s * 0.36)},{num(cy - s * 0.02)}V{num(cy + s * 0.36)}H{num(cx + s * 0.36)}V{num(cy - s * 0.02)}")
    style.glyph(view, d, color, control, width=size * 0.1)


def neutral_guide(view, cx, cy, r, cap, ring, control):
    """The guide button with its logo removed (D5): a plain cap and a thin neutral ring"""
    style.button(view, cx, cy, r, cap, control)
    style.glyph(view, circle_d(cx, cy, r * 0.62), ring, control, width=r * 0.09, opacity=0.8)
