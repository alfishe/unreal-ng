#!/usr/bin/env python3
"""Verify a core-exported clip (POST /ttd/export-clip) frame by frame.

Checks, for every frame:
  1. plane B explains the picture: each drawn pixel's color index, through the
     ZX palette, equals its RGBA; screen pixels' color follows attribute + ink
     bit; border pixels carry attribute 0; all 256x192 paper pixels described
  2. (--reference) the RGBA equals the same frame of a reference clip
     extracted earlier over WebAPI (capture/extract_clip.py)
  3. meta: consecutive frame numbers; displayed screen equals the reference's

Exit code 1 on any failure. Example:
  python3 capture/verify_clip_v2.py data/clip_v2 --reference data/clip_full
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from common.clip import Clip, ClipV2  # noqa: E402
from common.zxscreen import ZX_RGB  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("clip")
ap.add_argument("--reference")
args = ap.parse_args()
clip = ClipV2(args.clip)
ref = Clip(args.reference) if args.reference else None
if not clip.has_planeb:
    sys.exit("clip has no plane B (export with the zxdlss feature on)")

bad_planeb, bad_ref, bad_meta = [], [], []
for i in range(len(clip)):
    frame = clip.meta[i]["frame"]
    rgb = clip.rgb(i)
    pb = ClipV2.decode_planeb(clip.planeb(i))
    attr = pb["attr"].astype(np.int32)
    color = pb["color"].astype(np.int32)
    drawn = pb["role"] != 0
    screen = pb["role"] == ClipV2.ROLE_SCREEN
    border = pb["role"] == ClipV2.ROLE_BORDER
    bright = (attr >> 6) & 1
    want = np.where(pb["ink"], (attr & 7) + 8 * bright, ((attr >> 3) & 7) + 8 * bright)
    problems = []
    if not np.array_equal(ZX_RGB[color[drawn]], rgb[drawn]):
        problems.append("color index does not match RGBA")
    if not np.array_equal(color[screen], want[screen]):
        problems.append("screen color does not follow attribute/ink")
    if np.any(attr[border] != 0):
        problems.append("border pixel with an attribute")
    if screen.sum() != 256 * 192:
        problems.append(f"{int(screen.sum())} screen pixels described")
    if problems:
        bad_planeb.append((frame, problems))
    if i > 0 and frame != clip.meta[i - 1]["frame"] + 1:
        bad_meta.append((frame, "frame numbers not consecutive"))
    if ref is not None:
        j = ref.index_of_frame(frame)
        if 0 <= j < len(ref):
            diff = int(np.any(ref.rgb(j) != rgb, axis=2).sum())
            if diff:
                bad_ref.append((frame, diff))
            if ref.meta[j]["active_screen"] != clip.meta[i]["active_screen"]:
                bad_meta.append((frame, "displayed screen differs from reference"))
    if i % 2000 == 0:
        print(f"frame {frame}: checked", flush=True)

print(f"frames: {len(clip)} ({clip.meta[0]['frame']}..{clip.meta[-1]['frame']}), geometry {clip.w}x{clip.h}")
print(f"plane B consistency failures: {len(bad_planeb)}", bad_planeb[:5])
if ref is not None:
    print(f"RGBA differing from reference: {len(bad_ref)} frames", bad_ref[:5])
print(f"meta problems: {len(bad_meta)}", bad_meta[:5])
sys.exit(1 if (bad_planeb or bad_ref or bad_meta) else 0)
