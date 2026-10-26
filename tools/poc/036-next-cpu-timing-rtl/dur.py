#!/usr/bin/env python3
"""POC 036: instruction durations of the body, the RTL against ours (independent of the time origin).

    dur.py <rtl trace> <our trace> <body address hex> [lines to show]
"""
import sys
import compare

rtl, our, body = sys.argv[1:4]
show = int(sys.argv[4]) if len(sys.argv) > 4 else 30
run_at = int(body, 16)
ints, inst, ports = compare.rtl_events(rtl)
ref, i0 = compare.body_origin(ints, inst, run_at)
r = [((c - ref) / 8, a, vc, hc) for c, a, vc, hc in inst[i0:]]
oi, op, oint = compare.ours(our)
k0 = next(i for i, (t, a) in enumerate(oi) if a == run_at)
o = oi[k0:]
base = o[0][0] - r[0][0]
print("body start: ours %d T after its INT, RTL %.1f" % (o[0][0] - max(x for x in [0] + oint if x <= o[0][0]), r[0][0]))
bad = 0
n = min(len(r), len(o))
for k in range(1, n):
    if r[k][1] != o[k][1]:
        print("instruction %d: RTL at %04X, ours at %04X: the paths part" % (k, r[k][1], o[k][1]))
        n = k
        break
    dr = r[k][0] - r[k - 1][0]
    do = o[k][0] - o[k - 1][0]
    if dr != do:
        if bad < show:
            print("k %6d pc %04X at RTL T %8.1f (line %3d hc %3d): RTL %4.1f T, ours %d" % (k, r[k - 1][1], r[k - 1][0], r[k - 1][2], r[k - 1][3], dr, do))
        bad += 1
print("%d instructions, %d durations differ" % (n, bad))
