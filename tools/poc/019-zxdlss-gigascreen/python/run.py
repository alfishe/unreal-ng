#!/usr/bin/env python3
"""Run a POC version over a clip range: side-by-side video + metrics.

Clips: a WebAPI-extracted clip (palette-index planes, capture/extract_clip.py)
or a core-exported clip with plane B (POST /ttd/export-clip, --clip-v2).
v5 needs plane B.

Example:
  python3 python/run.py --clip data/clip_v2 --clip-v2 --alg v5 --from 4600 --to 4800 --name ate-hiphop-border
"""
import argparse
import json
import os
import sys
import time
from functools import partial

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from common.clip import Clip, ClipV2  # noqa: E402
from common.zxscreen import ZX_RGB  # noqa: E402
from python.dlss_v1 import CLASS_COLORS, CLASS_NAMES, DeflickerV1  # noqa: E402
from python.dlss_v2 import DeflickerV2  # noqa: E402
from python.dlss_v3 import DeflickerV3  # noqa: E402
from python.dlss_v4 import DeflickerV4  # noqa: E402
from python.dlss_v5 import DeflickerV5  # noqa: E402
from python.dlss_v6 import DeflickerV6  # noqa: E402
from python.dlss_v7 import DeflickerV7  # noqa: E402
from python.mixers import MIXERS  # noqa: E402
from python.quality import Oracle, QualityAccumulator  # noqa: E402
from python.video import SideBySide  # noqa: E402

ALGORITHMS = {"v1": DeflickerV1, "v2": DeflickerV2, "v3": DeflickerV3, "v4": DeflickerV4,
              "v5": DeflickerV5, "v5m": partial(DeflickerV5, tile_motion=True), "v6": DeflickerV6,
              "v6c": partial(DeflickerV6, confirm=8), "v6c12": partial(DeflickerV6, confirm=12),
              "v6b": partial(DeflickerV6, confirm=8, bias=0.8),
              "v6p2": partial(DeflickerV6, max_period=2, key="pixel"), "v6p2c8": partial(DeflickerV6, max_period=2, confirm=8, key="pixel"),
              "v6p2c6": partial(DeflickerV6, max_period=2, confirm=6, key="pixel"),
              "v6cell": partial(DeflickerV6, max_period=2, confirm=4), "v6cellc6": partial(DeflickerV6, max_period=2, confirm=6),
              "v6mem": partial(DeflickerV6, max_period=2, confirm=6, remember=True),
              "v6la": partial(DeflickerV6, max_period=2, confirm=6, remember=True, lookahead=True),
              "v6lanomem": partial(DeflickerV6, max_period=2, confirm=6, lookahead=True),
              "v6las": partial(DeflickerV6, max_period=2, confirm=6, remember=True, lookahead=True, spatial=4),
              "v7": DeflickerV7}
NEEDS_PLANE_B = {"v5", "v5m"} | {k for k in ALGORITHMS if k.startswith(("v6", "v7"))}
# class map colors: v1 classes, then motion (8, red; v7: motion veto) and split (9, white)
COLORS = np.vstack([CLASS_COLORS, [[255, 0, 0], [240, 240, 240]]]).astype(np.uint8)
NAMES = CLASS_NAMES + ["motion", "split"]

ap = argparse.ArgumentParser()
ap.add_argument("--clip", required=True)
ap.add_argument("--clip-v2", action="store_true", help="core-exported clip with plane B")
ap.add_argument("--from", dest="first", type=int, required=True, help="first emulated frame")
ap.add_argument("--to", dest="last", type=int, required=True)
ap.add_argument("--name", required=True)
ap.add_argument("--alg", default="v2", choices=sorted(ALGORITHMS))
ap.add_argument("--mixer", default="linear-mean", choices=sorted(MIXERS))
ap.add_argument("--out", default="out/v1")
ap.add_argument("--video", default="mp4", choices=["mp4", "gif", "none"])
args = ap.parse_args()

clip = ClipV2(args.clip) if args.clip_v2 else Clip(args.clip)
if args.alg in NEEDS_PLANE_B and not args.clip_v2:
    sys.exit(f"{args.alg} needs plane B: use a core-exported clip with --clip-v2")
palette = ZX_RGB if args.clip_v2 else clip.palette_rgb
i0, i1 = clip.index_of_frame(args.first), clip.index_of_frame(args.last)
alg = ALGORITHMS[args.alg]((clip.h, clip.w), MIXERS[args.mixer](palette))
os.makedirs(args.out, exist_ok=True)
video = None if args.video == "none" else SideBySide(os.path.join(args.out, f"{args.name}.{args.video}"), clip.h, clip.w, panels=4)
oracle = Oracle(clip, palette)
quality = QualityAccumulator()

counts = np.zeros(len(NAMES))
prev_out, prev_raw, prev_cls = None, None, None
raw_change, out_change, cls_switches, n = 0.0, 0.0, 0.0, 0
differs_from_raw = 0.0
t0 = time.time()
delay = getattr(alg, "delay", 0)     # frames of look-ahead: feeding frame i + delay returns frame i


def feed(j):
    p = clip.plane(j)
    if args.alg in NEEDS_PLANE_B:
        pb = ClipV2.decode_planeb(clip.planeb(j))
        return alg.process(p, pb["attr"], pb["ink"])
    return alg.process(p)


for j in range(i0, i0 + delay):
    feed(j)
for i in range(i0, i1 + 1):
    plane = clip.plane(i)
    out, cls = feed(i + delay)
    raw = palette[plane]
    err = quality.add(raw, out, prev_out, oracle.at(i))
    counts += np.bincount(cls.ravel(), minlength=len(NAMES))[:len(NAMES)]
    differs_from_raw += np.any(out != raw, axis=2).mean()
    if prev_out is not None:
        raw_change += np.any(raw != prev_raw, axis=2).mean()
        out_change += np.any(out != prev_out, axis=2).mean()
        cls_switches += (cls != prev_cls).mean()
        n += 1
    prev_out, prev_raw, prev_cls = out, raw, cls
    if video:
        cmap = COLORS[np.repeat(cls, 8, axis=1)]
        if hasattr(alg, "last_motion"):
            cmap[np.repeat(alg.last_motion, 8, axis=1)] = (255, 0, 0)
        video.add(raw, out, cmap, err)
elapsed = time.time() - t0
if video:
    video.close()

frames = i1 - i0 + 1
metrics = {
    "name": args.name, "alg": args.alg, "frames": [args.first, args.last], "mixer": args.mixer,
    "class_share": {c: round(float(v / counts.sum()), 4) for c, v in zip(NAMES, counts)},
    "raw_changed_pixels_per_frame": round(raw_change / max(n, 1), 4),
    "out_changed_pixels_per_frame": round(out_change / max(n, 1), 4),
    "class_switches_per_segment_per_frame": round(cls_switches / max(n, 1), 4),
    "pixels_differing_from_raw": round(differs_from_raw / frames, 4),
    "ms_per_frame": round(1000 * elapsed / frames, 1),
    "quality": quality.report(),
}
json.dump(metrics, open(os.path.join(args.out, f"{args.name}.json"), "w"), indent=1)
print(json.dumps(metrics, indent=1))
