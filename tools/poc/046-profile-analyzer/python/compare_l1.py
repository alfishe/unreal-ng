#!/usr/bin/env python3
"""POC 046: compare a profile (poc046-profile replay --out) with the L1 build of the same recording.

Per instruction: count and T-states against L1's instruction rows (key: page, address within the page).
Per routine: the profile's discovered entries against L1's routines (entries found, self, inclusive), and the
self by address ownership (each executed byte to the nearest L1 entry at or below it on its page) against L1's
CFG-owned self.

    compare_l1.py profile.json l1.json [--top 24] [--json out.json]
"""
import argparse
import bisect
import json
import sys


def addr_of(text):
    return int(text.lstrip('$'), 16)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('profile')
    ap.add_argument('l1')
    ap.add_argument('--top', type=int, default=24)
    ap.add_argument('--json')
    a = ap.parse_args()
    prof = json.load(open(a.profile))
    l1 = json.load(open(a.l1))
    out = {}

    # ---- totals ----
    rep = l1['replay']
    print(f"frames      profile {prof['frames']:>12}  L1 {rep['frames']:>12}")
    print(f"instructions profile {prof['instructions']:>12}  L1 {rep['instructions']:>12}")
    print(f"tstates     profile {prof['tstates']:>12}  L1 {rep['tstates']:>12}")
    print(f"interrupts  profile {prof['acks']:>12}  L1 {rep['interrupts']:>12}")
    out['totals'] = dict(frames=[prof['frames'], rep['frames']], instructions=[prof['instructions'], rep['instructions']],
                         tstates=[prof['tstates'], rep['tstates']], interrupts=[prof['acks'], rep['interrupts']])

    # ---- per instruction ----
    mine = {}
    for addr, page, count, t, _op in prof['instructions_rows']:
        mine[(page, addr & 0x3FFF)] = (count, t, addr)
    theirs = {}
    for row in l1['instructions']:
        addr, page, count, t = addr_of(row[0]), row[1], row[4], row[5]
        theirs[(page, addr & 0x3FFF)] = (count, t, addr)
    keys = set(mine) | set(theirs)
    exact = count_diff = t_diff = only_mine = only_l1 = 0
    t_abs = 0
    worst = []
    for k in keys:
        m = mine.get(k)
        l = theirs.get(k)
        if m is None:
            only_l1 += 1
            t_abs += l[1]
            worst.append((l[1], k, None, l))
            continue
        if l is None:
            if m[0] or m[1]:
                only_mine += 1
                t_abs += m[1]
                worst.append((m[1], k, m, None))
            continue
        if m[0] == l[0] and m[1] == l[1]:
            exact += 1
            continue
        if m[0] != l[0]:
            count_diff += 1
        if m[1] != l[1]:
            t_diff += 1
        t_abs += abs(m[1] - l[1])
        worst.append((abs(m[1] - l[1]), k, m, l))
    total_t = max(1, rep['tstates'])
    print(f"\nper instruction: {len(theirs)} in L1, {len(mine)} in the profile; exact {exact}, count differs {count_diff}, "
          f"T differs {t_diff}, only in the profile {only_mine}, only in L1 {only_l1}; |dT| sum {t_abs} "
          f"({100.0 * t_abs / total_t:.4f} % of the window)")
    worst.sort(reverse=True)
    for d, k, m, l in worst[:12]:
        print(f"   page {k[0]:3} +{k[1]:04X}: profile {m} L1 {l}")
    out['instructions'] = dict(l1=len(theirs), profile=len(mine), exact=exact, count_diff=count_diff, t_diff=t_diff,
                               only_profile=only_mine, only_l1=only_l1, abs_dt=t_abs,
                               abs_dt_pct=100.0 * t_abs / total_t)

    # ---- routines: discovery ----
    found = {(r['page'], r['addr'] & 0x3FFF): r for r in prof['routines']}
    l1r = {(r['page'], addr_of(r['entry']) & 0x3FFF): r for r in l1['routines']}
    kinds = {}
    for k, r in l1r.items():
        for kind in r['kinds'].split(','):
            kind = kind.strip()
            kinds.setdefault(kind, [0, 0])
            kinds[kind][0] += 1
            if k in found:
                kinds[kind][1] += 1
    print(f"\nroutines: L1 {len(l1r)}, discovered {len(found)}, both {len(set(found) & set(l1r))}")
    for kind, (n, f) in sorted(kinds.items()):
        print(f"   L1 kind {kind:12} {n:4}  discovered {f:4} ({100.0 * f / max(1, n):.0f} %)")
    extra = [r for k, r in found.items() if k not in l1r]
    print(f"   discovered, not an L1 routine: {len(extra)}")
    for r in sorted(extra, key=lambda r: -r['incl'])[:8]:
        print(f"      ${r['addr']:04X} page {r['page']} calls {r['calls']} rsts {r['rsts']} ints {r['ints']} "
              f"dispatches {r['dispatches_in']} incl {r['incl']}")
    out['discovery'] = dict(l1=len(l1r), discovered=len(found), both=len(set(found) & set(l1r)), kinds=kinds,
                            extra=len(extra))

    # ---- routines: self by stack (the profile) vs L1 self; inclusive vs L1 inclusive ----
    top = sorted(l1r.items(), key=lambda kv: -kv[1]['profile']['tstates'])[:a.top]
    print(f"\ntop {a.top} L1 routines by self (permille of the window: L1 self | profile stack self | address self |"
          f" L1 incl | profile incl)")
    # address ownership: each executed byte to the nearest L1 entry at or below on its page
    entries_by_page = {}
    for (page, off) in l1r:
        entries_by_page.setdefault(page, []).append(off)
    for v in entries_by_page.values():
        v.sort()
    addr_self = {}
    for (page, off), (count, t, _addr) in mine.items():
        e = entries_by_page.get(page)
        if not e:
            continue
        i = bisect.bisect_right(e, off) - 1
        if i < 0:
            continue
        addr_self[(page, e[i])] = addr_self.get((page, e[i]), 0) + t
    rows = []
    max_self_err = max_addr_err = max_incl_err = 0.0
    for k, r in top:
        pr = r['profile']
        mine_r = found.get(k)
        l1_self = pr['tstates']
        l1_incl = pr['tstates_inclusive']
        st_self = mine_r['self'] if mine_r else 0
        st_incl = mine_r['incl'] if mine_r else 0
        ad_self = addr_self.get(k, 0)
        pm = lambda x: 1000.0 * x / total_t
        self_err = abs(pm(st_self) - pm(l1_self))
        addr_err = abs(pm(ad_self) - pm(l1_self))
        incl_err = abs(pm(st_incl) - pm(l1_incl))
        max_self_err = max(max_self_err, self_err)
        max_addr_err = max(max_addr_err, addr_err)
        max_incl_err = max(max_incl_err, incl_err)
        flags = ''
        if mine_r:
            if mine_r['jump_ins']:
                flags += f" jump_ins {mine_r['jump_ins']}"
            if mine_r['abandoned']:
                flags += f" abandoned {mine_r['abandoned']}"
            if mine_r['patched']:
                flags += f" patched {mine_r['patched']}"
            if mine_r['dispatches_out']:
                flags += f" dispatch_out {mine_r['dispatches_out']}"
        print(f"   {r['entry']:>6} p{k[0]:<3} {r['kinds'][:14]:14} {pm(l1_self):7.1f} | {pm(st_self):7.1f} | "
              f"{pm(ad_self):7.1f} | {pm(l1_incl):7.1f} | {pm(st_incl):7.1f}{flags}")
        rows.append(dict(entry=r['entry'], page=k[0], kinds=r['kinds'], l1_self=l1_self, stack_self=st_self,
                         addr_self=ad_self, l1_incl=l1_incl, stack_incl=st_incl))
    print(f"   max |error| permille of the window: stack self {max_self_err:.2f}, address self {max_addr_err:.2f}, "
          f"inclusive {max_incl_err:.2f}")
    out['top'] = rows
    out['max_err_permille'] = dict(stack_self=max_self_err, addr_self=max_addr_err, incl=max_incl_err)

    # anomalies over all routines
    tot = lambda key: sum(r[key] for r in prof['routines'])
    anomalies = dict(jump_ins=tot('jump_ins'), abandoned=tot('abandoned'), patched=tot('patched'),
                     dispatches=tot('dispatches_out'), stack_overflows=prof['stack_overflows'],
                     root_self_permille=1000.0 * prof['root_self'] / total_t)
    print(f"\nanomalies: {anomalies}")
    out['anomalies'] = anomalies
    if a.json:
        json.dump(out, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    sys.exit(main())
