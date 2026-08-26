#!/usr/bin/env python3
"""
benchmark_frame.py — Measure emulator frame performance in different modes.

Uses turbo mode (no frame limiting) to measure actual CPU time per frame.

Modes:
  1. Pure game mode (no TTD, no debug)
  2. TTD gaming mode (TTD enabled, no write journal)
  3. TTD development mode (TTD enabled, full write journal)
"""
import argparse
import json
import os
import subprocess
import sys
import time
import urllib.request
import urllib.error

BASE = "http://localhost:8090"
API = BASE + "/api/v1"
UNREAL_QT = "/Users/dev/Projects/Test/unreal-ng/cmake-build-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt"


def http(method, path, body=None, timeout=30):
    url = API + path
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    if body is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            resp_data = r.read()
            if resp_data:
                return r.status, json.loads(resp_data)
            return r.status, {}
    except urllib.error.HTTPError as e:
        resp_data = e.read()
        if resp_data:
            try:
                return e.code, json.loads(resp_data)
            except:
                return e.code, {"error": resp_data.decode('utf-8', errors='replace')}
        return e.code, {}
    except urllib.error.URLError as e:
        return None, {"error": str(e)}


def wait_for_api(timeout=30):
    start = time.time()
    while time.time() - start < timeout:
        try:
            with urllib.request.urlopen(BASE, timeout=2) as r:
                if r.status == 200:
                    return True
        except:
            pass
        time.sleep(0.5)
    return False


def get_frame_counter(iid):
    st, resp = http("GET", f"/emulator/{iid}/ttd/position")
    if st == 200:
        return resp.get("current", {}).get("frame", 0)
    return 0


def benchmark_mode(iid, mode_name, frames, ttd_mode=None):
    """Run benchmark for a specific mode."""
    print(f"\n{'='*60}")
    print(f"Benchmarking: {mode_name}")
    print(f"{'='*60}")

    # Stop any existing TTD session
    http("POST", f"/emulator/{iid}/ttd/invalidate", {"reason": "benchmark"})
    time.sleep(0.2)

    # Start TTD if needed
    if ttd_mode:
        st, resp = http("POST", f"/emulator/{iid}/ttd/start", {"mode": ttd_mode})
        if st != 200:
            print(f"  Failed to start TTD: {resp}")
            return None
        print(f"  TTD started: mode={ttd_mode}, journal={resp.get('write_journal_enabled')}")

    # Get starting frame
    http("POST", f"/emulator/{iid}/resume")
    time.sleep(0.5)

    start_frame = get_frame_counter(iid)
    start_time = time.perf_counter()

    # Let it run for the specified number of frames
    target_frame = start_frame + frames
    while True:
        current_frame = get_frame_counter(iid)
        if current_frame >= target_frame:
            break
        time.sleep(0.1)

    end_time = time.perf_counter()
    end_frame = get_frame_counter(iid)

    # Pause
    http("POST", f"/emulator/{iid}/pause")
    time.sleep(0.2)

    # Calculate results
    actual_frames = end_frame - start_frame
    elapsed = end_time - start_time
    fps = actual_frames / elapsed if elapsed > 0 else 0
    us_per_frame = (elapsed * 1_000_000) / actual_frames if actual_frames > 0 else 0

    # Get TTD stats if applicable
    ttd_stats = None
    if ttd_mode:
        st, resp = http("GET", f"/emulator/{iid}/ttd/status")
        if st == 200:
            ttd_stats = {
                "checkpoints": resp.get("checkpoint_count", 0),
                "page_store_kb": resp.get("page_store_used_bytes", 0) / 1024,
                "session_heap_kb": resp.get("session_heap_bytes", 0) / 1024,
            }

    result = {
        "mode": mode_name,
        "frames": actual_frames,
        "elapsed_s": elapsed,
        "fps": fps,
        "us_per_frame": us_per_frame,
        "ttd_stats": ttd_stats,
    }

    print(f"  Frames: {actual_frames}")
    print(f"  Elapsed: {elapsed:.3f}s")
    print(f"  FPS: {fps:.1f}")
    print(f"  Per-frame: {us_per_frame:.1f} µs")
    if ttd_stats:
        print(f"  TTD checkpoints: {ttd_stats['checkpoints']}")
        print(f"  TTD page store: {ttd_stats['page_store_kb']:.1f} KB")
        print(f"  TTD heap: {ttd_stats['session_heap_kb']:.1f} KB")

    return result


def main():
    parser = argparse.ArgumentParser(description="Benchmark emulator frame performance")
    parser.add_argument("--frames", type=int, default=500, help="Frames per benchmark")
    parser.add_argument("--snapshot", default="/Users/dev/Projects/Test/unreal-ng/testdata/loaders/sna/action.sna")
    parser.add_argument("--model", default="128k", help="Emulator model")
    args = parser.parse_args()

    print(f"=== Frame Performance Benchmark ===")
    print(f"Frames per test: {args.frames}")
    print(f"Snapshot: {os.path.basename(args.snapshot)}")
    print()

    # Launch unreal-qt
    print("[1] Launching unreal-qt...")
    proc = subprocess.Popen([UNREAL_QT], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"    PID: {proc.pid}")
    time.sleep(2)

    if not wait_for_api():
        print("    ERROR: WebAPI not available")
        proc.terminate()
        return 1

    print("    WebAPI ready")
    iid = None
    results = []

    try:
        # Create emulator
        print(f"\n[2] Creating {args.model} emulator...")
        st, resp = http("POST", "/emulator/start", {"model": args.model})
        if st != 201:
            print(f"    FAILED: {resp}")
            return 1
        iid = resp["id"]
        print(f"    ID: {iid}")
        time.sleep(0.5)

        # Load snapshot
        print(f"\n[3] Loading {os.path.basename(args.snapshot)}...")
        st, resp = http("POST", f"/emulator/{iid}/snapshot/load", {"path": args.snapshot})
        if st != 200:
            print(f"    FAILED: {resp}")
            return 1
        print(f"    OK")
        time.sleep(0.5)

        # Benchmark 1: Pure game mode (no TTD)
        results.append(benchmark_mode(iid, "Pure Game (no TTD)", args.frames, ttd_mode=None))

        # Reload snapshot to reset state
        http("POST", f"/emulator/{iid}/snapshot/load", {"path": args.snapshot})
        time.sleep(0.3)

        # Benchmark 2: TTD Gaming mode
        results.append(benchmark_mode(iid, "TTD Gaming (no journal)", args.frames, ttd_mode="gaming"))

        # Reload snapshot
        http("POST", f"/emulator/{iid}/ttd/invalidate", {"reason": "reset"})
        http("POST", f"/emulator/{iid}/snapshot/load", {"path": args.snapshot})
        time.sleep(0.3)

        # Benchmark 3: TTD Development mode
        results.append(benchmark_mode(iid, "TTD Development (full journal)", args.frames, ttd_mode="development"))

        # Summary
        print(f"\n{'='*60}")
        print("SUMMARY")
        print(f"{'='*60}")
        print(f"{'Mode':<35} {'FPS':>8} {'µs/frame':>12} {'Overhead':>10}")
        print("-" * 65)

        baseline_us = results[0]["us_per_frame"] if results[0] else 0
        for r in results:
            if r:
                overhead = ((r["us_per_frame"] / baseline_us) - 1) * 100 if baseline_us > 0 else 0
                overhead_str = f"+{overhead:.1f}%" if overhead > 0 else f"{overhead:.1f}%"
                print(f"{r['mode']:<35} {r['fps']:>8.1f} {r['us_per_frame']:>12.1f} {overhead_str:>10}")

        return 0

    finally:
        print(f"\n[*] Cleanup...")
        if iid:
            try:
                http("POST", f"/emulator/{iid}/destroy", timeout=5)
            except Exception:
                pass
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            pass
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=2)
        print("    Done")


if __name__ == "__main__":
    sys.exit(main() or 0)
