#!/usr/bin/env python3
"""Run one benchmark binary on its own Xvfb and attribute the X server's CPU to each phase.

Without a GPU the X server does part of every frame (it receives the window contents: XPutImage / MIT-SHM for Qt's
raster backing store, the GLX/EGL software swap for llvmpipe), so a fair "no GPU" number is client CPU + X server CPU.

usage: run_with_xcpu.py <out.json> <env: noop|sw> <binary> [args...]
  noop: Mesa no-op driver (rasterization free) - the CPU cost on a machine with a GPU
  sw:   llvmpipe - a machine with no usable GPU
The JSON written by the binary gets, per phase, "x_cpu_pct" (X server CPU, % of one core, over the phase's wall window).
"""
import json
import os
import subprocess
import sys
import threading
import time

out, mode, binary, *args = sys.argv[1:]
tick = os.sysconf("SC_CLK_TCK")

display = None
for n in range(90, 200):
    if not os.path.exists(f"/tmp/.X11-unix/X{n}") and not os.path.exists(f"/tmp/.X{n}-lock"):
        display = n
        break
xvfb = subprocess.Popen(["Xvfb", f":{display}", "-screen", "0", "3200x2000x24", "-nolisten", "tcp"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(100):
    if os.path.exists(f"/tmp/.X11-unix/X{display}"):
        break
    time.sleep(0.05)

samples = []  # (epoch, x server cpu seconds)
stop = threading.Event()


def sampler():
    while not stop.is_set():
        try:
            with open(f"/proc/{xvfb.pid}/stat") as f:
                fields = f.read().rsplit(")", 1)[1].split()
            cpu = (int(fields[11]) + int(fields[12])) / tick
            samples.append((time.time(), cpu))
        except OSError:
            pass
        time.sleep(0.1)


t = threading.Thread(target=sampler, daemon=True)
t.start()

env = dict(os.environ, DISPLAY=f":{display}")
# POC035_ENV_PREFIX: a conda-style prefix holding Qt and Mesa (the 2026-10-23 runs: qt6-main 6.9.3 + mesalib 26.2.1),
# so every variant runs on one Mesa and one libstdc++
prefix = os.environ.get("POC035_ENV_PREFIX")
if prefix:
    env["LD_LIBRARY_PATH"] = os.path.join(prefix, "lib")
    env["__EGL_VENDOR_LIBRARY_DIRS"] = os.path.join(prefix, "share", "glvnd", "egl_vendor.d")
if mode == "noop":
    # POC035_NOOP_SHIM: libnoop_egl_shim.so built from cpp/noop_egl_shim.c
    env.update(GALLIUM_NOOP="1", SDL_VIDEO_FORCE_EGL="1", QT_XCB_GL_INTEGRATION="xcb_egl",
               LD_PRELOAD=os.environ["POC035_NOOP_SHIM"])
elif mode == "sw":
    env["LIBGL_ALWAYS_SOFTWARE"] = "1"
rc = subprocess.call([binary, f"--out={out}", *args], env=env)
stop.set()
t.join()
xvfb.terminate()
xvfb.wait()


def cpu_at(ts):
    """X server CPU seconds at time ts, linearly interpolated between samples"""
    if not samples:
        return 0.0
    if ts <= samples[0][0]:
        return samples[0][1]
    for (t0, c0), (t1, c1) in zip(samples, samples[1:]):
        if t0 <= ts <= t1:
            return c0 + (c1 - c0) * (ts - t0) / max(1e-9, t1 - t0)
    return samples[-1][1]


try:
    with open(out) as f:
        d = json.load(f)
    for p in d["phases"]:
        if p.get("t1", 0) > p.get("t0", 0):
            p["x_cpu_pct"] = 100.0 * (cpu_at(p["t1"]) - cpu_at(p["t0"])) / (p["t1"] - p["t0"])
    d["mode"] = mode
    with open(out, "w") as f:
        json.dump(d, f, indent=1)
except (OSError, ValueError) as e:
    print(f"no result: {e}", file=sys.stderr)
    rc = rc or 1
sys.exit(rc)
