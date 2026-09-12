"""Verify that an annotated z80dasm listing covers every ROM byte exactly.

z80dasm -l annotates each line as ";ADDR<tab>xx xx xx<tab>ascii" (the raw
bytes of the instruction or defb).  Summing those byte runs must
reconstruct the 16 KiB monitor page 1:1 - any gap, overlap or value
mismatch means the block map or the disassembly is wrong.

Usage: checkcov.py <listing.asm> <binary> [<listing.asm> <binary> ...]
"""
import re
import sys

LINE = re.compile(r';([0-9a-f]{4})\t([0-9a-f]{2}(?: [0-9a-f]{2})*)')


def check(path, binPath):
    want = open(binPath, 'rb').read()
    got = {}
    for ln in open(path):
        m = LINE.search(ln)
        if not m:
            continue
        start = int(m.group(1), 16)
        for k, hx in enumerate(m.group(2).split(' ')):
            a = start + k
            if a in got:
                print('OVERLAP at $%04X' % a)
            got[a] = int(hx, 16)
    bad = 0
    for a in range(len(want)):
        if a not in got:
            print('MISSING  $%04X' % a)
            bad += 1
        elif got[a] != want[a]:
            print('MISMATCH $%04X: asm %02X bin %02X' % (a, got[a], want[a]))
            bad += 1
    extra = [a for a in got if a >= len(want)]
    if extra:
        print('OUT OF RANGE:', [hex(a) for a in extra[:8]])
        bad += len(extra)
    print('%s: %d/%d bytes verified%s' %
          (path.split('/')[-1], len(got), len(want),
           '' if not bad else ' - %d PROBLEMS' % bad))
    return bad == 0


if __name__ == '__main__':
    ok = True
    for asm, binf in zip(sys.argv[1::2], sys.argv[2::2]):
        ok &= check(asm, binf)
    sys.exit(0 if ok else 1)
