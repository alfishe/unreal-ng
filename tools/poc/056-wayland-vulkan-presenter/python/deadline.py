#!/usr/bin/env python3
"""POC 056: how far ahead of a refresh must a present be to make it? Per frame: the first refresh at or after the
call on the grid of screen times (headroom = refresh - call), and whether the frame was shown on that refresh."""
import csv
import sys

for path in sys.argv[1:]:
    rows = [r for r in csv.DictReader(open(path)) if int(r["presented"])][50:]
    per = sorted(int(r["refresh"]) for r in rows)[len(rows) // 2]
    base = int(rows[0]["presented"])
    buckets = {}
    for r in rows:
        call, shown = int(r["call"]), int(r["presented"])
        n = -(-(call - base) // per)
        v = base + n * per
        head = (v - call) / 1e6
        made = abs(shown - v) < per / 2
        b = int(head)
        hit, tot = buckets.get(b, (0, 0))
        buckets[b] = (hit + made, tot + 1)
    print(path)
    print("  headroom ms: " + "  ".join(f"{b}:{100 * h // t}%/{t}" for b, (h, t) in sorted(buckets.items())))
