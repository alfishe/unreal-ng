#!/usr/bin/env python3
"""POC 035: the tables of the measurement reports from scripts/run_mac.sh or scripts/run_linux.sh results.

usage: summarize_matrix.py <results-dir>     (prints Markdown; also writes <results-dir>/summary.json)

CPU columns are % of one core over the phase. "Compositor" is
  macOS: WindowServer's CPU during the phase minus its CPU in the 3 s before the run started (the rest of the desktop),
         so it is the compositing cost the variant adds (ws_cpu_pct - ws_baseline_pct);
  Linux: the CPU of the run's own Xvfb (x_cpu_pct), which serves only this window.
"""
import glob
import json
import math
import os
import statistics
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "results"

NAMES = {
    "imgui": "Dear ImGui (OpenGL)",
    "rmlui-custom": "RmlUi, custom elements (OpenGL)",
    "rmlui-dom": "RmlUi, DOM (OpenGL)",
    "qtwidgets": "Qt Widgets, QPainter",
    "qtwidgets-raster": "Qt Widgets, own raster",
    "qtquick": "Qt Quick, QML delegates (RHI)",
    "qtquick-raster": "Qt Quick, own raster (RHI)",
    "qtquick-raster-opengl": "Qt Quick, own raster (OpenGL RHI)",
    "qtquick-sg": "Qt Quick, QSGTextNode (RHI)",
    "imgui-sw": "Dear ImGui (software GL)",
    "rmlui-custom-sw": "RmlUi, custom elements (software GL)",
    "qtquick-software": "Qt Quick, QML delegates (software)",
    "qtquick-raster-software": "Qt Quick, own raster (software)",
    "qtquick-sg-software": "Qt Quick, QSGTextNode (software)",
    "imgui-swgl": "Dear ImGui (software GL)",
    "qtquick-raster-gpu": "Qt Quick, own raster (RHI)",
}


def nan():
    return float("nan")


def load(path):
    with open(path) as f:
        d = json.load(f)
    d["by"] = {p["phase"]: p for p in d["phases"]}
    return d


def med(xs):
    xs = [x for x in xs if x is not None and x == x]
    return statistics.median(xs) if xs else nan()


def ws(d, p):
    if "x_cpu_pct" in p:
        return p["x_cpu_pct"]
    v = p.get("ws_cpu_pct")
    if v is None:
        return nan()
    b = d.get("ws_baseline_pct", 0.0)
    return v - (b if b == b else 0.0)


def phase_row(d, phase="active50"):
    a = d["by"].get(phase, {})
    return a


def summarize(reps):
    a = [phase_row(r) for r in reps]
    u = [phase_row(r, "unthrottled") for r in reps]
    i = [phase_row(r, "idle") for r in reps]
    proc = med([x.get("cpu_1s_mean") for x in a])
    wsv = med([ws(r, x) for r, x in zip(reps, a)])
    return {
        "reps": len(reps),
        "dpr": reps[0].get("device_pixel_ratio", 1),
        "backend": reps[0].get("backend", ""),
        "fps": med([x.get("fps") for x in a]),
        "frame_p50": med([x.get("frame_ms_p50") for x in a]),
        "frame_p99": med([x.get("frame_ms_p99") for x in a]),
        "cpu": proc,
        "cpu_worst": med([x.get("cpu_1s_max") for x in a]),
        "ws": wsv,
        "total": med([x.get("cpu_1s_mean", nan()) + (ws(r, x) if ws(r, x) == ws(r, x) else 0)
                      for r, x in zip(reps, a)]),
        "capacity": med([1000.0 / x["frame_ms_mean"] for x in u if x.get("frame_ms_mean", 0) > 0]),
        "idle": med([x.get("cpu_1s_mean", nan()) + (ws(r, x) if ws(r, x) == ws(r, x) else 0) for r, x in zip(reps, i)]),
        "footprint_mb": med([x.get("footprint_kb", x.get("private_kb", nan())) for x in a]) / 1024,
        "footprint_peak_mb": med([r.get("footprint_kb_peak", nan()) for r in reps]) / 1024,
        "rss_mb": med([x.get("rss_kb_end") for x in a]) / 1024,
        "spread": (min(x.get("frame_ms_p50", nan()) for x in a), max(x.get("frame_ms_p50", nan()) for x in a)),
        "load": [r.get("load_avg_start", "") for r in reps],
    }


def collect(sub):
    groups = {}
    for path in sorted(glob.glob(f"{ROOT}/{sub}/*_r[0-9].json")):
        key = os.path.basename(path).rsplit("_r", 1)[0].replace("_", "-")  # stage 2 files used underscores
        try:
            groups.setdefault(key, []).append(load(path))
        except (OSError, ValueError):
            pass
    return {k: summarize(v) for k, v in groups.items()}


def f(x, nd=1):
    return "—" if x is None or (isinstance(x, float) and math.isnan(x)) else f"{x:.{nd}f}"


def holds(fps):
    return "yes" if fps == fps and fps > 47 else (f(fps) + " fps")


HEAD = ("| Variant | Process CPU, % | Compositor, % | Total, % of a core | Worst second, % | Frame p50, ms | p99, ms | "
        "Holds 50 Hz | Headroom, frames/s | Footprint, MB | Idle, % |")
SEP = "|---|---|---|---|---|---|---|---|---|---|---|"


def row(name, r):
    return (f"| {name} | {f(r['cpu'])} | {f(r['ws'])} | {f(r['total'])} | {f(r['cpu_worst'])} | {f(r['frame_p50'], 2)} | "
            f"{f(r['frame_p99'], 2)} | {holds(r['fps'])} | {f(r['capacity'], 0)} | {f(r['footprint_mb'])} | {f(r['idle'], 2)} |")


gpu, cpu = collect("gpu"), collect("cpu")
# Widgets never use the GPU: the same runs belong to the "no GPU" table
for k in ("qtwidgets", "qtwidgets-raster"):
    if k in gpu:
        cpu.setdefault(k, gpu[k])
cpu_names = {"imgui": "imgui-sw", "rmlui-custom": "rmlui-custom-sw"}

out = []
dpr = next((r["dpr"] for r in list(gpu.values()) + list(cpu.values())), 1)
out.append(f"Device pixel ratio of the runs: {dpr} (window 1600 x 1000 pt = {int(1600 * dpr)} x {int(1000 * dpr)} px).\n")
for title, data, rename in (("With the GPU (Qt Quick: the platform's default RHI; SDL: OpenGL)", gpu, {}),
                            ("Without the GPU in the application (software GL, Qt raster, Quick software adaptation)", cpu,
                             cpu_names)):
    out.append(f"### {title}\n")
    out.append(HEAD)
    out.append(SEP)
    for key in sorted(data, key=lambda k: (data[k]["total"] if data[k]["total"] == data[k]["total"] else 1e9)):
        out.append(row(NAMES.get(rename.get(key, key), key), data[key]))
    out.append("")
    reported = [f"{rename.get(k, k)}: {data[k]['backend']}" for k in sorted(data) if data[k]["backend"]]
    if reported:
        out.append("Backends reported by the binaries: " + "; ".join(reported) + "\n")

# scale2 (Linux: --scale=2 without a GPU, one run each)
sc2 = {}
for path in sorted(glob.glob(f"{ROOT}/cpu/*_scale2.json")):
    sc2[os.path.basename(path)[:-len("_scale2.json")].replace("_", "-")] = summarize([load(path)])
if sc2:
    out.append("### UI scale x2 without the GPU (one run each)\n")
    out.append(HEAD)
    out.append(SEP)
    for key in sorted(sc2, key=lambda k: sc2[k]["total"]):
        out.append(row(NAMES.get(key, key), sc2[key]))
    out.append("")

# px1
px1 = {}
for path in sorted(glob.glob(f"{ROOT}/px1/*.json")):
    px1[os.path.basename(path)[:-5]] = summarize([load(path)])
if px1:
    out.append("### One framebuffer pixel per logical pixel (--scale=0.5 on the 2x display; 1600 x 1000 px like Linux)\n")
    out.append(HEAD)
    out.append(SEP)
    for key in sorted(px1, key=lambda k: px1[k]["total"]):
        out.append(row(NAMES.get(key, key), px1[key]))
    out.append("")

# switch
sw_rows = []
for path in sorted(glob.glob(f"{ROOT}/switch/*.json")):
    key, what = os.path.basename(path)[:-5].rsplit("_", 1)
    try:
        d = load(path)
    except (OSError, ValueError):
        continue
    p = d["by"].get("switch", {})
    sw_rows.append((key, what, p))
if sw_rows:
    out.append("### Run-time switching (font 12 <-> 16 px, scale x1 <-> x1.5, every 250 ms for 10 s)\n")
    out.append("| Variant | Switch | Switch frame p50, ms | max, ms | Next 3 frames p50, ms | Frames p99, ms | Holds 50 Hz |")
    out.append("|---|---|---|---|---|---|---|")
    for key, what, p in sw_rows:
        out.append(f"| {NAMES.get(key, key)} | {what} | {f(p.get('switch_ms_p50', nan()), 2)} | "
                   f"{f(p.get('switch_ms_max', nan()), 2)} | {f(p.get('settle_ms_p50', nan()), 2)} | "
                   f"{f(p.get('frame_ms_p99', nan()), 2)} | {holds(p.get('fps', nan()))} |")
    out.append("")

# views
views = {}
for path in sorted(glob.glob(f"{ROOT}/views/*.json")):
    key, view = os.path.basename(path)[:-5].rsplit("_", 1)
    key = key.replace("_", "-")
    try:
        d = load(path)
    except (OSError, ValueError):
        continue
    a = d["by"].get("active50", {})
    views.setdefault(key, {})[view] = (a.get("frame_ms_p50", nan()), a.get("cpu_1s_mean", nan()) + (ws(d, a) if ws(d, a) == ws(d, a) else 0))
if views:
    out.append("### One view alone (frame p50 ms / total % of a core)\n")
    out.append("| Variant | disasm | hex | images | timeline |")
    out.append("|---|---|---|---|---|")
    for key, v in views.items():
        cells = [f"{f(v[x][0], 2)} / {f(v[x][1])}" if x in v else "—" for x in ("disasm", "hex", "images", "timeline")]
        out.append(f"| {NAMES.get(key, key)} | " + " | ".join(cells) + " |")
    out.append("")

out.append("Spread of frame p50 between repetitions (and the load average at each start):\n")
for title, data in (("gpu", gpu), ("cpu", cpu)):
    for key, r in sorted(data.items()):
        load = f", load average at the starts: {' | '.join(r['load'])}" if any(r["load"]) else ""
        out.append(f"- {title} {key}: {f(r['spread'][0], 2)}–{f(r['spread'][1], 2)} ms, {r['reps']} runs{load}")

text = "\n".join(out) + "\n"
print(text)
with open(os.path.join(ROOT, "summary.json"), "w") as fp:
    json.dump({"gpu": gpu, "cpu": cpu, "px1": px1}, fp, indent=1)
