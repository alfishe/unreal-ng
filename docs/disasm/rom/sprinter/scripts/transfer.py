#!/usr/bin/env python3
"""Carry the label names of a reference-source build onto the 3.04 ROM.

No public source of BIOS 3.04 exists, so every name in the listings comes
from the closest sources, matched by byte pattern:

1. Both images are "masked": inside every instruction, absolute 16-bit
   addresses (JP/CALL/LD nn) and relative jump offsets are set to zero, so
   code that only moved still matches.  Data bytes stay as they are.
2. Matching runs: difflib finds the longest common runs (>= MINRUN bytes)
   in order; a reference label inside a run lands at the same offset in the
   matching ROM run.
3. Unique windows: a label not placed by step 2 is placed when the masked
   bytes starting at it (WINDOW bytes, then shorter) occur exactly once in
   the ROM.

Result labels-<target>.json:
  [[romAddr, name, "file:line", "run"|"window", refName, isCode], ...]
plus "unmatched": reference labels whose code is not in 3.04 (changed or
added after 3.04, or before it).

Usage: transfer.py <target> <refName>:<refBinary>:<refOrigin> [...]
       (refName = refs-<refName>.json made by refs.py)
"""
import difflib
import json
import os
import sys

import analyze
import targets
import z80

HERE = os.path.dirname(os.path.abspath(__file__))
MINRUN = 8
WINDOWS = (16, 12, 10)


def maskImage(data, org, insnStarts):
    out = bytearray(data)
    for pos in sorted(insnStarts):
        if 0 <= pos < len(data):
            n = z80.length(data, pos)
            out[pos:pos + n] = z80.masked(data, pos, n)[:len(data) - pos]
    return bytes(out)


def romImage(name):
    t = targets.TARGETS[name]
    data = open(os.path.join(HERE, t['bin']), 'rb').read()
    org = t['org']
    _, starts = analyze.walk(data, org, analyze.allEntries(name, data, org, useLabels=False),
                             t.get('forceData', []))
    return data, org, maskImage(data, org, starts)


def refImage(refName, binPath, org):
    refs = json.load(open(os.path.join(HERE, f'refs-{refName}.json')))
    data = open(binPath, 'rb').read()
    starts = [a - org for a, n in refs['insns'] if 0 <= a - org < len(data)]
    return refs, data, maskImage(data, org, starts)


def place(target, refSpecs):
    rom, romOrg, romMasked = romImage(target)
    placed, unmatched = {}, []
    windowSources = {}
    for spec in refSpecs:
        refName, binPath, refOrg = spec.split(':')
        refOrg = int(refOrg, 0)
        refs, ref, refMasked = refImage(refName, binPath, refOrg)
        sm = difflib.SequenceMatcher(None, refMasked, romMasked, autojunk=False)
        runs = [b for b in sm.get_matching_blocks() if b.size >= MINRUN]
        codeAddrs = {a for a, n in refs['insns']}
        for addr, name, where in refs['labels']:
            off = addr - refOrg
            if not 0 <= off < len(ref):
                continue
            hit = None
            for b in runs:
                if b.a <= off < b.a + b.size:
                    hit = (romOrg + b.b + off - b.a, 'run')
                    break
            if hit is None:
                for w in WINDOWS:
                    if off + w > len(ref):
                        continue
                    pat = refMasked[off:off + w]
                    if len(set(pat)) <= 2:          # filler, not a pattern
                        break
                    first = romMasked.find(pat)
                    if first >= 0 and romMasked.find(pat, first + 1) < 0:
                        hit = (romOrg + first, 'window')
                        break
            if hit is None:
                unmatched.append([addr, name, where, refName])
                continue
            romAddr, how = hit
            if how == 'window':
                windowSources.setdefault(romAddr, set()).add((refName, addr))
            placed.setdefault(romAddr, [])
            if all(p[0] != name for p in placed[romAddr]):
                placed[romAddr].append([name, where, how, refName, addr in codeAddrs])
    # A window that several different reference addresses claim is not a
    # pattern but filler or a repeated idiom: drop those guesses.
    for romAddr, srcs in windowSources.items():
        if len(srcs) > 1:
            keep = [p for p in placed[romAddr] if p[2] != 'window']
            unmatched.extend([[0, p[0], p[1], p[3]] for p in placed[romAddr] if p[2] == 'window'])
            if keep:
                placed[romAddr] = keep
            else:
                del placed[romAddr]
    out = {'labels': [[a] + v for a in sorted(placed) for v in placed[a]],
           'unmatched': unmatched}
    path = os.path.join(HERE, f'labels-{target}.json')
    with open(path, 'w') as f:
        json.dump(out, f, indent=0, ensure_ascii=False)
    print(f'{os.path.basename(path)}: {len(out["labels"])} placed, {len(unmatched)} unmatched')


if __name__ == '__main__':
    place(sys.argv[1], sys.argv[2:])
