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
  stacked-opacity shadows; `schematic` draws flat fills and outlines from the same data. A style may leave out a
  role (the schematic may drop the keyword legends in a sparse variant).
- **Layers**: every SVG has `<g id="case">`, `caps`, `legends`, `hints`. The hybrid of phase 3 composes layers of
  different styles; Qt draws one by id.
- **Keys JSON** (`<id>.keys.json`): the rectangles and what each key presses, the same for every style: the overlay's
  cursor and pressed state come from it.

## Constraints of QSvgRenderer

SVG Tiny 1.2: paths, linear and radial gradients, opacity. No filters, so no blur: a shadow is a stack of growing
rounded rectangles of low opacity. Text is converted to outlines (fontTools from the system fonts), so a picture
does not depend on the fonts installed where it is shown. `qtrender/` renders with Qt for the check, `rsvg-convert`
gives the browser-like reference.
