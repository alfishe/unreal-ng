#!/usr/bin/env python3
"""Where and when a recording flickers, from a zxdlss-render RGB dump.

Per pixel, per frame (final picture, raw algorithm):
  p2        differs from t-1, equals t-2 (A,B,A)          period-2 flicker
  p3..p5    differs from t-1..t-(P-1), equals t-P          period-P flicker
  motion    changed, none of the above

Outputs in --out:
  timeline.png   flickering / moving pixels per frame (paper area)
  heatmap.png    period-2..5 flicker per 8x8 cell over the whole recording,
                 over a mid-recording frame
  frames.png     the frames with the most flicker, flickering pixels marked
  summary.json   totals, per-frame counts, top cells and frames

Example (dump from: zxdlss-render --ttd x.ttd --alg raw --no-audio --dump D):
  python3 analysis/dump_flicker.py D --out out/flicker
"""
import argparse
import json
import os

import numpy as np
import zstandard
from PIL import Image, ImageDraw

ap = argparse.ArgumentParser()
ap.add_argument("dump")
ap.add_argument("--out", required=True)
ap.add_argument("--paper", default="48,48,256,192", help="x,y,w,h of the 256x192 picture in the dump")
ap.add_argument("--top", type=int, default=6, help="frames in frames.png")
args = ap.parse_args()
os.makedirs(args.out, exist_ok=True)

meta = json.load(open(os.path.join(args.dump, "dump.json")))
W, H, N, first, chunk = meta["width"], meta["height"], meta["frames"], meta["from"], meta["chunk"]
px, py, pw, ph = (int(v) for v in args.paper.split(","))

# Frames as one uint32 per pixel (RGB packed), chunk by chunk
dctx = zstandard.ZstdDecompressor()
frames = np.empty((N, H, W), dtype=np.uint32)
done = 0
for c in range((N + chunk - 1) // chunk):
    raw = dctx.decompress(open(os.path.join(args.dump, "rgb_%04d.zst" % c), "rb").read(), max_output_size=chunk * W * H * 3)
    n = len(raw) // (W * H * 3)
    rgb = np.frombuffer(raw, dtype=np.uint8).reshape(n, H, W, 3).astype(np.uint32)
    frames[done:done + n] = (rgb[..., 0] << 16) | (rgb[..., 1] << 8) | rgb[..., 2]
    done += n
assert done == N, (done, N)

P = frames[:, py:py + ph, px:px + pw]
periods = (2, 3, 4, 5)
cls = np.zeros(P.shape, dtype=np.uint8)  # 0 still, 1 motion, 2..5 period
changed = np.zeros(P.shape, dtype=bool)
changed[1:] = P[1:] != P[:-1]
cls[changed] = 1
for per in reversed(periods):
    m = np.zeros(P.shape, dtype=bool)
    m[per:] = changed[per:] & (P[per:] == P[:-per])
    for k in range(2, per):
        m[per:] &= P[per:] != P[per - k:-k]
    cls[m] = per

perframe = {str(p): (cls == p).reshape(N, -1).sum(1) for p in periods}
motion = (cls == 1).reshape(N, -1).sum(1)
flick = sum(perframe[str(p)] for p in periods)

# Per 8x8 cell: frames in which the cell has any period-P flicker
cells = {}
for p in periods:
    m = (cls == p).reshape(N, ph // 8, 8, pw // 8, 8).any(axis=(2, 4))
    cells[str(p)] = m.sum(0)
anyf = (cls >= 2).reshape(N, ph // 8, 8, pw // 8, 8).any(axis=(2, 4)).sum(0)

# timeline.png
TW, TH = min(N, 1600), 220
tl = Image.new("RGB", (TW, TH + 20), (16, 16, 20))
d = ImageDraw.Draw(tl)
scale = max(1, int(max(flick.max(), 1)))
mscale = max(1, int(max(motion.max(), 1)))
for x in range(TW):
    a, b = x * N // TW, max(x * N // TW + 1, (x + 1) * N // TW)
    mv = motion[a:b].max() / mscale
    fv = flick[a:b].max() / scale
    d.line([(x, TH), (x, TH - int(mv * (TH - 10)))], fill=(70, 70, 90))
    d.line([(x, TH), (x, TH - int(fv * (TH - 10)))], fill=(255, 90, 60))
for f in range(0, N, 250):
    x = f * TW // N
    d.line([(x, TH), (x, TH + 5)], fill=(200, 200, 200))
    d.text((x + 2, TH + 6), str(first + f), fill=(200, 200, 200))
d.text((4, 2), "red: flickering px (max %d)  grey: moving px (max %d)" % (scale, mscale), fill=(230, 230, 230))
tl.save(os.path.join(args.out, "timeline.png"))

# heatmap.png: a mid frame, cells tinted by how often they flicker
base = frames[N // 2, py:py + ph, px:px + pw]
rgb = np.stack([(base >> 16) & 255, (base >> 8) & 255, base & 255], -1).astype(np.float32) * 0.45
heat = anyf / max(1, anyf.max())
tint = np.kron(heat, np.ones((8, 8)))[..., None] * np.array([255, 60, 20], np.float32)
img = np.clip(rgb + tint * 0.8, 0, 255).astype(np.uint8)
hm = Image.fromarray(img).resize((pw * 3, ph * 3), Image.NEAREST)
dh = ImageDraw.Draw(hm)
for cy in range(ph // 8):
    for cx in range(pw // 8):
        if anyf[cy, cx]:
            dh.text((cx * 24 + 2, cy * 24 + 6), str(int(anyf[cy, cx])), fill=(255, 255, 255))
hm.save(os.path.join(args.out, "heatmap.png"))

# frames.png: top frames (spread out: at least 25 frames apart), flicker in magenta
order = np.argsort(-flick)
picked = []
for i in order:
    if flick[i] == 0 or len(picked) >= args.top:
        break
    if all(abs(int(i) - j) >= 25 for j in picked):
        picked.append(int(i))
tiles = []
for i in picked:
    f = frames[i, py:py + ph, px:px + pw]
    t = np.stack([(f >> 16) & 255, (f >> 8) & 255, f & 255], -1).astype(np.uint8)
    t[cls[i] >= 2] = (255, 0, 255)
    im = Image.fromarray(t).resize((pw * 2, ph * 2), Image.NEAREST)
    ImageDraw.Draw(im).text((4, 4), "frame %d: %d px" % (first + i, flick[i]), fill=(255, 255, 0))
    tiles.append(im)
if tiles:
    cols = 3
    sheet = Image.new("RGB", (cols * pw * 2, ((len(tiles) + cols - 1) // cols) * ph * 2))
    for k, im in enumerate(tiles):
        sheet.paste(im, ((k % cols) * pw * 2, (k // cols) * ph * 2))
    sheet.save(os.path.join(args.out, "frames.png"))

active = np.nonzero(flick)[0]
runs = []
if len(active):
    s = prev = int(active[0])
    for f in active[1:]:
        f = int(f)
        if f - prev > 10:
            runs.append((first + s, first + prev))
            s = f
        prev = f
    runs.append((first + s, first + prev))
topcells = sorted(((int(anyf[y, x]), x, y) for y in range(ph // 8) for x in range(pw // 8) if anyf[y, x]), reverse=True)[:20]
summary = {
    "frames": N, "first": first,
    "flicker_frames": int((flick > 0).sum()),
    "px_by_period": {p: int(perframe[p].sum()) for p in perframe},
    "motion_px": int(motion.sum()),
    "runs": runs[:200], "run_count": len(runs),
    "top_cells": [{"col": x, "row": y, "frames": n} for n, x, y in topcells],
    "top_frames": [{"frame": first + i, "px": int(flick[i])} for i in picked],
    "cells_by_period": {p: int((cells[p] > 0).sum()) for p in cells},
}
json.dump(summary, open(os.path.join(args.out, "summary.json"), "w"), indent=1)
print(json.dumps({k: summary[k] for k in ("frames", "flicker_frames", "px_by_period", "motion_px", "run_count", "cells_by_period")}))
print("top cells:", summary["top_cells"][:8])
print("top frames:", summary["top_frames"])
print("runs:", runs[:30])
