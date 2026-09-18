#!/usr/bin/env python3
"""
v1_vs_v2_real_ttd.py — Compare V1 vs V2 (I-frame@50 + batch=4) on real TTD files.

V1: Full frame snapshot every frame
V2: Page XOR deltas + I-frame every 50 + batch=4 pages per compress call
"""
import sys
import os
import time
import random
import zstandard as zstd

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../../"))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools/verification/ttd-analyzer/src"))
import ttd_format

PAGE_SIZE = 4096
I_FRAME_INTERVAL = 50
BATCH_SIZE = 4  # Pages per compress call within a frame

cctx = zstd.ZstdCompressor(level=1, write_checksum=False)
dctx = zstd.ZstdDecompressor()

def analyze_ttd_file(filepath: str):
    dump = ttd_format.parse_file(filepath)
    num_frames = dump.header.checkpoint_count
    if num_frames < 2:
        return None

    model_pages = dump.header.model_ram_pages
    frame_size = model_pages * PAGE_SIZE

    print(f"\n{'='*80}")
    print(f" {os.path.basename(filepath)}")
    print(f" Frames: {num_frames}, RAM: {model_pages} pages ({frame_size//1024} KB)")
    print(f"{'='*80}")

    # Reconstruct frames
    frames = []
    for cp_idx in range(num_frames):
        cp = dump.checkpoints[cp_idx]
        frame_data = bytearray(frame_size)
        for p in range(model_pages):
            page_bytes = dump.get_sub_page(cp.ram_page_refs[p])
            frame_data[p * PAGE_SIZE:(p + 1) * PAGE_SIZE] = page_bytes
        frames.append(bytes(frame_data))

    # === V1: Compress each frame as full snapshot ===
    v1_compressed = []
    for frame in frames:
        v1_compressed.append(cctx.compress(frame))
    v1_total = sum(len(c) for c in v1_compressed)

    # === V2: I-frames + batched page deltas ===
    v2_keyframes = []  # Compressed keyframes
    v2_deltas = []     # Per-frame: list of (page_indices, compressed_batch)
    v2_keyframe_bytes = 0
    v2_delta_bytes = 0

    prev_frame = bytes(frame_size)
    for f_idx, frame in enumerate(frames):
        # I-frame every 50
        if f_idx % I_FRAME_INTERVAL == 0:
            kf_compressed = cctx.compress(frame)
            v2_keyframes.append(kf_compressed)
            v2_keyframe_bytes += len(kf_compressed)

        # Compute page deltas
        changed_pages = []
        changed_deltas = []
        for p in range(model_pages):
            curr = frame[p * PAGE_SIZE:(p + 1) * PAGE_SIZE]
            prev = prev_frame[p * PAGE_SIZE:(p + 1) * PAGE_SIZE]
            if curr != prev:
                changed_pages.append(p)
                changed_deltas.append(bytes(a ^ b for a, b in zip(curr, prev)))

        # Batch compress (4 pages per batch)
        frame_batches = []
        for i in range(0, len(changed_deltas), BATCH_SIZE):
            batch = changed_deltas[i:i + BATCH_SIZE]
            batch_pages = changed_pages[i:i + BATCH_SIZE]
            compressed = cctx.compress(b''.join(batch))
            frame_batches.append((batch_pages, compressed))
            v2_delta_bytes += len(compressed)

        v2_deltas.append(frame_batches)
        prev_frame = frame

    v2_total = v2_keyframe_bytes + v2_delta_bytes

    # === Measure restore times ===
    random.seed(42)

    # V1 restore
    v1_times = []
    for _ in range(200):
        target = random.randint(0, num_frames - 1)
        t0 = time.perf_counter()
        _ = dctx.decompress(v1_compressed[target])
        t1 = time.perf_counter()
        v1_times.append((t1 - t0) * 1e6)

    # V2 restore
    v2_times = []
    for _ in range(200):
        target = random.randint(0, num_frames - 1)
        kf_idx = target // I_FRAME_INTERVAL
        kf_frame = kf_idx * I_FRAME_INTERVAL

        t0 = time.perf_counter()

        # 1. Decompress keyframe
        restored = bytearray(dctx.decompress(v2_keyframes[kf_idx]))

        # 2. Apply deltas
        for f in range(kf_frame + 1, target + 1):
            for batch_pages, compressed in v2_deltas[f]:
                decompressed = dctx.decompress(compressed)
                for i, p in enumerate(batch_pages):
                    delta = decompressed[i * PAGE_SIZE:(i + 1) * PAGE_SIZE]
                    for j in range(PAGE_SIZE):
                        restored[p * PAGE_SIZE + j] ^= delta[j]

        t1 = time.perf_counter()
        v2_times.append((t1 - t0) * 1e6)

    v1_restore = sum(v1_times) / len(v1_times)
    v2_restore = sum(v2_times) / len(v2_times)
    ratio = v1_total / v2_total

    print(f"\n  {'Metric':<20} {'V1':<20} {'V2 (I@50+batch4)':<20} {'Delta':<15}")
    print(f"  {'-'*20} {'-'*20} {'-'*20} {'-'*15}")
    print(f"  {'Storage':<20} {v1_total/1024:>15.1f} KB {v2_total/1024:>15.1f} KB {ratio:>10.1f}x smaller")
    print(f"  {'  Keyframes':<20} {'-':>20} {v2_keyframe_bytes/1024:>15.1f} KB")
    print(f"  {'  Deltas':<20} {'-':>20} {v2_delta_bytes/1024:>15.1f} KB")
    print(f"  {'Restore':<20} {v1_restore:>15.1f} µs {v2_restore:>15.1f} µs {v2_restore/v1_restore:>10.1f}x slower")
    print(f"  {'% of 6ms':<20} {v1_restore/6000*100:>15.2f}% {v2_restore/6000*100:>15.2f}%")

    return {
        'file': os.path.basename(filepath),
        'frames': num_frames,
        'v1_storage': v1_total,
        'v2_storage': v2_total,
        'ratio': ratio,
        'v1_restore': v1_restore,
        'v2_restore': v2_restore,
    }

def main():
    ttd_dir = "testdata/ttd"
    files = ["demo_7threality.ttd", "demo_across-the-edge-second.ttd", "active_demo.ttd", "idle_session.ttd"]

    print("=" * 80)
    print(" V1 vs V2 (I-frame@50 + batch=4) — Real TTD Files")
    print("=" * 80)

    results = []
    for fname in files:
        fpath = os.path.join(ttd_dir, fname)
        if os.path.exists(fpath):
            r = analyze_ttd_file(fpath)
            if r:
                results.append(r)

    if results:
        print(f"\n\n{'='*80}")
        print(" SUMMARY")
        print("="*80)
        print(f"\n  {'File':<40} {'V1':>12} {'V2':>12} {'Ratio':>8} {'V1 Rest':>10} {'V2 Rest':>10}")
        print(f"  {'-'*40} {'-'*12} {'-'*12} {'-'*8} {'-'*10} {'-'*10}")

        for r in results:
            print(f"  {r['file']:<40} {r['v1_storage']/1024:>10.1f}KB {r['v2_storage']/1024:>10.1f}KB "
                  f"{r['ratio']:>7.1f}x {r['v1_restore']:>9.0f}µs {r['v2_restore']:>9.0f}µs")

        total_v1 = sum(r['v1_storage'] for r in results)
        total_v2 = sum(r['v2_storage'] for r in results)
        avg_v1_restore = sum(r['v1_restore'] for r in results) / len(results)
        avg_v2_restore = sum(r['v2_restore'] for r in results) / len(results)

        print(f"\n  {'TOTAL':<40} {total_v1/1024:>10.1f}KB {total_v2/1024:>10.1f}KB "
              f"{total_v1/total_v2:>7.1f}x {avg_v1_restore:>9.0f}µs {avg_v2_restore:>9.0f}µs")

        print(f"\n  V2: {(1 - total_v2/total_v1)*100:.1f}% smaller, {avg_v2_restore:.0f}µs restore ({avg_v2_restore/6000*100:.2f}% of 6ms)")

if __name__ == "__main__":
    main()
