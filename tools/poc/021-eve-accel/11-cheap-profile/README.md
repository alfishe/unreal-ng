# 11 - The CHEAP profile: just enough picture

Feeds: **CHEAP** ("almost a slideshow": a picture exists and changes, at minimum cost).

## Goal

Measure shortcuts that cut the drawing cost, and which of them keep every chip answer
(register reads, interrupts, coprocessor progress) exact, so that software timing is not
affected.

## Variants (all implemented; options of the `threads` variant and of eve-replay)

| Variant | How | Picture | Chip answers |
|---|---|---|---|
| skip unchanged frames (`EVE_POC_SKIP_UNCHANGED=1`) | a line is not drawn while nothing it reads has changed since the last frame drawn completely after the last change (a swap, a drawing register, a `RAM_G` write, `REG_TAG_X/Y`, a new output buffer) | **exact** | exact |
| ... with the read set (`+ EVE_POC_DEFER=1`) | only `RAM_G` writes the active list reads count as changes | **exact** | exact |
| draw 1 frame in N (`--draw-every N`) | `EveSetOutput(drawing = 0)` on the other frames: the host keeps the last picture | stale on skipped frames | exact except `REG_TAG` and line costs on skipped frames |
| half / quarter vertical resolution (`EVE_POC_LINE_STEP=2/4`) | draw every Nth line, copy it to the next | approximate | `REG_TAG` approximate if `REG_TAG_Y` is a copied line |
| draw on demand | the host presents frames at its own rate: equivalent to draw-every with the host's ratio | stale between presents | as draw-every |
| no drawing (`--no-draw`) | lower bound | none | `REG_TAG` frozen |

Not implemented: half horizontal resolution (every span and the texture stepping would
change; the line step gives the same order of saving with no risk to the other code),
skipping antialiasing or blends (R-Type and Zuma barely use primitives; blends are the
picture).

Everything that drives the chip's timing - the scan, `REG_FRAMES`, interrupts, swaps, the
coprocessor and its ring - is independent of drawing in eve-emu, so every variant keeps
those answers exact; only values derived from the drawn picture (`REG_TAG`, line costs)
can differ, and only in the inexact variants.

```
tools/poc/021-eve-accel/11-cheap-profile/run.sh check   # every check on, per variant
tools/poc/021-eve-accel/11-cheap-profile/run.sh time    # CPU per chip second
```

## Results

Only the first check ran before the user stopped the session (rtype-boot, picture hashes
and answers checked):

| Variant | Answers | Pictures | Lines not drawn |
|---|---|---|---|
| skip unchanged | exact | **exact** (0 mismatches) | 275 824 of 780 176 (35 %) |
| skip unchanged + read set | exact | **exact** | 305 776 (39 %) |

CPU estimates for the others follow from the line counts: drawing is ~85-95 % of the
replay's CPU time on these captures (01: profile; `--no-draw` is the floor), so
draw-every-N costs about 1/N of the drawing and line step 2 about half.

## Analysis

- The exact shortcut is the interesting one: a third of boot's lines do not need drawing
  at all, with bit-exact pictures and answers. It is free to combine with every other
  profile (CPU threads, GPU).
- The inexact ones are trivially cheap to add (a frame counter, a row copy) and keep the
  chip's timing answers exact, which is what software timing depends on.

## Conclusions

- CHEAP = skip-unchanged (exact) + draw-every-N or line-step as a user setting; an estimated
  ~10 % of one core for R-Type with N = 4. Run `run.sh time` on a quiet machine for the per-variant
  CPU figures.
- Windows / Linux: nothing platform-specific.
