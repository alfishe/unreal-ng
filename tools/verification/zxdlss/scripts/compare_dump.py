#!/usr/bin/env python3
"""Compare two RGB dumps (zxdlss-render --dump, POC run.py --dump) frame by frame.

  python3 compare_dump.py REFERENCE_DIR CANDIDATE_DIR [--tolerance 1] [--max-share 0.0001]

Reports, per dump, the share of pixels that differ at all, the share that
differ by more than the tolerance (per channel, 0..255) and the largest
difference. Exit code 1 when more than --max-share of the pixels differ by more
than the tolerance, or the frame ranges differ.

Why a tolerance: mixes of 3+ frames sum linear light in an order that depends
on the implementation; a last-bit difference can round differently. The
algorithm's decisions (which pixel mixes, which frames) must match exactly -
a decision mismatch shows as a large difference.
"""
import argparse
import json
import os
import sys

import numpy as np
import zstandard


def frames(d):
    info = json.load(open(os.path.join(d, "dump.json")))
    h, w, chunk = info["height"], info["width"], info["chunk"]
    dctx = zstandard.ZstdDecompressor()
    n = info["frames"]
    for c in range((n + chunk - 1) // chunk):
        raw = dctx.decompress(open(os.path.join(d, f"rgb_{c:04d}.zst"), "rb").read())
        arr = np.frombuffer(raw, np.uint8).reshape(-1, h, w, 3)
        for f in arr:
            yield f
    return


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("reference")
    ap.add_argument("candidate")
    ap.add_argument("--tolerance", type=int, default=1)
    ap.add_argument("--max-share", type=float, default=0.0001)
    args = ap.parse_args()
    ia = json.load(open(os.path.join(args.reference, "dump.json")))
    ib = json.load(open(os.path.join(args.candidate, "dump.json")))
    if (ia["from"], ia["frames"], ia["width"], ia["height"]) != (ib["from"], ib["frames"], ib["width"], ib["height"]):
        print(f"range/geometry differ: {ia} vs {ib}")
        return 1
    total = differ = beyond = 0
    worst = 0
    worst_frame = None
    for k, (a, b) in enumerate(zip(frames(args.reference), frames(args.candidate))):
        d = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2)
        total += d.size
        differ += int((d > 0).sum())
        beyond += int((d > args.tolerance).sum())
        m = int(d.max())
        if m > worst:
            worst, worst_frame = m, ia["from"] + k
    print(f"{ia['frames']} frames from {ia['from']}: pixels differing {differ / total:.6f}, "
          f"beyond +-{args.tolerance} {beyond / total:.6f}, max diff {worst}" + (f" (frame {worst_frame})" if worst else ""))
    return 1 if beyond / total > args.max_share else 0


if __name__ == "__main__":
    sys.exit(main())
