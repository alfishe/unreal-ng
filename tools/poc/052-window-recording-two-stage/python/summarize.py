#!/usr/bin/env python3
"""POC 052: medians of bench_encoders.py runs as a markdown table.

  summarize.py bench-*.jsonl [...]

One row per variant, one column per stream: median fps (x real time) and MB per minute.
"""
import json
import statistics
import sys
from collections import defaultdict

runs = defaultdict(list)
for path in sys.argv[1:]:
    for line in open(path):
        if not line.startswith("{"):
            continue
        d = json.loads(line)
        if d.get("error"):
            continue
        runs[(d["variant"], d["stream"][:-4])].append(d)

variants = list(dict.fromkeys(v for v, _ in runs))
streams = sorted({s for _, s in runs})
print("| variant | " + " | ".join(streams) + " |")
print("|---|" + "---|" * len(streams))
for v in variants:
    cells = []
    for s in streams:
        r = runs.get((v, s))
        if not r:
            cells.append("")
            continue
        fps = statistics.median(x["fps"] for x in r)
        xr = statistics.median(x["x_realtime"] for x in r)
        mb = statistics.median(x["mb_per_min"] for x in r)
        cells.append(f"{fps:.0f} fps x{xr:.1f} {mb} MB/min" + (f" (n={len(r)})" if len(r) < 3 else ""))
    print(f"| {v} | " + " | ".join(cells) + " |")
