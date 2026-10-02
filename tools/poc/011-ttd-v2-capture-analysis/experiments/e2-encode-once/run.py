"""E2 - encode a changed piece once.

v1 compresses every changed 4 KB piece twice (as the XOR difference and as the
full piece) and keeps the smaller. This experiment measures, on all changes of
all input sessions, how often the full piece wins and what skipping its
compression below an XOR-size threshold T costs in bytes; and, on a sample of
real pieces, how long each zstd call takes with the emulator's own zstd build.

Run: python3 run.py   (after ../common/record-datasets.sh; builds zstdtiming)
Writes results.md and timing.json (the decode times E4 uses) next to this file.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "common"))
import datasets  # noqa: E402
import piecestats as ps  # noqa: E402
import ttdhistory as th  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(datasets.ROOT, "scratch", "ttd-experiments", "e2")
THRESHOLDS = [128, 256, 512, 1024, 2048, 1 << 30]
SAMPLE_PER_SESSION = 200


def sample_pairs(path_out: str) -> int:
    """Write (previous, new) piece pairs from every session, evenly spread over its changes."""
    count = 0
    with open(path_out, "wb") as out:
        for name, path in datasets.sessions():
            st = ps.load(name, path)
            total = sum(len(r.change_cp) for r in st.regions.values())
            stride = max(1, total // SAMPLE_PER_SESSION)
            seen = {"n": 0}

            def on_change(region, i, j, prev, new):
                if seen["n"] % stride == 0:
                    out.write(prev)
                    out.write(new)
                    nonlocal_count[0] += 1
                seen["n"] += 1

            nonlocal_count = [0]
            th.scan(path, on_change=on_change)
            count += nonlocal_count[0]
    return count


def timing() -> np.ndarray:
    os.makedirs(WORK, exist_ok=True)
    pairs = os.path.join(WORK, "pairs.bin")
    n = sample_pairs(pairs)
    exe = subprocess.run([os.path.join(HERE, "build-timing.sh")], check=True, capture_output=True,
                         text=True).stdout.strip().splitlines()[-1]
    out = subprocess.run([exe, pairs], check=True, capture_output=True, text=True).stdout
    t = np.array([[int(x) for x in line.split()] for line in out.splitlines() if line.strip()], np.float64)
    assert len(t) == n, (len(t), n)
    return t  # columns: xor compress, full compress, xor decompress, full decompress (ns)


def main():
    xs_all, fs_all = [], []
    for name, path in datasets.sessions():
        st = ps.load(name, path)
        for r in st.regions.values():
            keep = r.change_full > 0   # all-zero new content is stored as Zero, no compression
            xs_all.append(r.change_xor[keep])
            fs_all.append(r.change_full[keep])
    xs = np.concatenate(xs_all)
    fs = np.concatenate(fs_all)
    best = np.minimum(xs, fs)
    full_wins = float((fs < xs).mean())

    t = timing()
    xc, fc, xd, fd = (t[:, k] / 1000.0 for k in range(4))   # microseconds
    # sizes of the sampled pairs, for the per-threshold time model
    with open(os.path.join(WORK, "pairs.bin"), "rb") as f:
        raw = f.read()
    sx = np.array([ps.zsize(ps.xor(raw[i:i + 4096], raw[i + 4096:i + 8192]))
                   for i in range(0, len(raw), 8192)], np.int64)

    lines = ["# E2 results - encode a changed piece once", "",
             f"Changes analyzed (non-zero new content): {len(xs):,}. Full piece smaller than the XOR in "
             f"{full_wins * 100:.2f}% of them. Timing sample: {len(t):,} real piece pairs, zstd level 1 as the "
             "emulator calls it, minimum of 15 runs per call.", "",
             "## zstd call times (µs per 4 KB piece)", "",
             "| Call | mean | p50 | p99 |", "|---|---|---|---|"]
    for label, a in (("compress XOR", xc), ("compress full", fc), ("decompress XOR", xd), ("decompress full", fd)):
        lines.append(f"| {label} | {a.mean():.2f} | {np.percentile(a, 50):.2f} | {np.percentile(a, 99):.2f} |")
    lines += ["", "## XOR size distribution of changes (bytes, zstd-1)", "",
              "| p50 | p90 | p99 | max |", "|---|---|---|---|",
              f"| {np.percentile(xs, 50):.0f} | {np.percentile(xs, 90):.0f} | {np.percentile(xs, 99):.0f} | {xs.max()} |",
              "", "## Threshold T: compress the full piece only when the XOR result is larger than T", "",
              "| T (bytes) | Full compression skipped | Stored bytes vs v1 | Encode time vs v1 |", "|---|---|---|---|"]
    base_time = float((xc + fc).sum())
    for T in THRESHOLDS:
        skip = xs <= T
        stored = np.where(skip, xs, best)
        time_t = float((xc + np.where(sx <= T, 0.0, fc)).sum())
        label = "never" if T >= (1 << 30) else f"{T:,}"
        lines.append(f"| {label} | {skip.mean() * 100:.1f}% | {stored.sum() / best.sum():.4f} | "
                     f"{time_t / base_time:.3f} |")
    lines.append("")
    with open(os.path.join(HERE, "results.md"), "w", newline="\n") as out:
        out.write("\n".join(lines) + "\n")
    with open(os.path.join(HERE, "timing.json"), "w", newline="\n") as out:
        json.dump({"compress_xor_us": round(float(xc.mean()), 3), "compress_full_us": round(float(fc.mean()), 3),
                   "decompress_xor_us": round(float(xd.mean()), 3), "decompress_full_us": round(float(fd.mean()), 3),
                   "samples": int(len(t))}, out, indent=1)
        out.write("\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
