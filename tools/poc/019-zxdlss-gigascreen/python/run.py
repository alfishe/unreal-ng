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

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from common.clip import Clip, ClipV2  # noqa: E402
from python.dlss_v1 import CLASS_COLORS, CLASS_NAMES  # noqa: E402
from python.mixers import MIXERS  # noqa: E402
from python.quality import Oracle, QualityAccumulator  # noqa: E402
from python.run_algorithms import ALGORITHMS, NEEDS_PLANE_B  # noqa: E402
from python.video import SideBySide  # noqa: E402

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
ap.add_argument("--dump", help="write the exact output as an RGB dump (zxdlss-rgb-dump, as zxdlss-render --dump)")
ap.add_argument("--oracle2", action="store_true", help="also score with oracle2 (multi-reference) and oracle3 (pixel XOR + averaging)")
ap.add_argument("--warmup", type=int, default=0,
                help="feed this many frames before --from first (history, as in a continuous run); not scored")
args = ap.parse_args()

clip = ClipV2(args.clip) if args.clip_v2 else Clip(args.clip)
if args.alg in NEEDS_PLANE_B and not args.clip_v2:
    sys.exit(f"{args.alg} needs plane B: use a core-exported clip with --clip-v2")
palette = clip.zx_palette if args.clip_v2 else clip.palette_rgb
i0, i1 = clip.index_of_frame(args.first), clip.index_of_frame(args.last)
alg = ALGORITHMS[args.alg]((clip.h, clip.w), MIXERS[args.mixer](palette))
os.makedirs(args.out, exist_ok=True)
video = None if args.video == "none" else SideBySide(os.path.join(args.out, f"{args.name}.{args.video}"), clip.h, clip.w, panels=4)
oracle = Oracle(clip, palette)
quality = QualityAccumulator()
quality2 = None
if args.oracle2:
    from python.oracle2 import Oracle2Accumulator
    from python.oracle3 import Oracle3Accumulator
    quality2 = Oracle2Accumulator(clip, MIXERS["linear-mean"](palette), strict=oracle)
    quality3 = Oracle3Accumulator(clip, MIXERS["linear-mean"](palette))

dump_frames, dump_chunk = [], 0


def dump_flush(final=False):
    global dump_frames, dump_chunk
    import zstandard
    if dump_frames:
        os.makedirs(args.dump, exist_ok=True)
        data = np.stack(dump_frames).astype(np.uint8).tobytes()
        open(os.path.join(args.dump, f"rgb_{dump_chunk:04d}.zst"), "wb").write(zstandard.ZstdCompressor(level=3).compress(data))
        dump_chunk += 1
        dump_frames = []


dump_count = 0
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


for j in range(max(0, i0 - args.warmup), i0 + delay):
    feed(j)
for i in range(i0, i1 + 1):
    plane = clip.plane(i)
    out, cls = feed(i + delay)
    raw = palette[plane]
    if args.dump:
        dump_frames.append(out)
        dump_count += 1
        if len(dump_frames) == 500:
            dump_flush()
    o1 = oracle.at(i)
    err = quality.add(raw, out, prev_out, o1)
    if quality2 is not None:
        quality3.add(i, out)
        warrant = None
        if quality3.last is not None:
            warrant = quality3.last[1] > 0
            if o1 is not None:
                warrant = warrant | o1[0] | o1[1]      # flicker and moving flicker
        quality2.add(i, out, warrant)
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
if args.dump:
    dump_flush()
    json.dump({"format": "zxdlss-rgb-dump", "width": clip.w, "height": clip.h, "from": args.first,
               "frames": dump_count, "chunk": 500, "files": "rgb_NNNN.zst (RGB8, frame after frame)"},
              open(os.path.join(args.dump, "dump.json"), "w"), indent=1)
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
    "quality2": quality2.report() if quality2 is not None else None,
    "quality3": quality3.report() if quality2 is not None else None,
}
json.dump(metrics, open(os.path.join(args.out, f"{args.name}.json"), "w"), indent=1)
print(json.dumps(metrics, indent=1))
