#!/usr/bin/env python3
"""
capture_ttd.py — Automated TTD capture via WebAPI.

Launches unreal-qt, creates emulator, loads snapshot, records TTD, dumps to file.

Usage:
    python3 capture_ttd.py [--frames N] [--snapshot PATH] [--output PATH]
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
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read()) if e.read() else {}
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


def main():
    parser = argparse.ArgumentParser(description="Capture TTD recording")
    parser.add_argument("--frames", type=int, default=3000, help="Frames to capture")
    parser.add_argument("--snapshot", default="/Users/dev/Projects/Test/unreal-ng/testdata/loaders/sna/action.sna")
    parser.add_argument("--output", default="/Users/dev/Projects/Test/unreal-ng/tools/poc/02-ttd-gui/testdata/capture.ttd")
    parser.add_argument("--model", default="128k", help="Emulator model")
    args = parser.parse_args()

    capture_seconds = args.frames // 50 + 5
    print(f"=== TTD Capture: {args.frames} frames (~{capture_seconds}s) ===\n")

    # Launch unreal-qt
    print(f"[1] Launching unreal-qt...")
    proc = subprocess.Popen([UNREAL_QT], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"    PID: {proc.pid}")
    time.sleep(2)

    if not wait_for_api():
        print("    ERROR: WebAPI not available")
        proc.terminate()
        return 1

    print("    WebAPI ready")
    iid = None

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

        # Start TTD
        print(f"\n[4] Starting TTD recording...")
        st, resp = http("POST", f"/emulator/{iid}/ttd/start")
        if st != 200:
            print(f"    FAILED: {resp}")
            return 1
        print(f"    State: {resp.get('state')}")

        # Record
        print(f"\n[5] Recording {args.frames} frames...")
        start = time.time()
        last_frame = 0
        while time.time() - start < capture_seconds:
            st, resp = http("GET", f"/emulator/{iid}/ttd/position")
            if st == 200:
                frame = resp.get("current", {}).get("frame", 0)
                if frame != last_frame:
                    elapsed = int(time.time() - start)
                    print(f"    t={elapsed:>3}s  frame={frame}")
                    last_frame = frame
                if frame >= args.frames:
                    break
            time.sleep(2)

        # Stop
        print(f"\n[6] Stopping TTD...")
        st, resp = http("POST", f"/emulator/{iid}/ttd/stop")
        print(f"    State: {resp.get('state')}")

        # Get stats
        st, resp = http("GET", f"/emulator/{iid}/ttd/status")
        if st == 200:
            print(f"    Checkpoints: {resp.get('checkpoint_count')}")
            print(f"    Frames: {resp.get('session_start_frame')} → {resp.get('current_end_frame')}")

        # Dump
        print(f"\n[7] Dumping to {args.output}...")
        st, resp = http("POST", f"/emulator/{iid}/ttd/dump", {"path": args.output}, timeout=300)
        if st != 200:
            print(f"    FAILED: {resp}")
            return 1
        print(f"    Bytes: {resp.get('bytes', 0):,}")

        if os.path.exists(args.output):
            size = os.path.getsize(args.output)
            print(f"\n[8] SUCCESS: {size:,} bytes ({size/1024:.1f} KB)")
            print(f"    Per-frame: {size/args.frames:.1f} bytes")
        return 0

    finally:
        if iid:
            print(f"\n[*] Cleanup...")
            http("POST", f"/emulator/{iid}/destroy", timeout=5)
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except:
            proc.kill()
        print("    Done")


if __name__ == "__main__":
    sys.exit(main() or 0)
