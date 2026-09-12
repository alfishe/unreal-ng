#!/usr/bin/env python3
"""Resolve unmapped symbols by matching normalized mnemonic signatures
between the base digest and another monitor's digest."""
import importlib.util
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HEXNUM = re.compile(r'\b[0-9a-f]+h\b')


def loadDigest(name):
    d = {}
    order = []
    with open(HERE + '/' + name) as f:
        for line in f:
            m = re.match(r'^([0-9a-f]{4}) (.*)$', line.rstrip('\n'))
            if m:
                a = int(m.group(1), 16)
                d[a] = m.group(2)
                order.append(a)
    order.sort()
    return d, order


def norm(s):
    return HEXNUM.sub('#', s)


def sig(d, order, addr, n=4):
    """normalized mnemonic tuple of up to n consecutive digest lines"""
    out = []
    for a in order:
        if a >= addr:
            if a != addr:
                return None            # addr not present (data region)
            i = order.index(a)
            for x in order[i:i + n]:
                out.append(norm(d[x]))
            return tuple(out)
    return None


def main():
    other = sys.argv[1]                            # digest_scorp295
    mapName = sys.argv[2]                          # map_mon_scorp295_p2.json
    bd, bo = loadDigest('digest_scorpion.txt')
    od, oo = loadDigest(other + '.txt')
    mapped = json.load(open(HERE + '/' + mapName))
    # normalised lookup: list of (tuple, addr) windows over other digest
    windows = []
    for i in range(len(oo)):
        windows.append((tuple(norm(od[x]) for x in oo[i:i + 4]), oo[i]))
    result = {}
    for name, v in mapped.items():
        if v is not None:
            continue
        # find the base address for this symbol
        spec = importlib.util.spec_from_file_location('b', HERE + '/dict_scorpion.py')
        b = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(b)
        base_addr = dict((n, a) for a, n in b.SYMBOLS)[name]
        s = sig(bd, bo, base_addr)
        if s is None:
            result[name] = ('NODIGEST', hex(base_addr))
            continue
        hits = [a for w, a in windows if w == s]
        if len(hits) == 1:
            result[name] = hits[0]
        else:
            result[name] = ('AMBIG' if hits else 'NOMATCH', hex(base_addr), hits[:6])
    print(json.dumps(result, indent=1))


if __name__ == '__main__':
    main()
