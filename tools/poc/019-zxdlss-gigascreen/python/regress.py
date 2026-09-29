#!/usr/bin/env python3
"""Regression gate: no scene may get worse than the recorded baseline.

Runs an algorithm over every golden scene (the Across the Edge clips and the
flicker test), scores it with three oracles - the per-pixel oracle
(quality.py), the multi-reference oracle (oracle2.py) and pixel XOR +
averaging (oracle3.py) - and compares with python/baseline.json. Per scene
(consensus of the oracles, 2026-09-28):
  ok          worse on no oracle
  REGRESSION  worse on some oracle (beyond tolerance) and better on none
  disputed    worse on some oracle, better on another: shown for review, not
              blocking (the per-pixel oracle cannot judge moving two-page
              textures - spiral, tunnel)
Exit code 1 on any REGRESSION.

  python3 python/regress.py --alg v10 --data <poc>/data --out <poc>/out
  python3 python/regress.py --alg v10 --data ... --out ... --reuse       # compare existing JSON only
  python3 python/regress.py --alg v10 --data ... --out ... --write-baseline

Baseline history: v10 (2026-09-28) - the first state accepted on every scene
except the irregular spiral's snake; mod-tpgw (2026-09-29) - two-page field render accepted on the spiral and tunnel;
mod-tpgwa (2026-09-29) - whole-frame average behind a large static picture, accepted on
the DJ scene (ate-dj-circles, added to the golden set the same day).
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
METRICS2 = {                     # oracle2 metric -> (tolerance, higher is better)
    "color_ok": (0.01, True),
    "de_p95": (0.005, False),
    "edge_p95": (0.3, False),
    "shimmer": (0.001, False),
}
METRICS3 = {"agree": (0.01, True)}
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
    ("ate-dj-circles", "clip_v2", 11300, 11500),       # two-page lattice moving in steps (2026-09-29)
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
               "--alg", args.alg, "--from", str(a), "--to", str(b), "--name", scene, "--out", out_dir, "--oracle2"]
        procs.append(subprocess.Popen(cmd, stdout=subprocess.DEVNULL))
        if len(procs) >= args.jobs:
            procs.pop(0).wait()
    for p in procs:
        p.wait()

results, results2 = {}, {}
for scene, *_ in SCENES:
    path = os.path.join(out_dir, f"{scene}.json")
    if not os.path.exists(path):
        sys.exit(f"missing result {path}")
    js = json.load(open(path))
    results[scene] = {m: js["quality"][m] for m in METRICS}
    q2 = js.get("quality2") or {}
    results2[scene] = {m: q2.get(m) for m in METRICS2}
    q3 = js.get("quality3") or {}
    results2[scene].update({m: q3.get(m) for m in METRICS3})

if args.write_baseline:
    json.dump({"alg": args.alg, "scenes": results, "scenes2": results2}, open(BASELINE, "w"), indent=1)
    print(f"baseline written from {args.alg}: {BASELINE}")
    sys.exit(0)

base = json.load(open(BASELINE))
regressions = disputed = 0
print(f"{args.alg} vs baseline {base['alg']}  (! worse, + better, beyond tolerance)")
def verdict(marks):
    """An oracle says worse if any of its metrics is worse, better if some is
    better and none is worse."""
    worse = "!" in marks
    return worse, (not worse) and "+" in marks


for scene, *_ in SCENES:
    rows, verdicts = {}, {}
    marks = []
    row = []
    for m, tol in METRICS.items():                       # oracle 1: lower is better
        new, old = results[scene][m], base["scenes"][scene][m]
        mark = "!" if new > old + tol else ("+" if new < old - tol else " ")
        marks.append(mark)
        row.append(f"{LABELS[m]} {old:.4f}->{new:.4f}{mark}")
    rows["o1"], verdicts["o1"] = row, verdict(marks)
    for name, metrics in (("o2", METRICS2), ("o3", METRICS3)):
        row, marks = [], []
        for m, (tol, higher) in metrics.items():
            new, old = results2[scene].get(m), base.get("scenes2", {}).get(scene, {}).get(m)
            if new is None or old is None:
                row.append(f"{m} -")
                continue
            better = new > old + tol if higher else new < old - tol
            worse = new < old - tol if higher else new > old + tol
            mark = "!" if worse else ("+" if better else " ")
            marks.append(mark)
            row.append(f"{m} {old:.3f}->{new:.3f}{mark}")
        rows[name], verdicts[name] = row, verdict(marks)
    any_worse = any(w for w, _ in verdicts.values())
    any_better = any(b for _, b in verdicts.values())
    if any_worse and not any_better:
        status = "REGRESSION"
        regressions += 1
    elif any_worse:
        status = "disputed"                              # worse on one oracle, better on another
        disputed += 1
    else:
        status = "ok"
    print(f"{scene:22s} {status:10s} o1: " + "  ".join(rows["o1"]))
    print(f"{'':33s} o2: " + "  ".join(rows["o2"]) + "   o3: " + "  ".join(rows["o3"]))
print(f"RESULT: {regressions} regression(s), {disputed} disputed scene(s)")
sys.exit(1 if regressions else 0)
