#!/usr/bin/env python3
"""List the code regions of a listing that have no counterpart in its reference source.

Same masking and run matching as transfer.py (runs >= 8 bytes). Every code
byte of the 3.04 image not covered by a matching run is "differs from the
source"; runs of such bytes >= MINGAP long are printed with the nearest label
before them, as a markdown table for the page READMEs.

Usage: diffregions.py <target> <refName>:<refBinary>:<refOrigin> [...]
"""
import difflib
import sys

import gen
import targets
import transfer

MINGAP = 24


def main(target, specs):
    rom, org, romMasked = transfer.romImage(target)
    covered = [False] * len(rom)
    for spec in specs:
        name, path, refOrg = spec.split(':')
        _, ref, refMasked = transfer.refImage(name, path, int(refOrg, 0))
        sm = difflib.SequenceMatcher(None, refMasked, romMasked, autojunk=False)
        for b in sm.get_matching_blocks():
            if b.size >= transfer.MINRUN:
                for k in range(b.size):
                    covered[b.b + k] = True
    t = targets.TARGETS[target]
    code = gen.codeMap(gen.os.path.join(gen.HERE, t['bin']), gen.os.path.join(gen.HERE, t['bin'].replace('.bin', '.blocks')), org, len(rom))
    mod = gen.loadDict(target)
    syms = gen.symbols(target, mod, org, len(rom))
    names = sorted(syms)
    print('| Range | Bytes | Near label |')
    print('|---|---|---|')
    i = 0
    total = 0
    while i < len(rom):
        if code[org + i] and not covered[i]:
            j = i
            while j < len(rom) and not covered[j] and code[org + j]:
                j += 1
            if j - i >= MINGAP:
                near = max((a for a in names if a <= org + i), default=None)
                lab = syms[near][0] if near is not None else ''
                print(f'| `#{org + i:04X}-#{org + j - 1:04X}` | {j - i} | `{lab}` |')
                total += j - i
            i = j
        else:
            i += 1
    codeBytes = sum(1 for a in range(len(rom)) if code[org + a])
    same = sum(1 for a in range(len(rom)) if code[org + a] and covered[a])
    print(f'\ncode bytes {codeBytes}, matched by the source {same} ({100 * same / max(codeBytes, 1):.0f}%), listed gaps {total}')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2:])
