# Stream Deck icon packs: how they are made and changed

The pictures on the deck's keys come from icon packs. Every pack draws the same list of glyphs, each in its own
style, so a scenario names a glyph (`run.pause`) and the pack decides how it looks. This page is the working guide for
anyone (human or agent) who changes a glyph, a pack or the list. A new style: [new-pack.md](new-pack.md). Every glyph
and the actions it is bound to: [glyphs.md](glyphs.md) (generated).

## 1. What is where

| Path | What | Edit by hand? |
|:--|:--|:--|
| `tools/icons/streamdeck_glyphs.py` | The canonical glyph list: id, group, title, the brief ("draw"), state variants, and the table binding catalogue actions to glyphs (`ACTIONS`), the pruned actions, the navigation map | **yes**: the source of the list |
| `data/streamdeck/icons/glyphs.json` | The list as data, written by the script above | no: regenerate |
| `tools/icons/streamdeck_packs/magicwb.py` | **The drawings**: one function per glyph id (`DRAW`), on a 32 x 32 grid with semantic palette keys | **yes**: change a picture here |
| `tools/icons/streamdeck_pixel.py` | Pixel helpers the drawings use (masks, shading, lines, the 4 x 7 digit font) | rarely |
| `tools/icons/streamdeck_packs/reskin.py` | Re-skins the drawings into other pixel styles (ramps per key, outline, shadow / glow hooks) | rarely |
| `tools/icons/streamdeck_packs/vector.py` | Renders the drawings smooth at 256 px (same API, shape ids, gaps, cut vs backdrop details) | rarely |
| `tools/icons/streamdeck_packs/<pack>.py` | One generator per pack: its palette and treatment | **yes**: change a style here |
| `data/streamdeck/icons/packs/<pack>/` | The pack: `pack.json` + `src/<glyph id>.png` | no: regenerate |
| `tools/icons/render_streamdeck_icons.py` | Validates packs, renders every key size, draws contact sheets, checks determinism | rarely |
| `tools/icons/streamdeck_icons_doc.py` | Writes [glyphs.md](glyphs.md) | rarely |
| `tools/icons/regenerate_streamdeck_icons.py` | Runs all of the above in order | no |
| `scratch/streamdeck-icons/` | Renders per pack and size, `sheets/<pack>.png`, `SHA256SUMS` (git-ignored) | no |

The packs today:

| Pack | Style | Source | Built by |
|:--|:--|:--|:--|
| `magicwb` | Amiga MagicWB: 8 colours, black outline, light top-left, dithered shade, a Workbench 2 button | png-pixel 32 x 32 | the drawings themselves; the **fallback** of every other pack |
| `newicons` | NewIcons: 5-step pastel ramps, black outline, no button | png-pixel 32 x 32 | `reskin.py` |
| `glowicons` | GlowIcons (OS 3.5 / 3.9): 8-step ramps, soft dark rim, drop shadow; on = yellow glow | png-pixel 32 x 32 | `reskin.py` |
| `workbench13` | Workbench 1.3: blue / white / black / orange, tall pixels | png-pixel 32 x 16, aspect 1:2 | `reskin.py` + row squash |
| `line` | Even strokes along every contour, cyan for the drawing's blue parts | png-smooth 256 | `vector.py` |
| `duotone` | Off-white glyph on a tinted plate per group, one accent per group | png-smooth 256 | `vector.py` |
| `banner` | White glyph, the UnrealNG banner's blue shadow, navy tile, no stripes | png-smooth 256 | `vector.py` |
| `keycap` | Matte dark keycap, legend high, a group-coloured bar low with a gap | png-smooth 256 | `vector.py` |
| `os4` | AmigaOS 4: gentle gradient per shape, thin dark rim, soft shadow on dark blue | png-smooth 256 | `vector.py` |

All nine packs are built from the one set of drawings in `magicwb.py`: change a drawing there and every pack shows
the change after a regeneration.

## 2. The drawings: semantic keys

`magicwb.DRAW[id](c)` draws on a canvas `c` that offers `solid(mask, key, bevel=True, outline=True)`, `px`, `hline`,
`vline`, `box`, `line`, `text`, `text_mask`, direct key writes `c.c[mask] = key`, and masks `c.m.poly / rect / ell /
circle / thick / arc` in pixel units of the 32 grid (content in 3..28; the outer ring is MagicWB's button bevel).
Draw with **meanings, not colours**: every pack maps the keys to its own palette.

| Key | Meaning | What the packs do with it |
|:--|:--|:--|
| `g` | backdrop | the pack's tile / background |
| `k` | black: outline rings, dark details | outline; as a fill (`solid`) a dark tone; as a detail on a shape a cut-out, on the backdrop a light line |
| `w` | white | the lightest tone (a face, paper) |
| `l` | light grey | a light body |
| `d` | dark grey | a dark body (cassette shell, chip); a dark detail stays dark |
| `b` | blue: the accent | the pack's accent (cyan, group colour, blue ramp, orange on Workbench 1.3) |
| `e` | beige / warm: the second accent | warm accent (amber, tan, orange) |

Rules that keep all packs working:
- Separate parts with `solid(..., outline=True)` (the default): the outline ring becomes the gap between shapes in
  the smooth packs and the black rim in the pixel packs.
- A state variant `<id>.on` must look different from `<id>` in **shape** too, not only in colour (several packs are
  near-monochrome): a needle high vs low, a lock closed vs open, a filled vs hollow dot.
- Thin details (1-pixel `px` / `line`) vanish at 72 px and on Workbench 1.3's half-height rows: give anything that
  carries meaning at least 2 pixels.
- Text: only the digits and letters of `streamdeck_pixel.FONT_4X7` (0 1 2 4 8 N T); prefer a symbol to text.

## 3. Typical changes and what to run

Run everything from the repository root (Python with Pillow, numpy, scipy, cairosvg).

| You want to | Edit | Then run |
|:--|:--|:--|
| Redraw a glyph | its entry in `magicwb.DRAW` | `python3 tools/icons/regenerate_streamdeck_icons.py` (all packs pick it up) |
| Tune one pack's colours / treatment | `tools/icons/streamdeck_packs/<pack>.py` | `python3 tools/icons/regenerate_streamdeck_icons.py <pack>` |
| Add a glyph | `GLYPHS` (and `ACTIONS` if an action shows it) in `streamdeck_glyphs.py`, a `DRAW` entry in `magicwb.py`; a pack drawn by hand (not from `magicwb`) needs its own `src/<id>.*` | the full regeneration; `--validate` names every pack still missing it |
| Bind an action to a glyph, add an "on" face or a per-parameter glyph | `ACTIONS` in `streamdeck_glyphs.py` (`face`, `face_on` + `state` = the readout, `params`, `label`) | `python3 tools/icons/streamdeck_glyphs.py && python3 tools/icons/streamdeck_icons_doc.py` |
| A new catalogue action appeared (`GET /api/v1/controllers/actions`) | add it to `ACTIONS` with a face, or to `PRUNED` with the reason (dial-only, GUI-only, set by menus) | the glyph list + the doc; then draw any new glyph |
| Rename or remove a glyph | `streamdeck_glyphs.py`, `magicwb.py`, scenarios using the id (grep `companions/streamdeck`, `data/streamdeck`), hand-drawn packs | the full regeneration |
| A new style | see [new-pack.md](new-pack.md) | |

`regenerate_streamdeck_icons.py` writes the glyph list, the packs (MagicWB first), [glyphs.md](glyphs.md), then
validates (every pack complete, right sizes), renders every key size with contact sheets into
`scratch/streamdeck-icons/` and renders twice to prove the same sources give the same bytes (`check: ok,
deterministic`). It stops at the first failure.

## 4. Checking a change: look, do not trust

Numbers do not judge pictures. After any change:
1. Open the sheet: `open -a Preview scratch/streamdeck-icons/sheets/<pack>.png` (each glyph at 112 px and 72 px, with
   its id; a glyph served by the fallback pack is framed red).
2. Look at every glyph you touched in **every** pack, at 72 px: is it readable, does it say what its brief says, does
   the `.on` variant differ in shape, are stroke weight and optical size like its neighbours'?
3. Only then show it to the owner, and only what you are satisfied with.

Owner rules for visuals (they delete what breaks them): beautiful and stylish, never quantity; fine consistent
strokes, no chunky clumsy shapes; a restrained palette, **no pink or magenta**, nothing garish; matte, no glossy kitsch
(the retro packs keep their originals' own shading); the TTD timeline belongs on the strip, never on keys.

## 5. Showing a pack on the real deck

Nothing goes on the deck before the owner asked for it. To show a page, write a throwaway scenario in your worktree's
`scratch/` whose key faces are rendered PNGs (`scratch/streamdeck-icons/<pack>/112/<id>.png` for the + XL) and load
it on that deck only: `PUT /api/v1/decks/{serial}/scenario {"file": ...}` on the daemon's automation (default
`127.0.0.1:37053`, see [daemon.md](../daemon.md)); give the deck back with `DELETE /api/v1/decks/{serial}/scenario`.

## 6. Status

- The packs live on the branch `streamdeck-iconset` until the owner's review; they are not on master.
- Scenarios cannot pick a pack yet: the lookup (theme / scenario names a pack, missing glyphs from its `fallback`
  chain, the key size from the model) is still to be built in the companion (`companions/streamdeck/lib`); keys that
  need a text over the picture (a key name, a drive letter, a count) need a label on image faces there too.
- The product scenarios are generated only after the owner approved the packs and the use-case document.
