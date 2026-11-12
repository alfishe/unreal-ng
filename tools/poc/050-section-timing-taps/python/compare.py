#!/usr/bin/env python3
"""POC 050: pass-by-pass comparison of a variant's passes with the reference's (OUT/ref-MODEL.json)."""

import json
import sys


def load(path):
    data = json.load(open(path))
    passes = sorted((tuple(p) for p in data.get("passes", [])), key=lambda p: (p[1], p[2], p[0]))
    return data, passes


def main(ref_path, *paths):
    ref, ref_passes = load(ref_path)
    first = ref_passes[0][1] if ref_passes else 0
    last = ref_passes[-1][1] if ref_passes else 0
    for path in paths:
        data, passes = load(path)
        # the same frames only: the reference stops at a frame end, a tap run may run on a frame or two
        passes = [p for p in passes if first <= p[1] <= last]
        a, b = set(ref_passes), set(passes)
        diff = sorted(a ^ b, key=lambda p: (p[1], p[2], p[0]))
        print("{}: {} passes, reference {}, equal {}, only in reference {}, only here {}".format(
            path, len(passes), len(ref_passes), len(a & b), len(a - b), len(b - a)))
        for p in diff[:12]:
            print("   ", "ref " if p in a else "here", p)


if __name__ == "__main__":
    main(*sys.argv[1:])
