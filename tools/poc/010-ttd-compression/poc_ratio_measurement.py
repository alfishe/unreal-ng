#!/usr/bin/env python3
"""
poc_ratio_measurement.py — Offline analyzer for TTD I-frame / P-frame ratio.

Consumes a directory of frame_NNNN.bin files (each 48 KB = full RAM image)
produced by capture_workload.py. Computes:

  1. Per-frame dirty page statistics
     - 16 KB granularity (matches current emulator page size)
     - 4 KB and 1 KB granularity (hypothetical sub-page granularity)
  2. Dedup hit rate (page content identical to a previously-stored slot)
  3. Compression ratios at multiple algorithms and levels:
     - zstd level 1, 3, 9 (slowest but best ratio)
     - LZ4 level 1, 9 (fastest)
     - zlib level 1, 6, 9 (baseline, available everywhere)
  4. I/P ratio simulation:
     For each K in {25, 50, 100, 250, 500, 1000}, simulate:
       - Storage cost (I-frames + P-frames) under each codec
       - Average seek cost (frames walked = K/2)
       - P95 seek cost (frames walked = 0.95*K)

Prints a markdown report to stdout. Saves a CSV to <out_dir>/results.csv.

Acceptance criteria (from plan):
  - Storage cost <= 25% of uncompressed baseline at chosen ratio
  - Avg seek walks <= 100 P-frames
  - Compression overhead per frame <= 1 ms measured
"""
import argparse
import csv
import hashlib
import os
import sys
import time
from collections import defaultdict
from statistics import mean, median

import zstandard  # pip install zstandard
import lz4.block  # pip install lz4
import zlib  # stdlib

PAGE_16K = 16384
PAGE_4K = 4096
PAGE_1K = 1024


# ---------------------------------------------------------------------------
# Frame loading
# ---------------------------------------------------------------------------

def load_frames(in_dir):
    """Return sorted list of (frame_idx, bytes) tuples."""
    files = sorted(f for f in os.listdir(in_dir) if f.startswith("frame_") and f.endswith(".bin"))
    out = []
    for fname in files:
        idx = int(fname[6:11])  # frame_NNNNN.bin -> NNNNN
        with open(os.path.join(in_dir, fname), "rb") as f:
            data = f.read()
        out.append((idx, data))
    return out


# ---------------------------------------------------------------------------
# Per-frame dirty analysis at multiple granularities
# ---------------------------------------------------------------------------

def compute_dirty_pages(frames, page_size):
    """
    Return list of dicts, one per frame transition:
      {
        'frame': N,
        'dirty_count': <num pages changed since prev frame>,
        'total_pages': <total pages in full RAM>,
        'dirty_bytes_estimate': <page_size * dirty_count>,
        'actual_changed_bytes': <bytes that differ>,
      }
    First frame is the baseline (all pages "dirty").
    """
    out = []
    total_pages = len(frames[0][1]) // page_size
    for i, (idx, ram) in enumerate(frames):
        if i == 0:
            out.append({
                'frame': idx,
                'dirty_count': total_pages,
                'total_pages': total_pages,
                'dirty_bytes_estimate': total_pages * page_size,
                'actual_changed_bytes': total_pages * page_size,
            })
            continue
        prev_ram = frames[i - 1][1]
        dirty = 0
        actual_bytes = 0
        for p in range(total_pages):
            slice_a = ram[p * page_size:(p + 1) * page_size]
            slice_b = prev_ram[p * page_size:(p + 1) * page_size]
            if slice_a != slice_b:
                dirty += 1
                # Count actual differing bytes within the dirty page
                actual_bytes += sum(1 for a, b in zip(slice_a, slice_b) if a != b)
        out.append({
            'frame': idx,
            'dirty_count': dirty,
            'total_pages': total_pages,
            'dirty_bytes_estimate': dirty * page_size,
            'actual_changed_bytes': actual_bytes,
        })
    return out


# ---------------------------------------------------------------------------
# Codec benchmarking — compress a 16 KB page, return (size, time_us)
# ---------------------------------------------------------------------------

def bench_codecs(pages):
    """
    pages: list of bytes (each 16 KB)
    Returns dict: codec_name -> {avg_size, avg_time_us, total_size, samples}
    """
    codecs = {
        'raw': lambda d: (d, 0.0),
        'zstd-1': lambda d: _bench_zstd(d, 1),
        'zstd-3': lambda d: _bench_zstd(d, 3),
        'zstd-9': lambda d: _bench_zstd(d, 9),
        'lz4-1': lambda d: _bench_lz4(d, 1),
        'lz4-9': lambda d: _bench_lz4(d, 9),
        'zlib-1': lambda d: _bench_zlib(d, 1),
        'zlib-6': lambda d: _bench_zlib(d, 6),
        'zlib-9': lambda d: _bench_zlib(d, 9),
    }
    accum = {name: {'total_size': 0, 'total_us': 0.0, 'samples': 0} for name in codecs}
    for page in pages:
        for name, fn in codecs.items():
            out, us = fn(page)
            accum[name]['total_size'] += len(out)
            accum[name]['total_us'] += us
            accum[name]['samples'] += 1
    result = {}
    for name, a in accum.items():
        s = a['samples']
        result[name] = {
            'avg_size': a['total_size'] / s,
            'avg_time_us': a['total_us'] / s,
            'total_size': a['total_size'],
        }
    return result


def _bench_zstd(data, level):
    cctx = zstandard.ZstdCompressor(level=level)
    t0 = time.perf_counter()
    out = cctx.compress(data)
    return out, (time.perf_counter() - t0) * 1e6


def _bench_lz4(data, level):
    t0 = time.perf_counter()
    if level >= 9:
        out = lz4.block.compress(data, mode='high_compression', compression=level, store_size=False)
    else:
        out = lz4.block.compress(data, mode='default', acceleration=1, store_size=False)
    return out, (time.perf_counter() - t0) * 1e6


def _bench_zlib(data, level):
    t0 = time.perf_counter()
    out = zlib.compress(data, level)
    return out, (time.perf_counter() - t0) * 1e6


# ---------------------------------------------------------------------------
# Dedup analysis: how often is a "dirty" page's content identical to a slot
# we've already stored?
# ---------------------------------------------------------------------------

def analyze_dedup(frames, page_size=PAGE_16K):
    """
    Walk frames in order. For each frame, for each page that's "dirty" since
    the previous frame, check if the new content matches any slot currently
    in the live set. Return:
      {
        'interns': total dirty observations,
        'dedup_hits': how many matched an existing slot,
        'unique_slots': distinct content hashes ever stored,
      }
    """
    total_pages = len(frames[0][1]) // page_size
    # live_slots[page_pos] = set of content hashes currently in store for this slot
    # (because the same RAM page index can have different content over time)
    # But for dedup we don't care about slot position — any matching content counts.
    seen_hashes = set()
    interns = 0
    dedup_hits = 0
    # Initialize baseline
    for p in range(total_pages):
        h = hashlib.blake2b(frames[0][1][p * page_size:(p + 1) * page_size], digest_size=8).digest()
        seen_hashes.add(h)
        interns += 1
    # Walk subsequent frames
    for i in range(1, len(frames)):
        prev = frames[i - 1][1]
        cur = frames[i][1]
        for p in range(total_pages):
            a = cur[p * page_size:(p + 1) * page_size]
            b = prev[p * page_size:(p + 1) * page_size]
            if a == b:
                continue  # clean page, no Intern
            interns += 1
            h = hashlib.blake2b(a, digest_size=8).digest()
            if h in seen_hashes:
                dedup_hits += 1
            else:
                seen_hashes.add(h)
    return {
        'interns': interns,
        'dedup_hits': dedup_hits,
        'unique_slots': len(seen_hashes),
    }


# ---------------------------------------------------------------------------
# I/P ratio simulation
# ---------------------------------------------------------------------------

def simulate_ratio(dirty_stats, codec_results, keyframe_interval):
    """
    Simulate a recording where:
      - Every Kth frame is an I-frame: stores full RAM (3 * 16 KB pages)
      - Other frames are P-frames: stores only dirty pages since prev frame

    For each codec, compute:
      - Storage total
      - Avg seek cost (frames walked = K/2)
      - P95 seek cost (frames walked = 0.95*K)
      - Avg bytes decompressed on seek

    Returns dict keyed by codec name.
    """
    K = keyframe_interval
    N = len(dirty_stats)
    total_pages = dirty_stats[0]['total_pages']
    full_frame_bytes = total_pages * PAGE_16K  # 48 KB for 48K model

    # Per-codec size of a full I-frame and average per-page compressed size
    iframe_per_page = {c: codec_results[c]['avg_size'] for c in codec_results}
    # P-frame cost = (dirty pages) * (avg compressed page size)
    # I-frame cost = (all pages) * (avg compressed page size)

    avg_dirty_per_pframe = mean(s['dirty_count'] for s in dirty_stats[1:]) if len(dirty_stats) > 1 else 0
    # Worst-case seek walks K-1 P-frames; avg walks K/2; P95 walks 0.95*K
    avg_seek_walk = (K - 1) / 2
    p95_seek_walk = int(0.95 * K)
    # Bytes decompressed on avg seek = avg_seek_walk * avg_dirty_per_pframe * per_page_size
    out = {}
    for codec in codec_results:
        per_page = iframe_per_page[codec]
        iframe_size = total_pages * per_page
        pframe_size = avg_dirty_per_pframe * per_page
        n_keyframes = (N + K - 1) // K
        n_pframes = N - n_keyframes
        storage = n_keyframes * iframe_size + n_pframes * pframe_size
        # Seek cost: avg frame = (keyframe walk) + sum of P-frame pages along the way
        # Once we hit the I-frame we materialize full state, then walk forward
        avg_seek_pages_touched = total_pages + avg_seek_walk * avg_dirty_per_pframe
        p95_seek_pages_touched = total_pages + p95_seek_walk * avg_dirty_per_pframe
        avg_seek_bytes = avg_seek_pages_touched * per_page
        p95_seek_bytes = p95_seek_pages_touched * per_page
        out[codec] = {
            'storage_bytes': storage,
            'storage_kb': storage / 1024,
            'iframe_size': iframe_size,
            'pframe_size_avg': pframe_size,
            'avg_seek_walk_frames': avg_seek_walk,
            'p95_seek_walk_frames': p95_seek_walk,
            'avg_seek_bytes': avg_seek_bytes,
            'p95_seek_bytes': p95_seek_bytes,
        }
    return out


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def fmt_bytes(n):
    if n >= 1024 * 1024:
        return f"{n / 1024 / 1024:.2f} MB"
    if n >= 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n:.0f} B"


def report(frames, dirty_stats_16k, dirty_stats_4k, dirty_stats_1k, dedup, codec_results, ratio_sim, args):
    print(f"# TTD Codec PoC — {args.in_dir}")
    print()
    print(f"## Workload summary")
    print()
    print(f"- Frames sampled: **{len(frames)}**")
    print(f"- Frame indices: {frames[0][0]}..{frames[-1][0]} (step ~{(frames[-1][0]-frames[0][0])/(len(frames)-1):.0f})")
    print(f"- Full RAM per frame: {fmt_bytes(len(frames[0][1]))}")
    print(f"- Total bytes captured: {fmt_bytes(sum(len(r) for _, r in frames))}")
    print()
    print(f"## Per-frame dirty-page statistics (16 KB granularity)")
    print()
    d_counts = [s['dirty_count'] for s in dirty_stats_16k[1:]]  # skip baseline
    b_counts = [s['actual_changed_bytes'] for s in dirty_stats_16k[1:]]
    if d_counts:
        print(f"| Metric | Min | Median | Mean | Max |")
        print(f"|---|---|---|---|---|")
        print(f"| Dirty 16KB pages/frame | {min(d_counts)} | {median(d_counts):.0f} | {mean(d_counts):.2f} | {max(d_counts)} |")
        print(f"| Actual changed bytes/frame | {min(b_counts)} | {median(b_counts):.0f} | {mean(b_counts):.0f} | {max(b_counts)} |")
        print(f"| Theoretical min bytes/frame (actual bytes) | — | — | {mean(b_counts)/1024:.1f} KB | — |")
        print(f"| Current capture cost (16KB pages × dirty) | — | — | {mean(d_counts)*16:.1f} KB | — |")
        waste = (mean(d_counts) * 16384) / max(mean(b_counts), 1)
        print(f"| Overpayment factor (current vs theoretical) | — | — | **{waste:.1f}×** | — |")
    print()

    print(f"## Sub-page granularity analysis (theoretical)")
    print()
    for label, stats in [("16 KB", dirty_stats_16k), ("4 KB", dirty_stats_4k), ("1 KB", dirty_stats_1k)]:
        d = [s['dirty_count'] for s in stats[1:]]
        if d:
            page = PAGE_16K if "16" in label else PAGE_4K if "4" in label else PAGE_1K
            bytes_touched = mean(d) * page
            print(f"- {label}: mean {mean(d):.1f} dirty pages = {fmt_bytes(bytes_touched)}/frame")
    print()

    print(f"## Dedup analysis (16 KB granularity)")
    print()
    hit_rate = (dedup['dedup_hits'] / dedup['interns'] * 100) if dedup['interns'] else 0
    print(f"- Total Intern calls simulated: **{dedup['interns']}**")
    print(f"- Dedup hits (identical content already stored): **{dedup['dedup_hits']}** ({hit_rate:.1f}%)")
    print(f"- Unique slots actually needed: **{dedup['unique_slots']}**")
    print(f"- If dedup applied: storage reduces from {dedup['interns']} slots → {dedup['unique_slots']} slots (**{dedup['interns']/max(dedup['unique_slots'],1):.1f}× win**)")
    print()

    print(f"## Codec benchmark (per 16 KB page)")
    print()
    print(f"| Codec | Avg compressed size | Ratio | Time/page |")
    print(f"|---|---|---|---|")
    for codec, r in sorted(codec_results.items(), key=lambda x: x[1]['avg_size']):
        ratio = PAGE_16K / r['avg_size'] if r['avg_size'] > 0 else 0
        print(f"| {codec} | {fmt_bytes(r['avg_size'])} | {ratio:.2f}× | {r['avg_time_us']:.1f} µs |")
    print()

    print(f"## I/P ratio simulation")
    print()
    for K, sim in ratio_sim:
        print(f"### K = {K} (keyframe every {K} frames)")
        print()
        print(f"| Codec | Storage | vs raw @ K={K} | Avg seek walks | Avg seek bytes | P95 seek bytes |")
        print(f"|---|---|---|---|---|---|")
        raw_storage = sim['raw']['storage_bytes']
        for codec, r in sim.items():
            vs_raw = (r['storage_bytes'] / raw_storage * 100) if raw_storage else 0
            print(f"| {codec} | {fmt_bytes(r['storage_bytes'])} | {vs_raw:.1f}% | {r['avg_seek_walk_frames']:.0f} | {fmt_bytes(r['avg_seek_bytes'])} | {fmt_bytes(r['p95_seek_bytes'])} |")
        print()

    print(f"## Conclusions")
    print()
    print(f"See `phase-5-codec-poc-results.md` for recommendation.")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("in_dir", help="Directory containing frame_NNNNN.bin files")
    ap.add_argument("--ratios", default="25,50,100,250,500,1000",
                    help="Comma-separated keyframe intervals to simulate")
    ap.add_argument("--csv", default=None, help="Write results CSV here")
    args = ap.parse_args()

    print(f"Loading frames from {args.in_dir}...", file=sys.stderr)
    frames = load_frames(args.in_dir)
    if len(frames) < 2:
        print(f"ERROR: need at least 2 frames, got {len(frames)}", file=sys.stderr)
        sys.exit(1)
    print(f"  loaded {len(frames)} frames", file=sys.stderr)

    print("Computing dirty-page stats at 16KB / 4KB / 1KB granularity...", file=sys.stderr)
    dirty_16k = compute_dirty_pages(frames, PAGE_16K)
    dirty_4k = compute_dirty_pages(frames, PAGE_4K)
    dirty_1k = compute_dirty_pages(frames, PAGE_1K)

    print("Analyzing dedup...", file=sys.stderr)
    dedup = analyze_dedup(frames, PAGE_16K)

    # Build set of dirty pages for codec benchmark — we want a representative
    # sample, not all pages. Use all dirty pages from first 100 frame transitions.
    print("Benchmarking codecs on dirty pages...", file=sys.stderr)
    sample_pages = []
    total_pages = len(frames[0][1]) // PAGE_16K
    for i in range(1, min(len(frames), 100)):
        prev = frames[i - 1][1]
        cur = frames[i][1]
        for p in range(total_pages):
            a = cur[p * PAGE_16K:(p + 1) * PAGE_16K]
            b = prev[p * PAGE_16K:(p + 1) * PAGE_16K]
            if a != b:
                sample_pages.append(a)
    # Also include 3 baseline pages (representative I-frame content)
    for p in range(total_pages):
        sample_pages.append(frames[0][1][p * PAGE_16K:(p + 1) * PAGE_16K])
    codec_results = bench_codecs(sample_pages)

    print("Simulating I/P ratios...", file=sys.stderr)
    ratios = [int(x) for x in args.ratios.split(",")]
    ratio_sim = []
    for K in ratios:
        ratio_sim.append((K, simulate_ratio(dirty_16k, codec_results, K)))

    print(file=sys.stderr)
    report(frames, dirty_16k, dirty_4k, dirty_1k, dedup, codec_results, [(K, s) for K, s in ratio_sim], args)

    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["ratio", "codec", "storage_bytes", "iframe_size", "pframe_size_avg",
                        "avg_seek_walk_frames", "p95_seek_walk_frames", "avg_seek_bytes", "p95_seek_bytes"])
            for K, sim in ratio_sim:
                for codec, r in sim.items():
                    w.writerow([K, codec, r['storage_bytes'], r['iframe_size'], r['pframe_size_avg'],
                                r['avg_seek_walk_frames'], r['p95_seek_walk_frames'],
                                r['avg_seek_bytes'], r['p95_seek_bytes']])
        print(f"CSV written to {args.csv}", file=sys.stderr)


if __name__ == "__main__":
    main()
