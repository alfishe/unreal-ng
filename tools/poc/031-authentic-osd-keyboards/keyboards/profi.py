"""Profi v3 in the БК-0010 case: the black case of the БК-0010 with the Soviet 58-key keyboard (the Spectrum+ layout,
the Pentagon 2+'s caps) set in its recess behind a thin frame, and the "Profi///" badge at the rear left: "Profi" in
light rounded letters with a small ring and three slanted stripes, red, white and green, on a dark plate with a
light rim.

Sources: refs-clones §6.1, the only photo of a real Profi keyboard, references/photos/clones/profi-v3-in-bk0010-case-
speccyinfo-screenshot.jpg (686 px; the case and the badge measured against the key block's 13.5 u, 445 px); the layout
key for key the Pentagon 2+'s (keyboards/pentagon2plus.py), so are the caps and their legends: the photo is too small to
read the print, and its caps' yellow is the light and the age (no patina, owner). UNVERIFIED: the case's depth (the
photo is shot a little from the front), the badge's fourth stripe (a second green, or the green's lit edge).
"""

from keyboards import pentagon2plus
from keyboards.pentagon2plus import U
from model import Model, Part

KEYBOARD = {"id": "profi", "name": "Profi (БК-0010 case)", "models": ["PROFI", "PROFI3"], "size": [357, 183],
            "matrix": "zx", "twins": {"CS": 2, "SS": 2}}

CASE = "#1e1e1e"
X0, Y0 = 71.0, 44.0      # the key block's top left


def badge(x, y):
    """The "Profi///" badge, its top left at @p x, @p y: 54.7 x 19.6 mm"""
    w, h = 54.7, 19.6
    stripes = []
    for i, colour in enumerate(["#c8283a", "#e6e2d6", "#2f9a52"]):
        sx = x + 33.6 + i * 4.6
        stripes.append(Part("stripe", {"points": [(sx + 4.6, y + 3.4), (sx + 7.6, y + 3.4), (sx + 3.0, y + 16.2),
                                                  (sx, y + 16.2)]}, colour))
    return [
        Part("panel", {"x": x, "y": y, "w": w, "h": h, "r": 0.6}, "#8c8c88"),
        Part("panel", {"x": x + 0.7, "y": y + 0.7, "w": w - 1.4, "h": h - 1.4, "r": 0.4}, "#161617"),
        Part("logo", {"text": "Profi", "x": x + 2.8, "y": y + 15.6, "width": 29.5, "size": 12.6, "face": "rounded"},
             "#d9d7cf"),
        Part("ring", {"x": x + 33.4, "y": y + 4.4, "r": 0.8, "width": 0.35}, "#d9d7cf"),
    ] + stripes


def layout():
    m = Model()
    W, H = KEYBOARD["size"]
    bw, bh = 13.5 * U, 5 * U
    m.case += [
        Part("body", {"x": 0, "y": 0, "w": W, "h": H, "r": 3.0, "matte": True}, CASE),
        Part("steps", {"x": 2.0, "y": 30.0, "w": W - 4.0, "h": 4.0, "steps": 1}, CASE),  # the rear panel's edge
        Part("steps", {"x": 2.0, "y": 146.0, "w": W - 4.0, "h": 7.0, "steps": 1}, CASE),  # the front's slope
        # the frame round the keyboard: a light rim, the dark frame, the light plate seen between the caps
        Part("panel", {"x": X0 - 5.0, "y": Y0 - 5.0, "w": bw + 10.0, "h": bh + 10.0, "r": 1.0}, "#5e5e5a"),
        Part("panel", {"x": X0 - 4.4, "y": Y0 - 4.4, "w": bw + 8.8, "h": bh + 8.8, "r": 0.8}, "#2a2b2b"),
        Part("panel", {"x": X0 - 0.6, "y": Y0 - 0.6, "w": bw + 1.2, "h": bh + 1.2, "r": 0.6}, "#8b8676"),
    ]
    m.case += badge(8.1, 8.1)
    pentagon2plus.keys(m, X0, Y0)
    return m
