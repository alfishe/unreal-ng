# Analysis

**Renderer agreement.** Qt and rsvg agree to under 0.09 of 255 on every set, about 20 times below the 2/255 bar, with
no pixel off by more than 32 except a few edge pixels on the One S. The drawing uses only paths, linear and radial
gradients and opacity (SVG Tiny 1.2), text as outlines, so nothing depends on the renderer or on installed fonts.

**Hotspots.** The hotspots come from the same millimetre geometry as the drawing, so the hotspot maps in the sheets sit
on their controls by construction. Every control name of the previous sets is kept (`generate.py` compares with the
committed set and fails on a lost name); the new sets carry the names `devices.json` lists for the family (the Elite 2's
`paddle.p1`-`p4`, the DualSense's `touchpad.click` and `misc.1`, the Series' `share`, the Elite's `profile`).

**Themes.** `ControllerPicture` draws the SVG as it is. The light pads (One S white, DualSense white) carry a darker
rim and read on both; the dark pads (Series Carbon Black, Switch Pro, Elite 2) are lighter than the dark dialog
background (#2a2b2e-class bodies at #3b-#45) and keep a lighter rim, so their outline reads on the dark theme, and
they stand out by contrast on the light one. The generic pad is mid grey and reads on both.

**Highlight.** With one control held the dialog's lit fill (orange, alpha 150) over the matte pictures is clear and
acceptable. With many held (the "every control held" panels) it gets loud, and the rectangles for bumpers and triggers
show as boxes over curved shapes. That is the widget's paint, not the pictures; the picture side already gives each
control its own `<g id="control-<name>">`, so the widget could tint the control's own shape instead (proposal).

**Cost.** A picture is 6-21 KB instead of 1-3 KB, and `QSvgRenderer::render` takes about 3-11 ms at 800 x 600 instead of
0.4 ms. `ControllerPicture::paintEvent` re-renders the SVG on every paint, and it repaints on every input change, so a
held stick moving can cost up to ~10 ms of the GUI thread per repaint. Not a problem at the dialog's update rate seen so
far, but a cached pixmap per widget size would make it free (proposal).

**Accuracy.** Outlines and control positions come from published sizes and near-orthographic reference photos, measured
by eye to about 1-2 mm. Estimates, marked in each `provenance.json`: the Elite 2 at the Series' size, its paddle
positions (about 2 mm), the Switch Pro's positions (three-quarter photo corrected by eye), the Competition Pro base size
and heights (no maker's drawing found). The Sinclair and the Cursor sets use the same Competition Pro-type stick: no
usable reference of the Sinclair SJS-1 was found. The Kempston set shows Fire 2 on the second button, although on the
real stick both buttons are wired in parallel.
