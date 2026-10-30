# smoothmotion - smoothness of 50 -> 60 Hz frame rate conversion

Measures how smoothly a smoothing filter (smooth motion of the
[frame presenter](../../../docs/inprogress/2026-10-26-frame-presenter/README.md), or an experiment of
[POC 039](../../poc/039-smoothing-filters/README.md)) shows motion, from the pictures alone: the source frames and the
pictures it put on the display, refresh by refresh. Built like the
[ZX DLSS verification](../zxdlss/README.md): synthetic programs with a known answer, independent oracles, hard gates
and a baseline.

| Part | What it is |
|---|---|
| [`testdata/video/smoothmotion`](../../../testdata/video/smoothmotion/README.md) | the synthetic scenes (Z80, `.sna` / `.tap` / `.trd`) and their ground truth |
| `smoothness.py` | the measurement: source frames + a filter's refreshes -> numbers per region and filter |
| `regress.py` | hard gates and the baseline comparison of `smoothness.py` results |
| `baseline.json` | the measured numbers of the current filters on the synthetic scenes |

## What is measured

The eye follows a moving object at its average speed. A refresh is right when it shows the object where that steady
motion has it at the refresh's time, sharp and whole. So per region that moves rigidly in the source:

1. **Source track**: the shift from frame k to k+1 that matches most pixels; summed, the position p[k]. A pair no shift
   explains (a wrap, a cut) breaks the track; refreshes near it are not scored.
2. **Ideal trajectory**: a line fitted to p over a window around the refresh time, as long as a multiple of the
   region's cadence (a game that moves every 3rd frame is ideally a steady third of its step per frame).
3. **Oracle A (mixture)**: the refresh is decomposed by non-negative least squares into copies of the source frames
   shifted near the ideal position. Weighted mean of the copies' positions = the shown position; their weighted spread
   = ghost; what no mix explains = tear.
4. **Oracle B (ideal picture)**: the refresh against the source placed at the ideal position rounded to whole pixels
   (the best picture without blending), normalised squared error. It knows nothing of how filters work.
5. **Oracle C (ground truth)**, synthetic scenes only: every unbroken stretch of the source track must be exactly the
   program's trajectory (`floor(floor(n / period) * step16 / 16)` px for some frame phase).
6. **Static pixels**: outside the regions, pixels that do not change in the source must be the source's.

| Number | Meaning |
|---|---|
| `jstep` | RMS of the frame-to-frame change of the position error (px): the jerk. A repeated 8 px step jumps 8 px on every 6th refresh |
| `floor` | `jstep` of the ideal position rounded to whole pixels: the best a picture without sub-pixel blending can do |
| `judder` | RMS of the position error about its mean (px) |
| `lag ms` | mean position error as time (positive: behind) |
| `ghost`, `ghost%` | mean spread of the copies (px); share of refreshes with more than 0.75 px |
| `tear%`, `tear95` | mean and 95th percentile of the unexplained share of the region |
| `pic%` | oracle B, mean |
| `static` | static pixels changed, mean per checked refresh |

`jstep` alone rewards blending (the mean of a blend moves steadily); `ghost` shows its price. A filter is smooth when
`jstep` is near `floor` with `ghost` and `tear` near 0, and oracle B agrees.

## Run

```
# the scenes: build (or use the committed files), capture through the WebAPI, render the filters, measure
SJASMPLUS=/path/to/sjasmplus testdata/video/smoothmotion/build.sh
python3 -I tools/poc/039-smoothing-filters/capture.py --port <webapi> --out scratch/smoothcap --warmup 50 --frames 300 \
    testdata/video/smoothmotion/smooth-speeds.sna
FILTERS_RAW=1 scratch/2026-10-28-poc-039/filters scratch/smoothcap/smooth-speeds.rgba 352 288 300 \
    scratch/smoothcap/speeds data/rom/48.rom
python3 -I tools/verification/smoothmotion/smoothness.py --source scratch/smoothcap/smooth-speeds.rgba \
    --truth testdata/video/smoothmotion/truth/speeds.json --out-dir scratch/smoothcap/speeds --json scratch/smoothcap/speeds.json
python3 -I tools/verification/smoothmotion/regress.py --baseline tools/verification/smoothmotion/baseline.json \
    scratch/smoothcap/speeds.json

# any capture (a game): 16-line strips of the paper instead of the ground truth
python3 -I tools/verification/smoothmotion/smoothness.py --source <clip>.rgba --out-dir <filters' pictures>
```

The filters tool renders refresh r at `3 ms + r / 60 s`, frame k due at `k / 50 s` (`--phase-ms`, `--src-hz`,
`--disp-hz` change that). numpy and scipy; one worker process per region (`--jobs`).
