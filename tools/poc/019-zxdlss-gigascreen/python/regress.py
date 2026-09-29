#!/usr/bin/env python3
"""Regression gate: no scene may get worse than the recorded baseline.

Runs an algorithm over every golden scene (the Across the Edge clips and the
flicker test) and compares the quality metrics with python/baseline.json.
Exit code 1 when any metric of any scene is worse than its baseline by more
than the tolerance. Lower is better for every tracked metric.

  python3 python/regress.py --alg v10 --data <poc>/data --out <poc>/out
  python3 python/regress.py --alg v10 --data ... --out ... --reuse       # compare existing JSON only
  python3 python/regress.py --alg v10 --data ... --out ... --write-baseline

Baseline history: v10 (2026-09-28) - the first state accepted on every scene
except the irregular spiral's snake.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASELINE = os.path.join(HERE, "baseline.json")
METRICS = {                      # metric -> tolerance (absolute)
    "ghost_pixels": 0.00005,
    "ghost_moving_flicker": 0.00005,
    "missed_flicker": 0.002,
    "color_error_share": 0.002,
}
LABELS = {"ghost_pixels": "ghost", "ghost_moving_flicker": "gmf", "missed_flicker": "missed", "color_error_share": "color"}
SCENES = [                       # (scene, clip dir, first, last)
    ("ate-pageflip-static", "clip_v2", 2700, 2900),
    ("ate-hiphop-border", "clip_v2", 4600, 4800),
    ("ate-border-only", "clip_v2", 5300, 5500),
    ("ate-balls-floor", "clip_v2", 8000, 8200),
    ("ate-tunnel", "clip_v2", 10000, 10200),
    ("ate-irregular", "clip_v2", 12100, 12300),
    ("ate-raster-negative", "clip_v2", 3500, 3700),
    ("ate-raster-negative-2", "clip_v2", 7300, 7500),
    ("flicker-test", "clip_flicker_v2", 1501, 3405),   # leaves room for 10 frames of look-ahead
]

ap = argparse.ArgumentParser()
ap.add_argument("--alg", required=True)
ap.add_argument("--data", required=True, help="POC data directory (clip_v2, clip_flicker_v2)")
ap.add_argument("--out", required=True, help="POC out directory; results go to <out>/regress/<alg>")
ap.add_argument("--reuse", action="store_true", help="compare existing results, do not run")
ap.add_argument("--write-baseline", action="store_true", help="record these results as the new baseline")
ap.add_argument("--jobs", type=int, default=3)
args = ap.parse_args()
out_dir = os.path.join(args.out, "regress", args.alg)

if not args.reuse:
    os.makedirs(out_dir, exist_ok=True)
    procs = []
    for scene, clip, a, b in SCENES:
        cmd = [sys.executable, os.path.join(HERE, "run.py"), "--clip", os.path.join(args.data, clip), "--clip-v2",
               "--alg", args.alg, "--from", str(a), "--to", str(b), "--name", scene, "--out", out_dir]
        procs.append(subprocess.Popen(cmd, stdout=subprocess.DEVNULL))
        if len(procs) >= args.jobs:
            procs.pop(0).wait()
    for p in procs:
        p.wait()

results = {}
for scene, *_ in SCENES:
    path = os.path.join(out_dir, f"{scene}.json")
    if not os.path.exists(path):
        sys.exit(f"missing result {path}")
    q = json.load(open(path))["quality"]
    results[scene] = {m: q[m] for m in METRICS}

if args.write_baseline:
    json.dump({"alg": args.alg, "scenes": results}, open(BASELINE, "w"), indent=1)
    print(f"baseline written from {args.alg}: {BASELINE}")
    sys.exit(0)

base = json.load(open(BASELINE))
worse = 0
print(f"{args.alg} vs baseline {base['alg']}  (lower is better; ! = worse beyond tolerance)")
for scene, *_ in SCENES:
    row = []
    for m, tol in METRICS.items():
        new, old = results[scene][m], base["scenes"][scene][m]
        bad = new > old + tol
        worse += bad
        row.append(f"{LABELS[m]:6s} {old:.4f}->{new:.4f}{'!' if bad else ' '}")
    print(f"{scene:22s} " + "  ".join(row))
print("RESULT:", "WORSE on", worse, "metric(s)" if worse else "no regression")
sys.exit(1 if worse else 0)
