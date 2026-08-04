#!/usr/bin/env python3
"""
capture_workload.py — Walk a TTD recording frame-by-frame and dump the full
48 KB RAM image for each frame, so the offline analyzer can measure dirty
page statistics, dedup hit rates, and compression ratios.

Usage:
    python3 capture_workload.py <instance_id> <frame_count> <frame_step> <out_dir>

The emulator must already have a stopped TTD recording with at least
<frame_count> checkpoints. This script will:
    1. Seek to each frame (0, frame_step, 2*frame_step, ..., frame_count)
    2. Read 48 KB of RAM via /memory/read (3 × 16 KB pages)
    3. Write frame_NNNN.bin into <out_dir>

Reads emulator base URL from $UNREAL_API or defaults to localhost:8090.
"""
import json
import os
import sys
import time
import urllib.request

BASE = os.environ.get("UNREAL_API", "http://localhost:8090/api/v1")


def seek(iid, frame):
    body = json.dumps({"frame": frame, "tinframe": 0}).encode()
    req = urllib.request.Request(
        f"{BASE}/emulator/{iid}/ttd/seek",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    return urllib.request.urlopen(req, timeout=10).read()


def read_ram_page(iid, z80_addr):
    """Read 16 KB starting at z80_addr. Returns raw bytes."""
    url = f"{BASE}/emulator/{iid}/memory/read/0x{z80_addr:04X}?length=16384"
    resp = urllib.request.urlopen(url, timeout=30)
    payload = json.loads(resp.read())
    return bytes(payload["data"])


def read_full_ram(iid):
    """Read the three RAM banks of a 48K machine (0x4000-0xFFFF)."""
    p0 = read_ram_page(iid, 0x4000)
    p1 = read_ram_page(iid, 0x8000)
    p2 = read_ram_page(iid, 0xC000)
    assert len(p0) == 16384 and len(p1) == 16384 and len(p2) == 16384
    return p0 + p1 + p2


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        sys.exit(1)
    iid = sys.argv[1]
    frame_count = int(sys.argv[2])
    frame_step = int(sys.argv[3])
    out_dir = sys.argv[4]
    os.makedirs(out_dir, exist_ok=True)

    print(f"=== Capture workload ===")
    print(f"Instance: {iid}")
    print(f"Frames: 0..{frame_count} step {frame_step} -> {(frame_count // frame_step) + 1} samples")
    print(f"Output: {out_dir}")
    print()

    t_start = time.time()
    for frame in range(0, frame_count + 1, frame_step):
        t0 = time.time()
        seek(iid, frame)
        ram = read_full_ram(iid)
        dt = time.time() - t0
        path = os.path.join(out_dir, f"frame_{frame:05d}.bin")
        with open(path, "wb") as f:
            f.write(ram)
        print(f"  frame {frame:5d}: {len(ram)} bytes in {dt*1000:.0f} ms")
    print()
    print(f"Total: {time.time() - t_start:.1f} s")


if __name__ == "__main__":
    main()
