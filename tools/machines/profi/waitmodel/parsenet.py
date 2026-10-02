#!/usr/bin/env python3
"""Parse the Profi v5.06 P-CAD ASCII netlist (converted to UTF-8) into a component/pin/net dump."""
import re, sys, collections, json
txt = open(sys.argv[1] if len(sys.argv) > 1 else 'net.sch', encoding='utf-8').read()
# compDefs: pin number -> pin name
defs = {}
for m in re.finditer(r'\(compDef "([^"]+)"(.*?)\n  \)\n', txt, re.S):
    pins = {}
    for p in re.finditer(r'\(compPin "([^"]+)" \(pinName "([^"]*)"\) \(partNum (\d+)\)', m.group(2)):
        pins[p.group(1)] = (p.group(2), int(p.group(3)))
    defs[m.group(1)] = pins
nl = txt[txt.index('(netlist "Netlist_1"'):]
comps = {}
for m in re.finditer(r'\(compInst "([^"]+)"\s*\(compRef "([^"]+)"\)\s*\(originalName "([^"]+)"\)(?:\s*\(compValue "([^"]*)"\))?', nl):
    comps[m.group(1)] = dict(ref=m.group(2), orig=m.group(3), value=m.group(4))
nets = collections.OrderedDict()
for m in re.finditer(r'\(net "([^"]+)"(.*?)\n  \)', nl, re.S):
    nodes = re.findall(r'\(node "([^"]+)" "([^"]+)"\)', m.group(2))
    nets[m.group(1)] = nodes
pin2net = {}
for n, nodes in nets.items():
    for c, p in nodes:
        pin2net[(c, p)] = n
def pname(c, p):
    d = defs.get(comps.get(c, {}).get('ref'), {})
    return d.get(p, ('?', 0))[0]
json.dump(dict(comps=comps, nets=nets, pinnames={c: {p: pname(c, p) for (cc, p) in pin2net if cc == c} for c in comps}),
          open('net.json', 'w'), ensure_ascii=False, indent=0)
if __name__ == '__main__':
    with open('netdump.txt', 'w') as f:
        for c in sorted(comps, key=lambda s: (re.sub(r'\d', '', s), int(re.sub(r'\D', '', s) or 0))):
            f.write(f"{c} {comps[c]['orig']} {comps[c]['value'] or ''}\n")
            pins = sorted([p for (cc, p) in pin2net if cc == c], key=lambda s: int(s) if s.isdigit() else 999)
            for p in pins:
                f.write(f"   {p:>3} {pname(c,p):<8} {pin2net[(c,p)]}\n")
        f.write('\n=== NETS ===\n')
        for n, nodes in nets.items():
            f.write(n + ': ' + ' '.join(f"{c}.{p}({pname(c,p)})" for c, p in nodes) + '\n')
