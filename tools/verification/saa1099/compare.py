#!/usr/bin/env python3
"""Compare SAA1099 event dumps: our model against one reference, per generator.

    ./compare.py <ours.txt> <ref.txt> --ref saasound|mame|mister [--checks ...]

Checks (dump format in drivers/cosim.h):
  tone      output edge times of each tone generator
  polarity  each tone generator's level after power-on
  noise     the noise output bit at each shift (MAME: the stream must be a phase of ours)
  noisetime the times of the noise shifts
  env       the sequence of envelope levels (enabled, left, right) and their times
  out       the summed output level over time, ignoring a short window around every
            event (references resolve simultaneous edges in a different order)

Time tolerances per reference (chip clocks):
  mister    0 for edges; noise / envelope 1 (a tone edge clocks them a posedge later)
  saasound  1 (12-bit fractional periods put an edge up to one clock late; it never
            accumulates because the accumulator keeps its remainder)
  mame      one sample = 256 clocks; MAME starts every tone generator at counter 0, so
            only the intervals between edges are compared (best of two alignments)

With --stimulus, a SAASound tone edge that disagrees right where a write to that
generator's tone / octave register met the edge (within the edge jitter, or between
a tone number and the octave written after it, where SAASound's lone-tone-number
rule applies) ends the comparison of every check at that clock: from there the two
models legitimately run different periods. The line reports the clock.

Prints one line per check; exit status 1 if any check differs.
"""

import argparse
import bisect
import sys

TIME_TOL = {"mister": 0, "saasound": 1, "mame": 256}
CLOCKED_TOL = {"mister": 1, "saasound": 2, "mame": 512}  # noise / envelope, driven by edges
OUT_WINDOW = {"mister": 2, "saasound": 2, "mame": 512}


def load(path):
    ev = {"T": {}, "N": {}, "E": {}, "O": []}
    with open(path) as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            k, c = p[0], int(p[1])
            if k == "T":
                ev["T"].setdefault(int(p[2]), []).append((c, int(p[3])))
            elif k == "N":
                ev["N"].setdefault(int(p[2]), []).append((c, int(p[3]), int(p[4])))
            elif k == "E":
                ev["E"].setdefault(int(p[2]), []).append((c, int(p[3]), int(p[4]), int(p[5])))
            elif k == "O":
                ev["O"].append((c, int(p[2]), int(p[3])))
    return ev


def match_times(ta, tb, tol, horizon):
    """First index where two edge-time lists disagree by more than tol (before horizon)."""
    ta = [t for t in ta if t < horizon]
    tb = [t for t in tb if t < horizon]
    for i in range(min(len(ta), len(tb))):
        if abs(ta[i] - tb[i]) > tol:
            return f"edge {i}: {ta[i]} vs {tb[i]}"
    if abs(len(ta) - len(tb)) > 0:
        # an edge right at the horizon can fall on either side
        short = min(len(ta), len(tb))
        longer = ta if len(ta) > len(tb) else tb
        if len(longer) - short > 1 or longer[short] < horizon - 2 * tol - 2:
            return f"{len(ta)} edges vs {len(tb)}"
    return None


def intervals_match(ta, tb, tol):
    ia = [y - x for x, y in zip(ta, ta[1:])]
    ib = [y - x for x, y in zip(tb, tb[1:])]
    best = None
    for shift in (0, 1):
        jb = ib[shift:]
        n = min(len(ia), len(jb)) - 1  # the last interval can straddle the stream end
        bad = next((i for i in range(n) if abs(ia[i] - jb[i]) > tol), None)
        if bad is None:
            return None
        best = best or f"interval {bad}: {ia[bad]} vs {jb[bad]}"
    return best


def check_tone(ours, ref, refname, end):
    res = []
    for g in range(6):
        ta = [c for c, _ in ours["T"].get(g, [])[1:]]
        tb = [c for c, _ in ref["T"].get(g, [])[1:]]
        if refname == "mame":
            r = intervals_match(ta, tb, TIME_TOL["mame"])
        else:
            r = match_times(ta, tb, TIME_TOL[refname], end - 4)
        if r:
            res.append(f"tone {g}: {r}")
    return res


def check_polarity(ours, ref):
    res = []
    for g in range(6):
        a, b = ours["T"].get(g, []), ref["T"].get(g, [])
        if a and b and a[0][1] != b[0][1]:
            res.append(f"tone {g}: level after power-on {a[0][1]} vs {b[0][1]}")
    return res


_MSEQ = None


def m_sequence():
    """Output bits of our noise LFSR (x^18 + x^11 + 1, Galois, seed all ones), one
    full period plus a wrap, as a string."""
    global _MSEQ
    if _MSEQ is None:
        r, bits = 0x3FFFF, []
        for _ in range((1 << 18) - 1 + 64):
            r = (r >> 1) ^ 0x20400 if r & 1 else r >> 1
            bits.append("1" if r & 1 else "0")
        _MSEQ = "".join(bits)
    return _MSEQ


def check_noise(ours, ref, refname):
    res = []
    for g in range(2):
        ba = [bit for _, bit, _ in ours["N"].get(g, [])[1:]]
        bb = [bit for _, bit, _ in ref["N"].get(g, [])[1:]]
        if refname == "mame":
            # MAME shifts a Fibonacci register: the same polynomial gives the same
            # m-sequence from another phase (possibly read backwards). Look its first
            # bits up in the whole 2^18 - 1 sequence of ours
            sb = "".join(map(str, bb[:64]))
            seq = m_sequence()
            if len(sb) >= 24 and sb not in seq and sb not in seq[::-1]:
                res.append(f"noise {g}: the reference bit stream is not a phase of our m-sequence")
            continue
        n = min(len(ba), len(bb))
        bad = next((i for i in range(n) if ba[i] != bb[i]), None)
        if bad is not None:
            res.append(f"noise {g}: bit {bad}: {ba[bad]} vs {bb[bad]}")
    return res


def check_noisetime(ours, ref, refname, end):
    res = []
    for g in range(2):
        ta = [c for c, _, _ in ours["N"].get(g, [])[1:]]
        tb = [c for c, _, _ in ref["N"].get(g, [])[1:]]
        if refname == "mame":
            r = intervals_match(ta, tb, CLOCKED_TOL["mame"])
        else:
            r = match_times(ta, tb, CLOCKED_TOL[refname], end - 4)
        if r:
            res.append(f"noise {g}: {r}")
    return res


def dedupe(seq):
    out = []
    for e in seq:
        if not out or out[-1][1:] != e[1:]:
            out.append(e)
    return out


def check_env(ours, ref, refname):
    res = []
    tol = CLOCKED_TOL[refname]
    for g in range(2):
        a = dedupe([e for e in ours["E"].get(g, []) if e[1]])
        b = dedupe([e for e in ref["E"].get(g, []) if e[1]])
        for i in range(max(len(a), len(b))):
            x = a[i] if i < len(a) else None
            y = b[i] if i < len(b) else None
            if x is None or y is None or x[1:] != y[1:] or abs(x[0] - y[0]) > tol:
                res.append(f"env {g}: change {i}: ours {x} vs ref {y}")
                break
    return res


def check_out(ours, ref, refname, value_tol):
    window = OUT_WINDOW[refname]
    events = sorted(
        [c for g in ours["T"].values() for c, _ in g]
        + [c for g in ref["T"].values() for c, _ in g]
        + [c for g in ours["N"].values() for c, *_ in g]
        + [c for g in ref["N"].values() for c, *_ in g]
        + [c for g in ours["E"].values() for c, *_ in g]
        + [c for g in ref["E"].values() for c, *_ in g]
    )

    def near_event(t):
        i = bisect.bisect_left(events, t - window)
        return i < len(events) and events[i] <= t + window

    a, b = ours["O"], ref["O"]
    times = sorted(set([c for c, *_ in a] + [c for c, *_ in b]))
    ia = ib = 0
    va = vb = (0, 0)
    for t in times:
        while ia < len(a) and a[ia][0] <= t:
            va = a[ia][1:]
            ia += 1
        while ib < len(b) and b[ib][0] <= t:
            vb = b[ib][1:]
            ib += 1
        if abs(va[0] - vb[0]) > value_tol or abs(va[1] - vb[1]) > value_tol:
            if not near_event(t):
                return [f"out at clock {t}: ours {va} vs ref {vb}"]
    return []


def load_writes(path):
    """(clock, register) of every data write in a stimulus."""
    writes, addr = [], 0
    with open(path) as f:
        for line in f:
            p = line.split(";", 1)[0].split()
            if len(p) < 3:
                continue
            v = int(p[2][1:], 16) if p[2].startswith("#") else int(p[2], 0)
            if p[1] == "A":
                addr = v & 0x1F
            elif p[1] == "D":
                writes.append((int(p[0]), addr))
    return writes


def jitter_cut(ours, ref, writes, tol):
    """Earliest clock where a tone edge differs because a write met the edge."""
    cut = None
    for g in range(6):
        ta = [c for c, _ in ours["T"].get(g, [])[1:]]
        tb = [c for c, _ in ref["T"].get(g, [])[1:]]
        regs = (0x08 + g, 0x10 + g // 2)
        for i in range(min(len(ta), len(tb))):
            if abs(ta[i] - tb[i]) <= tol:
                continue
            prev = ta[i - 1] if i else 0
            hit = any(r in regs and prev - 24 <= c <= prev + tol + 1 for c, r in writes)
            if hit:
                cut = prev - 24 if cut is None else min(cut, prev - 24)
            break
    return cut


def truncate(ev, cut):
    out = {"T": {}, "N": {}, "E": {}, "O": [e for e in ev["O"] if e[0] < cut]}
    for k in ("T", "N", "E"):
        for g, seq in ev[k].items():
            out[k][g] = [e for e in seq if e[0] < cut]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ours")
    ap.add_argument("ref")
    ap.add_argument("--ref", dest="refname", required=True, choices=sorted(TIME_TOL))
    ap.add_argument("--checks", default="tone,polarity,noise,noisetime,env,out")
    ap.add_argument("--out-tol", type=int, default=0, help="output level tolerance (units of our model)")
    ap.add_argument("--stimulus", help="the stream both dumps ran (enables the jitter cut for SAASound)")
    args = ap.parse_args()

    ours, ref = load(args.ours), load(args.ref)
    note = ""
    if args.stimulus and args.refname == "saasound":
        cut = jitter_cut(ours, ref, load_writes(args.stimulus), TIME_TOL["saasound"])
        if cut is not None:
            ours, ref = truncate(ours, cut), truncate(ref, cut)
            note = f" (compared up to clock {cut}: a tone write met an edge within the reference's jitter)"
    end = max(
        [c for c, *_ in ours["O"]]
        + [c for g in ours["T"].values() for c, _ in g]
        + [c for g in ours["N"].values() for c, *_ in g]
        + [c for g in ours["E"].values() for c, *_ in g]
        + [0]
    )
    results = {}
    for check in args.checks.split(","):
        if check == "tone":
            results[check] = check_tone(ours, ref, args.refname, end)
        elif check == "polarity":
            results[check] = check_polarity(ours, ref)
        elif check == "noise":
            results[check] = check_noise(ours, ref, args.refname)
        elif check == "noisetime":
            results[check] = check_noisetime(ours, ref, args.refname, end)
        elif check == "env":
            results[check] = check_env(ours, ref, args.refname)
        elif check == "out":
            results[check] = check_out(ours, ref, args.refname, args.out_tol)
    bad = False
    for k, v in results.items():
        print(f"{k} {'ok' + note if not v else 'DIFF ' + v[0]}")
        bad |= bool(v)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
