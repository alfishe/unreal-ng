#!/usr/bin/env python3
"""Show parts of the Profi v5.06 netlist with each pin's net and the net's other members.
Usage: part.py <netdump.txt> DD33 DD30 ...   (netdump.txt: written by ../waitmodel/parsenet.py)"""
import re, sys
if len(sys.argv) < 3:
    sys.exit(__doc__)
path = sys.argv[1]
lines = open(path, encoding='utf-8').read().split('\n')
parts, nets, cur = {}, {}, None
innets = False
for l in lines:
    if l.startswith('=== NETS'): innets = True; continue
    if innets:
        m = re.match(r'^(\S+): (.*)$', l)
        if m: nets[m.group(1)] = m.group(2).split()
        continue
    if l and not l.startswith(' '):
        cur = l.split()[0]; parts[cur] = {'type': l, 'pins': []}
    elif l.strip() and cur:
        f = l.split()
        parts[cur]['pins'].append((f[0], f[1], f[2] if len(f) > 2 else ''))
for p in sys.argv[2:]:
    d = parts.get(p)
    if not d: print(p, 'not found'); continue
    print(d['type'])
    for pin, fn, net in sorted(d['pins'], key=lambda x: int(x[0]) if x[0].isdigit() else 999):
        others = [x for x in nets.get(net, []) if not x.startswith(p + '.')]
        if net in ('VCC', 'GND'): others = []
        print(f'   {pin:>3} {fn:5} {net:10} -> {" ".join(others)}')
