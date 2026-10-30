# Smooth-motion test scenes

Synthetic ZX Spectrum programs for the 50 -> 60 Hz smoothing filters (smooth motion,
[frame presenter](../../../docs/inprogress/2026-10-26-frame-presenter/README.md)) and their measurement
([tools/verification/smoothmotion](../../../tools/verification/smoothmotion/README.md)). Every object moves by a known
rule, so the motion a filter should show is known exactly.

| File | Scene |
|---|---|
| `smooth-speeds.*` | eight balls (16 x 16) moving right at 0.5, 1, 1.5, 2, 3, 4, 6, 8 px per frame |
| `smooth-scroll.*` | text bands scrolling left at 0.5, 1, 2, 3 px per frame; character grids scrolling up at 1 and 2 px |
| `smooth-cadence.*` | balls moving the way games draw: 2N px every N frames (N = 1..6, the same 2 px/frame on average), 2 px every 2nd frame, 6 px every 2nd |
| `smooth-dots.*` | a dot-matrix scroller on an 8 px grid (Aleste's kind) at 8 and 4 px per frame: the motion is as long as the dot pitch |
| `truth/<scene>.json` | the regions (rectangles in paper pixels) and their motion: axis, direction, `step16` / 16 px every `period` frames |

Each scene comes as a 128K snapshot (`.sna`), a tape with a BASIC loader (`.tap`, `LOAD ""`) and a TR-DOS disk with a
`boot` (`.trd`). 128K or Pentagon. The labels at the left name the speed of their row.

## How the programs work

[`src/smoothtest.a80`](src/smoothtest.a80) holds the code; `src/smooth-<scene>.a80` picks the scene and the file names.

- Double buffering on the two 128K screens: the picture of frame n+1 is drawn into the hidden screen while frame n
  is shown, and port `#7FFD` switches the screens right after the next interrupt. Nothing flickers; every frame shows
  one complete step of every object.
- Positions in 1/16 px, advanced once per frame (or every `period` frames) by `step16`: the trajectory of every object
  is exactly `floor(floor(n / period) * step16 / 16)` px within a wrap, nothing depends on timing.
- Texts and grids are pre-shifted tables (8 shifts of the texture); balls are pre-shifted sprites drawn 5 bytes wide,
  the two leading zero bytes clear what the screen showed two frames earlier.
- Self-check: the program counts frames at `#5B00` (word) and pictures that were not ready by the next interrupt at
  `#5B02` (word; the border turns red). A test reads both: `GET /emulator/{id}/memory/page/ram/5?offset=6912&length=4`.
  On a Pentagon every scene draws well inside a frame (`#5B02` stays 0).

## Build

```
SJASMPLUS=/path/to/sjasmplus testdata/video/smoothmotion/build.sh
```

sjasmplus 1.20 or newer; writes the 12 files next to the script.
