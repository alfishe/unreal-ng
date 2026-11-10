#!/usr/bin/env python3
"""Polls the Kempston mouse counters of a running emulator at ~100 Hz, unwraps the 8-bit counters and prints one line
per stroke (motion separated from the next by 300 ms without any): net travel, path length, biggest step, reversals.
Usage: mouse-strokes.py <emulator-id> <seconds> [port]   (the WebAPI on localhost, port 8090 by default; it runs on
the machine the emulator runs on, the Steam Deck for a pad or a touchscreen)"""
import json
import sys
import time
import urllib.request

emu, seconds = sys.argv[1], float(sys.argv[2])
port = int(sys.argv[3]) if len(sys.argv) > 3 else 8090
url = f"http://localhost:{port}/api/v1/emulator/{emu}/mouse/status"


def read():
    with urllib.request.urlopen(url, timeout=2) as r:
        d = json.load(r)["device"]
    return d["x"], d["y"]


def unwrap(delta):
    return (delta + 128) % 256 - 128


class Stroke:
    def __init__(self, t):
        self.t0 = t
        self.net = [0, 0]
        self.path = [0, 0]
        self.biggest = 0
        self.reversals = [0, 0]
        self.last_sign = [0, 0]

    def add(self, d):
        for a in (0, 1):
            if d[a] == 0:
                continue
            self.net[a] += d[a]
            self.path[a] += abs(d[a])
            self.biggest = max(self.biggest, abs(d[a]))
            sign = 1 if d[a] > 0 else -1
            if self.last_sign[a] and sign != self.last_sign[a]:
                self.reversals[a] += 1
            self.last_sign[a] = sign


prev = read()
start = time.time()
stroke = None
last_motion = 0.0
n = 0
while time.time() - start < seconds:
    time.sleep(0.008)
    now = time.time()
    cur = read()
    d = (unwrap(cur[0] - prev[0]), unwrap(cur[1] - prev[1]))
    prev = cur
    if d != (0, 0):
        if stroke is None:
            stroke = Stroke(now - start)
        stroke.add(d)
        last_motion = now
    elif stroke is not None and now - last_motion > 0.3:
        n += 1
        print(f"stroke {n}: at {stroke.t0:5.1f}s dur {last_motion - start - stroke.t0:4.1f}s net dx={stroke.net[0]:+5d} "
              f"dy={stroke.net[1]:+5d} path x={stroke.path[0]:4d} y={stroke.path[1]:4d} max step {stroke.biggest:3d} "
              f"reversals x={stroke.reversals[0]} y={stroke.reversals[1]}", flush=True)
        stroke = None
print("END", flush=True)
