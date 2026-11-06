#!/usr/bin/env python3
"""POC 046: compare the core profile's kit export (poc046-profile replay --core 1, or cpuprofile export format=kit)
with another profile of the same recording, per instruction and per routine.

The other side is an L1 build (l1.json: its interrupt acknowledge sits in the interrupted instruction) or a POC profile
(poc046-profile replay --out, with --split-ack 1 it books the acknowledge as the core does). Per instruction the
execution counts must match exactly; the T-states match exactly against a split-ack POC run and, against L1, differ
only by acknowledges moved from the interrupted instruction to the handler's first one (the totals match).

    compare_kit.py core-kit.json other.json [--top 24] [--json out.json]
"""
import argparse
import json
import sys


def addr_of(text):
    return int(text.lstrip('$'), 16) if isinstance(text, str) else int(text)


def rows_of(doc):
    rows = {}
    if 'instructions_rows' in doc:  # the POC's own format
        for addr, page, count, t, _op in doc['instructions_rows']:
            rows[(page, addr & 0x3FFF)] = (count, t)
    else:
        for row in doc['instructions']:
            rows[(row[1], addr_of(row[0]) & 0x3FFF)] = (row[4], row[5])
    return rows


def routines_of(doc):
    out = {}
    for r in doc['routines']:
        if 'profile' in r:
            key = (r['page'], addr_of(r['entry']) & 0x3FFF)
            p = r['profile']
            out[key] = dict(self=p.get('tstates', 0), incl=p.get('tstates_inclusive', 0), name=r.get('name', ''))
        else:  # the POC
            key = (r['page'], r['addr'] & 0x3FFF)
            out[key] = dict(self=r['self'], incl=r['incl'], name='sub_%04X' % r['addr'])
    return out


def total_t(doc):
    return doc['replay']['tstates'] if 'replay' in doc else doc['tstates']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('core')
    ap.add_argument('other')
    ap.add_argument('--top', type=int, default=24)
    ap.add_argument('--json')
    a = ap.parse_args()
    core = json.load(open(a.core))
    other = json.load(open(a.other))
    result = {}

    ct, ot = total_t(core), total_t(other)
    print(f"tstates  core {ct:>14}  other {ot:>14}  diff {ct - ot}")
    result['tstates'] = [ct, ot]

    mine, theirs = rows_of(core), rows_of(other)
    keys = set(mine) | set(theirs)
    count_exact = t_exact = 0
    count_diff = []
    t_diff_abs = 0
    for k in keys:
        m = mine.get(k, (0, 0))
        o = theirs.get(k, (0, 0))
        if m[0] == o[0]:
            count_exact += 1
        else:
            count_diff.append((k, m, o))
        if m[1] == o[1]:
            t_exact += 1
        else:
            t_diff_abs += abs(m[1] - o[1])
    print(f"instructions {len(keys)}: count exact {count_exact} ({100.0 * count_exact / max(1, len(keys)):.3f} %), "
          f"T exact {t_exact} ({100.0 * t_exact / max(1, len(keys)):.3f} %), |T diff| {t_diff_abs} "
          f"({1000.0 * t_diff_abs / max(1, ot):.3f} permille of the total)")
    for k, m, o in sorted(count_diff, key=lambda x: -abs(x[1][0] - x[2][0]))[:8]:
        print(f"  count differs at page {k[0]} ${k[1]:04X}: core {m} other {o}")
    result['instructions'] = dict(keys=len(keys), count_exact=count_exact, t_exact=t_exact, t_diff_abs=t_diff_abs)

    cr, orr = routines_of(core), routines_of(other)
    common = set(cr) & set(orr)
    print(f"routines core {len(cr)} other {len(orr)} common {len(common)}")
    top = sorted(common, key=lambda k: -orr[k]['self'])[:a.top]
    worst_self = worst_incl = 0.0
    for k in top:
        ds = 1000.0 * abs(cr[k]['self'] - orr[k]['self']) / max(1, ot)
        di = 1000.0 * abs(cr[k]['incl'] - orr[k]['incl']) / max(1, ot)
        worst_self, worst_incl = max(worst_self, ds), max(worst_incl, di)
        print(f"  {cr[k]['name']:<24} self {cr[k]['self']:>12} vs {orr[k]['self']:>12} ({ds:6.3f} permille)  "
              f"incl {cr[k]['incl']:>12} vs {orr[k]['incl']:>12} ({di:6.3f} permille)")
    print(f"top-{len(top)} worst: self {worst_self:.3f} permille, inclusive {worst_incl:.3f} permille")
    result['routines'] = dict(core=len(cr), other=len(orr), common=len(common), worst_self_permille=worst_self,
                              worst_inclusive_permille=worst_incl)
    if a.json:
        json.dump(result, open(a.json, 'w'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
