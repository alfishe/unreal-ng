# Keyboard 3D models: the data model and its 3D mapping

The 3D generator reads the same data as the POC 031 styles. This file says what that data holds, how each part kind
and cap material maps to 3D, and what 3D adds.

## 1. What POC 031 gives

`tools/poc/031-authentic-osd-keyboards/model.py`, one `Model` per keyboard from `keyboards/<id>.py` `layout()`:

| Item | Fields | Notes |
|---|---|---|
| `KEYBOARD` (module dict) | `id`, `name`, `models` (the emulator's short names), `size` [W, D] mm, `matrix` (`zx`), `twins` (keys present twice), optional `depth` (a two-row key keeps a one-row slope), `skirt` | the footprint is the top of the case |
| `Model.case` | a list of `Part(kind, geometry, color, extra)` | geometry in mm from the case's rear-left corner, x to the right, y toward the user; drawn in list order |
| `Model.keys` | `Key(id, rect, cap, color, legends, zx, pc, line, shape, label)` | `rect` (x, y, w, h) mm; `shape` a polygon for an L ENTER; `cap` the material family; `zx` / `pc` / `line` what it presses |
| `Legend` | `text`, `role`, `at`, `x`, `y`, `size`, `face`, `color`, `anchor`, `squeeze`, `swatch`, `glyph`, `inverse` | `at`: `cap` (x, y in the top face's 0..1 box; y > 1 is the front slope), `cell` (the key's box), `above` / `below` (on the case); `glyph` a drawn shape (`block:N`, `arrow:dir`, `enter`, `box`, `speaker`...) |
| `keys.json` | `id`, `rect`, `label`, `shape`, `zx`, `pc`, `line` | the overlay's key map; the same ids name the 3D key nodes |

Fonts: `svgkit.py` `FONTS` (system fonts by path: Helvetica, Arial Narrow, Arial Black, DIN Condensed, ...);
`text_path()` turns a legend into an SVG path, which a 3D builder can extrude or rasterise.

## 2. Part kinds → 3D

| Kind | 2D meaning | 3D |
|---|---|---|
| `body` | the case's footprint, matte or satin | the case shell: the footprint extruded to the case's height profile, filleted edges, the underside with feet |
| `hump` | a raised rear section (48K, Amstrad strip) | a raised block on the shell with its front step |
| `well` | the recess the keys stand in | a pocket cut into the top, its floor (the deck) at the deck height |
| `plate` | a flat panel set in (the 48K's metal plate) | a separate thin plate, brushed-metal or plastic material |
| `panel` | a recessed flat plate (a blank, an emblem's plate) | a shallow pocket with a plate at its floor |
| `boss` | a raised flat moulding (the Scorpion's plaque, ribs) | a raised block with a small fillet |
| `groove` | a groove across the case | a cut, 0.8 mm wide, ≈0.5 mm deep |
| `teeth` | the Amstrad's castellated edge | notches cut in the rear edge |
| `grille`, `vents` | vent slots | slots cut through the top (or deep pockets with a dark floor) |
| `steps` | ledges stepping down | stepped faces of the shell |
| `fins` | the 128's heatsink | a separate finned extrusion, anodised-aluminium material |
| `insert` | the Next's rainbow strip | a glossy plastic insert in its groove |
| `logo` | a printed word (`stroke`: outline, `outline` / `emboss`: moulded, `gloss`: glossy on matte) | printed: a decal or the base-colour texture; moulded: relief geometry 0.2–0.4 mm or a normal map |
| `stripe` | a printed band (rainbows, the Quorum's cells) | a decal |
| `ring` | a printed ring | a decal |
| `tab` | a glossy black part (LED window) | a small inset part, glossy |
| `led` | an LED | a small emissive dome (off by default) |
| `datacorder`, `buttons` | the +2's deck and its six buttons | a sub-assembly: the lid, the window (clear plastic), the well, the spindles; the buttons as pressable nodes without a mapping (`line: TAPE_*`) |
| `fddpanel` | the +3's drive label panel | a decal on the top; the drive slot on the front face |

## 3. Cap materials → cap families

The `cap` field names a family. Each family is a parametric generator; a keyboard picks one, a key may override a
parameter (`depth`, a corner radius). Parameters marked UNVERIFIED need the measurements of
[keyboards.md](keyboards.md) §4.

| Family | Keyboards | Shape | Parameters (initial values, UNVERIFIED unless sourced) |
|---|---|---|---|
| `rubber` | zx48k | one mat; each key a raised rounded pad, flat top, soft vertical sides | pad 14.5 × 10.7 mm (refs-sinclair §1.2), corner r 1.5, height 3.0, edge fillet 0.8, travel 2.0 |
| `plus` | zx128 | a hard block: a flat rear ledge (rear third) and a convex front pad | block = key rect − 1 mm gap, height 6, pad crown 0.6, travel 2.0 (membrane under hard caps) |
| `amstrad` | zxplus2, 2a, 3 | a typewriter cap: tapered skirt, top ≈70 % of the pitch, cylindrical dish | top inset 0.15 u each side, skirt height 8, draft 12°, dish depth 0.5 (cylinder axis left–right), travel 3.5 |
| `mech` | pentagon | a low square cap on a mechanical switch, flat top with a sticker | cap 15 × 15, height 7, draft 5°, travel 4.0; the switch body optional |
| `soviet` | pentagon2plus, scorpion, quorum | MS 7004 family: flat top face ≈2/3 of the key, nearly flush with the back, narrow sides, a tall front slope | the 2D `soviet()` shares: side 0.14, back 0.04, front 0.3 of the key (of `depth` at most); height 9, front slope 35°, travel 3.5 |
| `next` | zxnext | a low laptop-like cap, matte flat top, rounded front corners | height 3, top r 0.38 u front, travel 2.0 |
| `hard` | (generic) | four lit facets and a dished top | the 2D insets (1.1, 0.7, 1.1, 1.9) mm; height 8 |

The 2D cap functions (`styles/photocaps.py`: `rubber`, `hard`, `next_cap`, `amstrad`, `soviet`) hold the top face
insets that match the photos; the 3D generator takes the top face rectangle from the same numbers, so the top view of
the 3D cap equals the 2D cap.

## 4. Legends → 3D

- `at: cap`, 0 ≤ y ≤ 1: on the top face; project the 2D placement onto the face (for a dished face, wrap along the
  dish).
- `at: cap`, y > 1: on the front slope (the Pentagon 2+'s and Quorum's E-mode words); y − 1 runs down the slope.
- `at: cell`, `above`, `below`: printed on the case (the 48K's keywords on the plate, the Next's strip).
- `glyph`: build the shape from `styles/common.py` `glyph_shape()` (it returns an SVG path; the same path extrudes).
- Print method per keyboard: printed ink (a decal, 0.02 mm proud or texture), stickers (Pentagon: a sticker mesh
  0.1 mm thick), double-shot (none of these).

## 5. What 3D adds

A 3D file per keyboard next to its 2D module, `keyboards/<id>.3d.py` or a `THREE_D` dict in the module (proposal;
the generator imports both):

| Field | What | Example |
|---|---|---|
| `case.heights` | the case's height at the rear, the front and the deck, mm, with sources | `{"rear": 30, "front": 16, "deck": 18, "src": "Sinclair leaflet; UNVERIFIED front"}` |
| `case.profile` | the side profile as a polyline (y, z) when it is not a plain wedge | the Amstrad's rear strip |
| `case.feet` | feet positions and sizes | `[(20, 130, 12, 6), ...]` |
| `caps.family`, overrides | the family and parameter overrides | `{"family": "soviet", "height": 9.5}` |
| `materials` | per material: base colour (from the 2D colour), roughness, metallic, clear-coat, subsurface (rubber) | `{"rubber": {"roughness": 0.85}}` |
| `parts` | per 2D part: height or depth, and whether it is geometry or a decal | `{"hump": {"height": 6}}` |

Rules: no 2D number is copied into the 3D file (it reads the module); a 3D number states its source or UNVERIFIED.
