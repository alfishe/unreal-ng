#!/usr/bin/env python3
"""Add raw ZX screens (bitmap + attributes, both screen pages) to an extracted clip.

For every frame of the clip: RAM page 5 and RAM page 7 (bitmap 6144 bytes +
attributes 768 bytes each) and the displayed page, read at the frame's TTD
position (its checkpoint = the frame's start). Stored next to the pixel
chunks as screens_NNNN.zst: N x 2 x 6912 uint8, same chunking as the clip.

Limitation: a memory snapshot holds the attributes as they are in RAM at the
frame boundary, not the ones the beam fetched line by line - multicolor parts
need the renderer's plane B (core work, rollout P0a).

Example:
  python3 capture/extract_screens.py --emulator <id> --clip data/clip_full
"""
import argparse
import json
import os
import sys
import time
import urllib.request

import numpy as np
import zstandard

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from common.webapi import Emulator  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--emulator", required=True, help="instance holding the clip's TTD session")
ap.add_argument("--clip", required=True)
args = ap.parse_args()

emu = Emulator(args.emulator)
info = json.load(open(os.path.join(args.clip, "clip.json")))
chunk = info["chunk"]
first, last = info["start"], info["end"]


def page(p):
    with urllib.request.urlopen(f"{emu.url}/memory/ram/{p}/0?len=6912", timeout=30) as r:
        return np.array(json.loads(r.read())["data"], np.uint8)


cctx = zstandard.ZstdCompressor(level=10)
emu.call("POST", "/ttd/seek", {"frame": first})
buf, cid, frame, t0 = [], 0, first, time.time()
while True:
    buf.append(np.stack([page(5), page(7)]))
    if len(buf) == chunk:
        open(os.path.join(args.clip, f"screens_{cid:04d}.zst"), "wb").write(cctx.compress(np.stack(buf).tobytes()))
        buf, cid = [], cid + 1
        print(f"frame {frame}/{last}  {(frame - first + 1) / (time.time() - t0):.0f} fps", flush=True)
    if frame >= last:
        break
    frame = emu.call("POST", "/ttd/step-forward")["frame"]
if buf:
    open(os.path.join(args.clip, f"screens_{cid:04d}.zst"), "wb").write(cctx.compress(np.stack(buf).tobytes()))
info["screens"] = {"pages": [5, 7], "bytes_per_page": 6912, "at": "frame start (TTD checkpoint)"}
json.dump(info, open(os.path.join(args.clip, "clip.json"), "w"), indent=1)
print("done", first, frame)
