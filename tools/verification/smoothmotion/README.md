# smoothmotion - smoothness of 50 -> 60 Hz frame rate conversion

Measures how smoothly a smoothing filter (smooth motion of the
[frame presenter](../../../docs/inprogress/2026-10-26-frame-presenter/README.md), or an experiment of
[POC 047](../../poc/047-smoothing-filters/README.md) in `filters.cpp`) shows motion, from the pictures alone: the
source frames and the pictures it put on the display, refresh by refresh. Built like the
[ZX DLSS verification](../zxdlss/README.md): synthetic programs with a known answer, independent oracles, hard gates
and a baseline.

| Part | What it is |
|---|---|
| [`testdata/video/smoothmotion`](../../../testdata/video/smoothmotion/README.md) | the synthetic Z80 programs (`.sna`) and their ground truth, synced from their own repo |
| `sync-testdata.sh` | copies the built programs and the truth from that repo, checks the sums |
| `filters.cpp` | the filters harness: the shipped modes through the core's CPU reference plus POC 047's experiments, 60 Hz refreshes as raw RGBA (`FILTERS_RAW=1`) and videos |
| `compile-tool.sh` | builds a tool (`filters.cpp`, `live/replay.cpp`, POC 049's `smoothmotion-eval.cpp`) with the core sources it needs, no CMake |
| `run_scenes.sh` | the scenes end to end: capture, filters, `smoothness.py`, `regress.py` |
| `smoothness.py` | the measurement: source frames + a filter's refreshes -> numbers per region and filter |
| `regress.py` | hard gates and the baseline comparison of `smoothness.py` results |
| `baseline.json` | the measured numbers of the current filters on the synthetic scenes |
| `scenes.py` | the generic scenes of the eye's model (`truth/` below) |

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
   program's trajectory (`floor(floor(n / period) * step16 / 16)` px for some frame phase; with the truth's `end`
   and `cycle`, at n mod `cycle` and gone from `end` on: a stretch while the ball is gone must not move). The pairs
   where the region turns empty or shows content again break the track: no oracle scores across the jump.
6. **Static pixels**: outside the regions, pixels that do not change in the source must be the source's (gated for
   every filter except the never shipped experiments `pan-global`, `pan-strips` and `extrapolate`).

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
# the scenes end to end against a running emulator (its WebAPI port): a Pentagon 128K instance of its own
tools/verification/smoothmotion/run_scenes.sh <webapi port> [--update] [scene ...]

# the same by hand, one scene
tools/verification/smoothmotion/compile-tool.sh tools/verification/smoothmotion/filters.cpp scratch/smoothmotion/filters
python3 -I tools/smoothmotion/capture.py --port <webapi> --model PENTAGON:128 --out scratch/smoothmotion --warmup 50 \
    --frames 400 testdata/video/smoothmotion/bin/smooth-speeds.sna
FILTERS_RAW=1 scratch/smoothmotion/filters scratch/smoothmotion/smooth-speeds.rgba 352 288 400 \
    scratch/smoothmotion/speeds data/rom/48.rom
python3 -I tools/verification/smoothmotion/smoothness.py --source scratch/smoothmotion/smooth-speeds.rgba \
    --truth testdata/video/smoothmotion/truth/speeds.json --out-dir scratch/smoothmotion/speeds \
    --json scratch/smoothmotion/speeds.json
python3 -I tools/verification/smoothmotion/regress.py --baseline tools/verification/smoothmotion/baseline.json \
    scratch/smoothmotion/speeds.json

# any capture (a game): 16-line strips of the paper instead of the ground truth
python3 -I tools/verification/smoothmotion/smoothness.py --source <clip>.rgba --out-dir <filters' pictures>
```

The baseline follows the core: a change to smooth motion's output (a cap, a rule) or to the test programs (after
`sync-testdata.sh`) needs `run_scenes.sh <port> --update` and the new `baseline.json` in the same commit. The action
screen (`truth/action.json`) has no regions; it is not scored here.

The filters tool renders refresh r at `3 ms + r / 60 s`, frame k due at `k / 50 s` (`--phase-ms`, `--src-hz`,
`--disp-hz` change that). numpy and scipy; one worker process per region (`--jobs`).

## What the eye sees: `truth/`

`smoothness.py` measures positions; [`truth/`](truth/) measures pictures against what the eye should see. It renders the
generic scenes of [POC 047](../../poc/047-smoothing-filters/README.md) (`scenes.py` here: scrollers, parallax, sprites,
text, dot matrices, animated shapes, flicker, overlays), whose motion is known at any time, and scores a filter's 60 Hz
refreshes the way the eye integrates them: a 20 ms box per sample, the eye either following a moving element
(pursuit) or resting (fixation).

| File | What it is |
|---|---|
| `truth.sh` | the runner: truth once per scene, then render, score and table per run |
| `truthdump.py` | a scene's 50 Hz source, its truth at 500 Hz with element ids, the velocity the eye follows per element |
| `truth60.py` | the truth refreshed at 60 Hz: the best any 60 Hz display shows, the pursuit reference |
| `truthscore.cpp` | the score (the comment at its top has the model) |
| `aliasmin.py` | scenes whose shapes change between frames (`animated`, `flux`) have two right answers (frame k's shapes or k + 1's): per element the better |
| `table.py` | pursuit / fixation per scene and filter |

```
tools/verification/smoothmotion/compile-tool.sh tools/verification/smoothmotion/filters.cpp scratch/smoothmotion/filters
tools/verification/smoothmotion/truth/truth.sh scratch/smoothmotion/filters off,blend,layers [scenes...]
```

The columns `truth60` (the 60 Hz truth itself) and `native` (the source at 50 Hz on a 50 Hz display) bracket the
filters: pursuit is the error a filter adds to the 60 Hz hold (judder, ghosts, parts left behind), so `truth60` is 0
on scenes without shape changes; fixation is against the continuous truth (flicker, shimmer). Lower is better.
Fixation favours the 50 Hz source on some scenes (`parallax`, `strips`): a 60 Hz hold of the truth crosses more
sub-pixel phases than the source's own frames, so compare filters against `truth60` there, not against `native`.

Measured 2026-11-01 (sum over the 12 scenes, pursuit / fixation): `truth60` 118 / 909, `native` 306 / 302,
`layers` 196 / 936; with `MotionField::kAccept` 4 -> 16 `layers` 179 / 933 (`animated` 52 -> 38: its water, a fifth
of the pixels new each frame, matched no block before). With `nearTies` (`LayerFlow::kNear`) 177 / 933. Per-pixel exact matching (an experiment, not kept) reached 173 / 318 on the suite but lost
to `layers` on every game clip under `smoothness.py`: dots that turn as they scroll (Aleste's dot-matrix text) are
locally a turn in place, and only the block motion of `layers` follows the letters.

## What the window shows: `live/`

The scores above judge filters on rendered clips; [`live/`](live/) judges what a running unreal-qt window really put
on the screen, refresh by refresh. `live.py` launches unreal-qt with `UNREAL_SMOOTH_TRACE` (its own ports, muted, view
settings off), loads each clip through the WebAPI, warms it up, records the frames pushed and the pictures drawn
(`core/src/emulator/video/smoothtrace.h`), then:

- `replay` (built from `replay.cpp`) pushes the same frames and plans the same refresh times through the core on the
  CPU: every plan and picture must be the window's (which may have drawn on the GPU), and it prints the timing
  (refreshes skipped, the frames' target jitter, the look-ahead);
- `smoothness.py` scores the window's pictures at their own times, `off` and `blend` on the same refreshes, and the
  same mode on a perfect clock (what the timing costs).

```
tools/verification/smoothmotion/compile-tool.sh tools/verification/smoothmotion/live/replay.cpp scratch/live/replay
python3 -I tools/verification/smoothmotion/live/live.py \
    --bin cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt --replay scratch/live/replay \
    --out scratch/live testdata/loaders/sna/aleste1.sna@8 <clip.rzx>@30
```

A clip is `<file>[@<warm-up seconds>]` (.sna / .z80 / .rzx). The window must be seen: macOS gives a covered window no
refreshes (the presenter rests while occluded), so `live.py` brings it to the front (`--no-front` leaves it). The
table also gives the presenter's late and dropped presents and its lead over the clip.
