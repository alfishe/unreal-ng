# Smooth-motion test programs

Synthetic ZX Spectrum programs for the 50 -> 60 Hz smoothing filters (smooth motion,
[frame presenter](../../../docs/inprogress/2026-10-26-frame-presenter/README.md)) and their measurement
([tools/verification/smoothmotion](../../../tools/verification/smoothmotion/README.md)). Every object moves by a known
rule, so the motion a filter should show is known exactly.

This folder holds only the built programs and their truth. The Z80 sources, the build, the docs and the programs'
own checker live in their own repo, `zx-smooth-motion-tests` (locally
`/Volumes/TB4-4Tb/Projects/ZX-Spectrum/zx-smooth-motion-tests`): change them there, never here.

| File | What it is |
|---|---|
| `bin/smooth-<scene>.sna` | the program as a 128K snapshot (Pentagon 128K), starting on its scene |
| `bin/SHA256SUMS` | the snapshots' sums, as the source repo's build produced them |
| `truth/<scene>.json` | per scene: the regions (rectangles in paper pixels) and their motion |
| `truth/action.json` | the action screen: balls, vectors and dots by start and speed, with the formulas in `note` |

Every program holds six screens; keys 1..6 pick one, any other key goes to the next. The five programs differ only
in the screen they start on:

| Key | Program | Scene |
|---|---|---|
| 1 | `smooth-speeds` | eight 16 x 16 balls moving right at 0.5, 1, 1.5, 2, 3, 4, 6, 8 px per frame |
| 2 | `smooth-scroll` | text bands scrolling left at 0.5, 1, 2, 3 px per frame; character grids scrolling up at 1 and 2 px |
| 3 | `smooth-cadence` | balls moving 2N px every N frames (N = 1..5), plus 2 px and 6 px every 2nd frame |
| 4 | `smooth-dots` | a dot-matrix scroller on an 8 px grid at 8 and 4 px per frame |
| 5 | `smooth-mixed` | a dashed box whose sides run round it while its text bounces; bands moving apart; a grid next to a text |
| 6 | (any, key 6) | action: diagonal bouncing balls, rotating vector shapes, flying dots |

## Truth model

A scene file lists regions whose content moves rigidly: `rect` (paper pixels; `paper` is the border added on each
side), `axis` and `dir`, then either `step16` / 16 px once every `period` frames or a repeating list of `offsets`.
`speeds` and `cadence` add `end` and `cycle`: in a cycle of `cycle` pictures, picture k = n mod `cycle` shows the ball
at x = floor(`step16` * floor(k / `period`) / 16) while x < `end`; past it the ball is gone until the cycle starts
again. n is the program's frame counter of the screen (`#5B05`), 0 on the first picture after the snapshot starts.
The other scenes wrap their content and have no `end`.

`smoothness.py` reads the region files (oracle C: the source track against the program's motion). `action.json`
has no regions; the source repo's `tools/truth.py` renders it pixel for pixel.

The programs write a self-check block every frame at `#5B00` (frames shown, pictures late, the screen shown, its frame
counter, spare time, the last `#7FFD` value); on a Pentagon the late count stays 0.

## Updating

Build in the source repo (`./build.sh`, sjasmplus 1.20 or newer; it checks the bytes against its `bin/SHA256SUMS`),
then copy here and re-measure:

```
tools/verification/smoothmotion/sync-testdata.sh /Volumes/TB4-4Tb/Projects/ZX-Spectrum/zx-smooth-motion-tests
tools/verification/smoothmotion/run_scenes.sh <webapi port> --update
```

Only the `.sna` files are copied: the tools load snapshots, and the repo's `.tap` and `.trd` hold the same code.
