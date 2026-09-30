#!/usr/bin/env python3
"""Add "palette16" to the clip.json of a v2 clip exported before the core wrote it.

The core writes the 16 ZX colors plane B's color indices were drawn in into
clip.json (ExportClip, "palette16"): the emulator's live palette. Older clips
lack it; this recovers it from the clip itself - every pixel's RGBA is the
palette color of its plane B color index - scanning frames until all 16 indices
are seen. An index the whole clip never draws is written as null (nothing in the
clip can need it).

  python3 capture/add_palette16.py data/clip_v2 [data/clip_flicker_v2 ...]
"""
import json
import os
import sys

import numpy as np
import zstandard


def recover(path):
    info = json.load(open(os.path.join(path, "clip.json")))
    assert info.get("format") == "unreal-ng-clip" and info.get("version") == 2, path
    assert "planeb" in info["planes"], f"{path}: no plane B"
    h, w, chunk, frames = info["height"], info["width"], info["chunk"], info["frames"]
    dctx = zstandard.ZstdDecompressor()
    palette = [None] * 16
    for cid in range((frames + chunk - 1) // chunk):
        pb = np.frombuffer(dctx.decompress(open(os.path.join(path, f"planeb_{cid:04d}.zst"), "rb").read()), "<u2")
        color = ((pb >> 8) & 0xF).reshape(-1, h * w)
        missing = [c for c in range(16) if palette[c] is None and (color == c).any()]
        if not missing:
            continue
        rgba = np.frombuffer(dctx.decompress(open(os.path.join(path, f"rgba_{cid:04d}.zst"), "rb").read()),
                             np.uint8).reshape(-1, h * w, 4)
        for c in missing:
            f, p = np.argwhere(color == c)[0]
            values = rgba[f][color[f] == c][:, :3]
            first = values[0]
            assert (values == first).all(), f"{path}: color index {c} is drawn in several RGB values"
            palette[c] = "#%02x%02x%02x" % tuple(int(v) for v in first)
        if all(v is not None for v in palette):
            break
    info["palette16"] = palette
    with open(os.path.join(path, "clip.json"), "w") as f:
        json.dump(info, f, indent=2)
        f.write("\n")
    return palette


for clip in sys.argv[1:]:
    print(clip, recover(clip))
