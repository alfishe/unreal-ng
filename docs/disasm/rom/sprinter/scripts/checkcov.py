#!/usr/bin/env python3
"""Verify that every listing reconstructs its binary byte for byte.

z80dasm -l annotates each line as ";ADDR<tab>xx xx xx<tab>ascii".  Summing
those byte runs must give the binary exactly: a gap, an overlap or a wrong
value means the block map or the disassembly is wrong.

Usage: checkcov.py            (all targets of targets.py)
"""
import importlib.util
import os
import re
import sys

import targets

HERE = os.path.dirname(os.path.abspath(__file__))
LINE = re.compile(r';([0-9a-f]{4})\t([0-9a-f]{2}(?: [0-9a-f]{2})*)')


def check(name):
    t = targets.TARGETS[name]
    spec = importlib.util.spec_from_file_location('d', os.path.join(HERE, f'dict_{name}.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    asm = os.path.join(HERE, '..', mod.LISTING)
    want = open(os.path.join(HERE, t['bin']), 'rb').read()
    org = t['org']
    got = {}
    bad = 0
    for ln in open(asm):
        m = LINE.search(ln)
        if not m:
            continue
        start = int(m.group(1), 16) - org
        for k, hx in enumerate(m.group(2).split(' ')):
            if start + k in got:
                print(f'OVERLAP at #{org + start + k:04X}')
                bad += 1
            got[start + k] = int(hx, 16)
    for a in range(len(want)):
        if got.get(a) != want[a]:
            bad += 1
            if bad < 10:
                print(f'{"MISSING" if a not in got else "MISMATCH"} #{org + a:04X}')
    bad += sum(1 for a in got if a >= len(want))
    print(f'{mod.LISTING}: {sum(1 for a in got if a < len(want))}/{len(want)} bytes verified'
          + ('' if not bad else f' - {bad} PROBLEMS'))
    return bad == 0


if __name__ == '__main__':
    ok = all([check(n) for n in (sys.argv[1:] or targets.TARGETS)])
    sys.exit(0 if ok else 1)
