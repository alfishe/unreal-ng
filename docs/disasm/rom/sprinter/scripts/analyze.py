#!/usr/bin/env python3
"""Code reachability for one listing, driven by targets.py.

Walks the code from the target's entry points (vectors, function tables,
hand-verified extra entries), marks every reached instruction byte as code
and writes the z80dasm block file: every byte NOT reached becomes a named
data block (padding, text, tables, the packed SETUP stream ...).

Usage: analyze.py <target>        (target names: see targets.py)
Writes <target>.blocks and prints the data regions it found.
"""
import os
import sys

import targets
import z80

HERE = os.path.dirname(os.path.abspath(__file__))


def walk(data, org, entries, blocked=()):
    n = len(data)
    code = [False] * n
    for s, e in blocked:                 # hand-verified data: never decode
        for a in range(s - org, e - org):
            code[a] = True
    starts = set()
    stack = [e for e in entries]
    while stack:
        addr = stack.pop()
        pos = addr - org
        while 0 <= pos < n and not code[pos]:
            ln = z80.length(data, pos)
            if pos + ln > n:
                break
            starts.add(pos)
            for i in range(ln):
                code[pos + i] = True
            targetsOut, falls = z80.flow(data, pos, org)
            for t in targetsOut:
                if org <= t < org + n:
                    stack.append(t)
            if not falls:
                break
            pos += ln
    for s, e in blocked:
        for a in range(s - org, e - org):
            code[a] = False
    return code, starts


def wordTableEntries(data, org, tables):
    out = []
    for start, count in tables:
        for i in range(count):
            p = start - org + 2 * i
            out.append(data[p] | (data[p + 1] << 8))
    return out


def allEntries(name, data, org, useLabels=True):
    """Hand entries + jump tables + code labels placed by transfer.py from a
    matching run (the reference source says the bytes there are code)."""
    t = targets.TARGETS[name]
    entries = list(t['entries']) + wordTableEntries(data, org, t.get('wordTables', []))
    path = os.path.join(HERE, f'labels-{name}.json')
    if useLabels and os.path.exists(path):
        import json
        for row in json.load(open(path))['labels']:
            if len(row) > 5 and row[3] == "run" and row[5]:
                entries.append(row[0])
    skip = set(t.get('notCode', []))
    return [e for e in entries if org <= e < org + len(data) and e not in skip]


def classify(seg):
    if all(c == 0xFF for c in seg):
        return 'Fill'
    if all(c == 0x00 for c in seg):
        return 'Zero'
    printable = sum(1 for c in seg if 32 <= c < 127)
    if len(seg) >= 6 and printable >= 0.8 * len(seg):
        return 'Text'
    return 'Data'


def regions(code, data, org, named):
    """Unreached runs, split at hand-named data starts (targets DATA)."""
    n = len(data)
    cuts = {a - org for a in named}
    out = []
    i = 0
    while i < n:
        if code[i]:
            i += 1
            continue
        j = i + 1
        while j < n and not code[j] and j not in cuts:
            j += 1
        out.append((i, j))
        i = j
    return out


def run(name):
    t = targets.TARGETS[name]
    data = open(os.path.join(HERE, t['bin']), 'rb').read()
    org = t['org']
    entries = allEntries(name, data, org)
    code, _ = walk(data, org, entries, t.get('forceData', []))
    named = dict(t.get('data', {}))
    lines = []
    for s, e in regions(code, data, org, named):
        a = org + s
        label = named.get(a) or f'{classify(data[s:e])}{a:04X}'
        lines.append(f'{label}: start 0x{a:04X} end 0x{org + e:04X} type bytedata')
    path = os.path.join(HERE, t['bin'].replace('.bin', '.blocks'))
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    c = sum(code)
    print(f'{name}: {c}/{len(data)} code bytes ({100 * c / len(data):.1f}%), {len(lines)} data blocks -> {os.path.basename(path)}')
    return lines


if __name__ == '__main__':
    names = [a for a in sys.argv[1:] if not a.startswith('--')]
    for nm in (names or targets.TARGETS):
        for ln in run(nm):
            if '--verbose' in sys.argv:
                print('  ' + ln)
