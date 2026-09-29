# ZX DLSS GigaScreen de-flicker - final algorithm `mod-tpgw`

Status: accepted on all golden scenes (2026-09-29), end of the Python POC
(`tools/poc/019-zxdlss-gigascreen`). This document specifies the algorithm
completely: an implementation written only from it must reproduce the
reference implementation's output (Python: `python/mod/`, `python/twopage.py`,
`python/mixers.py`; C++: `tools/verification/zxdlss/`).

Related: [requirements](requirements.md), [plane B](p0a-plane-b.md),
[mixers](design-mixers.md), [optimization ideas](optimization-ideas.md),
POC [results](../../../tools/poc/019-zxdlss-gigascreen/results.md), C++ prototype
[walkthrough](cpp-prototype-walkthrough.md).

---

## 1. What it does

A ZX Spectrum program that alternates two (or more) pictures every frame
("GigaScreen") relies on the CRT and the eye to blend them into colors the
machine cannot show. An LCD at 50 Hz shows raw flicker instead. The algorithm
outputs, for every emulated frame t, a picture where

- pixels that flicker with a period P = 2..5 show the average of the P frames of
  the cycle (the blended color the author intended);
- everything else (static pictures, moving objects, flashes, scrolling textures)
  is the raw frame t, bit for bit.

**Render rule (a hard constraint).** A mixed pixel is a weighted average of at
most 5 raw frames around t (some of them moved by a motion vector); an unmixed
pixel is the raw frame t. No other image processing (no blur, no spatial
filter, no memory of older pictures in the output).

**Look-ahead.** Decisions for frame t use frames up to t+6: the output is
presented 6 frames (120 ms at 50 Hz) after the frame is emulated.

## 2. Glossary

| Term | Meaning |
|---|---|
| frame | one emulated 50 Hz picture, 352 x 288 pixels (border + 256 x 192 paper) |
| paper | the 256 x 192 bitmap area: rows 48..239, columns 48..303 |
| border | every pixel outside the paper |
| color index | 0..15 (bright x 8 + color), the ZX palette entry of a pixel |
| plane B | per-pixel record written by the renderer: attribute byte used, color index, ink bit, role (paper/border); see [p0a-plane-b.md](p0a-plane-b.md) |
| key | per-pixel identity of what was drawn (section 4.2) |
| period P | the pixel's key repeats every P frames |
| two-page scene | the program shows two different pictures on alternate frames (screen pages 5 and 7), possibly moving |
| tile | a square block of pixels (16 x 16 for the field detector, 32 x 32 for the translation detector, 8 x 8 for motion compensation) |

## 3. Input

Per frame, from the emulator (or a clip exported with `POST /ttd/export-clip`):

- `plane`: H x W color indices (uint8, 0..15) - bits 8..11 of plane B;
- `attr`: H x W attribute bytes - bits 0..7 of plane B (0 on the border);
- `ink`: H x W ink bits - bit 12 of plane B.

H = 288, W = 352. All tile grids below divide these exactly.

### 3.1 Palette

RGB of the 16 color indices (the emulator's palette):

| idx | RGB | idx | RGB |
|---|---|---|---|
| 0 | 0, 0, 0 | 8 | 0, 0, 0 |
| 1 | 0, 34, 199 | 9 | 0, 43, 251 |
| 2 | 214, 40, 22 | 10 | 255, 51, 28 |
| 3 | 212, 51, 199 | 11 | 255, 64, 252 |
| 4 | 0, 197, 37 | 12 | 0, 249, 47 |
| 5 | 0, 199, 201 | 13 | 0, 251, 254 |
| 6 | 204, 200, 42 | 14 | 255, 252, 54 |
| 7 | 202, 202, 202 | 15 | 255, 255, 255 |

Brightness (luma) of index c: `luma[c] = 0.299 R + 0.587 G + 0.114 B` (on the
0..255 values, float32).

## 4. Frame context

### 4.1 Ring of frames

Frames are kept newest first: index 0 is the frame just received, index `L` is
the frame t being rendered, index `L + k` is t - k. Constants:

- look-ahead `LA = 6`;
- ring depth `D = LA + 15 + 2 = 23` frames (15 = the longest period run, section 5);
- `n` = number of frames held (grows from 1 to D at start-up);
- `L = min(LA, n - 1)` (at start-up t is the oldest frame held).

Feeding: the caller pushes frame f and receives the output for frame f - LA
(during the first LA pushes the outputs are for the oldest frame held and are
discarded by the tools). If fewer than 3 frames are held, the output is raw
frame t.

### 4.2 Key

- paper pixel (y, x): `byte` = the 8 ink bits of its 8-pixel segment
  (x rounded down to a multiple of 8), leftmost pixel = bit 7;
  `key = (1 << 20) | (byte << 8) | attr(y, x)`;
- border pixel: `key = plane(y, x)` (the color index).

Two pixels drawn from the same bitmap byte and attribute have the same key, so a
sprite that changes the byte or the attribute of a cell never matches the
background it covers even when the colors coincide.

### 4.3 Translation between two frames (`motion(i)`)

For ring index i (i + 1 < n), with `cur = plane(i)`, `prev = plane(i + 1)`:

1. For every shift (dy, dx), dy and dx in -8..8 (dy outer loop, dx inner loop,
   ascending): per 32 x 32 tile (9 x 11 tiles),
   `cost[dy,dx] = number of pixels with cur(y, x) != prev((y - dy) mod H, (x - dx) mod W)`.
2. `cost0` = cost of (0, 0). Best non-zero shift = the first minimum in the loop
   order above, excluding (0, 0).
3. The tile is moving when `best_cost < 0.25 * cost0` and `cost0 > 0.02 * 1024`.
4. `motion(i)(y, x) = moving(tile) and cur(y, x) == prev((y - dy) mod H, (x - dx) mod W) and cur(y, x) != prev(y, x)`
   with (dy, dx) the tile's best shift.

The result depends only on the two frames; cache it per frame pair.

### 4.4 Tile histograms

Per 16 x 16 tile (18 x 22 tiles) and frame i: `hist(i)[tile][c]` = number of
pixels of color index c (16 bins).

## 5. Pixel stage: period detectors P = 2, 3, 4, 5

Each period P is an independent detector with

- `span(P) = max(2P, 6, 3P)` = 6, 9, 12, 15 frames (three whole periods);
- the runs considered are the windows `[a, a + span - 1]` of ring indices that
  contain t: `a` from `max(0, L - span + 1)` to `min(L, n - span)` inclusive,
  ascending (the most future run first).

For each run, per pixel not yet claimed by an earlier run of the same detector:

1. **Periodic**: `key(j) == key(j + P)` for every j in `[a, a + span - P - 1]`,
   and not constant: not all of `key(a + 1) .. key(a + P - 1)` equal `key(a)`.
2. **Series**: the P frames of the run that contain t, as old as possible:
   `b = max(min(L, a + span - P), a, L - P + 1)`; the series is ring indices
   `b .. b + P - 1`.
3. **Brightness veto**: the series must differ in brightness: some
   `luma[plane(b + j)] != luma[plane(b)]`, j = 1..P-1. Equal brightness does not
   flicker to the eye.
4. **Translation veto** (P >= 3 only): no `motion(i)` at the pixel for any i in
   `[a, a + span - 2]` (a texture moving at a constant speed repeats per pixel;
   the veto covers the whole run the decision was made on). P = 2 is not
   vetoed: A,B,A,B at 50 Hz is seen as the blend, moving or not.
5. A pixel passing 1-4 is claimed by this detector with series start b.

Proposal: claimed pixels, confidence 1, rank P, recipe = weight `1/P` on each
frame of the series.

**Consensus of the pixel stage.** Per pixel, among the detectors that claimed
it, the lowest rank wins (the smallest period). Unclaimed pixels keep the raw
recipe (weight 1 on frame t).

After the stage, render (section 8) and compute
`explained = pixels whose rendered RGB differs from raw t` (a recipe that
renders equal to raw explains nothing).

## 6. Scene classifier

`two_page` = share of 16 x 16 tiles whose color set alternates (`set_alt`,
section 7.1), computed when `n >= L + 3`; otherwise 0.

The field stage runs only when `two_page > 0`; otherwise its detector's
`skip()` is called (section 7.5).

## 7. Field stage: two-page textures that move

Handles scenes where the two pages are different pictures that move (a lattice
scrolling under a moving snake): per pixel there is no period at all, but every
16 x 16 tile keeps alternating its colors. Runs after the pixel stage and may
override it (a patchwork of pixel-stage mixes and raw pixels was the artifact).

Constants: `field_on = 0.15`, `field_off = 0.08`, `unexplained = 0.25`,
`min_tiles = 12`, `present = 4`, `grow_min = 24`, `whole = 0.3`,
`whole_off = 0.1`, `whole_hold = 12`. Paper tiles: tile rows 3..14, columns
3..18. Requires `n >= L + 3` and `L >= 1`, else no proposal.

### 7.1 Tile features around t

For ring indices i from `max(0, L - 3)` to `min(n - 2, L + 4) - 1` (newest
first, i.e. frames t+3 .. t-3 as available), with `h_k = hist(k)`:

- `alt_i = (|h_i - h_{i+1}|_1 - |h_i - h_{i+2}|_1) / (2 * 256)`;
- `s_k = h_k >= present` (the set of colors present with >= 4 pixels);
- `vote_i = (s_i == s_{i+2} for all 16 colors) and (s_i != s_{i+1} for some color)`.

`alt = mean(alt_i)`; `set_alt = 2 * sum(vote_i) > count(i)` (a strict majority).

### 7.2 Unexplained flicker per tile

`un(tile)` = share of the tile's pixels with `plane(L) != plane(L + 1)` and not
`explained` (section 5).

### 7.3 Seeds (hysteresis state `seeds`, initially empty)

```
cand  = (seeds_prev ? alt >= field_off : (alt >= field_on and un >= unexplained)) and set_alt
seeds = 4-connected components of cand with >= min_tiles tiles (whole tile grid)
```

### 7.4 Growth and the whole-paper mode

```
region = alt >= field_on and set_alt and paper
big    = 4-connected components of (seeds and paper) with >= grow_min tiles
grown  = big, grown 4-connectedly through region, union big
field  = seeds or (dilate4(grown) and paper)          # dilate4: +1 tile up/down/left/right
share  = mean of field over the paper tiles
if share >= whole:               whole_on = true, below = 0
elif whole_on:
    below = below + 1 if share < whole_off else 0
    if below >= whole_hold:      whole_on = false, below = 0
if whole_on:                     field = field or paper
```

(Hysteresis lives only on the seeds and on `whole_on`: a grown field kept as
state sustained itself on every alternating tile. Growth stays inside the paper:
the border's scrolling raster bars are one page moving, not two pages.)

### 7.5 skip()

When the stage does not run: `field` and `seeds` are cleared; if `whole_on`,
`below += 1` and `whole_on` turns off at `below >= whole_hold`.

### 7.6 Proposal

`px` = pixels of the field tiles (the stage may re-claim explained pixels).
Recipe on px: 1/2 frame t + 1/4 `page_prev` + 1/4 `page_next`, where
`page_prev`, `page_next` are the other page at instant t (section 7.7).
Confidence 1, rank 10. The stage's consensus claims every px pixel and replaces
its pixel-stage recipe.

### 7.7 Two-page motion compensation (`other_page`)

`prev = plane(L + 1)` (t-1) and `next = plane(L - 1)` (t+1) show the SAME page,
moved; matching them is reliable (t and t-1 are different pictures).

Block motion on the midway grid (8 x 8 blocks, 36 x 44 blocks, radius 8): for
every shift v = (dy, dx), dy outer / dx inner from -8 to 8 ascending,

```
ay = floor(dy / 2), ax = floor(dx / 2)        # floor division (-3 -> -2)
by = dy - ay,       bx = dx - ax
A(y, x) = prev((y - ay) mod H, (x - ax) mod W)
B(y, x) = next((y + by) mod H, (x + bx) mod W)
cost_v(block) = number of pixels with A != B + 0.001 * (|dy| + |dx|)
```

Each block takes the first strictly smaller cost in the loop order. Per pixel,
with its block's v: `page_prev(y, x) = A(y, x)`, `page_next(y, x) = B(y, x)`.

(A per-pixel refinement among the 3 x 3 neighboring blocks' vectors was tried:
it changed 786 pixels of a frame and no metric - the staircase edges are in the
source; not part of the algorithm.)

## 8. Render

A recipe is a set of (source, weight) with weights summing to 1: sources are
ring frames (raw planes) or the moved planes `page_prev`, `page_next`.

Mixing in linear light:

```
srgb_to_linear(c) = c/255 <= 0.04045 ? (c/255) / 12.92 : ((c/255 + 0.055) / 1.055) ^ 2.4
linear_to_srgb(v) = v = clamp(v, 0, 1); (v <= 0.0031308 ? v * 12.92 : 1.055 * v^(1/2.4) - 0.055) * 255
out = round_half_even(linear_to_srgb(sum over sources of weight * srgb_to_linear(palette[source index])))
```

per channel, in double precision. (An implementation may tabulate mixes of
equal weights - the output values are the same.)

## 9. Whole algorithm per frame

```
push frame; L = min(6, n - 1)
if n < 3: return raw plane(L)
recipe = raw t everywhere
pixel stage (section 5) -> recipe, explained
two_page = classifier (section 6)
if two_page > 0: field stage (section 7) -> override recipe on field pixels
else: field.skip()
return render(recipe)
```

## 10. How it was validated

### 10.1 Golden scenes

| Scene | Clip frames | What it holds |
|---|---|---|
| ate-pageflip-static | 2700..2900 | static GigaScreen logo over a moving checker grid |
| ate-hiphop-border | 4600..4800 | GigaScreen picture, scrolling border raster bars |
| ate-border-only | 5300..5500 | border flicker only |
| ate-balls-floor | 8000..8200 | GigaScreen-shaded balls over flickering stripes and a moving floor |
| ate-tunnel | 10000..10200 | moving two-page tunnel |
| ate-irregular | 12100..12300 | two lattices on two pages, scrolling, under a moving snake |
| ate-raster-negative | 3500..3700 | one-frame inverted flashes |
| ate-raster-negative-2 | 7300..7500 | rotating raster-band vase |
| flicker-test | 1501..3405 | test program: GigaScreen palette, gamma patterns, R-G-B rabbit (period 3), 1 px and 8 px moving squares |

Across the Edge by Demarche (`testdata/loaders/trd/across_the_edge_by_demarche.trd`)
and `testdata/flicker/flickering_test.tap`, recorded as TTD sessions and exported
as clips with plane B (POC `walkthrough.md`).

### 10.2 Three oracles

No single oracle judges every scene; each scene is scored by three.

- **Oracle 1, per pixel** (`python/quality.py`). Two-sided +-6 frames: a pixel
  is flicker when its color is strictly periodic (P 2..5) over the window, or
  irregular (<= 3 values, mostly A,B,A), or strictly alternating in any 5-frame
  window containing t; motion (one constant tile velocity explaining t-1 -> t ->
  t+1, value progressing) is not flicker. Metrics: ghost (mixed where not
  flicker), ghost-mf (mixed in place on moving flicker), missed, color error.
  Weak on moving two-page textures.
- **Oracle 2, multi-reference** (`python/oracle2.py`). References:
  1/4 t-1 + 1/2 t + 1/4 t+1; 1/2 t + 1/2 other page (section 7.7); oracle 1's
  period mix where defined. Consensus reference = component-wise median in
  OKLab. Judges HOW to mix, only where oracle 1 or 3 says mixing is warranted.
  Metrics: color_ok (share within dE 0.02), dE p95, edge_p95 (px), shimmer.
- **Oracle 3, XOR + rhythm + flashes** (`python/oracle3.py`). Per pixel and P:
  the run of color(k) == color(k - P) through t must cover two whole periods;
  a flash (a lone tile-brightness spike that does not repeat at any period)
  inside the run cancels it. Reference: the mean of the P frames, else raw.
  Metric: agree.

### 10.3 Regression gate

`python/regress.py` runs all scenes, scores them with the three oracles and
compares with `python/baseline.json`. Per scene: REGRESSION when some oracle
says worse and none says better; disputed when one says worse and another
better (shown to a human); ok otherwise. An oracle says worse when any of its
metrics is worse beyond tolerance, better when some is better and none worse.

### 10.4 Results of `mod-tpgw` (the baseline)

| Scene | Oracle 1: ghost / missed | Oracle 2: color_ok / dE p95 / edge p95 | Oracle 3 |
|---|---|---|---|
| pageflip | 0 / 4.4 % | 0.928 / 0.086 / 3.9 px | 0.917 |
| hiphop | 0.83 % / 12 % | 0.856 / 0.195 / 2.1 px | 0.881 |
| border-only | 0 / 0 | 1.000 / 0 / 0 px | 1.000 |
| balls | 0.01 % / 8.4 % | 0.964 / 0.003 / 1.9 px | 0.756 |
| tunnel | 1.2 % / 26 % | 0.801 / 0.120 / 4.0 px | 0.523 |
| irregular spiral | 5.0 % / 11 % | 0.952 / 0.013 / 2.4 px | 0.494 |
| raster-negative | 0 / 0 | - (nothing to mix) | 1.000 |
| raster-negative-2 | 0 / 48 % of a ~0 % share | - | 0.281 |
| flicker-test | 0 / 0.05 % | 0.997 / 0 / 0 px | 0.998 |

Oracle 1's ghost figures on the tunnel and the spiral are its known weakness
(it calls the blend of a moving two-page texture a ghost); the scenes were
accepted visually.

## 11. Design decisions and rejected alternatives

| Decision | Why |
|---|---|
| Key = bitmap byte + attribute, not the color | a red ball drawn in ink over a red/yellow ink stripe matched the stripe's red phase and mixed pieces of the ball |
| Render only 2..5 raw frames | a long history (10-frame windows, remembered backgrounds) left ball-shaped patches half a second behind the ball |
| 6 frames of look-ahead | the background a sprite uncovers mixes at once; a past-only decision waited for a new confirmation (a raw cap trailing every ball) |
| Three whole periods per detector | two periods of P3 matched short chance patterns (a border blinking 1,0,0,1,0,0 once) |
| Translation veto over the whole run, P >= 3 only | the pageflip checker grid moves unevenly and repeats per pixel (period 5); a veto on frame t alone missed a bouncing square lighting a pixel once every 5 frames |
| Field seeds need unexplained flicker | the balls' sky alternates too, but the pixel stage explains it; only the spiral (and tunnel) leave flicker unexplained |
| Color SET alternation, not only histogram shares | scrolling raster bars show every color in every frame |
| Two-page motion compensation instead of 1/4-1/2-1/4 | blends the pages at the same instant: crisp lattice and snake (edges 5.0 -> 2.4 px) |
| Whole-paper mode with scene hysteresis | the spiral's seeds shrink for single frames and the mode blinked between methods |
| No per-pixel vector refinement | no measurable gain (section 7.7) |

## 12. Cost (Python reference)

About 0.25-0.4 s per frame in NumPy. The expensive parts are the translation
detector (289 shifts x full frame per frame pair), the two-page block motion
(289 shifts per frame) and the period runs (up to 10 runs x span compares per
detector). All are per-pixel compares and counts: SIMD candidates (O-series in
[optimization-ideas.md](optimization-ideas.md)).
