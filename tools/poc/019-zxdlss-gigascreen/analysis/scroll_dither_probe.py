#!/usr/bin/env python3
"""Probe: dither flicker under a horizontal scroll (Mario-style levels).

A checkerboard-dithered picture whose phase inverts every frame reads as a
solid half-tone; when the level also scrolls, the A,B,A repeat is no longer
at the same screen position, so a same-position period-2 detector misses it.
This probe averages frame t with frame t-1 shifted by the scroll vector:

  dx per 8-row band: argmin over -8..8 of |blur(t) - shift(blur(t-1), dx)|,
  blur = 2x2 box (removes the checker so it cannot fake an odd shift)
  out(t) = (t + shift(t-1, dx)) / 2 where the shifted pixels agree after the
  blur, raw t elsewhere

Outputs in --out: probe.png (per frame: raw t | raw t-1 | algorithm output |
motion-compensated average), probe.json (bands' dx, residual flicker of the
compensated sequence vs raw vs the algorithm over --from..--to).

Example:
  python3 analysis/scroll_dither_probe.py RAW_DUMP --alg-dump OUT_DUMP --frames 1102,2102 \\
      --from 1050 --to 1250 --out out/x
"""
import argparse
import json
import os

import numpy as np
import zstandard
from PIL import Image, ImageDraw

ap = argparse.ArgumentParser()
ap.add_argument("dump")
ap.add_argument("--alg-dump", help="the algorithm's dump of the same range (for the side by side)")
ap.add_argument("--frames", default="", help="comma-separated frames for probe.png")
ap.add_argument("--from", dest="lo", type=int, required=True)
ap.add_argument("--to", dest="hi", type=int, required=True)
ap.add_argument("--paper", default="48,48,256,192")
ap.add_argument("--out", required=True)
args = ap.parse_args()
os.makedirs(args.out, exist_ok=True)
px, py, pw, ph = (int(v) for v in args.paper.split(","))


def load(d, lo, hi):
    meta = json.load(open(os.path.join(d, "dump.json")))
    W, H, first, chunk = meta["width"], meta["height"], meta["from"], meta["chunk"]
    dctx = zstandard.ZstdDecompressor()
    out = []
    for c in range((lo - first) // chunk, (hi - first) // chunk + 1):
        raw = dctx.decompress(open(os.path.join(d, "rgb_%04d.zst" % c), "rb").read(), max_output_size=chunk * W * H * 3)
        out.append(np.frombuffer(raw, np.uint8).reshape(-1, H, W, 3))
    a = np.concatenate(out)
    s = (lo - first) - ((lo - first) // chunk) * chunk
    return a[s:s + hi - lo + 1, py:py + ph, px:px + pw].astype(np.float32)


R = load(args.dump, args.lo - 1, args.hi)  # R[0] = lo-1
A = load(args.alg_dump, args.lo, args.hi) if args.alg_dump else None


def blur(f):
    g = f.mean(-1)
    g = (g + np.roll(g, 1, 1)) / 2
    return (g + np.roll(g, 1, 0)) / 2


def shift(img, dx):
    return np.roll(img, dx, axis=1)


def compensate(cur, prev):
    bc, bp = blur(cur), blur(prev)
    out = cur.copy()
    dxs = []
    for y0 in range(0, ph, 8):
        best = min(range(-8, 9), key=lambda dx: (np.abs(bc[y0:y0 + 8, 8:-8] - shift(bp, dx)[y0:y0 + 8, 8:-8]).mean(), abs(dx)))
        dxs.append(best)
        sp = shift(prev, best)[y0:y0 + 8]
        agree = np.abs(bc[y0:y0 + 8] - shift(bp, best)[y0:y0 + 8]) < 24
        band = out[y0:y0 + 8]
        band[agree] = (cur[y0:y0 + 8][agree] + sp[agree]) / 2
    return out, dxs


C, bands = [], []
for i in range(1, len(R)):
    o, d = compensate(R[i], R[i - 1])
    C.append(o)
    bands.append(d)
C = np.stack(C)


def flicker(seq):
    s = seq.reshape(len(seq), -1, 3)
    ch = np.any(s[1:] != s[:-1], -1)
    p2 = ch[1:] & np.all(s[2:] == s[:-2], -1)
    return int(p2.sum()), float(np.abs(np.diff(seq.mean(-1), axis=0)).mean())


res = {"range": [args.lo, args.hi], "raw": flicker(R[1:]), "compensated": flicker(C)}
if A is not None:
    res["algorithm"] = flicker(A)
res["note"] = "[period-2 pixels, mean |frame-to-frame luminance change|]"
res["band_dx_first_frames"] = {str(args.lo + i): bands[i] for i in range(min(8, len(bands)))}

tiles = []
for f in [int(v) for v in args.frames.split(",") if v]:
    i = f - args.lo
    row = [R[i + 1], R[i]] + ([A[i]] if A is not None else []) + [C[i]]
    names = ["raw t=%d" % f, "raw t-1"] + (["algorithm"] if A is not None else []) + ["motion-compensated avg"]
    ims = []
    for img, n in zip(row, names):
        im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).resize((pw * 2, ph * 2), Image.NEAREST)
        ImageDraw.Draw(im).text((4, 4), n, fill=(255, 255, 0))
        ims.append(im)
    strip = Image.new("RGB", (len(ims) * (pw * 2 + 4), ph * 2), (60, 60, 60))
    for k, im in enumerate(ims):
        strip.paste(im, (k * (pw * 2 + 4), 0))
    tiles.append(strip)
if tiles:
    sheet = Image.new("RGB", (tiles[0].width, sum(t.height + 4 for t in tiles)), (60, 60, 60))
    y = 0
    for t in tiles:
        sheet.paste(t, (0, y))
        y += t.height + 4
    sheet.save(os.path.join(args.out, "probe.png"))
json.dump(res, open(os.path.join(args.out, "probe.json"), "w"), indent=1)
print(json.dumps(res))
