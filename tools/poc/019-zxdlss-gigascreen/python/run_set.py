#!/usr/bin/env python3
"""Run the POC over the golden-clip candidates and print one metrics line each.

Example: python3 python/run_set.py --clip data/clip_full --out out/v1
"""
import argparse
import json
import os
import subprocess
import sys

CLIPS = [
    ("ate-pageflip-static", 2700, 2900),
    ("ate-hiphop-border", 4600, 4800),
    ("ate-border-only", 5300, 5500),
    ("ate-balls-floor", 8000, 8200),
    ("ate-tunnel", 10000, 10200),
    ("ate-irregular", 12100, 12300),
    ("ate-raster-negative", 3500, 3700),
    ("ate-raster-negative-2", 7300, 7500),
]

ap = argparse.ArgumentParser()
ap.add_argument("--clip", required=True)
ap.add_argument("--out", default="out/v1")
ap.add_argument("--only", nargs="*")
ap.add_argument("--alg", default="v2")
ap.add_argument("--clip-v2", action="store_true", help="core-exported clip with plane B")
ap.add_argument("--still", type=int, default=120, help="also save this frame of each video as PNG")
args = ap.parse_args()
here = os.path.dirname(__file__)
for name, a, b in CLIPS:
    if args.only and name not in args.only:
        continue
    cmd = [sys.executable, os.path.join(here, "run.py"), "--clip", args.clip, "--from", str(a), "--to", str(b),
           "--name", name, "--out", args.out, "--alg", args.alg]
    if args.clip_v2:
        cmd.append("--clip-v2")
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    m = json.load(open(os.path.join(args.out, f"{name}.json")))
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", os.path.join(args.out, f"{name}.mp4"),
                    "-vf", f"select=eq(n\\,{args.still})", "-vframes", "1", os.path.join(args.out, f"{name}_f{args.still}.png")])
    q = m["quality"]
    print(f"{name:24s} ghost {q['ghost_pixels']:.4f}  ghost-mf {q['ghost_moving_flicker']:.4f}  missed {q['missed_flicker']:.3f}  "
          f"flicker {q['oracle_flicker_pixels']:.3f}/mf {q['oracle_moving_flicker_pixels']:.3f}  mixed {q['mixed_pixels']:.3f}  residue {q['flicker_residue']:.3f}  "
          f"dE p50/p99 {q['color_dE_p50']:.3f}/{q['color_dE_p99']:.3f}  bad-color {q['color_error_share']:.3f}", flush=True)
