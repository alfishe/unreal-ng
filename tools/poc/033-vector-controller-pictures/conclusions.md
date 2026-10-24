# Conclusions

| Goal / criterion | Result |
|:--|:--|
| One generator draws the C5 pads and the DB9 sticks in one house style | Met: 13 sets from `generate.py` (Xbox Series, Xbox One S, Xbox Elite Series 2 with a back view of the four paddles, DualSense, Switch Pro, the generic pad in four glyph variants, the DB9 stick for the generic, Kempston, Sinclair and Cursor sets). |
| Same files and hotspot format, `ControllerPicture` unchanged | Met: `front.svg` + `front.hotspots.json` + `provenance.json`, schema 1; no unreal-qt change. |
| `generate.py --check` passes, hotspots inside the canvas and over their controls | Met. |
| Qt vs rsvg mean below 2/255 | Met: 0.016-0.085. |
| Galleries: dark / light, every control highlighted, hotspot map, overview | Met (scratch, see the measurements). |
| Owner judges authenticity | Pending. |
| Owner's taste: matte, ±5 % shading, sheen 0.05, no dirt, no logos | Met by construction (`style.py`); the dim highlight is the widget's colour, see the proposal. |

Q-1 is answered for the bundled set: vector pictures drawn from per-device data work in the dialog as it is, render
identically in Qt and rsvg, and cost 6-21 KB per device.
