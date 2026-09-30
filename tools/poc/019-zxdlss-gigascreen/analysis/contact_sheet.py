#!/usr/bin/env python3
"""Thumbnail grid of every Nth frame of a clip (quick visual survey).

Example: python3 analysis/contact_sheet.py data/clip_full --every 250 --out out/contact.png
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from common.clip import Clip  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("clip")
ap.add_argument("--every", type=int, default=250)
ap.add_argument("--cols", type=int, default=8)
ap.add_argument("--out", required=True)
args = ap.parse_args()

clip = Clip(args.clip)
tiles = []
for i in range(0, len(clip), args.every):
    t = Image.fromarray(clip.rgb(i)).resize((176, 144))
    ImageDraw.Draw(t).text((2, 2), str(clip.meta[i]["frame"]), fill=(255, 0, 255))
    tiles.append(t)
sheet = Image.new("RGB", (args.cols * 176, ((len(tiles) + args.cols - 1) // args.cols) * 144))
for i, t in enumerate(tiles):
    sheet.paste(t, ((i % args.cols) * 176, (i // args.cols) * 144))
os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
sheet.save(args.out)
print("saved", args.out, len(tiles), "tiles")
