"""Phase 3: hybrid. The photo's caps and their highlights on the schematic's flat case, so the keys stand out of a
quiet board, and a hints layer: what a key presses where that is not its own matrix key (EDIT: CS+1, the Profi's
cursor keys: the joystick line), as a small tag on the cap's corner. The legends are the same data in every style and
each role is a group of its own (`legends-<role>`), so an app can dim the roles that do not matter now."""

from styles import photo, schematic
from styles.common import Picture, glyphs, legends
from svgkit import rounded, text_width

TAG = 1.9          # the tag's text size in mm
TAG_FILL = "#f2b632"
TAG_INK = "#1b1c1f"
NAMES = {"CS": "CS", "SS": "SS", "SPACE": "SP", "ENTER": "ENT"}


def hint(key):
    """The tag's text, or "" when the key presses its own matrix key or PC key"""
    if key.zx and len(key.zx) > 1:
        return "+".join(NAMES.get(name, name) for name in key.zx)
    if key.line and key.line.startswith("JOY:"):
        return "JOY"  # the key shows its direction already
    return ""


def tag(key, text):
    """A pill on the key's top right corner, half over the cap's edge, out of the way of the legends"""
    x, y, w, h = key.rect
    width = text_width(text, TAG, "sans-bold") + TAG * 0.9
    height = TAG * 1.35
    right = x + w - 0.6
    top = y - height * 0.35
    return (f'<path d="{rounded(right - width, top, width, height, height / 2)}" fill="{TAG_FILL}" '
            f'stroke="{TAG_INK}" stroke-width="0.2"/>' +
            glyphs(text, right - width / 2, top + height * 0.5 + TAG * 0.36, TAG, "sans-bold", TAG_INK))


def draw(keyboard, model):
    pic = Picture(keyboard)
    for part in model.case:
        pic.add("case", schematic.CASE[part.kind](pic, part))
    for key in model.keys:
        svg, face = photo.cap(pic, key)
        pic.add("caps", svg)
        legends(pic, key, face)
        text = hint(key)
        if text:
            pic.add("hints", tag(key, text))
    return pic.svg()
