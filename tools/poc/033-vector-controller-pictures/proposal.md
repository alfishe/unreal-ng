# Proposal

1. **Adopt** the generator as the source of the bundled controller pictures (controller manager C5). `generate.py`
   owns the pad and DB9 sets; `tools/controllers/make_fallback_images.py` keeps md6-pad, surface-dial and
   generic-keyboard until they are drawn here too. `devices.json` points xbox-series, xbox-one, xbox-one-s,
   xbox-elite-2, dualsense and switch-pro at their own sets; the other families keep the generic pad.
2. **Next devices**, same way: DualSense Edge (back buttons), DualShock 3, Xbox 360, Elite 1, Joy-Con, 8BitDo Ultimate
   2C / Ultimate 2 / Pro 3 / Lite 2 (now on the Ultimate and Pro 2 pictures), the Mega Drive 6-button pad (replacing the md6 line drawing), the Sinclair SJS-1 when a reference
   is found, the Surface Dial.
3. **Widget, small changes for the controllers dialog owner** (not done here, the dialog is being changed in another
   stream):
   - Cache the rendered SVG in a `QPixmap` per widget size and device pixel ratio; repaint the overlay only.
   - A dimmer lit colour on the matte pictures, e.g. QColor(255, 170, 0, 90) with a 1.5 px (255, 150, 0, 200)
     outline, and for `rect` hotspots on shoulders a radius of half the short side; or, better, tint the control's
     own `control-<name>` group (`QSvgRenderer::boundsOnElement` / `render(painter, id, bounds)`) so the highlight
     follows the drawn shape.
4. **Owner review** of the overview; the remarks go into the device modules, and the POC status becomes "adopted".
