#!/usr/bin/env python3
"""POC 052: probe_warning.py runs as a markdown table.

  summarize_probe.py probe-*.jsonl

Per setting: the steady rate (median, min-max), and for each probe kind how many runs landed within ±25 % of the
steady rate measured right before it, and the median error.
"""
import json
import statistics as st
import sys
from collections import defaultdict

runs = defaultdict(list)
for line in open(sys.argv[1]):
    d = json.loads(line)
    if not d["error"]:
        runs[(d["variant"], d["stream"][:-4])].append(d)

probes = ("first_fps", "first_fps_net", "synth_fps", "synth_fps_net")
print("| setting | steady fps, median (min-max) | load | " + " | ".join(probes) + " | probe s | start-up s |")
print("|---|---|---|" + "---|" * len(probes) + "---|---|")
for (variant, stream), r in runs.items():
    steady = [x["steady_fps"] for x in r]
    cells = []
    for p in probes:
        err = [x[p] / x["steady_fps"] - 1 for x in r]
        cells.append(f"{sum(abs(e) <= 0.25 for e in err)}/{len(r)}, {st.median(err) * 100:+.0f} %")
    load = [x["load"] for x in r]
    print(f"| {variant} {stream} | {st.median(steady):.0f} ({min(steady):.0f}-{max(steady):.0f}) | "
          f"{min(load):.0f}-{max(load):.0f} | " + " | ".join(cells) +
          f" | {st.median(x['probe_s'] for x in r):.2f} | {st.median(x['startup_s'] for x in r):.2f} |")
