#!/usr/bin/env python3
"""Smoothness results (smoothness.py --json) against the hard gates and a baseline.

Hard gates (any scene):
  - the source track matches the scene's ground truth (synthetic scenes)
  - `off` shows the source frames: no ghost, no tear
  - no filter damages static pixels (static_px == 0), except the experiments listed in EXPERIMENTS
Baseline: per scene, filter and region, jstep / ghost / tear / pic may not get worse than the baseline by more than
the tolerance. `--update` writes the current results as the new baseline.

    python3 -I regress.py --baseline baseline.json scratch/smoothcap/speeds.json scratch/smoothcap/scroll.json
"""
import argparse
import json
import os
import sys

# Experiments of POC 039 that move whole pictures (static damage is their known flaw, measured, not gated)
EXPERIMENTS = {"pan-global", "pan-strips"}
TOLERANCE = {"jstep": 0.05, "ghost": 0.05, "tear": 0.005, "pic": 0.01}


def scene_of(path):
    return os.path.splitext(os.path.basename(path))[0]


def gates(scene, result):
    failures = []
    for name, check in result.get("truth_check", {}).items():
        if not check["ok"]:
            failures.append(f"{scene}/{name}: {check['matched']} of {check['segments']} track stretches follow the "
                            f"program's motion")
    for name, region in result["regions"].items():
        off = region["filters"].get("off")
        if off and (off["ghost"] > 0.01 or off["tear"] > 0.001):
            failures.append(f"{scene}/{name}: off ghost {off['ghost']:.2f} tear {100 * off['tear']:.2f}%")
    for fname, damage in result["static"].items():
        if fname not in EXPERIMENTS and damage["max"] > 0:
            failures.append(f"{scene}: {fname} damages static pixels (mean {damage['mean']:.1f}, max {damage['max']})")
    return failures


def compare(scene, result, base):
    worse, better = [], []
    for name, region in result["regions"].items():
        for fname, got in region["filters"].items():
            want = base.get(name, {}).get(fname)
            if want is None:
                continue
            for key, tol in TOLERANCE.items():
                delta = got[key] - want[key]
                line = f"{scene}/{name}/{fname}: {key} {want[key]:.3f} -> {got[key]:.3f}"
                if delta > tol:
                    worse.append(line)
                elif delta < -tol:
                    better.append(line)
    return worse, better


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--update", action="store_true", help="write these results as the baseline")
    ap.add_argument("results", nargs="+")
    args = ap.parse_args()

    baseline = json.load(open(args.baseline)) if os.path.exists(args.baseline) else {}
    failures, worse, better = [], [], []
    for path in args.results:
        scene, result = scene_of(path), json.load(open(path))
        failures += gates(scene, result)
        if scene in baseline:
            w, b = compare(scene, result, baseline[scene])
            worse += w
            better += b
        if args.update:
            baseline[scene] = {name: {f: {k: v[k] for k in TOLERANCE} for f, v in region["filters"].items()}
                               for name, region in result["regions"].items()}
    for line in failures:
        print("FAIL", line)
    for line in worse:
        print("REGRESSION", line)
    for line in better:
        print("better", line)
    if args.update:
        with open(args.baseline, "w") as f:
            json.dump(baseline, f, indent=1, sort_keys=True)
        print(f"baseline written: {args.baseline}")
    print(f"{len(failures)} gate failures, {len(worse)} regressions, {len(better)} improvements")
    return 1 if failures or (worse and not args.update) else 0


if __name__ == "__main__":
    sys.exit(main())
