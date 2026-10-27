#!/usr/bin/env python3
"""POC 032: turn the harness CSVs (q90.csv, sweep.csv, frame.csv) into the markdown tables of the report.

usage: python3 summarize.py RUN_DIR > tables.md
"""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

BASE = "a-turbojpeg"


def load(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def group_key(r):
    return (r["category"], r["model"])


def q90_tables(rows):
    encoders = list(dict.fromkeys(r["encoder"] for r in rows))
    groups = defaultdict(lambda: defaultdict(list))
    for r in rows:
        groups[group_key(r)][r["encoder"]].append(r)
    out = ["### q = 90, per image group", "",
           "Time: mean over the group's images of each image's median / p99 (us). Bytes: mean per image. "
           "Reports: mean HID reports per image. PSNR: mean dB (stb_image decode). Ratios against turbojpeg.", "",
           "| Group | Images | Encoder | Median us | p99 us | x tj time | Bytes | x tj bytes | Reports | PSNR dB | dPSNR |",
           "|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for (cat, model), per in groups.items():
        base = per[BASE]
        bt = statistics.mean(float(r["median_us"]) for r in base)
        bb = statistics.mean(int(r["bytes"]) for r in base)
        bp = statistics.mean(float(r["psnr"]) for r in base)
        for e in encoders:
            rs = per[e]
            t = statistics.mean(float(r["median_us"]) for r in rs)
            p99 = statistics.mean(float(r["p99_us"]) for r in rs)
            b = statistics.mean(int(r["bytes"]) for r in rs)
            rep = statistics.mean(int(r["reports"]) for r in rs)
            p = statistics.mean(float(r["psnr"]) for r in rs)
            out.append(f"| {cat} {model} | {len(rs)} | {e} | {t:.1f} | {p99:.1f} | {t / bt:.2f} | {b:.0f} | "
                       f"{b / bb:.3f} | {rep:.2f} | {p:.2f} | {p - bp:+.2f} |")
    out.append("")
    # decoder agreement
    bad = [r for r in rows if float(r["psnr"]) < 0 or float(r["psnr_tj"]) < 0]
    worst = max(abs(float(r["psnr"]) - float(r["psnr_tj"])) for r in rows)
    out.append(f"Decode failures (stb_image or libjpeg-turbo): {len(bad)} of {len(rows)} streams; "
               f"largest PSNR difference between the two decoders: {worst:.3f} dB.")
    out.append("")
    return out


def interp_bytes(curve, target):
    """curve: [(psnr, bytes)] sorted by quality. Linear interpolation of bytes at a PSNR; None if out of range."""
    pts = sorted(curve)
    for (p0, b0), (p1, b1) in zip(pts, pts[1:]):
        if p0 <= target <= p1:
            if p1 == p0:
                return b0
            return b0 + (b1 - b0) * (target - p0) / (p1 - p0)
    return None


def equal_psnr_table(q90, sweep):
    target = {(r["category"], r["model"], r["image"]): float(r["psnr"]) for r in q90 if r["encoder"] == BASE}
    curves = defaultdict(list)
    for r in sweep:
        curves[(r["encoder"], r["category"], r["model"], r["image"])].append((float(r["psnr"]), int(r["bytes"])))
    encoders = list(dict.fromkeys(r["encoder"] for r in sweep))
    cats = list(dict.fromkeys(r["category"] for r in sweep))
    out = ["### Bytes at equal PSNR (turbojpeg q90's PSNR per image, interpolated on each q sweep)", "",
           "| Category | Encoder | Images matched | Total bytes | x tj | q90 total bytes | x tj at q90 |",
           "|---|---|---:|---:|---:|---:|---:|"]
    q90bytes = defaultdict(int)
    for r in q90:
        q90bytes[(r["encoder"], r["category"])] += int(r["bytes"])
    for cat in cats:
        images = [k for k in target if k[0] == cat]
        base_total = None
        rows = []
        for e in encoders:
            total, n = 0.0, 0
            for k in images:
                b = interp_bytes(curves[(e,) + k], target[k])
                if b is not None:
                    total += b
                    n += 1
            rows.append((e, n, total))
            if e == BASE:
                base_total = total
        for e, n, total in rows:
            out.append(f"| {cat} | {e} | {n}/{len(images)} | {total:.0f} | {total / base_total:.3f} | "
                       f"{q90bytes[(e, cat)]} | {q90bytes[(e, cat)] / q90bytes[(BASE, cat)]:.3f} |")
    out.append("")
    return out


def frame_table(rows):
    out = ["### A whole + XL frame (36 keys 112 px + strip 1200x100, q90, one thread)", "",
           "| Content | Encoder | Median us | p99 us | x tj | Bytes | Reports |", "|---|---|---:|---:|---:|---:|---:|"]
    base = {r["content"]: float(r["median_us"]) for r in rows if r["encoder"] == BASE}
    for r in rows:
        out.append(f"| {r['content']} | {r['encoder']} | {float(r['median_us']):.0f} | {float(r['p99_us']):.0f} | "
                   f"{float(r['median_us']) / base[r['content']]:.2f} | {r['bytes']} | {r['reports']} |")
    out.append("")
    return out


def main():
    run = Path(sys.argv[1])
    q90 = load(run / "q90.csv")
    sweep = load(run / "sweep.csv")
    frame = load(run / "frame.csv")
    print("\n".join(frame_table(frame) + equal_psnr_table(q90, sweep) + q90_tables(q90)))


if __name__ == "__main__":
    main()
