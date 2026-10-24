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

**Second round (owner review 1).** The generic PlayStation pad was a generic outline with PlayStation legends and did
not read as a PlayStation pad. It now uses the DualShock shape with the D-pad as four separate keys and both sticks
low in the centre, and a plain centre with DS3-style Select / Start instead of the touchpad. The DS4 set adds the
touchpad, Share / Options, the light bar and muted symbol colours.

**Third round (coordinator review).** Drawn by eye, the DualShock and 8BitDo bodies were slabs with near-vertical sides
and square grips, and the SN30 an oval with bumps. All their outlines are now measured with `trace.py`: the silhouette
of an orthographic view, the left half mirrored about the best symmetry axis (mirror overlap 0.999-1.000), scaled to the
maker's width, fitted with cubic Beziers within 0.25-0.3 mm. The DS4 comes from the Tokyoship DS4 layout drawing (CC BY
3.0, 162 mm wide gives 100.6 mm deep): the grips taper outward to round tips at (±70, 49) mm, the U between them starts
just under the sticks (y 14 mm), the top dips between the shoulders and the dishes sit on bulges of the outline. The
DS3 drawing of the same author is a 3/4 view and unusable, so the generic PlayStation pad uses the DS4 outline at DS3
width (157 mm). The 8BitDo outlines come from 8BitDo's colourway front views (cut out on a transparent ground); scaled
to the maker's width, every traced depth lands within 1 % of the maker's figure (Pro 2 100.6, SN30 Pro 63.4, M30
74.8, SN30 64.2, Zero 2 36.4 mm), which shows the views are true plans. The SN30 / SF30 is now the SNES dog-bone with a
flat top and round ends; the Ultimate is no longer the Xbox shell but the Ultimate 2C's front view (its own view stands
in the dock; where both show, the two outlines agree within 0.5 mm). The control positions are read on the same views
on a 5 mm grid; read so, the Ultimate's Star key is the middle one of the -, Star, + row (now on `share`) and the
pill under it the profile key, where the guessed drawing had a single Star on `profile`. The overlays (`compare-<set>.png`, ours at 50 % with our edge red over the reference silhouette in
blue) match within a line width round the whole case.

The 8BitDo estimates left: the SN30 Pro+ has no 8BitDo page left and is drawn on the Pro 2's shell without its back
buttons; the Pro 2's lighter face plate is the SN30 Pro outline scaled to the plate's size; SF30 Pro and SF30 are
colourways of the SN30 Pro and SN30; the Zero 2 is drawn at twice the scale (73 mm wide, it would be a thumbnail at
the pads' scale). The M30's six buttons sit on the canonical names as SDL_GameControllerDB maps them (Z and C on the
bumpers, L / R on the triggers) and the 8BitDo back buttons on paddle.p3 (left) / paddle.p1
(right); core has no 8BitDo-specific mapping yet, so both are assumptions to check on a real pad.
