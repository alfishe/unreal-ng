"""aliasmin.py <scene> <score files of the alias sets...>: per render, per element the least pursuit over the alias
sets (the viewer follows whichever of the tied motions the render shows), fixation the least; prints truthscore lines
(pursuit = mean over the followed elements, p95 = -, lag of set 0)"""
import sys
scene = sys.argv[1]
sets = []
for path in sys.argv[2:]:
    lines = [l.split() for l in open(path) if l.strip()][1:]  # truthscore: header, n results, n element rows
    n = len(lines) // 2
    head = {scene + "/" + f[0].split("/", 1)[1]: (float(f[1]), float(f[3]), f[4]) for f in lines[:n]}
    elems = {scene + "/" + f[0]: [None if x == "-" else float(x) for x in f[1:]] for f in lines[n:]}
    sets.append((head, elems))
names = list(sets[0][0])
for name in names:
    per = None
    for head, elems in sets:
        e = elems.get(name)
        if e is None:
            continue
        per = e if per is None else [None if a is None else min(a, b) for a, b in zip(per, e)]
    followed = [x for x in (per or []) if x is not None]
    pursuit = sum(followed) / len(followed) if followed else 0.0
    fixation = min(h[name][1] for h, _ in sets if name in h)
    print(f"{name:28s} {pursuit:10.2f} {'-':>8s} {fixation:11.2f} {sets[0][0][name][2]:>7s}")
