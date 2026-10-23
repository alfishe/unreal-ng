# A new icon pack style

How to add a pack in a new style. Read [README.md](README.md) first (the drawings, the semantic keys, the checks and
the owner's rules for visuals).

## 1. Sample first, full pack second

Never generate a whole pack straight away. The owner picks styles from samples:
1. Draw 3 glyphs that stress a style: `run.play` (a plain shape), `ttd.menu` (a clock with the back arrow: detail),
   `media.tape` (a cassette: a complex object). Look at them yourself at 112 and 72 px and fix what is weak.
2. Show them next to the existing packs: a contact sheet (open it in Preview) and, if asked, a page on the deck.
3. Build the full pack only for the styles the owner chose, then show the full sheet.

## 2. Choose how the pack is built

| The style is | Build it as | Example | Effort |
|:--|:--|:--|:--|
| A palette / shading variant of pixel art (any Amiga, Atari, PC-era icon look) | a `reskin.Skin` over the MagicWB drawings | `newicons.py`, `glowicons.py`, `workbench13.py` | ~50 lines |
| A flat, line, plate or soft-gradient look | a render over `vector.HiResCanvas` | `banner.py`, `duotone.py`, `keycap.py`, `os4.py` | ~50-100 lines |
| Something the shared drawings cannot carry (a different shape language, hand-drawn art) | hand-drawn sources: `src/<id>.svg` or `.png` for every glyph | none yet | 248 drawings |

Prefer the first two: the pack then follows every redrawn glyph for free and keeps the same meanings.

### 2.1 A pixel style (`reskin.Skin`)

Copy `tools/icons/streamdeck_packs/newicons.py` to `<pack>.py` and set:

| Field | Meaning |
|:--|:--|
| `steps`, `light`, `edge` | the ramp: how many tones per key, how strongly lit from the top left, how strong the light / dark rims are |
| `ramps` | per key (`b l w d e k g`): the tones darkest .. lightest; used inside shaded shapes |
| `flats` | per key: the colour of details (pixel runs, lines) and of shapes drawn without shading |
| `bg`, `bg_on` | the backdrop, and the backdrop of `.on` faces |
| `outline` | draw MagicWB's outline rings (key `k`) |
| `flat()` / `ramp()` | override for special rules (Workbench 1.3 turns orange parts blue on its orange "on" backdrop) |
| `finish(img, canvas)` | a last pass over the 32 x 32 RGB: shadows, glows, soft rims (`glowicons.py`); `canvas.shapes` is the silhouette |

`reskin.write_pack(name, Skin(), manifest, image_of=None)` writes the pack; `image_of` turns the 32 x 32 RGB into the
saved picture (Workbench 1.3 squashes it to 32 x 16 tall pixels). Manifest: `title`, `style`, `licence`
(`reskin.LICENCE`), `source: "png-pixel"`, `native`, `pixel_aspect`, `background`.

### 2.2 A smooth style (`vector`)

Copy `banner.py` (flat glyph on a tile) or `os4.py` (per-shape gradients) and write `render(g) -> PIL.Image` (256 x
256 RGB) from `c = vector.canvas_for(g['id'], gap=...)`:

| Layer | Meaning |
|:--|:--|
| `c.c` | the key of every pixel |
| `c.ids` | `> 0` the solid shape a pixel belongs to (`c.keys[id]` its key), `-1` a detail, `0` backdrop or gap |
| `c.gap` | MagicWB's outline rings: the gaps between shapes |
| `c.cut` | a detail drawn over a shape (a cut into it) rather than on the backdrop (a line of its own) |

Helpers: `vector.flat_glyph(c, colours, black_fill=, black_line=)` (a one-colour-per-key glyph with alpha),
`vector.place(dst, rgb, alpha, scale, cx, cy)` (scale and composite onto a tile), `vector.write_pack(name, render,
manifest)`. Use `g['group']` for per-group colours (see `duotone.GROUPS`). Manifest: `title`, `style`, `background`
(`source` is `png-smooth`).

### 2.3 A hand-drawn pack

`data/streamdeck/icons/packs/<pack>/pack.json` + `src/<glyph id>.svg` (or `.png`) for every id in
[glyphs.md](glyphs.md), drawn to each glyph's brief. `source: "svg"` is rasterised at each key size;
`"png-pixel"` needs `native` (and `pixel_aspect`); `"png-smooth"` wants 256 px or more. Keep the sources editable
(SVG, or the pixel PNG at native size) and say in `pack.json` who drew them.

## 3. pack.json

```json
{
 "format": "streamdeck-icon-pack", "version": 1,
 "name": "mypack", "title": "My pack",
 "style": "one line: palette, outline, shading, the on state",
 "author": "unreal-ng",
 "licence": "Original drawings made for unreal-ng ...; no original artwork of the imitated style is used",
 "source": "png-pixel | png-smooth | svg",
 "native": [32, 32], "pixel_aspect": [1, 1],
 "background": "#RRGGBB",
 "sizes": [72, 80, 96, 112, 120],
 "fallback": "magicwb",
 "generated_by": "tools/icons/streamdeck_packs/mypack.py"
}
```

`fallback` serves a glyph the pack lacks (framed red on the sheet; a finished pack has none). Retro styles imitate a
palette and a technique only: never copy original artwork.

## 4. Register, build, check

1. Add the name to `ORDER` in `tools/icons/regenerate_streamdeck_icons.py` (after `magicwb`).
2. `python3 tools/icons/regenerate_streamdeck_icons.py mypack`: validate, render, sheet, determinism.
3. Look at `scratch/streamdeck-icons/sheets/mypack.png` glyph by glyph at 72 px ([README.md §4](README.md#4-checking-a-change-look-do-not-trust)):
   the `.on` variants must differ from their base; fix the treatment, or a drawing in `magicwb.py` (then look at
   every pack again).
4. Add the pack's row to the table in [README.md](README.md#1-what-is-where).
5. Show the sheet to the owner; commit only when asked, on the icon branch until the packs are reviewed.
