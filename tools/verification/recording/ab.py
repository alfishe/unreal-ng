#!/usr/bin/env python3
"""Window recorder A/B between two unreal-qt builds:
  ab.py UNREAL_QT_A UNREAL_QT_B OUTDIR ROUNDS PH[,PH..] TRANSFER [hardware]

Every run is a fresh application through live.py (one round, MODES=TRANSFER), A and B interleaved, the order swapped
every round. Prints per run the writer's median / p90 ms per sent picture and the path it took
(UNREAL_WINDOW_RECORDER_TIMING), the wall time of the 200 stepped frames, and per picture height the median of the
medians and how many rounds B won.

The two sides may be one binary: ENV_A / ENV_B ("NAME=value,NAME=value") are added to each side's environment, for a
switch built into a test build. Other live.py variables (SNA, SKIP, ENGINE, QT_QPA_PLATFORM) pass through.
"""
import os, re, statistics, subprocess, sys

BIN_A, BIN_B, OUT, ROUNDS, PHS, TRANSFER = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5].split(","), sys.argv[6]
HW = sys.argv[7:8] == ["hardware"]
here = os.path.dirname(os.path.abspath(__file__))
os.makedirs(OUT, exist_ok=True)


def extra(name):
    return dict(kv.split("=", 1) for kv in os.environ.get(name, "").split(",") if kv)


sides = {"a": (BIN_A, extra("ENV_A")), "b": (BIN_B, extra("ENV_B"))}
summary = {}
for ph in PHS:
    for r in range(ROUNDS):
        for side in (("a", "b") if r % 2 == 0 else ("b", "a")):
            binary, more = sides[side]
            env = dict(os.environ, MODES=TRANSFER, PH=ph, **more)
            args = [sys.executable, os.path.join(here, "live.py"), binary, OUT, "1"] + (["hardware"] if HW else [])
            res = subprocess.run(args, env=env, capture_output=True, text=True, timeout=900)
            line = next((l for l in res.stdout.splitlines() if l.startswith("r0")), None)
            m = line and re.search(r"([0-9.]+) s, writer n=(\d+) med +([0-9.?]+) p90 +([0-9.?]+) ms path=(\S+),", line)
            if not m or m.group(3) == "?":
                print(f"PH={ph} r{r + 1} {side} failed: {(line or res.stdout[-300:] + res.stderr[-300:]).strip()}", flush=True)
                continue
            summary.setdefault((ph, side), []).append(float(m.group(3)))
            print(f"PH={ph} r{r + 1} {side} n={m.group(2)} med {m.group(3)} p90 {m.group(4)} ms path={m.group(5)} "
                  f"wall {m.group(1)} s", flush=True)
for ph in PHS:
    a, b = summary.get((ph, "a"), []), summary.get((ph, "b"), [])
    if a and b:
        wins = sum(1 for x, y in zip(a, b) if y < x)
        print(f"PH={ph}: A median {statistics.median(a):.2f} ms, B {statistics.median(b):.2f} ms, "
              f"B faster in {wins}/{min(len(a), len(b))} rounds", flush=True)
