#!/usr/bin/env python3
"""POC 050: the cost table of bench.sh's results: median wall time per variant, the cost over `none` per frame and
per marker hit."""

import collections
import glob
import json
import os
import statistics
import sys


def main(out):
    runs = collections.defaultdict(list)
    for path in glob.glob(os.path.join(out, "**", "*.json"), recursive=True):
        d = json.load(open(path))
        dense = "_dense_" in path
        if "variant" in d and "wall_s" in d and "/exact/" not in path:
            runs[(d["model"], dense, d["variant"])].append(d)
    for (model, dense) in sorted({(m, x) for m, x, _ in runs}):
        base = statistics.median(r["wall_s"] for r in runs[(model, dense, "none")])
        hits = statistics.median(r.get("hits", 0) for r in runs.get((model, dense, "sync"), []) or
                                 runs[(model, dense, "sync-empty")]) or 0
        frames = runs[(model, dense, "none")][0]["frames"]
        print("\n{} {}: {} frames, {} marker hits ({:.1f} per frame)".format(
            model, "dense" if dense else "demo markers", frames, int(hits), hits / frames))
        print("| Variant | Runs | Wall, median s | Spread | Frames/s | Over none, us/frame | Over none, ns/hit |")
        print("|:--|--:|--:|--:|--:|--:|--:|")
        for v in ("none", "count", "sync-empty", "sync-grow", "sync", "native", "frame", "deferred-empty", "deferred"):
            rs = runs.get((model, dense, v), [])
            if not rs:
                continue
            walls = sorted(r["wall_s"] for r in rs)
            med = statistics.median(walls)
            over = med - base
            print("| `{}` | {} | {:.3f} | {:.3f}..{:.3f} | {:.0f} | {:.1f} | {} |".format(
                v, len(rs), med, walls[0], walls[-1], frames / med, over / frames * 1e6,
                "{:.0f}".format(over / hits * 1e9) if hits and v != "none" else "-"))


if __name__ == "__main__":
    main(sys.argv[1])
