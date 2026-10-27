# 033 — vector-controller-pictures

| | |
|---|---|
| **Date** | 2026-10-23 |
| **Status** | done, awaiting the owner's review |
| **Feature** | [docs/inprogress/2026-10-17-controller-manager](../../../docs/inprogress/2026-10-17-controller-manager/README.md), TODO C5 (the image set), Q-1 (vector or raster) |

The controllers dialog shows the selected host device with a callout per control and lights the controls while they
are held (`unreal-qt/src/controllers/controllerpicture.cpp`). Until now the pictures were line drawings from
`tools/controllers/make_fallback_images.py`. Q-1 asked whether the image set should be retouched photos or vector
art; the owner answered on 2026-10-23: vector, made the way POC 031 made the on-screen keyboards. This POC draws the
first set.

## Goals

1. Can one generator draw the pads of the C5 list (Xbox Series, Xbox One, Xbox Elite Series 2 with its four back
   paddles, DualSense, Switch Pro, a generic pad) and the DB9 retro sticks as authentic, matte vector pictures, from
   the devices' real dimensions and control placement, in one house style?
2. Do the pictures work in the dialog as it is: the same files and hotspot format (`front.svg`,
   `front.hotspots.json`, `provenance.json`, device-database.md §6-§8), drawn by `QSvgRenderer`, with the dialog's live
   highlight of every control looking right on the light and the dark theme?

## Requirements

- Data per device, a style draws it (as POC 031): `devices/<family>.py` gives the body outline, the controls with
  their positions and sizes in millimetres and their real colours; `style.py` draws them. One scale (px per mm) for
  every pad, so pads compare at their true size.
- SVG Tiny 1.2 for `QSvgRenderer`: paths, gradients, opacity; no filters, no clip paths, no embedded bitmaps, text as
  outlines (fontTools), so no picture depends on installed fonts.
- Named layers in every SVG: `shoulders` (the parts behind the shell), `case`, `controls`, `highlights` (the sheen, its own semi-transparent layer),
  `legends`; each control drawn in `<g id="control-<name>">`, so a later callout or highlight can address it.
- Hotspots in the 2048 x 1536 design space, computed from the same geometry the SVG is drawn from (a hotspot cannot
  drift from its control). Every control name of the previous set of the same name is kept.
- Platform logos removed (D5): the guide button is a neutral ring, no wordmarks. Button glyphs (A B X Y, the
  PlayStation shapes, the Nintendo signs) identify the controls and stay.
- Each image set states its sources (dimensions, layout references) in `provenance.json`; an estimate is marked as
  one.
- The owner's taste (2026-10-19 .. 23, POC 031): matte and restrained. Body shading within ±5 %, a sheen of about
  0.05, no dirt, no wear, no texture noise; the pressed-control highlight dim, not garish.

## Constraints

- POC code in `tools/poc/033-vector-controller-pictures/`; its output is the image sets in `data/controllers/images/`
  (the app loads them). `unreal-qt` code is not changed beyond what the pictures need; `ControllerPicture` and the
  hotspot format stay as they are.
- Renders, galleries and diffs go to `scratch/2026-10-23-poc-033/` (git-ignored), not into the repository.
- Photos and press images are references; no pixels are embedded. Outlines are measured from orthographic drawings
  and maker's front views with `trace.py` (the contour as Bezier numbers in the device module, nothing else kept).

## Acceptance criteria

- `generate.py --check` passes (the committed sets are what the generator writes); every hotspot lies inside the
  canvas and over its drawn control.
- Every SVG renders the same in Qt's `QSvgRenderer` and in rsvg: mean absolute difference below 2 of 255 per channel,
  as POC 031's check.
- A gallery per device shows: the picture on the dark and on the light theme, every control highlighted the way
  `ControllerPicture` paints it, the hotspot map; an overview sheet shows every set. The owner judges each picture as
  authentic or names what is wrong.
- Expected: 6 host pictures (Series, One, Elite 2, DualSense, Switch Pro, generic pad in its four glyph variants) and
  the DB9 sticks (generic, Kempston, Sinclair, Cursor) from one stick drawing. Added after the owner's first review:
  DualShock 4 and nine 8BitDo sets.

## Files

| File | Content |
|---|---|
| `generate.py` | writes every image set into `data/controllers/images/`; `--check` compares |
| `svgkit.py` | gradients, shades, text outlines, path helpers |
| `style.py` | the house style: body, wells, sticks, buttons, D-pads, bumpers, triggers, paddles |
| `model.py` | the canvas, the scale (8.6 px per mm), views, layers, hotspots in mm converted to canvas px |
| `devices/` | one module per maker or device: geometry in mm, colours, controls, sources (`xbox`, `playstation`, `dualshock`, `switchpro`, `eightbitdo`, `generic`, `db9`; the all-pads round adds `xbox_more`, `logitech`, `sony_more`, `joycon`, `nintendo_retro`, `others`, `eightbitdo_more`, `arcade`, each with a `SETS` dict) |
| `trace.py` | measures an outline on a top view: silhouette, symmetry axis, left half mirrored, scaled to the measured width, cubic Beziers within a tolerance; `--shell` prints the right half for `mirrored_d`, `--grid` a 5 mm grid for reading the control positions, `--overlay` the fit |
| `render.py` | Qt (PySide6 `QSvgRenderer`) and rsvg renders, the Qt-vs-rsvg numbers, the per-device sheets with every control highlighted as `ControllerPicture` paints it, the overview |
| [`measurements-2026-10-23-render-check.md`](measurements-2026-10-23-render-check.md) | Qt vs rsvg, sizes, render cost |
| [`analysis.md`](analysis.md) | what the numbers and the galleries show |
| [`conclusions.md`](conclusions.md) | the goals and criteria, met / pending |
| [`proposal.md`](proposal.md) | adopt; next devices; small widget changes |
| [`inventory.md`](inventory.md) | every controller connectable today: ids per bus, the path on macOS / Windows / Linux, family, picture, gaps per host |

## Results

23 image sets drawn; Qt vs rsvg mean 0.016-0.085 of 255, no pixel off by more than 32; `--check` passes.
Owner review 1 (2026-10-23): the generic PlayStation pad did not look like one; it is redrawn on the DualShock shape
(measured on a DS4 top view), a dualshock-4 set is added, and the most popular 8BitDo pads got their own sets
(Ultimate, Pro 2, SN30 Pro+, SN30 Pro / SF30 Pro, SN30 / SF30, M30, Zero 2). Each was checked side by side with its
reference photo (`scratch/2026-10-23-poc-033/compare-<set>.png`). Coordinator review 2: the PlayStation and 8BitDo
silhouettes were guessed and did not read; they are now measured with `trace.py` (DS4 on the Tokyoship DS4 layout
drawing, the 8BitDo pads on 8BitDo's front views), and each compare sheet lays our render over the reference at 50 %.
Details in [conclusions.md](conclusions.md).

All-pads round (2026-10-24): 24 more sets for every controller one can connect today ([inventory.md](inventory.md)):
Xbox 360, Xbox Adaptive, Logitech F310, DualShock 3, DualSense Edge (front and back), Joy-Con L / R / grip, NSO NES /
SNES / N64 / Genesis, GameCube, Steam Controller (front and back), Stadia, Luna, 8BitDo Ultimate 2 / Lite 2 / Micro /
64 / Arcade Stick, an arcade stick, a Saturn pad and a fight pad. Each module (`devices/xbox_more.py`, `logitech.py`,
`sony_more.py`, `joycon.py`, `nintendo_retro.py`, `others.py`, `eightbitdo_more.py`, `arcade.py`) registers its
`SETS`; `generate.py` collects them. Qt vs rsvg mean 0.02-0.07 of 255 on every new set. Not drawn: Switch 2 Pro and
the 2026 Steam Controller (no orthographic view found; they use the Switch Pro and Steam Controller sets).

Run: `python3 generate.py [--check] [--only <set>]`, then `python3 render.py [<set> ...]` (PySide6, rsvg-convert).
