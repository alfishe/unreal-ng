# 031 — design

## Model, styles, layers

```
keyboards/<id>.py  layout() -> Model          data only: what the keyboard is
styles/<style>.py  draw(keyboard, Model) -> SVG   how it looks
```

- **Model** (`model.py`): the case as parts of known kinds (`body`, `plate`, `stripe`, `logo`, ...) with their
  geometry in mm and their real colours; the keys with their rectangle (or polygon), cap material and colour, the
  legends by role and placement, and what each presses (`zx`: the matrix keys, `["CS", "1"]` for EDIT; `pc`: the PC
  key).
- **Style**: draws each part kind and cap material its own way. `photo` lights materials with gradients and
  stacked-opacity shadows; `schematic` draws flat fills in the real colours and one outline per part and key (a
  lighter line on a dark part, a darker one on a light part; the photo's top face as a second, fainter line where
  the cap has one; the textures thinned to a diagram: every other fin and slot, the mesh as its plate, moulded words
  flat with a contour); `hybrid` puts the photo's caps and highlights on the schematic's case and fills `hints`. The
  legends are the same in every style, placed on the photo's cap faces (the schematic asks the photo for them), so a
  key reads the same in all three.
- **Layers**: every SVG has `<g id="case">`, `caps`, `highlights`, `legends`, `hints`. The specular
  highlights (the sheen on glossy tops, the rubber dome) are their own semi-transparent layer, not part of a cap's
  material: the SVG draws it at the keyboard's opacity (0.5 by default; the approved Next at 1.0), an app can turn it
  down or draw the picture without it (owner 2026-10-19: highlights baked into the caps looked kitsch). The hybrid of phase 3 composes layers of
  different styles; Qt draws one by id.
- **Roles**: inside `legends`, each role is a group of its own (`legends-main`, `legends-keyword`, `legends-symbol`,
  `legends-extend`, `legends-extend-shift`, `legends-graphic`, `legends-cyrillic`, `legends-pc`...), so an app can
  draw or dim one role alone (the mode legends that matter now: the symbols while Symbol Shift is held).
- **Hints** (the hybrid): a tag on the top right corner of each key that presses something other than its own
  matrix or PC key: the combination (`CS+1` on EDIT, `CS+SS` on EXTEND MODE, `SS+O` on the 128's `;`) or `JOY` on a
  key wired to a joystick line (the Profi+ cursor keys and `*`). Amber with dark text, 1.9 mm: readable at the
  overlay's size, clear of the legends.
- **Keys JSON** (`<id>.keys.json`): the rectangles and what each key presses, the same for every style: the overlay's
  cursor and pressed state come from it.

## Constraints of QSvgRenderer

SVG Tiny 1.2: paths, linear and radial gradients, opacity. No filters, so no blur: a shadow is a stack of growing
rounded rectangles of low opacity. Text is converted to outlines (fontTools from the system fonts), so a picture
does not depend on the fonts installed where it is shown. `qtrender/` renders with Qt for the check, `rsvg-convert`
gives the browser-like reference.
