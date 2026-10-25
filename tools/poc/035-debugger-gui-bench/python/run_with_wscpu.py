#!/usr/bin/env python3
"""macOS: run one benchmark binary and attribute WindowServer's CPU to each phase of its run.

On macOS every window is composited by WindowServer: a frame the application presents (a CALayer backing store from
Qt Widgets / the Quick software adaptation, a CAMetalLayer / OpenGL surface) is also work for WindowServer, so the
full cost of a variant is its own CPU + the CPU WindowServer spends on it. WindowServer also composites everything
else on the screen; a run with no window of ours (the "baseline" sample written first) gives what the rest of the
desktop costs, and the summary subtracts it.

usage: run_with_wscpu.py <out.json> <binary> [args...]
The JSON written by the binary gets, per phase, "ws_cpu_pct" (WindowServer CPU, % of one core, over the phase's
wall window), plus "load_avg_start" and "ws_baseline_pct" (WindowServer over the 3 s before the binary started).
Environment variables (QT_QUICK_BACKEND, POC035_RHI, POC035_GL_SOFTWARE) pass through to the binary.
"""
import json
import os
import subprocess
import sys
import threading
import time

out, binary, *args = sys.argv[1:]


def ws_pid():
    r = subprocess.run(["pgrep", "-x", "WindowServer"], capture_output=True, text=True)
    pids = r.stdout.split()
    return int(pids[0]) if pids else None


def cpu_seconds(pid):
    """Cumulative CPU time of pid from ps (user + system), seconds; None if not readable"""
    r = subprocess.run(["ps", "-o", "time=", "-p", str(pid)], capture_output=True, text=True)
    t = r.stdout.strip()
    if not t:
        return None
    try:
        parts = t.replace("-", ":").split(":")  # [[dd-]hh:]mm:ss.ss
        sec = 0.0
        for p in parts:
            sec = sec * 60 + float(p)
        return sec
    except ValueError:
        return None


pid = ws_pid()
samples = []  # (epoch, cpu seconds)
stop = threading.Event()


def sampler():
    while not stop.is_set():
        c = cpu_seconds(pid) if pid else None
        if c is not None:
            samples.append((time.time(), c))
        time.sleep(0.25)


def load_avg():
    r = subprocess.run(["sysctl", "-n", "vm.loadavg"], capture_output=True, text=True)
    return r.stdout.strip().strip("{} ")


t = threading.Thread(target=sampler, daemon=True)
t.start()
time.sleep(3.0)                     # baseline: the desktop without our window
baseline_end = time.time()
load = load_avg()
rc = subprocess.call([binary, f"--out={out}", *args])
stop.set()
t.join()


def cpu_at(ts):
    if not samples:
        return float("nan")
    if ts <= samples[0][0]:
        return samples[0][1]
    for (t0, c0), (t1, c1) in zip(samples, samples[1:]):
        if t0 <= ts <= t1:
            return c0 + (c1 - c0) * (ts - t0) / max(1e-9, t1 - t0)
    return samples[-1][1]


try:
    with open(out) as f:
        d = json.load(f)
    first = samples[0][0] if samples else baseline_end
    d["ws_baseline_pct"] = (100.0 * (cpu_at(baseline_end) - cpu_at(first)) / (baseline_end - first)
                            if samples and baseline_end > first else float("nan"))
    d["ws_readable"] = bool(samples)
    d["load_avg_start"] = load
    for p in d["phases"]:
        if p.get("t1", 0) > p.get("t0", 0) and samples:
            p["ws_cpu_pct"] = 100.0 * (cpu_at(p["t1"]) - cpu_at(p["t0"])) / (p["t1"] - p["t0"])
    with open(out, "w") as f:
        json.dump(d, f, indent=1)
except (OSError, ValueError) as e:
    print(f"no result: {e}", file=sys.stderr)
    rc = rc or 1
sys.exit(rc)
