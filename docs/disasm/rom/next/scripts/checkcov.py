#!/usr/bin/env python3
"""Check that every listing rebuilds its binary byte for byte (the `;ADDR<tab>bytes` columns)."""
import importlib.util
import os
import re
import sys

import gen
import targets

LINE = re.compile(r';([0-9a-f]{4})\t([0-9a-f]{2}(?: [0-9a-f]{2})*)')


def check(name):
    t = targets.TARGETS[name]
    d = gen.loadDict(name)
    want = open(os.path.join(gen.HERE, t['bin']), 'rb').read()
    org = t['org']
    got = {}
    bad = 0
    for ln in open(os.path.join(gen.OUTDIR, d.LISTING)):
        m = LINE.search(ln)
        if not m:
            continue
        start = int(m.group(1), 16) - org
        for k, hx in enumerate(m.group(2).split(' ')):
            if start + k in got:
                print(f'{name}: OVERLAP at #{org + start + k:04X}')
                bad += 1
            got[start + k] = int(hx, 16)
    for i, b in enumerate(want):
        if got.get(i) != b:
            print(f'{name}: MISMATCH at #{org + i:04X}: want {b:02x}, got {got.get(i)}')
            bad += 1
            if bad > 10:
                break
    print(f'{name}: {"OK" if not bad else "FAILED"} ({len(want)} bytes)')
    return bad == 0


if __name__ == '__main__':
    ok = all([check(n) for n in (sys.argv[1:] or targets.TARGETS)])
    sys.exit(0 if ok else 1)
