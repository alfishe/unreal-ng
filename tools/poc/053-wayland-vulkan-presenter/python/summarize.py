#!/usr/bin/env python3
"""POC 053: summary of one or more wlvk CSV runs.

For each run: frames, screen times (wp_presentation) and present_wait returns, discarded frames, the error (screen
time minus the frame's target), present_wait minus the screen time, frames per refresh (a refresh that showed two
frames' worth: one was never seen), and the call-to-screen latency.

    summarize.py run1.csv [run2.csv ...] [--skip 50]
"""
import csv
import statistics
import sys


def pct(values, p):
    if not values:
        return float("nan")
    s = sorted(values)
    i = min(len(s) - 1, max(0, int(round(p / 100.0 * (len(s) - 1)))))
    return s[i]


def ms(v):
    return f"{v / 1e6:7.2f}"


def summarize(path, skip):
    rows = list(csv.DictReader(open(path)))[skip:]
    n = len(rows)
    presented = [r for r in rows if int(r["presented"]) > 0]
    discarded = sum(1 for r in rows if int(r["discarded"]))
    waited = [r for r in rows if int(r["waited"]) > 0]
    err = [int(r["presented"]) - int(r["target"]) for r in presented]
    lat = [int(r["presented"]) - int(r["call"]) for r in presented]
    wait_minus = [int(r["waited"]) - int(r["presented"]) for r in rows if int(r["waited"]) and int(r["presented"])]
    refresh = statistics.median([int(r["refresh"]) for r in presented if int(r["refresh"])] or [0])
    # Refresh slots: two frames on the same slot means the first was replaced; a gap of 2+ slots between consecutive
    # frames is a repeat (the cadence of 50 on 60 gives one repeat every 5 frames by itself)
    slots = []
    if presented and refresh:
        base = int(presented[0]["presented"])
        slots = [round((int(r["presented"]) - base) / refresh) for r in presented]
    same = sum(1 for a, b in zip(slots, slots[1:]) if b == a)
    gaps = [b - a for a, b in zip(slots, slots[1:])]
    repeats = sum(g - 1 for g in gaps if g > 1)
    flags = sorted({int(r["flags"]) for r in presented})
    print(f"{path}: {n} frames, screen time {len(presented)} ({100.0 * len(presented) / max(1, n):.1f} %), "
          f"present_wait {len(waited)}, discarded {discarded}, refresh {refresh / 1e6:.3f} ms, flags {flags}")
    if err:
        print(f"  error (screen - target) ms: p50 {ms(pct(err, 50))}  p1 {ms(pct(err, 1))}  p99 {ms(pct(err, 99))}  "
              f"max {ms(max(err))}  min {ms(min(err))}")
        print(f"  call -> screen ms:          p50 {ms(pct(lat, 50))}  p99 {ms(pct(lat, 99))}  max {ms(max(lat))}")
    if wait_minus:
        print(f"  present_wait - screen ms:   p50 {ms(pct(wait_minus, 50))}  p1 {ms(pct(wait_minus, 1))}  "
              f"p99 {ms(pct(wait_minus, 99))}")
    if err and refresh:
        # On a fixed refresh the best a timed present can do is the first refresh at or after the target:
        # 0 <= error < one refresh. Earlier is "early", one refresh or more is "late" (a refresh missed)
        early = sum(1 for e in err if e < -500_000)
        late = sum(1 for e in err if e >= refresh + 500_000)
        ontime = len(err) - early - late
        print(f"  first refresh after target: on time {ontime} ({100.0 * ontime / len(err):.1f} %), "
              f"early {early}, late {late}")
    if "blit_ns" in rows[0]:
        blit = [int(r["blit_ns"]) for r in rows if int(r["blit_ns"]) > 0]
        glc = [int(r["gl_cpu_ns"]) for r in rows if int(r["gl_cpu_ns"]) > 0]
        checks = [int(r["check"]) for r in rows if int(r["check"])]
        if blit:
            print(f"  GPU copy (timestamps) ms:   p50 {ms(pct(blit, 50))}  p99 {ms(pct(blit, 99))}  max {ms(max(blit))}")
        if glc:
            print(f"  GL draw + signal CPU ms:    p50 {ms(pct(glc, 50))}  p99 {ms(pct(glc, 99))}  max {ms(max(glc))}")
        if checks:
            print(f"  picture checks: {checks.count(1)} right, {checks.count(-1)} wrong")
    if slots:
        expected = (len(slots) - 1) * 60.0 / 50.0  # nominal: the refresh slots a 50 Hz stream spans on ~60 Hz
        print(f"  refresh slots: two frames on one slot {same}, repeated slots {repeats}, span {slots[-1]} "
              f"(50 on {1e9 / refresh:.2f} Hz: ~{expected:.0f})")


def main(argv):
    skip = 50
    paths = []
    it = iter(argv)
    for a in it:
        if a == "--skip":
            skip = int(next(it))
        else:
            paths.append(a)
    for p in paths:
        summarize(p, skip)


if __name__ == "__main__" and "--consistency" not in sys.argv:
    main(sys.argv[1:])


def consistency(path, skip=50):
    """The best constant delay c: the share of frames shown on the first refresh at or after target + c.
    Every frame off that refresh is a visible judder beyond the 50-on-60 cadence."""
    rows = [r for r in list(csv.DictReader(open(path)))[skip:] if int(r["presented"])]
    per = statistics.median(int(r["refresh"]) for r in rows)
    base = int(rows[0]["presented"])
    best = (0, 0)
    for c in range(0, 40_000_000, 250_000):
        hit = 0
        for r in rows:
            t = int(r["target"]) + c
            v = base + -(-(t - base) // per) * per
            hit += abs(int(r["presented"]) - v) < per / 2
        best = max(best, (hit, c))
    print(f"  consistency: best delay {best[1] / 1e6:.2f} ms, on that refresh {best[0]}/{len(rows)} "
          f"({100.0 * best[0] / len(rows):.1f} %), judder frames {len(rows) - best[0]}")


if __name__ == "__main__" and "--consistency" in sys.argv:
    for p in sys.argv[1:]:
        if p.endswith(".csv"):
            print(p)
            consistency(p)
