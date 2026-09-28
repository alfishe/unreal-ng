#!/usr/bin/env python3
"""Run a POC version over a clip range: side-by-side video + metrics.

Example:
  python3 python/run.py --clip data/clip_full --from 4600 --to 4800 --name ate-hiphop-border
"""
import argparse
import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from common.clip import Clip  # noqa: E402
from python.dlss_v1 import CLASS_COLORS, CLASS_NAMES, DeflickerV1  # noqa: E402
from python.dlss_v2 import DeflickerV2  # noqa: E402
from python.dlss_v3 import DeflickerV3  # noqa: E402
from python.dlss_v4 import DeflickerV4  # noqa: E402

ALGORITHMS = {"v1": DeflickerV1, "v2": DeflickerV2, "v3": DeflickerV3, "v4": DeflickerV4}
from python.mixers import MIXERS  # noqa: E402
from python.quality import Oracle, QualityAccumulator  # noqa: E402
from python.video import SideBySide  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--clip", required=True)
ap.add_argument("--from", dest="first", type=int, required=True, help="first emulated frame")
ap.add_argument("--to", dest="last", type=int, required=True)
ap.add_argument("--name", required=True)
ap.add_argument("--alg", default="v2", choices=sorted(ALGORITHMS))
ap.add_argument("--mixer", default="linear-mean", choices=sorted(MIXERS))
ap.add_argument("--out", default="out/v1")
ap.add_argument("--video", default="mp4", choices=["mp4", "gif", "none"])
args = ap.parse_args()

clip = Clip(args.clip)
i0, i1 = clip.index_of_frame(args.first), clip.index_of_frame(args.last)
alg = ALGORITHMS[args.alg]((clip.h, clip.w), MIXERS[args.mixer](clip.palette_rgb))
os.makedirs(args.out, exist_ok=True)
video = None if args.video == "none" else SideBySide(os.path.join(args.out, f"{args.name}.{args.video}"), clip.h, clip.w, panels=4)
oracle = Oracle(clip, clip.palette_rgb)
quality = QualityAccumulator()

counts = np.zeros(len(CLASS_NAMES))
prev_out, prev_raw, prev_cls = None, None, None
raw_change, out_change, cls_switches, n = 0.0, 0.0, 0.0, 0
differs_from_raw = 0.0
t0 = time.time()
for i in range(i0, i1 + 1):
    plane = clip.plane(i)
    out, cls = alg.process(plane)
    raw = clip.palette_rgb[plane]
    err = quality.add(raw, out, prev_out, oracle.at(i))
    counts += np.bincount(cls.ravel(), minlength=len(CLASS_NAMES))
    differs_from_raw += np.any(out != raw, axis=2).mean()
    if prev_out is not None:
        raw_change += np.any(raw != prev_raw, axis=2).mean()
        out_change += np.any(out != prev_out, axis=2).mean()
        cls_switches += (cls != prev_cls).mean()
        n += 1
    prev_out, prev_raw, prev_cls = out, raw, cls
    if video:
        cmap = CLASS_COLORS[np.repeat(cls, 8, axis=1)]
        if hasattr(alg, "last_motion"):
            cmap[np.repeat(alg.last_motion, 8, axis=1)] = (255, 0, 0)
        video.add(raw, out, cmap, err)
elapsed = time.time() - t0
if video:
    video.close()

frames = i1 - i0 + 1
metrics = {
    "name": args.name, "alg": args.alg, "frames": [args.first, args.last], "mixer": args.mixer,
    "class_share": {c: round(float(v / counts.sum()), 4) for c, v in zip(CLASS_NAMES, counts)},
    "raw_changed_pixels_per_frame": round(raw_change / max(n, 1), 4),
    "out_changed_pixels_per_frame": round(out_change / max(n, 1), 4),
    "class_switches_per_segment_per_frame": round(cls_switches / max(n, 1), 4),
    "pixels_differing_from_raw": round(differs_from_raw / frames, 4),
    "ms_per_frame": round(1000 * elapsed / frames, 1),
    "quality": quality.report(),
}
json.dump(metrics, open(os.path.join(args.out, f"{args.name}.json"), "w"), indent=1)
print(json.dumps(metrics, indent=1))
