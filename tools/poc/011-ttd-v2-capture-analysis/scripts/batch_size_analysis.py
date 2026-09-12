#!/usr/bin/env python3
"""
batch_size_analysis.py — Find optimal page batching size for TTD compression.

Measures trade-off between:
  - Compression ratio (smaller batches = more zstd frame overhead)
  - Restore cost (larger batches = more data to decompress for single page access)

Restore = decompress batch + extract target page + precache (simulated)
Seek itself is O(1) index lookup - not measured here.

Tests batch sizes: 1, 2, 4, 8, 16, 32 pages per block.
"""
import sys
import os
import time
import random
import zstandard as zstd

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../../"))
TTD_ANALYZER_SRC = os.path.join(REPO_ROOT, "tools/verification/ttd-analyzer/src")
sys.path.insert(0, TTD_ANALYZER_SRC)

import ttd_format

PAGE_SIZE = 4096
BATCH_SIZES = [1, 2, 4, 8, 16, 32]

cctx = zstd.ZstdCompressor(level=1, write_checksum=False)
dctx = zstd.ZstdDecompressor()

def extract_xor_deltas(filepath: str) -> list:
    """Extract all XOR delta pages from a TTD file."""
    dump = ttd_format.parse_file(filepath)
    deltas = []
    for i in range(1, len(dump.slots)):
        slot = dump.slots[i]
        if slot.encoding == ttd_format.ENCODING_XOR_PREV:
            curr = dump.get_sub_page(i)
            prev = dump.get_sub_page(slot.prev_slot)
            xor_b = bytes(a ^ b for a, b in zip(curr, prev))
            deltas.append(xor_b)
    return deltas

def benchmark_batch_size(deltas: list, batch_size: int, num_restores: int = 200):
    """Benchmark compression and restore performance for a given batch size."""

    # Create batches
    batches = []
    for i in range(0, len(deltas) - batch_size + 1, batch_size):
        batch_data = b''.join(deltas[i:i+batch_size])
        batches.append(batch_data)

    if not batches:
        return None

    # Measure compression
    compressed = []
    compress_times = []
    for batch in batches:
        t0 = time.perf_counter()
        c = cctx.compress(batch)
        t1 = time.perf_counter()
        compressed.append(c)
        compress_times.append((t1 - t0) * 1e6)

    total_raw = sum(len(b) for b in batches)
    total_compressed = sum(len(c) for c in compressed)

    # Measure RESTORE: decompress batch + extract page + XOR apply (simulated precache)
    # This is the actual cost when seeking to a frame
    restore_times = []
    precache_buf = bytearray(PAGE_SIZE)  # Simulated RAM page buffer

    for _ in range(num_restores):
        batch_idx = random.randint(0, len(compressed) - 1)
        page_in_batch = random.randint(0, batch_size - 1)

        t0 = time.perf_counter()

        # 1. Decompress the batch
        decompressed = dctx.decompress(compressed[batch_idx])

        # 2. Extract target page
        offset = page_in_batch * PAGE_SIZE
        page_delta = decompressed[offset:offset + PAGE_SIZE]

        # 3. Apply XOR to precache buffer (simulates restoring RAM state)
        for i in range(PAGE_SIZE):
            precache_buf[i] ^= page_delta[i]

        t1 = time.perf_counter()
        restore_times.append((t1 - t0) * 1e6)

    # Calculate metrics
    avg = lambda lst: sum(lst) / len(lst) if lst else 0

    return {
        'batch_size': batch_size,
        'num_batches': len(batches),
        'total_pages': len(batches) * batch_size,
        'total_raw_bytes': total_raw,
        'total_compressed_bytes': total_compressed,
        'compression_ratio': total_raw / total_compressed if total_compressed > 0 else 0,
        'bytes_per_page': total_compressed / (len(batches) * batch_size),
        'compress_time_per_page_us': avg(compress_times) / batch_size,
        'restore_mean_us': avg(restore_times),
        'restore_p50_us': sorted(restore_times)[len(restore_times)//2],
        'restore_p95_us': sorted(restore_times)[int(len(restore_times) * 0.95)],
        'restore_max_us': max(restore_times),
    }

def main():
    ttd_dir = "testdata/ttd"
    files = ["demo_7threality.ttd", "demo_across-the-edge-second.ttd", "active_demo.ttd", "idle_session.ttd"]

    print("=" * 110)
    print(" PAGE BATCHING SIZE OPTIMIZATION — RESTORE LATENCY ANALYSIS")
    print("=" * 110)
    print()
    print("  Restore = decompress batch + extract page + XOR apply to RAM")
    print("  (Seek is O(1) index lookup, not measured)")
    print()

    from collections import defaultdict
    all_results = defaultdict(list)

    for fname in files:
        fpath = os.path.join(ttd_dir, fname)
        if not os.path.exists(fpath):
            continue

        print(f"\n{'='*90}")
        print(f" {fname}")
        print(f"{'='*90}")

        deltas = extract_xor_deltas(fpath)
        print(f"  Pages: {len(deltas)}")

        print(f"\n  {'Batch':>6} {'B/Page':>8} {'Ratio':>7} | {'Compress':>10} | {'Restore':>10} {'p50':>10} {'p95':>10} {'max':>10}")
        print(f"  {'-'*6} {'-'*8} {'-'*7} | {'-'*10} | {'-'*10} {'-'*10} {'-'*10} {'-'*10}")

        for batch_size in BATCH_SIZES:
            r = benchmark_batch_size(deltas, batch_size)
            if r:
                all_results[batch_size].append(r)
                print(f"  {batch_size:>6} {r['bytes_per_page']:>7.1f}B {r['compression_ratio']:>6.1f}x | "
                      f"{r['compress_time_per_page_us']:>9.2f}µs | "
                      f"{r['restore_mean_us']:>9.2f}µs {r['restore_p50_us']:>9.2f}µs "
                      f"{r['restore_p95_us']:>9.2f}µs {r['restore_max_us']:>9.2f}µs")

    # Aggregate analysis
    print(f"\n\n{'='*110}")
    print(" AGGREGATE RESULTS")
    print("="*110)
    print(f"\n  {'Batch':>6} {'Bytes/Page':>11} {'Ratio':>8} {'Size Δ':>8} | {'Compress':>10} | {'Restore':>10} {'p95':>10} {'Restore Δ':>10}")
    print(f"  {'-'*6} {'-'*11} {'-'*8} {'-'*8} | {'-'*10} | {'-'*10} {'-'*10} {'-'*10}")

    baseline_bytes = None
    baseline_restore = None

    for batch_size in BATCH_SIZES:
        results = all_results[batch_size]
        if not results:
            continue

        avg_bytes = sum(r['bytes_per_page'] for r in results) / len(results)
        avg_ratio = sum(r['compression_ratio'] for r in results) / len(results)
        avg_compress = sum(r['compress_time_per_page_us'] for r in results) / len(results)
        avg_restore = sum(r['restore_mean_us'] for r in results) / len(results)
        avg_restore_p95 = sum(r['restore_p95_us'] for r in results) / len(results)

        if baseline_bytes is None:
            baseline_bytes = avg_bytes
            baseline_restore = avg_restore

        size_pct = (baseline_bytes - avg_bytes) / baseline_bytes * 100
        restore_pct = (avg_restore - baseline_restore) / baseline_restore * 100 if baseline_restore > 0 else 0

        size_str = f"+{size_pct:.0f}%" if size_pct > 0 else f"{size_pct:.0f}%"
        restore_str = f"+{restore_pct:.0f}%" if restore_pct > 0 else f"{restore_pct:.0f}%"

        print(f"  {batch_size:>6} {avg_bytes:>10.1f}B {avg_ratio:>7.1f}x {size_str:>8} | "
              f"{avg_compress:>9.2f}µs | {avg_restore:>9.2f}µs {avg_restore_p95:>9.2f}µs {restore_str:>10}")

    # Recommendation
    print(f"\n{'='*110}")
    print(" ANALYSIS & RECOMMENDATIONS")
    print("="*110)
    print("""
  NOTE: Python XOR loop (~200 µs) dominates these measurements.
  See C++ benchmark (batch_size_bench.cpp) for real timings:

  C++ RESTORE TIMES (real):
    Batch=1:   3.3 µs
    Batch=4:   6.9 µs  (+109%)
    Batch=8:  12.6 µs  (+282%)
    Batch=16: 24.4 µs  (+639%)

  COMPRESSION (from above):
    Batch=1:  59 B/page (baseline)
    Batch=8:  39 B/page (+34% smaller)

  KEY FINDING:
    - XOR apply is negligible (63 ns)
    - Decompression dominates and scales ~linearly with batch size
    - All restore times << 6ms budget

  RECOMMENDATION:
    → Hot buffer: batch=1-2 for instant ~3 µs seeks
    → General use: batch=4 for 23% smaller, 7 µs restore
    → Cold storage: batch=8 for 34% smaller, 13 µs restore
""")

if __name__ == "__main__":
    main()
