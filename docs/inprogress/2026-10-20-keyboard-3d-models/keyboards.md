# Keyboard 3D models: the inventory

The 21 models of `Config::mem_model` (`core/src/emulator/config.h`), the keyboard each gets, and what its 3D model
needs. "2D" is the POC 031 status on 2026-10-20 (`tools/poc/031-authentic-osd-keyboards/README.md`, Progress).
Footprints are the POC 031 data in mm (W × D); heights are published figures or **UNVERIFIED**.

## 1. Keyboards drawn in 2D

| Keyboard id | Models | Keys | Cap family | Footprint | Height | 2D |
|---|---|---|---|---|---|---|
| `zx48k` | 48K | 40 | `rubber` | 233 × 144 | 30 mm (Wikipedia, Sinclair leaflet) | drawn, shown |
| `zx128` | 128k | 58 | `plus` | 343 × 149 (319 + heatsink) | 45 mm (CCH, may include the heatsink) | drawn, shown |
| `zxplus2` | PLUS2 | 58 | `amstrad` | 440 × 171 | 55 mm (one spec sheet, UNVERIFIED) | approved |
| `zxplus2a` | PLUS2A | 58 | `amstrad` | 440 × 171 | 50–55 mm (UNVERIFIED) | approved |
| `zxplus3` | PLUS3 | 58 | `amstrad` | 440 × 171 | 50 mm (RigPix) | approved |
| `pentagon` | PENTAGON | 40 | `mech` | 222 × 110 (the bare plate) | UNVERIFIED (switches on a textolite plate, no case) | drawn, shown |
| `pentagon2plus` | PENTAGON (second variant) | 58 | `soviet` | 343 × 165 | UNVERIFIED | approved (2026-10-20) |
| `scorpion` | SCORPION, PROFSCORP, GMX | 58 | `soviet` | 327 × 155 | UNVERIFIED | drawn, shown |
| `quorum` | QUORUM | 88 | `soviet` | 438 × 281 | UNVERIFIED | approved (2026-10-20) |
| `orel` | LSY256 | 67 | `soviet` | 386 × 176 (the keyboard unit) | UNVERIFIED | approved (2026-10-20) |
| `profi` | PROFI, PROFI3 | 58 | `soviet` | 379 × 202 (the БК-0010 case) | UNVERIFIED | approved (2026-10-20) |
| `profiplus` | PROFI, PROFI3 | 46 | `soviet` | 325 × 138 | UNVERIFIED (no photo) | approved (2026-10-20) |
| `pcxt` | ATM710 (XT firmware), KAY | 83 | `soviet` (PC sculpt) | 485 × 180 | UNVERIFIED (IBM XT ≈ 60 mm at the back) | approved (2026-10-20) |
| `pcat` | ATM710 (AT firmware), SPRINTER | 101 | `soviet` (PC sculpt) | 470 × 190 | UNVERIFIED (Model M ≈ 50 mm at the back) | approved (2026-10-20) |
| `pcps2` | ATM3, TSL, PHOENIX | 104 | `soviet` (PC sculpt) | 449 × 162 | UNVERIFIED | approved (2026-10-20) |
| `zxnext` | NEXT | 58 | `next` | 330 × 146 | UNVERIFIED (a wedge, ≈35–40 mm at the back) | approved |

The 2D atlas of each is `tools/poc/031-authentic-osd-keyboards/atlas/<id>.photo.atlas.png`; its keys
`data/controllers/keyboards/<id>.keys.json`.

## 2. Keyboards still to come in 2D

| Keyboard | Models | What | Source |
|---|---|---|---|
| ZX matrix only | ATM450 | the 40-key matrix (it had no PC keyboard) | refs-pc §1 |

The 3D project starts on the keyboards of section 1 and takes these as POC 031 draws them.

## 3. Per keyboard: what 3D needs

### `zx48k`
- **Case**: one-piece black case, the raised rear hump ≈40 mm deep, stepping down to the metal keyboard plate (≈100 mm
  deep) which wraps over the front as a thin lip; the moulded double-contour "sinclair" on the hump; the printed
  "ZX Spectrum" and the rainbow stripes on the plate (refs-sinclair §1.1).
- **Caps**: one silicone rubber mat, blue-grey, matte; each key a raised rounded pad ≈14.5 × 10.7 mm, flat top,
  soft edges (radius ≈1.5 mm, UNVERIFIED); the mat's legends printed on the keys, the keywords on the plate.
- **Unknown**: pad height above the plate (≈3 mm?), the hump's height and slope, the plate's thickness.
- **Photos for profile**: `48k-k1-top.jpg` (three-quarter), `48k-bottom.jpg` (underside, feet), `48k-rubber-mat.jpg`.

### `zx128` (and the Spectrum+ it is built on)
- **Case**: the Spectrum+ slab (319 × 149 × 38 mm) with the 58-key keyboard; the 128's heatsink fins on the right
  end (≈24 mm wide, ≈20 fins front to back), the vent grille.
- **Caps**: hard plastic blocks almost touching (gap ≈1 mm), the rear third a flat ledge, the front a convex glossy
  pad (refs-sinclair §2.2); legends on the ledge and the pad.
- **Unknown**: cap height, the pad's curvature, the case's rear and front heights (the case is a wedge).

### `zxplus2`, `zxplus2a`, `zxplus3` (one generator, `keyboards/amstrad.py`)
- **Case**: the long slab with the castellated rear edge (teeth pitch 3.7 mm), the raised rear strip with its grooves,
  the grooves across the margins at the row lines; the Datacorder (+2, +2A: lid, window, spindles, the DATACORDER
  band, six piano-key buttons) or the disc drive panel (+3; the drive slot is in the front face).
- **Caps**: Amstrad sculpted typewriter caps, tapered sides, top face ≈70 % of the pitch, slightly dished
  (cylindrical) top, base gap ≈1.5 mm (refs-sinclair §4).
- **Unknown**: the slab's height profile (a wedge, the rear strip raised), cap height and dish depth, the Datacorder
  buttons' travel. The owner has a +2 (the owner photos `plus2-owner-top*.png`).

### `pentagon`
- **Case**: none: the 40 switches on a bare textolite plate with foam pads, the stickers on black mech caps.
- **Caps**: low black caps of Soviet mechanical switches (МП / ПКН family), a square sticker on each top.
- **Unknown**: switch and cap heights, the plate thickness; whether to show the switch bodies under the caps.

### `pentagon2plus`, `scorpion` (one cap generator)
- **Case**: Pentagon 2+: a black case with a vent grille along the rear right, an LED, the moulded name plaque
  ("ZX SPECTRUM / SINCLAIR", generic, owner 2026-10-20). Scorpion: a dark grey case, the same plaque, three ribs beside
  it and a long one under it, a light grey plate in the recess, a square blank right of BREAK, a front lip.
- **Caps**: Soviet sculpted caps (the MS 7004 family): a flat top face ≈2/3 of the key, narrow sides, a tall front
  slope carrying the E-mode word; cream, grey, red BREAK.
- **Unknown**: the case heights; the cap height and the front slope's angle. The owner has photos of the keyboard
  shot straight down (`pentagon-zxbyte-kbd-top-owner.png`) and on its frame (`pentagon-kbd-frame-owner.png`).

### `quorum`
- **Case**: the ivory desktop case: the rear half a field of vent slots (19 columns × 33 slots), three ledges stepping
  down to the keyboard deck, the "КВОРУМ" badge (red, green, blue cells, white letters on black), the maker's emblem;
  five key wells (the F row, the G / NMI / INV block, the speaker / RES pair, the main block, the keypad); the rear
  panel connectors (photo 2).
- **Caps**: the same Soviet sculpted family as the Pentagon 2+; two-row keys (the L ENTER, the tall ← and →).
- **Unknown**: every height; the case's depth (281 mm is an estimate from a perspective photo).

### `orel`
- **Case**: the separate keyboard unit (the main unit behind it is not drawn): cool grey plastic, the well of 5 rows, 18 moulded ribs across each end, the broad palm rest with one step line.
- **Caps**: sculpted reed-switch caps, white and dark blue-grey, an L ENTER, an 8-key space bar.
- **Unknown**: every height; the main unit (NMI, RESET, the LED, the vents, the label) if the 3D scene wants the whole computer.

### `profi`, `profiplus`
- **Case**: `profi` is the black (navy in the light) БК-0010 case: the raised rear with the "Profi///" badge (a white
  plate, purple rounded "Profi", red / yellow / green slanted stripes), the recess with a plain panel left of the keys
  and a finely textured surround, the broad palm rest in front; `profiplus` has no known photo: the same case, badge
  and surround, a RESET button and an LED (positions UNVERIFIED).
- **Caps**: Soviet sculpted caps (the Pentagon 2+'s sculpt): white with the Spectrum print and Cyrillic, light grey
  modifiers, a red BREAK; `profiplus` the same caps on the album's 40 ZX keys, the inverted-T cursor keys and "*".
- **Unknown**: every height, the palm rest's slope (only a top view); `profiplus` everything beyond the album's
  wiring drawing.

### `pcxt`, `pcat`, `pcps2` (one generator, `keyboards/pc.py`)
- **Case**: plain PC keyboard cases, no brand: the XT's broad raised top over two wells; the Model M-like case with
  the LED window over the keypad; the low PS/2 clone with each block in a shallow frame and three LEDs.
- **Caps**: the standard cylindrical PC sculpt (rows sculpted, OEM-like); two-tone grey on `pcxt` and `pcat`, one
  colour on `pcps2`; Latin black, Cyrillic red; the L Enter on `pcps2`, the tall XT Enter on `pcxt`.
- **Unknown**: the cases' heights and slopes, the per-row cap profile (refs-pc §7: OEM-like, not measured).

### `zxnext`
- **Case**: the wedge case, the rainbow insert in a groove, the vent, the logo moulded black on black, the front DB9
  ports and the left side buttons (refs-next).
- **Caps**: low flat laptop-like caps with a matte top, rounded front corners (radius ≈0.38 u).
- **Unknown**: the wedge's heights, cap height and dish (refs-next "UNVERIFIED").

## 4. Measurements wanted

Before a keyboard's 3D model can be judged, its heights need a source. In order of value:

1. Case height at the rear and at the front edge, and the height of the key deck.
2. Cap height above the deck at rest, and the travel.
3. The cap profile: the top face's size and dish (flat, cylindrical, spherical) and the skirt angle, from a side photo
   of one row.
4. The underside: feet positions and sizes, any recess.

A side photo shot level with a ruler in frame gives all four for one keyboard. The owner's own machines are the best
source (open question 3 of [requirements.md](requirements.md)).
