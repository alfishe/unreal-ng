#!/usr/bin/env python3
"""Run the SAA1099 co-simulation matrix: every corpus stream through our model and
every reference driver, compared per generator (compare.py). A difference passes
only when expect.txt lists it with the reason (the consensus table entry it follows
from); everything else fails.

    ./run-cosim.py                 all streams, all references
    ./run-cosim.py -s mixer -r saasound
    ./run-cosim.py -v              print every check, not only the summary

Needs ./fetch-refs.sh and ./build.sh first. Dumps land in out/ (git-ignored).
"""

import argparse
import fnmatch
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REFS = ["saasound", "mister", "mame"]
CHECKS = ["tone", "polarity", "noise", "noisetime", "env", "out"]


def load_expectations():
    rules = []
    with open(os.path.join(HERE, "expect.txt")) as f:
        for line in f:
            line = line.split(";", 1)[0].strip()
            if not line:
                continue
            scen, ref, check, reason = line.split(None, 3)
            rules.append((scen, ref, check, reason.strip()))
    return rules


def expected(rules, scen, ref, check):
    for s, r, c, reason in rules:
        if fnmatch.fnmatch(scen, s) and fnmatch.fnmatch(ref, r) and fnmatch.fnmatch(check, c):
            return reason
    return None


def run(cmd):
    subprocess.run(cmd, check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-s", "--stream", default="*")
    ap.add_argument("-r", "--ref", default="*")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    rules = load_expectations()
    out = os.path.join(HERE, "out")
    os.makedirs(out, exist_ok=True)
    bin_ = os.path.join(HERE, "bin")
    streams = sorted(glob.glob(os.path.join(HERE, "corpus", f"{args.stream}.saa")))
    refs = [r for r in REFS if fnmatch.fnmatch(r, args.ref)]
    failures = 0
    counts = {"ok": 0, "ok*": 0, "expected": 0, "FAIL": 0}
    used_rules = set()
    for path in streams:
        scen = os.path.splitext(os.path.basename(path))[0]
        ours = os.path.join(out, f"{scen}.ours.txt")
        ours_low = os.path.join(out, f"{scen}.ours-low.txt")
        run([os.path.join(bin_, "drv-ours"), path, ours])
        run([os.path.join(bin_, "drv-ours"), path, ours_low, "--tone-start-low"])
        for ref in refs:
            dump = os.path.join(out, f"{scen}.{ref}.txt")
            run([os.path.join(bin_, f"drv-{ref}"), path, dump])
            # MiSTer and MAME start the tone generators low: compare everything but
            # the polarity check against our run started low too
            base = ours if ref == "saasound" else ours_low
            line = []
            for check in CHECKS:
                if ref == "mame" and check == "out":
                    # one sample per 256 clocks: every output change sits next to an
                    # edge, so the windowed comparison would prove nothing
                    line.append("out:-")
                    continue
                mine = ours if check == "polarity" else base
                tol = "2" if ref == "mister" else ("8" if ref == "mame" else "0")
                r = subprocess.run(
                    [sys.executable, os.path.join(HERE, "compare.py"), mine, dump, "--ref", ref,
                     "--checks", check, "--out-tol", tol, "--stimulus", path],
                    capture_output=True, text=True,
                )
                text = r.stdout.strip()
                why = expected(rules, scen, ref, check)
                if r.returncode == 0:
                    status = "ok" if "compared up to" not in text else "ok*"
                elif why:
                    status = "expected"
                    used_rules.add(why)
                else:
                    status = "FAIL"
                    failures += 1
                counts[status] += 1
                if status == "FAIL" or args.verbose:
                    print(f"{scen:28s} {ref:9s} {text}" + (f"   [{why}]" if why and status != "ok" else ""))
                line.append(f"{check}:{status}")
            if not args.verbose:
                print(f"{scen:28s} {ref:9s} " + " ".join(line))
    print(
        f"\nchecks: {counts['ok']} ok, {counts['ok*']} ok up to a jitter cut, "
        f"{counts['expected']} expected differences, {counts['FAIL']} failed"
    )
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
