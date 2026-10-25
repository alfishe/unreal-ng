#!/usr/bin/env python3
"""Aggregate results/cpu and results/raster JSON files into the tables used by the report (Markdown + CSV)."""
import csv
import glob
import json
import os
import statistics
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "results"
VARIANTS = [("imgui", "Dear ImGui"), ("rmlui_custom", "RmlUi, custom elements"), ("rmlui_dom", "RmlUi, DOM"),
            ("qtwidgets", "Qt Widgets"), ("qtquick", "Qt Quick")]
VIEWS = ["disasm", "hex", "images", "timeline"]


def load(path):
    with open(path) as f:
        d = json.load(f)
    d["by"] = {p["phase"]: p for p in d["phases"]}
    return d


def med(values):
    return statistics.median(values) if values else float("nan")


rows = []
for key, name in VARIANTS:
    reps = [load(p) for p in sorted(glob.glob(f"{ROOT}/cpu/{key}_full_r*.json"))]
    if not reps:
        continue
    empty = load(f"{ROOT}/cpu/{key}_empty.json")
    a = [r["by"]["active50"] for r in reps]
    u = [r["by"]["unthrottled"] for r in reps]
    i = [r["by"]["idle"] for r in reps]
    row = {
        "key": key,
        "name": name,
        "reps": len(reps),
        "fps50": med([x["fps"] for x in a]),
        "frame_p50": med([x["frame_ms_p50"] for x in a]),
        "frame_p95": med([x["frame_ms_p95"] for x in a]),
        "frame_p99": med([x["frame_ms_p99"] for x in a]),
        "frame_spread": (min(x["frame_ms_p50"] for x in a), max(x["frame_ms_p50"] for x in a)),
        "build_p50": med([x["build_ms_p50"] for x in a]),
        "render_p50": med([x["render_ms_p50"] for x in a]),
        "main_cpu50": med([x["main_cpu_pct"] for x in a]),
        "proc_cpu50": med([x["process_cpu_pct"] for x in a]),
        "capacity_fps": med([1000.0 / x["frame_ms_mean"] for x in u if x["frame_ms_mean"] > 0]),
        "idle_cpu": med([x["process_cpu_pct"] for x in i]),
        "rss_active_mb": med([x["rss_kb_end"] for x in a]) / 1024,
        "rss_peak_mb": med([r["rss_kb_peak"] for r in reps]) / 1024,
        "rss_empty_mb": empty["by"]["active50"]["rss_kb_end"] / 1024,
        "pss_mb": med([x.get("pss_kb", 0) for x in a]) / 1024,
        "private_mb": med([x.get("private_kb", 0) for x in a]) / 1024,
        "anon_mb": med([x.get("anon_kb", 0) for x in a]) / 1024,
        "file_mb": med([x.get("rss_file_kb", 0) for x in a]) / 1024,
        "vmdata_mb": med([x.get("vm_data_kb", 0) for x in a]) / 1024,
        "private_empty_mb": empty["by"]["active50"].get("private_kb", 0) / 1024,
        "cpu_1s_mean": med([x.get("cpu_1s_mean", 0) for x in a]),
        "cpu_ms_frame": med([x["process_cpu_pct"] / 100.0 * x["wall_s"] / x["frames"] * 1000.0 for x in a if x["frames"]]),
        "cpu_1s_max": med([x.get("cpu_1s_max", 0) for x in a]),
        "empty_frame_p50": empty["by"]["active50"]["frame_ms_p50"],
        "empty_cpu50": empty["by"]["active50"]["process_cpu_pct"],
        "notes": reps[0].get("notes", ""),
    }
    for view in VIEWS:
        p = f"{ROOT}/cpu/{key}_{view}.json"
        if os.path.exists(p):
            v = load(p)["by"]["active50"]
            row[f"view_{view}_p50"] = v["frame_ms_p50"]
            row[f"view_{view}_cpu"] = v["process_cpu_pct"]
    rp = f"{ROOT}/raster/{key}_full.json"
    if os.path.exists(rp):
        r = load(rp)["by"]
        row["raster_p50"] = r["active50"]["frame_ms_p50"]
        row["raster_fps50"] = r["active50"]["fps"]
        row["raster_rss_mb"] = r["active50"]["rss_kb_end"] / 1024
    rows.append(row)


def f(x, nd=2):
    return "—" if x is None or x != x else f"{x:.{nd}f}"


out = []
out.append("### Main: CPU cost per frame at 50 Hz (no-op GL driver, median of %s runs)\n" % (rows[0]["reps"] if rows else 0))
out.append("| Variant | CPU per frame, ms | frame p50, ms | p95 | p99 | build p50 | render+present p50 | CPU at 50 Hz, % (mean of 1 s samples / worst second) | capacity, frames/s | idle CPU, % |")
out.append("|---|---|---|---|---|---|---|---|---|---|")
for r in rows:
    out.append(f"| {r['name']} | {f(r['cpu_ms_frame'])} | {f(r['frame_p50'])} | {f(r['frame_p95'])} | {f(r['frame_p99'])} | "
               f"{f(r['build_p50']) if r['build_p50'] > 0 else '—'} | {f(r['render_p50']) if r['render_p50'] > 0 else '—'} | "
               f"{f(r['cpu_1s_mean'], 1)} / {f(r['cpu_1s_max'], 1)} | {f(r['capacity_fps'], 0)} | {f(r['idle_cpu'], 3)} |")
out.append("\n### Per view (each view alone, frame p50 ms / CPU %)\n")
out.append("| Variant | " + " | ".join(VIEWS) + " | empty window |")
out.append("|---|" + "---|" * (len(VIEWS) + 1))
for r in rows:
    cells = [f"{f(r.get(f'view_{v}_p50'))} / {f(r.get(f'view_{v}_cpu'), 1)}" for v in VIEWS]
    out.append(f"| {r['name']} | " + " | ".join(cells) + f" | {f(r['empty_frame_p50'])} / {f(r['empty_cpu50'], 1)} |")
out.append("\n### Memory (RSS, MB; the workload itself is included in every number)\n")
out.append("| Variant | RSS empty window | RSS full | RSS peak | PSS | private (own) | anonymous (heap) | file-backed (libs) | private: full − empty |")
out.append("|---|---|---|---|---|---|---|---|---|")
for r in rows:
    out.append(f"| {r['name']} | {f(r['rss_empty_mb'], 1)} | {f(r['rss_active_mb'], 1)} | {f(r['rss_peak_mb'], 1)} | "
               f"{f(r['pss_mb'], 1)} | {f(r['private_mb'], 1)} | {f(r['anon_mb'], 1)} | {f(r['file_mb'], 1)} | "
               f"{f(r['private_mb'] - r['private_empty_mb'], 1)} |")
out.append("\n### Secondary: llvmpipe software rasterizer (GPU-load proxy, not a GPU measurement)\n")
out.append("| Variant | frame p50, ms | fps reached at 50 Hz target | RSS, MB |")
out.append("|---|---|---|---|")
for r in rows:
    out.append(f"| {r['name']} | {f(r.get('raster_p50'))} | {f(r.get('raster_fps50'), 1)} | {f(r.get('raster_rss_mb'), 1)} |")
out.append("\n### Run-to-run spread of frame p50 (min–max over repetitions)\n")
for r in rows:
    out.append(f"- {r['name']}: {f(r['frame_spread'][0])}–{f(r['frame_spread'][1])} ms; notes: {r['notes']}")

text = "\n".join(out) + "\n"
open(f"{ROOT}/summary.md", "w").write(text)
with open(f"{ROOT}/summary.csv", "w", newline="") as fcsv:
    keys = [k for k in rows[0].keys() if k not in ("frame_spread",)] if rows else []
    w = csv.DictWriter(fcsv, fieldnames=keys, extrasaction="ignore")
    w.writeheader()
    for r in rows:
        w.writerow(r)
json.dump(rows, open(f"{ROOT}/summary.json", "w"), indent=1, default=str)
print(text)
