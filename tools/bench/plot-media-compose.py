#!/usr/bin/env python3
"""Charts C1-C8 and the NFR table of the multi-source media plan from a Google Benchmark JSON run.

Reads the output of core/benchmarks/emulator/io/compose_benchmark.cpp (run with --benchmark_repetitions and
--benchmark_report_aggregates_only, so medians are in the file) and writes, into --out:
  c1-build.svg ... c8-session.svg (and .png), results.md (the NFR table and every number behind the charts).

Design: docs/inprogress/2026-10-05-media-multisource/test-and-benchmark-plan.md §5.4, §5.5.

Usage:
  python3 tools/bench/plot-media-compose.py scratch/bench/media-compose/run.json --out scratch/bench/media-compose/ \
      [--ab-base 'scratch/bench/ab/base-*.json' --ab-cur 'scratch/bench/ab/cur-*.json']

--ab-base / --ab-cur: interleaved runs of the same non-composite benchmarks (chdimage, zcontrollerspi) on the commit
before the media work and on the measured one, for NFR-P7.
"""

import argparse
import glob
import json
import statistics
import os
import re
import subprocess
import sys

try:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:  # pragma: no cover - a hint, not a feature
    sys.exit("matplotlib is needed: pip install matplotlib")

# The reference palette (light chart surface): categorical slots in fixed order, text and grid inks
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK2 = "#52514e"
MUTED = "#898781"
GRID = "#e4e3df"

MODES = ["raw", "hff", "c1f", "c8f", "cfat16", "graft", "part2", "isot"]
FLATTEN = ["img", "vhd", "chd", "compact", "raw-img"]


def load(path):
    with open(path) as f:
        run = json.load(f)
    rows = {}
    for b in run["benchmarks"]:
        if b.get("run_type") == "aggregate" and b.get("aggregate_name") != "median":
            continue
        name = re.sub(r"/iterations:\d+", "", b.get("run_name", b["name"]))
        parts = name.split("/")
        family, args = parts[0], tuple(int(p) for p in parts[1:] if re.fullmatch(r"\d+", p))
        rows[(family, args)] = b
    return run.get("context", {}), rows


def ns(b):
    """Time per iteration in nanoseconds"""
    scale = {"ns": 1, "us": 1e3, "ms": 1e6, "s": 1e9}[b.get("time_unit", "ns")]
    return b["real_time"] * scale


def ms(b):
    return ns(b) / 1e6


def mbps(b):
    return b.get("bytes_per_second", 0) / (1024 * 1024)


def style(ax, title, xlabel, ylabel):
    ax.set_facecolor(SURFACE)
    ax.set_title(title, loc="left", color=INK, fontsize=12, fontweight="bold")
    ax.set_xlabel(xlabel, color=INK2)
    ax.set_ylabel(ylabel, color=INK2)
    ax.tick_params(colors=MUTED, labelcolor=INK2)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(MUTED)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def figure():
    fig, ax = plt.subplots(figsize=(8, 4.6), dpi=110)
    fig.patch.set_facecolor(SURFACE)
    return fig, ax


def save(fig, out, name, footer):
    fig.text(0.01, 0.01, footer, color=MUTED, fontsize=7)
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    for ext in ("svg", "png"):
        fig.savefig(os.path.join(out, f"{name}.{ext}"), facecolor=SURFACE)
    plt.close(fig)


def number(v):
    return f"{v:,.0f}" if v >= 100 else f"{v:.3g}"


def lines(ax, series, labels, logx=False, logy=False, label_ends=True):
    """series: list of [(x, y), ...]; fixed colour per series, 2 px lines, 8 px markers, the last points labelled
    (moved apart so they never overlap)"""
    ends = []
    for i, (points, label) in enumerate(zip(series, labels)):
        if not points:
            continue
        xs, ys = zip(*points)
        ax.plot(xs, ys, color=SERIES[i % 4], linewidth=2, marker="o", markersize=6, markeredgecolor=SURFACE,
                markeredgewidth=1.5, label=label)
        ends.append((xs[-1], ys[-1]))
    if logx:
        ax.set_xscale("log")
    if logy:
        ax.set_yscale("log")
    if len([s for s in series if s]) >= 2:
        ax.legend(frameon=False, labelcolor=INK2, fontsize=9)
    if label_ends and len(series) <= 4 and ends:
        ax.autoscale_view()
        to_px = ax.transData
        placed = sorted(((to_px.transform(e)[1], e) for e in ends), key=lambda p: p[0])
        last = None
        for py, (x, y) in placed:
            shift = 0 if last is None or py - last >= 12 else 12 - (py - last)
            last = py + shift
            ax.annotate(number(y), (x, y), textcoords="offset points", xytext=(6, shift * 72 / ax.figure.dpi),
                        va="center", color=INK2, fontsize=8)


def bars(ax, groups, series, labels):
    """groups: category names; series: list of value lists (one per series), grouped side by side"""
    n = len(series)
    width = 0.8 / n
    for i, (values, label) in enumerate(zip(series, labels)):
        xs = [g + (i - (n - 1) / 2) * width for g in range(len(groups))]
        ax.bar(xs, values, width=width * 0.92, color=SERIES[i % 4], label=label, edgecolor=SURFACE, linewidth=1)
    ax.set_xticks(range(len(groups)))
    ax.set_xticklabels(groups)
    top = max(max(v) for v in series)
    ax.set_ylim(0, top * 1.28)  # room for the legend above the tallest bar
    if n >= 2:
        ax.legend(frameon=False, labelcolor=INK2, fontsize=9, ncol=2, loc="upper left")


def get(rows, family, *args):
    return rows.get((family, tuple(args)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run", help="Google Benchmark JSON (medians)")
    parser.add_argument("--out", required=True, help="folder for the charts and results.md")
    parser.add_argument("--commit", help="the measured commit (default: git rev-parse --short HEAD)")
    parser.add_argument("--ab-base", help="glob of JSON runs before the media work (NFR-P7)")
    parser.add_argument("--ab-cur", help="glob of JSON runs of the measured commit (NFR-P7)")
    a = parser.parse_args()
    os.makedirs(a.out, exist_ok=True)
    context, rows = load(a.run)
    commit = a.commit
    if not commit:
        try:
            commit = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], text=True).strip()
        except (OSError, subprocess.CalledProcessError):
            commit = "?"
    footer = (f"commit {commit} | {context.get('num_cpus', '?')} CPUs @ {context.get('mhz_per_cpu', '?')} MHz | "
              f"{context.get('date', '')[:10]} | medians")
    table = []  # (section, rows of markdown)

    # C1: build time against entries, per layer count
    entries = [1000, 10000, 100000]
    layer_counts = [1, 4, 16, 64]
    fig, ax = figure()
    series = [[(e, ms(get(rows, "ComposeScaleBuild", e, l))) for e in entries if get(rows, "ComposeScaleBuild", e, l)] for l in layer_counts]
    lines(ax, series, [f"{l} layer{'s' if l > 1 else ''}" for l in layer_counts], logx=True, logy=True)
    style(ax, "C1  Build time against entries", "entries (files)", "build time, ms")
    save(fig, a.out, "c1-build", footer)
    c1 = ["| entries | " + " | ".join(f"{l} layer{'s' if l > 1 else ''}, ms" for l in layer_counts) + " |", "|---|" + "---|" * len(layer_counts)]
    for e in entries:
        c1.append(f"| {e:,} | " + " | ".join(f"{ms(get(rows, 'ComposeScaleBuild', e, l)):.1f}" if get(rows, "ComposeScaleBuild", e, l) else "-"
                                         for l in layer_counts) + " |")
    table.append(("C1 build time", c1))

    # C2: read cost against layers
    layers = [1, 2, 4, 8, 16, 32, 64]
    kinds = ["sequential", "random", "metadata"]
    fig, ax = figure()
    series = [[(l, ns(get(rows, "ComposeLayersRead", l, k))) for l in layers if get(rows, "ComposeLayersRead", l, k)] for k in range(3)]
    lines(ax, series, kinds, logx=True)
    ax.set_xticks(layers)
    ax.set_xticklabels([str(l) for l in layers])
    ax.set_ylim(bottom=0)
    style(ax, "C2  A sector read against the layer count", "folder layers", "ns per sector")
    save(fig, a.out, "c2-layers", footer)
    c2 = ["| layers | " + " | ".join(f"{k}, ns" for k in kinds) + " |", "|---|---|---|---|"]
    for l in layers:
        c2.append(f"| {l} | " + " | ".join(f"{ns(get(rows, 'ComposeLayersRead', l, k)):.0f}" if get(rows, "ComposeLayersRead", l, k) else "-"
                                       for k in range(3)) + " |")
    table.append(("C2 read against layers", c2))

    # C3: fragmentation of a source file
    extents = [1, 4, 16, 64, 256, 1024, 4096]
    fig, ax = figure()
    series = [[(e, ns(get(rows, "ComposeFragmented", e, k))) for e in extents if get(rows, "ComposeFragmented", e, k)] for k in range(2)]
    lines(ax, series, ["sequential", "random"], logx=True)
    ax.set_ylim(bottom=0)
    style(ax, "C3  A sector read against the extents of its source file", "extents per file", "ns per sector")
    save(fig, a.out, "c3-fragmentation", footer)
    c3 = ["| extents | sequential, ns | random, ns |", "|---|---|---|"]
    for e in extents:
        c3.append(f"| {e} | " + " | ".join(f"{ns(get(rows, 'ComposeFragmented', e, k)):.0f}" if get(rows, "ComposeFragmented", e, k) else "-"
                                       for k in range(2)) + " |")
    table.append(("C3 fragmentation", c3))

    # C4: memory held by a built volume
    fig, ax = figure()
    rebuild = [(e, get(rows, "ComposeScaleBuild", e, 1)["heldBytes"] / 2**20) for e in entries if get(rows, "ComposeScaleBuild", e, 1)]
    iso = [(e, get(rows, "ComposeScaleBuildIso", e)["heldBytes"] / 2**20) for e in entries if get(rows, "ComposeScaleBuildIso", e)]
    graft = [(e, get(rows, "ComposeGraftVsRebuild", e, 0)["heldBytes"] / 2**20) for e in entries if get(rows, "ComposeGraftVsRebuild", e, 0)]
    floor = 0.05
    lines(ax, [[(x, max(y, floor)) for x, y in s] for s in (rebuild, iso, graft)],
          ["FAT rebuild", "ISO 9660", "graft (the base is not held; under 0.05 MiB drawn at 0.05)"], logx=True, logy=True)
    ax.legend(frameon=False, labelcolor=INK2, fontsize=9, loc="upper left", bbox_to_anchor=(0, 0.92))
    ax.axhline(32, color=MUTED, linewidth=1, linestyle="--")
    ax.annotate("NFR-M2: 32 MiB at 100 K", (1000, 32), textcoords="offset points", xytext=(0, 4), color=MUTED, fontsize=8)
    style(ax, "C4  Memory a built volume holds", "entries (files)", "heap held, MiB (log)")
    save(fig, a.out, "c4-memory", footer)
    c4 = ["| entries | rebuild, MiB | ISO, MiB | graft, MiB |", "|---|---|---|---|"]
    for e in entries:
        vals = []
        for fam, args in (("ComposeScaleBuild", (e, 1)), ("ComposeScaleBuildIso", (e,)), ("ComposeGraftVsRebuild", (e, 0))):
            b = get(rows, fam, *args)
            vals.append(f"{b['heldBytes'] / 2**20:.1f}" if b else "-")
        c4.append(f"| {e:,} | " + " | ".join(vals) + " |")
    table.append(("C4 memory", c4))

    # C5: modes, sequential and random, without and with a session over them
    fig, ax = figure()
    series, labels = [], []
    for kind, kname in ((0, "sequential"), (1, "random")):
        for sw, swname in ((0, ""), (10000, ", 10 K sectors changed")):
            series.append([mbps(get(rows, "ComposeMode", m, kind, sw)) if get(rows, "ComposeMode", m, kind, sw) else 0 for m in range(len(MODES))])
            labels.append(kname + swname)
    bars(ax, MODES, series, labels)
    style(ax, "C5  The same files as every kind of medium", "medium", "MiB/s (512-byte sectors)")
    save(fig, a.out, "c5-modes", footer)
    c5 = ["| mode | seq, ns | random, ns | metadata, ns | seq + session, ns | random + session, ns |", "|---|---|---|---|---|---|"]
    for m, name in enumerate(MODES):
        vals = []
        for kind, sw in ((0, 0), (1, 0), (2, 0), (0, 10000), (1, 10000)):
            b = get(rows, "ComposeMode", m, kind, sw)
            vals.append(f"{ns(b):.0f}" if b else "-")
        c5.append(f"| {name} | " + " | ".join(vals) + " |")
    table.append(("C5 modes", c5))

    # C6: graft against rebuild
    fig, ax = figure()
    series = [[(e, ms(get(rows, "ComposeGraftVsRebuild", e, k))) for e in entries if get(rows, "ComposeGraftVsRebuild", e, k)] for k in range(2)]
    lines(ax, series, ["graft", "rebuild"], logx=True, logy=True)
    style(ax, "C6  Graft against rebuild as the base grows", "base entries (100 upper files)", "build time, ms")
    save(fig, a.out, "c6-graft", footer)
    c6 = ["| base entries | graft, ms | rebuild, ms |", "|---|---|---|"]
    for e in entries:
        c6.append(f"| {e:,} | " + " | ".join(f"{ms(get(rows, 'ComposeGraftVsRebuild', e, k)):.2f}" if get(rows, "ComposeGraftVsRebuild", e, k) else "-"
                                         for k in range(2)) + " |")
    table.append(("C6 graft against rebuild", c6))

    # C7: attribution, and flatten (two charts: one measure each)
    changed = [10, 100, 1000, 10000, 100000]
    fig, ax = figure()
    series = [[(c, ms(get(rows, "ComposeAttribute", e, c))) for c in changed if get(rows, "ComposeAttribute", e, c)] for e in (10000, 100000)]
    lines(ax, series, ["10 K-entry volume", "100 K-entry volume"], logx=True, logy=True)
    ax.axhline(500, color=MUTED, linewidth=1, linestyle="--")
    ax.annotate("NFR-P9: 500 ms", (10, 500), textcoords="offset points", xytext=(0, 4), color=MUTED, fontsize=8)
    style(ax, "C7a  media changes: attributing the guest's writes", "changed data sectors", "ms")
    save(fig, a.out, "c7a-attribution", footer)
    fig, ax = figure()
    bars(ax, FLATTEN, [[mbps(get(rows, "ComposeFlatten", f)) if get(rows, "ComposeFlatten", f) else 0 for f in range(len(FLATTEN))]], ["MiB/s"])
    style(ax, "C7b  S1 flatten of a composite, by output format", "format (raw-img: an image file written again)", "MiB/s of the medium")
    save(fig, a.out, "c7b-flatten", footer)
    c7 = ["| changed sectors | 10 K entries, ms | 100 K entries, ms |", "|---|---|---|"]
    for c in changed:
        c7.append(f"| {c:,} | " + " | ".join(f"{ms(get(rows, 'ComposeAttribute', e, c)):.1f}" if get(rows, "ComposeAttribute", e, c) else "-"
                                         for e in (10000, 100000)) + " |")
    c7 += ["", "| format | MiB/s | ms per flatten |", "|---|---|---|"]
    for f, name in enumerate(FLATTEN):
        b = get(rows, "ComposeFlatten", f)
        c7.append(f"| {name} | {mbps(b):.0f} | {ms(b):.0f} |" if b else f"| {name} | - | - |")
    table.append(("C7 attribution and flatten", c7))

    # C8: the change layer
    counts = [1000, 10000, 100000]
    fig, ax = figure()
    hit = [(c, ns(get(rows, "ComposeSessionRead", c, 1))) for c in counts if get(rows, "ComposeSessionRead", c, 1)]
    miss = [(c, ns(get(rows, "ComposeSessionRead", c, 0))) for c in counts if get(rows, "ComposeSessionRead", c, 0)]
    lines(ax, [hit, miss], ["changed sector (hit)", "unchanged sector (miss)"], logx=True)
    ax.set_ylim(bottom=0)
    style(ax, "C8  A read through the change layer", "changed sectors in the session", "ns per sector")
    save(fig, a.out, "c8-session", footer)
    c8 = ["| changed sectors | hit, ns | miss, ns |", "|---|---|---|"]
    base = get(rows, "ComposeSessionRead", 0, 0)
    if base:
        c8.append(f"| 0 | - | {ns(base):.0f} |")
    for c in counts:
        c8.append(f"| {c:,} | " + " | ".join(f"{ns(get(rows, 'ComposeSessionRead', c, k)):.0f}" if get(rows, "ComposeSessionRead", c, k) else "-"
                                         for k in (1, 0)) + " |")
    table.append(("C8 change layer", c8))

    # NFR-P7: A/B of non-composite media, medians of the runs' medians
    p7 = "not measured (no --ab-base / --ab-cur)"
    if a.ab_base and a.ab_cur:
        def runs(pattern):
            out = {}
            for path in sorted(glob.glob(pattern)):
                with open(path) as f:
                    for b in json.load(f)["benchmarks"]:
                        if b.get("aggregate_name") == "median":
                            out.setdefault(b["run_name"], []).append(b["real_time"])
            return out
        before, after = runs(a.ab_base), runs(a.ab_cur)
        ab = ["| benchmark | before, ns | after, ns | change | run-to-run spread |", "|---|---|---|---|---|"]
        worst = 0.0
        for name in before:
            if name not in after:
                continue
            x, y = statistics.median(before[name]), statistics.median(after[name])
            spread = max(max(before[name]) / min(before[name]), max(after[name]) / min(after[name])) - 1
            change = y / x - 1
            worst = max(worst, change - spread)
            ab.append(f"| `{name}` | {x:.0f} | {y:.0f} | {change * 100:+.1f} % | ±{spread * 100:.1f} % |")
        table.append(("NFR-P7 A/B: non-composite media before and after the media work", ab))
        p7 = ("inside the noise band" if worst <= 0 else f"{worst * 100:.1f} % over the noise band") + \
             f" on {len(ab) - 2} benchmarks (RawImage, CHD, Z-Controller SD)"

    # The NFR table (§5.5)
    def ratio(x, y):
        return x / y if x and y else float("nan")

    def mode_ns(m, k, sw=0):
        b = get(rows, "ComposeMode", m, k, sw)
        return ns(b) if b else None

    p1d = ratio(mode_ns(2, 0), mode_ns(1, 0))
    p1r = ratio(mode_ns(2, 1), mode_ns(1, 1))
    p1m = ratio(mode_ns(2, 2), mode_ns(1, 2))
    p2 = max(ratio(ns(get(rows, "ComposeLayersRead", 64, k)), ns(get(rows, "ComposeLayersRead", 1, k)))
             for k in range(3) if get(rows, "ComposeLayersRead", 64, k) and get(rows, "ComposeLayersRead", 1, k))
    p5 = [get(rows, "ComposeScaleBuild", 100000, l) for l in layer_counts]
    p5text = ", ".join(f"{ms(b):.0f} ms ({l} layer{'s' if l > 1 else ''})" for b, l in zip(p5, layer_counts) if b)
    g = [get(rows, "ComposeGraftVsRebuild", e, 0) for e in entries]
    p8 = ratio(mbps(get(rows, "ComposeFlatten", 0)), mbps(get(rows, "ComposeFlatten", 4)))
    p9 = [max((ms(get(rows, "ComposeAttribute", e, c)) for c in changed if get(rows, "ComposeAttribute", e, c)), default=0)
          for e in (10000, 100000)]
    m2 = get(rows, "ComposeScaleBuild", 100000, 1)
    nfr = ["| NFR | Target | Measured | Chart |", "|---|---|---|---|",
           f"| NFR-P1 parity data / metadata (c1f against hff) | <= 1.10x / <= 1.05x | {p1d:.2f}x sequential, {p1r:.2f}x random / {p1m:.2f}x | C5 |",
           f"| NFR-P2 64 layers against 1 | <= 1.15x | {p2:.2f}x (the worst of sequential, random, metadata) | C2 |",
           "| NFR-P3 allocations per read | 0 | 0 (`ComposeReadAllocations_Test`: folder and image rebuild, graft, partitions, ISO, each with and without a session) | - |",
           f"| NFR-P5 build 100 K entries | <= 1.5 s | {p5text} | C1 |",
           f"| NFR-P6 graft independent of base | flat | {' / '.join(f'{ms(b):.2f}' for b in g if b)} ms at 1 K / 10 K / 100 K base entries | C6 |",
           f"| NFR-P7 non-composite media | no change (noise band) | {p7} | A/B table |",
           f"| NFR-P8 flatten img | >= 80% of a raw copy | {p8 * 100:.0f}% | C7b |",
           f"| NFR-P9 attribution 10 K / 100 K | <= 500 ms | {p9[0]:.0f} / {p9[1]:.0f} ms (the most changed sectors) | C7a |",
           f"| NFR-M2 memory 100 K entries | <= 32 MiB | {m2['heldBytes'] / 2**20:.1f} MiB (rebuild, heap the volume holds) | C4 |" if m2 else
           "| NFR-M2 memory 100 K entries | <= 32 MiB | - | C4 |"]

    with open(os.path.join(a.out, "results.md"), "w") as f:
        f.write(f"# Composite media benchmarks\n\n{footer}\n\n## NFR table\n\n" + "\n".join(nfr) + "\n")
        for title, md in table:
            f.write(f"\n## {title}\n\n" + "\n".join(md) + "\n")
    print(os.path.join(a.out, "results.md"))


if __name__ == "__main__":
    main()
