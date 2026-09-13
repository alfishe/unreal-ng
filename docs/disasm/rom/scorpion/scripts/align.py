#!/usr/bin/env python3
"""Align two 16K monitor binaries and map base symbols onto the other ROM.

Identical byte runs (difflib) give base->other address translation for the
literally-identical code the family shares.
"""
import difflib
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def loadDict(name):
    spec = importlib.util.spec_from_file_location('d', HERE + '/' + name)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def runs(a, b, minlen=8):
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    out = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal' and (i2 - i1) >= minlen:
            out.append((i1, i2, j1))
    return out


def main():
    baseBin = open(HERE + '/mon_scorpion_p2.bin', 'rb').read()
    otherName = sys.argv[1]                      # e.g. mon_scorp295_p2
    dictName = sys.argv[2]                       # symbol source dict
    otherBin = open(HERE + '/' + otherName + '.bin', 'rb').read()
    base = loadDict(dictName)
    rr = runs(baseBin, otherBin)
    json.dump(rr, open(HERE + '/runs_%s.json' % otherName, 'w'))
    mapped, missing = {}, []
    for addr, name in base.SYMBOLS:
        if addr >= 0x4000:                       # RAM equates stay valid
            mapped[name] = addr
            continue
        mapped[name] = None
        for s, e, j in rr:
            if s <= addr < e:
                mapped[name] = j + (addr - s)
                break
        if mapped[name] is None:
            missing.append((hex(addr), name))
    json.dump(mapped, open(HERE + '/map_%s.json' % otherName, 'w'), indent=1)
    print('runs:', len(rr), ' mapped symbols:', len(mapped) - len([1 for a, n in base.SYMBOLS if a >= 0x4000]),
          ' unmapped:', len(missing))
    for a, n in missing:
        print('  UNMAPPED', a, n)


if __name__ == '__main__':
    main()
