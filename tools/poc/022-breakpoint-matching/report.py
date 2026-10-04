#!/usr/bin/env python3
"""Markdown tables from an experiment's benchmark JSON (PoC 022).

    report.py "<experiment>/out/<stamp> - full run" [--traces game-dizzy,remapheavy] [--sets exec-cold-10,...]

For every set recipe: one row per matcher, one column per trace, the value = ns per access above the
unarmed path of that trace (the replay loop and the "any breakpoint of this kind?" gate), the minimum over
all repetitions and passes (the run least disturbed by other load on the shared machine); a cell is marked
with ~ when its repetitions spread by more than 15% (a noisy cell: read it with care).
"""
import json
import pathlib
import re
import sys
from collections import defaultdict


def load(folder):
    """[(name, benchmark)] of every repetition of every pass"""
    runs = []
    for f in sorted(pathlib.Path(folder).glob("pass*.json")):
        data = json.loads(f.read_text())
        runs += [(b["run_name"] if "run_name" in b else b["name"], b) for b in data["benchmarks"]
                 if b.get("run_type", "iteration") == "iteration"]
    return runs


def ns(b):
    return b["ns_per_access"] * 1e9 if b["ns_per_access"] < 1e-3 else b["ns_per_access"]


def main():
    folder = sys.argv[1]
    want_traces = want_sets = None
    for a in sys.argv[2:]:
        if a.startswith("--traces="):
            want_traces = a.split("=", 1)[1].split(",")
        if a.startswith("--sets="):
            want_sets = a.split("=", 1)[1].split(",")
    runs = load(folder)
    if not runs:
        sys.exit(f"no pass*.json in {folder}")
    # name = matcher/set/trace.trace/min_time:...
    cells = defaultdict(list)  # (matcher, set, trace) -> [ns per pass]
    for name, b in runs:
        parts = name.split("/")
        matcher, set_name, trace = parts[0], parts[1], parts[2].replace(".trace", "")
        cells[(matcher, set_name, trace)].append(ns(b))
    traces = sorted({k[2] for k in cells})
    if want_traces:
        traces = [t for t in traces if any(w in t for w in want_traces)]
    floor = {t: min(cells[("unarmed", "none", t)]) for t in traces if ("unarmed", "none", t) in cells}
    matchers = []
    for k in cells:
        if k[0] != "unarmed" and k[0] not in matchers:
            matchers.append(k[0])
    sets = []
    for k in cells:
        if k[0] != "unarmed" and k[1] not in sets:
            sets.append(k[1])

    def set_key(s):
        m = re.match(r"(.*)-(cold|warm)-(\d+)$", s)
        return (m.group(1), m.group(2), int(m.group(3))) if m else (s, "", 0)

    sets.sort(key=set_key)
    if want_sets:
        sets = [s for s in sets if s in want_sets]

    if floor:
        print("Unarmed path (ns per access, the floor subtracted below): " +
              ", ".join(f"{t} {floor[t]:.2f}" for t in traces if t in floor) + "\n")
    else:
        print("No unarmed floor in this run: the values are absolute ns per access\n")
    for s in sets:
        print(f"**{s}** (ns per access{' above unarmed' if floor else ', absolute'})\n")
        print("| matcher | " + " | ".join(traces) + " |")
        print("|:--|" + "--:|" * len(traces))
        for m in matchers:
            row = []
            for t in traces:
                v = cells.get((m, s, t))
                if not v:
                    row.append("-")
                    continue
                best = min(v)
                spread = (max(v) - best) / best if best else 0
                row.append(f"{best - floor.get(t, 0):.2f}{'~' if spread > 0.15 else ''}")
            print(f"| {m} | " + " | ".join(row) + " |")
        print()


if __name__ == "__main__":
    main()
