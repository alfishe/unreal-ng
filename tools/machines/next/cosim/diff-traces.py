#!/usr/bin/env python3
"""Compare two co-simulation traces (trace-format.md): a reference emulator against ours (or any two).

    diff-traces.py REF OURS [--kinds NRW,NRR,POUT,PIN,MMU] [--limit 20] [--skip-ports eb,e7]
                            [--ignore-regs 41,44] [--window 4000] [--no-collapse] [--context 3]

REF / OURS are trace.txt files or the run directories of run-ref.sh (trace.txt + state.txt are used).

Timing is ignored. Each kind is compared as its own stream of (addr, value) events; runs of the same event
(a polling loop: the same IN / NR read again and again) are collapsed to one unless --no-collapse. The streams are
aligned by resynchronising on the next window of matching events, so one missing or extra event does not turn the
rest of the stream into noise. Reported:

  * a summary table (event counts raw / collapsed, first divergence, number of divergence blocks per kind),
  * the first --limit divergence blocks of every stream: what the reference does that ours does not, what ours does
    that the reference does not, or both differ - with the PC of the events and a few events of context,
  * the first divergence of the MERGED stream (NRW + NRR + POUT + PIN + MMU in their original order),
  * the registers / ports used by only one side, with their counts,
  * the end-of-run state dumps (state.txt): registers, MMU slots and the NR table, when both exist.

Ports #243B / #253B never appear (they are NRW / NRR); IRQ, PCS and FRM are not compared (informational only).
"""
import argparse
import bisect
import os
import sys
from collections import Counter, OrderedDict

STREAMS = ["NRW", "NRR", "POUT", "PIN", "MMU"]


def load(path):
    """-> list of (seq, kind, addr:int, value:int, pc:int, raw_addr_text)"""
    if os.path.isdir(path):
        path = os.path.join(path, "trace.txt")
    events = []
    with open(path) as f:
        for n, line in enumerate(f, 1):
            p = line.split()
            if len(p) != 5:
                continue
            kind = p[1]
            if kind in ("PCS", "FRM"):
                a = int(p[2], 10 if kind == "FRM" else 16)
            else:
                a = int(p[2], 16)
            events.append((int(p[0]), kind, a, int(p[3], 16), int(p[4], 16)))
    return events


def load_state(path):
    p = os.path.join(path, "state.txt") if os.path.isdir(path) else None
    if not p or not os.path.exists(p):
        return None
    st = {"nr": {}, "mmu": {}, "regs": {}}
    with open(p) as f:
        for line in f:
            w = line.split()
            if not w:
                continue
            if w[0] == "nr":
                st["nr"][int(w[1], 16)] = int(w[2], 16)
            elif w[0] == "mmu":
                st["mmu"][int(w[1])] = int(w[2], 16)
            elif w[0] == "regs":
                for kv in w[1:]:
                    k, v = kv.split("=")
                    st["regs"][k] = int(v, 16) if k not in ("IM", "IFF1", "IFF2", "HALT") else int(v)
            elif w[0] in ("emulator", "frames"):
                st[w[0]] = w[1]
    return st


def fmt(ev):
    seq, kind, a, v, pc = ev
    if kind == "NRW":
        return f"NRW  NR {a:02X} <- {v:02X}  pc={pc:04X}"
    if kind == "NRR":
        return f"NRR  NR {a:02X} = {v:02X}  pc={pc:04X}"
    if kind == "POUT":
        return f"POUT {a:04X} <- {v:02X}  pc={pc:04X}"
    if kind == "PIN":
        return f"PIN  {a:04X} = {v:02X}  pc={pc:04X}"
    if kind == "MMU":
        return f"MMU  slot {a} = page {v:02X}  pc={pc:04X}"
    return f"{kind} {a:X} {v:02X} pc={pc:04X}"


def key(ev, ignore_value=False):
    return (ev[2],) if ignore_value else (ev[2], ev[3])


def collapse(events, enabled):
    """Runs of an identical (addr, value) collapse to one entry: (event, count)"""
    out = []
    for ev in events:
        k = key(ev)
        if enabled and out and key(out[-1][0]) == k:
            out[-1][1] += 1
        else:
            out.append([ev, 1])
    return out


def align(a, b, window, ahead=3):
    """a, b: lists of events (collapsed). Returns blocks (i, j, na, nb): a[i:i+na] vs b[j:j+nb] differ."""
    ka = [key(e[0]) for e in a]
    kb = [key(e[0]) for e in b]
    k = ahead
    index = {}
    for j in range(len(kb) - k + 1):
        index.setdefault(tuple(kb[j:j + k]), []).append(j)
    blocks = []
    i = j = 0
    while i < len(ka) and j < len(kb):
        if ka[i] == kb[j]:
            i += 1
            j += 1
            continue
        best = None
        for da in range(0, window):
            if i + da + k > len(ka):
                break
            lst = index.get(tuple(ka[i + da:i + da + k]))
            if lst:
                x = bisect.bisect_left(lst, j)
                if x < len(lst) and lst[x] - j < window:
                    cost = da + (lst[x] - j)
                    if best is None or cost < best[0]:
                        best = (cost, da, lst[x] - j)
            if best is not None and best[0] <= da:
                break
        if best is None:
            blocks.append((i, j, len(ka) - i, len(kb) - j))
            return blocks, True
        _, da, db = best
        blocks.append((i, j, da, db))
        i += da
        j += db
    if i < len(ka) or j < len(kb):
        blocks.append((i, j, len(ka) - i, len(kb) - j))
    return blocks, False


def describe(block, a, b, ctx):
    i, j, na, nb = block
    if na and not nb:
        what = f"ref has {na} event(s) ours lacks"
    elif nb and not na:
        what = f"ours has {nb} extra event(s)"
    else:
        what = f"differ: ref {na} event(s) vs ours {nb}"
    lines = [f"  [{what}]  ref #{i}  ours #{j}"]
    for e in a[max(0, i - ctx):i]:
        lines.append("      = " + fmt(e[0]) + (f"  x{e[1]}" if e[1] > 1 else ""))
    for e in a[i:i + min(na, 6)]:
        lines.append("      - " + fmt(e[0]) + (f"  x{e[1]}" if e[1] > 1 else ""))
    if na > 6:
        lines.append(f"      - ... {na - 6} more")
    for e in b[j:j + min(nb, 6)]:
        lines.append("      + " + fmt(e[0]) + (f"  x{e[1]}" if e[1] > 1 else ""))
    if nb > 6:
        lines.append(f"      + ... {nb - 6} more")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ref")
    ap.add_argument("ours")
    ap.add_argument("--kinds", default=",".join(STREAMS))
    ap.add_argument("--limit", type=int, default=20)
    ap.add_argument("--skip-ports", default="", help="low port bytes in hex to ignore, e.g. eb,e7")
    ap.add_argument("--ignore-regs", default="", help="NR numbers (hex) to ignore in NRW / NRR")
    ap.add_argument("--window", type=int, default=4000)
    ap.add_argument("--context", type=int, default=2)
    ap.add_argument("--no-collapse", action="store_true")
    ap.add_argument("--ref-name", default="ref")
    ap.add_argument("--ours-name", default="ours")
    args = ap.parse_args()

    skip_ports = {int(x, 16) for x in args.skip_ports.split(",") if x}
    ignore_regs = {int(x, 16) for x in args.ignore_regs.split(",") if x}
    kinds = [k for k in args.kinds.split(",") if k]

    ev_a = load(args.ref)
    ev_b = load(args.ours)

    def keep(ev):
        if ev[1] in ("NRW", "NRR") and ev[2] in ignore_regs:
            return False
        if ev[1] in ("POUT", "PIN") and (ev[2] & 0xFF) in skip_ports:
            return False
        return True

    ev_a = [e for e in ev_a if keep(e)]
    ev_b = [e for e in ev_b if keep(e)]

    print(f"# co-simulation diff: {args.ref_name} = {args.ref}  vs  {args.ours_name} = {args.ours}\n")

    per = OrderedDict()
    summary = []
    for kind in kinds:
        sa = [e for e in ev_a if e[1] == kind]
        sb = [e for e in ev_b if e[1] == kind]
        ca = collapse(sa, not args.no_collapse)
        cb = collapse(sb, not args.no_collapse)
        blocks, truncated = align(ca, cb, args.window)
        per[kind] = (ca, cb, blocks, truncated)
        first = f"ref #{blocks[0][0]} / ours #{blocks[0][1]}" if blocks else "-"
        summary.append((kind, len(sa), len(sb), len(ca), len(cb), first, len(blocks)))

    print("## Summary\n")
    print(f"| kind | {args.ref_name} raw | {args.ours_name} raw | {args.ref_name} collapsed | {args.ours_name} collapsed | first divergence (collapsed index) | divergence blocks |")
    print("|---|---:|---:|---:|---:|---|---:|")
    for r in summary:
        print(f"| {r[0]} | {r[1]} | {r[2]} | {r[3]} | {r[4]} | {r[5]} | {r[6]} |")
    other = Counter(e[1] for e in ev_a if e[1] not in kinds)
    other_b = Counter(e[1] for e in ev_b if e[1] not in kinds)
    print(f"\nNot compared: {args.ref_name} {dict(other)}, {args.ours_name} {dict(other_b)}\n")

    for kind in kinds:
        ca, cb, blocks, truncated = per[kind]
        print(f"## {kind}: first {min(args.limit, len(blocks))} of {len(blocks)} divergence block(s)\n")
        if not blocks:
            print("  identical streams\n")
            continue
        for blk in blocks[:args.limit]:
            print(describe(blk, ca, cb, args.context))
        if truncated:
            print(f"  (the streams could not be resynchronised within {args.window} events after the last block)")
        print()

    # merged stream
    mk = [k for k in kinds]
    ma = collapse([e for e in ev_a if e[1] in mk], not args.no_collapse)
    mb = collapse([e for e in ev_b if e[1] in mk], not args.no_collapse)
    # a merged event carries its kind in the key
    def mkey(e):
        return (e[0][1], e[0][2], e[0][3])
    ka = [mkey(e) for e in ma]
    kb = [mkey(e) for e in mb]
    n = min(len(ka), len(kb))
    first = next((i for i in range(n) if ka[i] != kb[i]), None)
    print("## Merged stream (all compared kinds in original order)\n")
    if first is None and len(ka) == len(kb):
        print("  identical\n")
    else:
        first = n if first is None else first
        print(f"  first difference at merged event #{first} (collapsed): ref {len(ma)} events, ours {len(mb)}")
        for e in ma[max(0, first - 4):first]:
            print("      = " + fmt(e[0]))
        for e in ma[first:first + 4]:
            print("      - " + fmt(e[0]))
        for e in mb[first:first + 4]:
            print("      + " + fmt(e[0]))
        print()

    # registers / ports only one side uses
    print("## Used by one side only\n")
    for kind, label in (("NRW", "NR writes"), ("NRR", "NR reads"), ("POUT", "port writes"), ("PIN", "port reads")):
        if kind not in kinds:
            continue
        ca = Counter(e[2] for e in ev_a if e[1] == kind)
        cb = Counter(e[2] for e in ev_b if e[1] == kind)
        w = 2 if kind in ("NRW", "NRR") else 4
        ra = sorted(set(ca) - set(cb))
        rb = sorted(set(cb) - set(ca))
        print(f"- {label}: only {args.ref_name}: " + (" ".join(f"{x:0{w}X}x{ca[x]}" for x in ra) or "-"))
        print(f"  {label}: only {args.ours_name}: " + (" ".join(f"{x:0{w}X}x{cb[x]}" for x in rb) or "-"))
        both = sorted(set(ca) & set(cb))
        ratio = [(x, ca[x], cb[x]) for x in both if max(ca[x], cb[x]) > 3 * max(1, min(ca[x], cb[x]))]
        if ratio:
            print(f"  {label}: very different counts (addr, {args.ref_name}, {args.ours_name}): " +
                  " ".join(f"{x:0{w}X}:{p}/{q}" for x, p, q in ratio[:30]))
    print()

    # state dumps
    sa = load_state(args.ref)
    sb = load_state(args.ours)
    if sa and sb:
        print("## End-of-run state\n")
        diff_regs = {k: (sa["regs"].get(k), sb["regs"].get(k)) for k in sa["regs"] if sa["regs"].get(k) != sb["regs"].get(k)}
        print(f"- registers differing ({args.ref_name} / {args.ours_name}): " +
              (" ".join(f"{k}={v[0]:X}/{v[1]:X}" for k, v in diff_regs.items() if v[0] is not None and v[1] is not None) or "none"))
        print(f"- MMU slots {args.ref_name}: " + " ".join(f"{sa['mmu'].get(i, 0):02X}" for i in range(8)))
        print(f"- MMU slots {args.ours_name}: " + " ".join(f"{sb['mmu'].get(i, 0):02X}" for i in range(8)))
        nd = [(r, sa["nr"][r], sb["nr"].get(r)) for r in sorted(sa["nr"]) if r not in ignore_regs and sa["nr"][r] != sb["nr"].get(r)]
        print(f"- NR registers differing ({len(nd)}): " + " ".join(f"{r:02X}={x:02X}/{y:02X}" for r, x, y in nd[:80]))
        print()


if __name__ == "__main__":
    main()
