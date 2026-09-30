#!/usr/bin/env python3
"""First-pass effect map of a clip: where does what kind of flicker happen.

Per pixel, on each frame's final picture:
  p2        differs from t-1, equals t-2 (A,B,A)          period-2 flicker
  p3..p5    differs from t-1..t-(P-1), equals t-P          period P flicker
  other     changed, none of the above                     motion / new content
Measured separately for the 256x192 picture area (paper) and the border.
Plus flips/f: how often the displayed screen page toggles per frame.

Frames are aggregated into 50-frame blocks; adjacent blocks with similar
indicators are merged into segments. Coarse by design: it locates effects,
it does not classify them (that is the analyzer's job).

Outputs in --out: perframe.json, effect_map.json, effect_map.png (one
thumbnail per segment), effect_map.md (table).

Example: python3 analysis/effect_map.py data/clip_full --out out/effect_map
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from common.clip import Clip  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("clip")
ap.add_argument("--out", required=True)
ap.add_argument("--block", type=int, default=50)
ap.add_argument("--merge", type=float, default=0.18, help="L1 distance of indicator vectors to merge blocks")
args = ap.parse_args()
os.makedirs(args.out, exist_ok=True)

clip = Clip(args.clip)
H, W = clip.h, clip.w
paper = np.zeros((H, W), bool)
paper[(H - 192) // 2:(H - 192) // 2 + 192, (W - 256) // 2:(W - 256) // 2 + 256] = True
border = ~paper
KEYS = ["pa_p2", "pa_p3", "pa_p4", "pa_p5", "pa_other", "bo_p2", "bo_p3", "bo_other"]

rows, hist = [], []
for i, f in clip.planes():
    hist.insert(0, f)
    del hist[6:]
    if len(hist) < 6:
        continue
    c = hist
    ne1 = c[0] != c[1]
    cls = {"p2": ne1 & (c[0] == c[2])}
    cls["p3"] = ne1 & (c[0] != c[2]) & (c[0] == c[3])
    cls["p4"] = ne1 & (c[0] != c[2]) & (c[0] != c[3]) & (c[0] == c[4])
    cls["p5"] = ne1 & (c[0] != c[2]) & (c[0] != c[3]) & (c[0] != c[4]) & (c[0] == c[5])
    cls["other"] = ne1 & ~cls["p2"] & ~cls["p3"] & ~cls["p4"] & ~cls["p5"]
    row = {"i": i, "frame": clip.meta[i]["frame"],
           "flip": int(clip.meta[i]["active_screen"] != clip.meta[i - 1]["active_screen"])}
    for name, m in cls.items():
        row["pa_" + name] = float(m[paper].mean())
        row["bo_" + name] = float(m[border].mean())
    rows.append(row)
json.dump(rows, open(os.path.join(args.out, "perframe.json"), "w"))

segments = []
for s in range(0, len(rows), args.block):
    blk = rows[s:s + args.block]
    seg = {"i0": blk[0]["i"], "i1": blk[-1]["i"], "from": blk[0]["frame"], "to": blk[-1]["frame"],
           "v": np.array([np.mean([r[k] for r in blk]) for k in KEYS]), "flips": np.mean([r["flip"] for r in blk])}
    last = segments[-1] if segments else None
    if last and np.abs(last["v"] - seg["v"]).sum() < args.merge and abs(last["flips"] - seg["flips"]) < 0.3:
        n0, n1 = last["i1"] - last["i0"] + 1, seg["i1"] - seg["i0"] + 1
        last["v"] = (last["v"] * n0 + seg["v"] * n1) / (n0 + n1)
        last["flips"] = (last["flips"] * n0 + seg["flips"] * n1) / (n0 + n1)
        last["to"], last["i1"] = seg["to"], seg["i1"]
    else:
        segments.append(seg)

out, tiles = [], []
for n, s in enumerate(segments):
    frames = s["i1"] - s["i0"] + 1
    out.append({"n": n, "from": s["from"], "to": s["to"], "frames": frames, "sec": round(frames / 48.83, 1),
                "flips": round(float(s["flips"]), 2), **{k: round(float(x), 3) for k, x in zip(KEYS, s["v"])}})
    t = Image.fromarray(clip.rgb((s["i0"] + s["i1"]) // 2)).resize((176, 144))
    d = ImageDraw.Draw(t)
    d.rectangle([0, 0, 175, 11], fill=(0, 0, 0))
    d.text((2, 0), f"#{n} {s['from']}-{s['to']}", fill=(255, 255, 0))
    tiles.append(t)
json.dump(out, open(os.path.join(args.out, "effect_map.json"), "w"), indent=1)

cols = 6
sheet = Image.new("RGB", (cols * 176, ((len(tiles) + cols - 1) // cols) * 144))
for i, t in enumerate(tiles):
    sheet.paste(t, ((i % cols) * 176, (i // cols) * 144))
sheet.save(os.path.join(args.out, "effect_map.png"))

lines = ["| # | Frames | Seconds | Page flips | Paper p2 | p3 | p4 | p5 | Paper other | Border p2 | p3 | Border other |",
         "|---|---|---|---|---|---|---|---|---|---|---|---|"]
for o in out:
    lines.append(f"| {o['n']} | {o['from']}–{o['to']} | {o['sec']} | {o['flips']} | {o['pa_p2']:.2f} | {o['pa_p3']:.2f} | "
                 f"{o['pa_p4']:.2f} | {o['pa_p5']:.2f} | {o['pa_other']:.2f} | {o['bo_p2']:.2f} | {o['bo_p3']:.2f} | "
                 f"{o['bo_other']:.2f} |")
open(os.path.join(args.out, "effect_map.md"), "w").write("\n".join(lines) + "\n")
print("\n".join(lines))
