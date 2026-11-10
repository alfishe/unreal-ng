#!/usr/bin/env python3
"""Draft a game profile's `video:` section (mapping-and-profiles.md section 7.7) from captured frames.

Input: the <name>.json / <name>.rgba pairs tools/smoothmotion/capture.py writes (consecutive emulated frames of the
game in play). Output: the section as YAML, with the evidence as comments:

- `picture_period`: the median number of frames between the game's pictures. A burst of changes on consecutive
  frames is one picture (Darkwing Duck redraws the play area, then the status row a frame later), unless the content
  changes on most frames (then every frame is a picture: period 1).
- `window`: the 8x8 blocks that change on the game's beat (the first frame of a burst), their bounding box.
- `exclude`: blocks inside the window that change off the beat for at least half of the pictures (a status row
  drawn a frame later; a sprite drawn over two frames now and then stays in): runs of them per block row, merged
  down into rectangles.
- `smooth`: left to the eye (cadence | carry: frame presenter design, section 4.5); the draft says `cadence`.

    python3 -I tools/smoothmotion/draft-video-profile.py scratch/video-profiles/darkwingduck.json
"""
import argparse
import json
import os
import sys

import numpy as np

BLOCK = 8


def load(path):
    with open(path) as f:
        meta = json.load(f)
    width, height, count = meta["width"], meta["height"], meta["count"]
    raw = np.fromfile(os.path.splitext(path)[0] + ".rgba", dtype=np.uint32)
    count = min(count, raw.size // (width * height))
    return meta.get("name", os.path.basename(path)), raw[: count * width * height].reshape(count, height, width)


def pictures(changed):
    """The frames that start and end each picture: [(first, last)], and whether every frame is a picture"""
    frames = np.flatnonzero(changed) + 1  # changed[k] compares frame k + 1 with frame k
    if frames.size == 0:
        return [], False
    single = int(np.count_nonzero(np.diff(frames) == 1))
    every_frame = single * 2 > frames.size
    bursts = []
    for k in frames:
        if not every_frame and bursts and k - bursts[-1][1] == 1:
            bursts[-1][1] = k
        else:
            bursts.append([int(k), int(k)])
    return bursts, every_frame


def rectangles(mask):
    """Runs of set blocks per block row, merged down while the run below is the same: [x, y, w, h] in pixels"""
    open_runs = {}  # (x0, x1) -> [x, y, w, h]
    done = []
    for by in range(mask.shape[0]):
        runs = []
        bx = 0
        while bx < mask.shape[1]:
            if mask[by, bx]:
                start = bx
                while bx < mask.shape[1] and mask[by, bx]:
                    bx += 1
                runs.append((start, bx))
            bx += 1
        next_runs = {}
        for run in runs:
            if run in open_runs:
                rect = open_runs.pop(run)
                rect[3] += BLOCK
            else:
                rect = [run[0] * BLOCK, by * BLOCK, (run[1] - run[0]) * BLOCK, BLOCK]
            next_runs[run] = rect
        done.extend(open_runs.values())
        open_runs = next_runs
    done.extend(open_runs.values())
    return sorted(done, key=lambda r: (r[1], r[0]))


def draft(name, frames):
    count, height, width = frames.shape
    changed_px = frames[1:] != frames[:-1]
    changed = changed_px.reshape(count - 1, -1).any(axis=1)
    bursts, every_frame = pictures(changed)
    lines = [f"# {name}: {count} frames {width}x{height}"]
    if len(bursts) < 3:
        lines.append(f"# {len(bursts)} pictures: too few to draft from (a static screen?)")
        return lines
    ends = [b[1] for b in bursts]
    gaps = np.diff(ends)
    period = 1 if every_frame else int(np.median(gaps))
    values, counts = np.unique(gaps, return_counts=True)
    histogram = ", ".join(f"{v}: {c}" for v, c in zip(values.tolist(), counts.tolist()))
    lines.append(f"# {len(bursts)} pictures, frames between them {{{histogram}}}"
                 + (" (changes on most frames: every frame a picture)" if every_frame else ""))

    # Changes per 8x8 block: on the beat (a burst's first frame) and off it (the rest of the burst)
    bh, bw = (height + BLOCK - 1) // BLOCK, (width + BLOCK - 1) // BLOCK
    padded = np.zeros((count - 1, bh * BLOCK, bw * BLOCK), dtype=bool)
    padded[:, :height, :width] = changed_px
    blocks = padded.reshape(count - 1, bh, BLOCK, bw, BLOCK).any(axis=(2, 4))
    first = [b[0] - 1 for b in bursts]
    rest = [k - 1 for b in bursts for k in range(b[0] + 1, b[1] + 1)]
    on_beat = blocks[first].sum(axis=0)
    off_beat = blocks[rest].sum(axis=0) if rest else np.zeros_like(on_beat)
    minimum = max(2, len(bursts) // 20)
    play = on_beat >= minimum
    if not play.any():
        lines.append("# no block changes on the game's beat: no window")
        return lines
    ys, xs = np.nonzero(play)
    x0, y0 = int(xs.min()) * BLOCK, int(ys.min()) * BLOCK
    x1, y1 = min(int(xs.max() + 1) * BLOCK, width), min(int(ys.max() + 1) * BLOCK, height)
    window = [x0, y0, x1 - x0, y1 - y0]
    off = off_beat * 2 >= len(bursts)
    inside = np.zeros_like(off)
    inside[y0 // BLOCK : (y1 + BLOCK - 1) // BLOCK, x0 // BLOCK : (x1 + BLOCK - 1) // BLOCK] = True
    exclude = [[r[0], r[1], min(r[2], x1 - r[0]), min(r[3], y1 - r[1])] for r in rectangles(off & inside)]

    lines.append("video:")
    lines.append(f"  picture_period: {min(max(period, 1), 16)}")
    lines.append("  smooth: cadence                   # or carry: choose by eye")
    lines.append(f"  window: [{', '.join(map(str, window))}]")
    if exclude:
        lines.append("  exclude: [" + ", ".join("[" + ", ".join(map(str, r)) + "]" for r in exclude) + "]")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("captures", nargs="+", help="<name>.json of capture.py")
    args = ap.parse_args()
    for path in args.captures:
        name, frames = load(path)
        if frames.shape[0] < 3:
            print(f"{path}: fewer than 3 frames", file=sys.stderr)
            continue
        print("\n".join(draft(name, frames)))
        print()


if __name__ == "__main__":
    main()
