"""Microsoft's Surface Dial, the debugger's jog wheel (surface-dial.md).

- `surface-dial`: the Dial from above, drawn at twice the pads' scale so a 59 mm puck reads in the dialog: the
  aluminium shell (magnesium colour) round the edge, the satin top inside it. The whole puck turns (`knob`, an
  encoder: the widget draws a dot on the ring where it has turned to), the top presses (`push`, its hotspot the
  middle of the top), the hand on it (`touch`) has no place of its own and stays a tag under the puck. Replaces the
  line drawing of `tools/controllers/make_fallback_images.py`; the control names, their hotspots and the encoder
  (36 steps, its ring) are kept.

The size is Microsoft's (59 mm across, 30 mm high, the base 54 mm); the proportions of the top and the colours are
read on Microsoft's product shots. The Dial has no print on the top (its logo is on the base), so nothing is left out.
"""

import style
from model import Picture
from svgkit import circle_d, rounded_d, shade

RETRIEVED = "2026-10-31"
SCALE = 17.2  # twice the pads' 8.6 px per mm


def surface_dial():
    pic = Picture("surface-dial", "Microsoft Surface Dial, magnesium (drawn at twice the scale)")
    pic.sources = [
        {"what": "size 59 mm across, 30 mm high, the base 54 x 4 mm, 145 g; aluminium, magnesium colour",
         "source": "https://www.microsoft.com/en-us/d/surface-dial/925r551sktgn", "retrieved": RETRIEVED},
        {"what": "the top seen from above: the satin top inset in the shell, the colours",
         "source": "https://www.microsoft.com/en-us/d/surface-dial/925r551sktgn", "author": "Microsoft",
         "licence": "product photo", "use": "reference only, not traced", "retrieved": RETRIEVED},
        {"what": "the scale: twice the pads' 8.6 px per mm, the hotspots of the line drawing it replaces kept "
                 "(press, turn with its 36-step ring, touch as a tag)", "source": "this POC", "retrieved": RETRIEVED}]
    colors = {"body": "#b9bcbf", "top": "#c8cbce", "chamfer": "#dfe1e3", "tag": "#c9cbcd"}
    view = pic.view("front", oy=700, scale=SCALE)
    r_body, r_top = 29.5, 27.4
    # the shell: it turns with the top, its edge seen as a band
    style.body(view, circle_d(0, 0, r_body), colors["body"])
    band = view.defs.radial([(0, colors["body"]), (0.9, colors["body"]), (1, shade(colors["body"], 0.93))], r=0.5)
    view.add("case", f'<path d="{circle_d(0, 0, r_body)}" fill="{band}"/>')
    # the aluminium top: a satin disc, a bright chamfer on its edge, faint concentric turning
    top = view.defs.radial([(0, shade(colors["top"], 1.03)), (0.7, colors["top"]), (1, shade(colors["top"], 0.97))],
                           r=0.5)
    view.add("controls", f'<path d="{circle_d(0, 0, r_top)}" fill="{top}"/>', "push")
    view.add("controls", f'<path d="{circle_d(0, 0, r_top - 0.35)}" fill="none" stroke="{colors["chamfer"]}" '
                         f'stroke-width="0.3" stroke-opacity="0.7"/>', "push")
    for r in (8.0, 14.0, 20.0):
        view.add("controls", f'<path d="{circle_d(0, 0, r)}" fill="none" stroke="#ffffff" stroke-opacity="0.05" '
                             f'stroke-width="0.25"/>', "push")
    style.sheen(view, circle_d(0, 0, r_top))
    view.spot("push", "Press", ("circle", 0, 0, 170 / SCALE), "right")
    view.spot("knob", "Turn", ("circle", 0, 0, r_body + 0.7), "left",
              analog={"kind": "encoder", "steps": 36, "radius": 440 / SCALE})
    # the hand on the Dial: a tag under it
    x, y, w, h = -150 / SCALE, 600 / SCALE, 300 / SCALE, 90 / SCALE
    view.add("controls", f'<path d="{rounded_d(x, y, w, h, h / 2)}" fill="{colors["tag"]}" '
                         f'stroke="{style.rim_of(colors["tag"])}" stroke-width="0.12"/>', "touch")
    view.spot("touch", "Touch", ("rect", x, y, w, h), "right")
    return pic


SETS = {
    "surface-dial": surface_dial,
}
