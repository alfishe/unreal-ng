#!/usr/bin/env python3
"""Tables for the font-size / UI-scale experiments (results/scaling)."""
import json
import os
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "results/scaling"
VARIANTS = [("imgui", "Dear ImGui"), ("rmlui_custom", "RmlUi, custom elements"), ("rmlui_dom", "RmlUi, DOM"),
            ("qtwidgets", "Qt Widgets"), ("qtquick", "Qt Quick")]
SCALES = ["1.0", "1.25", "1.5", "2.0"]
FONTS = [("10", "font10"), ("12", "scale1.0"), ("16", "font16"), ("20", "font20")]


def load(path):
    if not os.path.exists(path):
        return None
    with open(path) as f:
        d = json.load(f)
    d["by"] = {p["phase"]: p for p in d["phases"]}
    return d


def cpu_ms(p):
    return p["process_cpu_pct"] / 100.0 * p["wall_s"] / p["frames"] * 1000.0 if p and p["frames"] else float("nan")


def f(x, nd=2):
    return "—" if x is None or x != x else f"{x:.{nd}f}"


data = {}
out = []
out.append("### UI scale (framework's own high-DPI path): CPU per frame, ms / private memory, MB (no-op GL)\n")
out.append("| Variant | " + " | ".join(f"×{s}" for s in SCALES) + " | growth ×1 → ×2 |")
out.append("|---|" + "---|" * (len(SCALES) + 1))
for key, name in VARIANTS:
    cells, vals = [], []
    for s in SCALES:
        d = load(f"{ROOT}/cpu/{key}_scale{s}.json")
        a = d["by"]["active50"] if d else None
        v = cpu_ms(a)
        vals.append(v)
        cells.append(f"{f(v)} / {f(a['private_kb'] / 1024, 1) if a else '—'}")
        data.setdefault(key, {})[f"scale{s}"] = {"cpu_ms": v, "private_mb": a["private_kb"] / 1024 if a else None,
                                                 "cpu_pct": a.get("cpu_1s_mean") if a else None}
    growth = vals[-1] / vals[0] if vals[0] == vals[0] and vals[0] else float("nan")
    out.append(f"| {name} | " + " | ".join(cells) + f" | {f(growth)}× |")

out.append("\n### UI scale with the software rasterizer: RSS including texture memory, MB (llvmpipe)\n")
out.append("| Variant | ×1.0 | ×1.5 | ×2.0 |")
out.append("|---|---|---|---|")
for key, name in VARIANTS:
    cells = []
    for s in ["1.0", "1.5", "2.0"]:
        d = load(f"{ROOT}/raster/{key}_scale{s}.json")
        cells.append(f(d["by"]["active50"]["rss_kb_end"] / 1024, 1) if d else "—")
    out.append(f"| {name} | " + " | ".join(cells) + " |")

out.append("\n### Content font size: CPU per frame, ms (rows that fit: hex / disassembly)\n")
rows_fit = {"10": "32 / 60", "12": "32 / 60", "16": "24 / 45", "20": "18 / 35"}
out.append("| Variant | " + " | ".join(f"{px} px ({rows_fit[px]})" for px, _ in FONTS) + " |")
out.append("|---|" + "---|" * len(FONTS))
for key, name in VARIANTS:
    cells = []
    for px, tag in FONTS:
        d = load(f"{ROOT}/cpu/{key}_{tag}.json")
        v = cpu_ms(d["by"]["active50"]) if d else float("nan")
        cells.append(f(v))
        data[key][f"font{px}"] = v
    out.append(f"| {name} | " + " | ".join(cells) + " |")

out.append("\n### Switching at run time: the frame that applies the change, ms (median / worst of ~40), and the 3 frames after it\n")
out.append("| Variant | font 12 ↔ 16: switch frame | settle | scale ×1 ↔ ×1.5: switch frame | settle | steady frame (×1, 12 px) |")
out.append("|---|---|---|---|---|---|")
for key, name in VARIANTS:
    fo = load(f"{ROOT}/cpu/{key}_switch_font.json")
    sc = load(f"{ROOT}/cpu/{key}_switch_scale.json")
    base = load(f"{ROOT}/cpu/{key}_scale1.0.json")
    fs = fo["by"]["switch"] if fo else None
    ss = sc["by"]["switch"] if sc else None
    out.append(f"| {name} | {f(fs['switch_ms_p50']) + ' / ' + f(fs['switch_ms_max']) if fs else '—'} | "
               f"{f(fs['settle_ms_p50']) if fs else '—'} | "
               f"{f(ss['switch_ms_p50']) + ' / ' + f(ss['switch_ms_max']) if ss else 'not available'} | "
               f"{f(ss['settle_ms_p50']) if ss else '—'} | {f(base['by']['active50']['frame_ms_p50']) if base else '—'} |")
    data[key]["switch_font"] = fs and {k: fs[k] for k in ("switch_ms_p50", "switch_ms_max", "settle_ms_p50", "switch_n")}
    data[key]["switch_scale"] = ss and {k: ss[k] for k in ("switch_ms_p50", "switch_ms_max", "settle_ms_p50", "switch_n")}

text = "\n".join(out) + "\n"
open(f"{ROOT}/summary.md", "w").write(text)
json.dump(data, open(f"{ROOT}/summary.json", "w"), indent=1)
print(text)
