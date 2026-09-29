# Results - Phase 1 (Python POC)

Run: `python3 python/run_set.py --clip data/clip_full --out out/<version> --alg <version>`
(side-by-side videos raw | processed | class map and metrics JSON per clip in `out/`).

## Metrics

| Metric | Meaning | Target |
|---|---|---|
| raw_chg | share of pixels changing frame to frame in the raw frames | - |
| out_chg | same for the processed output | well below raw_chg where the picture flickers |
| mixed | share of output pixels that differ from raw (= pixels the algorithm mixed) | **0 on negatives** (hard gate: no ghosting) |
| switch | class changes per segment per frame | low (unstable classes flicker themselves) |

## v1 - causal per-segment period detection (2026-09-28)

8x1 segments over the whole frame (picture + border), signature = 8 palette
indices; 10-frame window; strict period 2..5, "recurring set" (2..5 values),
hold on phase slips; per-pixel mixing in linear light.

| Clip | raw_chg | out_chg | switch | Classes |
|---|---|---|---|---|
| pageflip-static | 0.347 | 0.280 | 0.157 | p2 23 %, recur 21 %, hold 12 % |
| hiphop-border | 0.405 | 0.269 | 0.095 | p2 22 %, recur 26 % |
| border-only | 0.284 | 0.011 | 0.013 | p2 29 % |
| balls-floor | 0.568 | 0.459 | 0.174 | p2 27 %, recur 33 % |
| tunnel | 0.491 | 0.382 | 0.106 | recur 63 % |
| irregular | 0.299 | **0.385** | 0.181 | recur 27 % |
| raster-negative | 0.027 | **0.044** | 0.048 | recur 2 % |
| raster-negative-2 | 0.222 | **0.432** | 0.195 | recur 23 % |

**Finding:** the recurring-set rule fires on pure motion (a rotating few-color
shape recurs within 10 frames) and blends its frames into ghosts; border
stripes that scroll were blurred into pastel bands. Output changed more than
raw on both negatives.

## v2 - strict irregular-flicker rule

RECUR only for near-period-2 alternation with slips (≤ 3 distinct values,
≥ 70 % of steps A,B,A, value changes on ≥ 50 % of steps); hold only after a
strict period.

| Clip | raw_chg | out_chg | switch | mixed | Classes |
|---|---|---|---|---|---|
| pageflip-static | 0.347 | **0.181** | 0.051 | 0.244 | p2 23 %, hold 5 % |
| hiphop-border | 0.405 | **0.214** | 0.022 | 0.204 | p2 22 % |
| border-only | 0.284 | **0.008** | 0.008 | 0.276 | p2 29 % |
| balls-floor | 0.568 | 0.379 | 0.141 | 0.419 | p2 27 %, recur 15 % |
| tunnel | 0.491 | 0.526 | 0.207 | 0.350 | p2 8 %, recur 27 % |
| irregular | 0.299 | 0.292 | 0.112 | 0.157 | recur 11 % |
| raster-negative | 0.027 | 0.027 | 0.035 | **0.000** | - |
| raster-negative-2 | 0.222 | 0.222 | 0.029 | **0.000** | - |

**Findings:**
- Negatives are untouched (mixed = 0): the no-ghosting gate holds.
- Static page-flip pictures and border-only flicker are handled: flicker
  mostly gone, scrolling border stripes stay sharp (hiphop).
- Tunnel: the big flickering shapes get the intended mixed colors, but the
  output still changes more than raw - segments on moving edges switch
  between classes (switch 0.207). Moving flicker needs region-level motion
  handling (design §7) rather than per-segment decisions.
- ~50 ms per frame (numpy, unoptimized) - fine for the POC.

## v2 visual review (user)

- balls-floor: ghosts from the bouncing balls;
- irregular: distortions at the edges of the winding stripe;
- pageflip-static: the checker grid scrolling in the background distorted.

All three are motion that looks periodic per pixel (a grid moving one cell
per two frames IS period 2 in place).

## Quality oracle (`python/quality.py`)

Requirement: the processed frame may differ from the raw one only where the
raw picture flickers, and only by the flicker's mixed color - no geometric or
color artefacts, no blur, no ghosting. Measured by an offline, two-sided
(±6 frames) per-pixel oracle:

| Label | Rule | Required output |
|---|---|---|
| motion | changed, matches t-1 shifted by v and t+1 shifted by -v (one velocity) | raw |
| moving flicker | matches t±2 shifted by ∓2v, not motion | motion-compensated mix (in-place mix is an error) |
| periodic / irregular flicker | strict period 2..5, or near-period-2 with slips | its mixed color |
| steady | anything else | raw |

Metrics: **ghost** (mixed where the oracle says steady or motion; hard gate
0), ghost on moving flicker, **missed** flicker, **color** ΔE OKLab p50/p99
against the oracle's mix, flicker residue. The videos get a fourth panel with
the error map (red ghost, orange ghost on moving flicker, yellow missed,
magenta wrong color).

## v3 - motion gate (t vs t-2)

32x32 tiles, best shift |v| ≤ 8 between frame t and t-2 (same phase, the
flicker cancels; comparing with t-1 found nothing because the flicker on the
same tile hid the motion), ties to the smallest shift (ZX content repeats
every 8 pixels). A changed segment matching the shifted t-2 is motion: raw
for this and the next 2 frames.

| Clip | ghost | missed | bad color |
|---|---|---|---|
| pageflip-static | 0.038 | 0.022 | 0.000 |
| balls-floor | 0.086 | 0.271 | 0.322 |

## v4 - tile-level motion rule

The scrolling checker pauses one frame in five, so its cell edges looked like
period 5 and escaped the per-segment match. A tile that moved in the last 6
frames admits only strict period-2 in-place segments.

| Clip | ghost | missed | bad color |
|---|---|---|---|
| pageflip-static | 0.016 | 0.022 | 0.000 |
| balls-floor | 0.065 | 0.372 | 0.253 |

**Finding:** pixel colors alone hit a ceiling - a moving grid edge or the halo
of a bouncing ball cannot be told from flicker without knowing whether the
bitmap or the attributes changed. Memory snapshots are not enough either: a
memory-decoded screen matches the picture 100 % in parts without multicolor
and only 77–92 % in multicolor parts (balls, tunnel, Hip-Hop King). Hence
plane B in the renderer (P0a,
[p0a-plane-b.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/p0a-plane-b.md)),
and the core clip export: `data/clip_v2` holds RGBA + plane B for all 16 571
frames (`capture/verify_clip_v2.py` - 0 failures).

## v5 - mod-tpgw (2026-09-28 .. 29): from rules to a mixture of detectors

The final algorithm is specified completely in
[algorithm-mod-tpgw.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/algorithm-mod-tpgw.md);
this is how it got there. Every step was checked on the golden scenes, the
later ones with the regression gate (`python/regress.py`).

| Version | Idea | What it taught |
|---|---|---|
| v5 | plane B signatures (bitmap + attribute), split mode, sustain | static GigaScreen clean; a ball-shaped raw patch trails every ball (10-frame window + 8-frame sustain); RECUR/split mixed 10 frames - a delayed ghost |
| v6 | per-pixel mask over the last 2..5 frames only; the render rule "at most 5 raw frames" | long-lived memory of any kind goes stale on dynamic scenes; key = bitmap byte + attribute (cell key) separates a red ink ball from a red ink stripe; period 2 only + 6-frame confirmation: 0 ghosts on every clip |
| v6la | remembered background pair + one frame of look-ahead | the strip a sprite vacates mixes at once when t+1 is visible |
| v7 | independent detectors P = 2..5, brightness veto, translation veto, consensus | the rabbit (R-G-B, period 3) mixes; the veto must cover the whole decision window and not require a constant velocity; 3 whole periods per detector |
| v8 | mix only cycles established >= 16 frames, adopt a long-held neighbor cycle | balls and their ghosts stay raw, but a moving floor never establishes; no threshold separates floor from ball |
| v9 | window around t with 6 frames of look-ahead, no memory | the balls work (user: from look-ahead 6 on) |
| v10 | + field mode for two-page moving textures (16x16 tiles alternating their color set, unexplained by the pixel stage, >= 12 tiles) | the spiral's lattice mixes; accepted on every scene but the snake |
| MoD | the same as a mixture of detectors: frame context with a shared feature cache, registry, classifier, decision graph, consensus | reproduces v10 exactly (gate: no regression) |
| mod-tpgw | field stage: two-page motion compensation (other page at t from t-1 and t+1), growth, whole-paper mode with scene hysteresis, override of the pixel stage | spiral: color within 2 % 78 -> 95 %, edges p95 5.0 -> 2.4 px, shimmer 0.050 -> 0.001; accepted |
| mod-tpgwr | + per-pixel motion vector refinement | 786 pixels of a frame change, no metric moves: the snake's staircase edges are in the source; rejected |

Oracles grew along the way (see the spec, section 10): oracle 1 (per pixel)
calls the blend of a moving two-page texture a ghost; oracle 2 (several
references, median consensus) judges how to mix where mixing is warranted;
oracle 3 (pixel XOR + rhythm + a flash detector) keeps one-frame flashes out.
The gate takes their consensus.

Next: the C++ prototype in `tools/verification/zxdlss` (renders TTD files and
clips through swappable algorithms; `mod-tpgw` first, verified against this
reference, then optimized).
