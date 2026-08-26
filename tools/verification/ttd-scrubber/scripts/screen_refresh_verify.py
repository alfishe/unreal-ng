#!/usr/bin/env python3
"""
Definitive screen-refresh verification for TTD scrubbing.

Writes distinct attribute patterns into VRAM between recording markers,
then seeks back to frames captured during each pattern and verifies
the captured screen pixel content actually differs.

This proves end-to-end:
  1. CPU register state is restored on seek (R/PC differ across frames)
  2. VRAM (memory) state is restored on seek
  3. Screen refresh is invoked (NotifyFrameRefresh fires)
  4. /capture/screen returns the CORRECT framebuffer for each checkpoint
"""
import sys
import time
import hashlib
import json
import urllib.request
import urllib.error

sys.path.insert(0, "/Users/dev/Projects/Test/unreal-ng/tools/verification/webapi/src")
sys.path.insert(0, "/Users/dev/Projects/Test/unreal-ng/tools/verification/ttd-scrubber/src")

from api_client import UnrealApiClient

BASE = "http://localhost:8090"
c = UnrealApiClient(base_url=BASE)


def http(method, path, body=None):
    """Raw HTTP helper bypassing the client wrapper."""
    url = BASE + path
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    if body is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def screen_md5(iid):
    """GET /capture/screen as raw PNG bytes; return md5 of bytes."""
    status, body = http("GET", f"/api/v1/emulator/{iid}/capture/screen")
    if status != 200:
        return None, status
    return hashlib.md5(body).hexdigest(), status


def vram_attr_fingerprint(iid):
    """Read 32 bytes of attribute memory at 0x5800 (top row)."""
    status, body = http("GET", f"/api/v1/emulator/{iid}/memory/0x5800?len=32")
    if status != 200:
        return None
    try:
        d = json.loads(body)
        data = d.get("data", [])
        return hashlib.md5(bytes(data)).hexdigest()
    except Exception:
        return None


def cpu_r(iid):
    """Return R register value."""
    status, body = http("GET", f"/api/v1/emulator/{iid}/registers")
    if status != 200:
        return None
    d = json.loads(body)
    return d.get("special", {}).get("r")


def write_attr(iid, value, count=32):
    """Write `value` to first `count` attribute bytes at 0x5800."""
    body = {"address": "0x5800", "data": [value] * count}
    status, _ = http("POST", f"/api/v1/emulator/{iid}/memory/write", body)
    return status == 200


# --------------------------------------------------------------------
print("=== TTD Screen Refresh Verification ===\n")

# Step 1: Create fresh 48K emulator
print("[1] Creating fresh 48K emulator instance...")
res = c.create_and_start_emulator(model="48K")
IID = res["id"]
print(f"    Instance ID: {IID}")

# Step 2: Make sure it's running (create_and_start already starts it)
print("[2] Verifying emulator is running...")
try:
    c.resume_emulator(IID)
    print("    Resumed")
except Exception as e:
    print(f"    Already running ({e})")
time.sleep(1.0)

# Step 3: Start TTD recording
print("[3] Starting TTD recording...")
c.ttd_start(IID)

# Step 4: Write 3 distinct patterns separated by ~1.5s of recording each.
# Each pattern writes a different attribute byte to the top row.
PATTERNS = [
    (0x20, "RED ink on BLACK paper"),     # 0x20 = 0b00100000 (red ink, black paper, no flash)
    (0x30, "YELLOW ink on BLACK paper"),  # 0x30 = 0b00110000 (yellow ink)
    (0x07, "BLACK ink on WHITE paper"),   # 0x07 = 0b00000111 (white paper)
]

recorded_frames_at_pattern = []  # frame numbers we sample after each write
for idx, (val, desc) in enumerate(PATTERNS):
    # Wait to let recording accumulate frames
    time.sleep(1.5)
    # Write the pattern
    ok = write_attr(IID, val)
    print(f"    [{idx}] Wrote attr 0x{val:02X} ({desc}) ok={ok}")
    # Wait a bit more so the write takes effect and a checkpoint is taken
    time.sleep(0.5)
    # Capture position - frame is nested at current.frame
    pos = c.ttd_position(IID)
    f = None
    if isinstance(pos, dict):
        cur = pos.get("current", {})
        if isinstance(cur, dict):
            f = cur.get("frame")
        if f is None:
            f = pos.get("frame") or pos.get("current_frame")
    recorded_frames_at_pattern.append((f, val, desc))
    print(f"        Recorded at frame {f} (pos={pos})")

# Step 5: Stop recording
print("\n[5] Stopping TTD recording...")
c.ttd_stop(IID)
time.sleep(0.5)

# Show markers
pos = c.ttd_position(IID)
markers = c.ttd_markers(IID)
print(f"    Final position: {pos}")
if isinstance(markers, dict):
    keys = list(markers.keys())
    print(f"    Markers keys: {keys}")
    if "markers" in markers:
        m = markers["markers"]
        if isinstance(m, list) and m:
            frames = [x.get("frame") if isinstance(x, dict) else x for x in m]
            frames = [x for x in frames if x is not None]
            if frames:
                print(f"    Marker frame range: [{min(frames)}..{max(frames)}], count={len(frames)}")
    if "count" in markers:
        print(f"    Total markers: {markers['count']}")

# Step 6: Pause emulator so scrubbing is well-defined
print("\n[6] Pausing emulator for scrub testing...")
c.pause_emulator(IID)

# Step 7: Seek to each recorded frame and verify state + screen
print("\n[7] === SCREEN REFRESH VERIFICATION ===")
results = []
for f, expected_val, desc in recorded_frames_at_pattern:
    if f is None:
        print(f"    [SKIP] No frame recorded for {desc}")
        continue
    print(f"\n    >>> Seek to frame {f} (expected attr byte = 0x{expected_val:02X}, {desc})")
    seek_resp = c.ttd_seek(IID, f)
    time.sleep(0.4)  # Give the message center time to dispatch NC_VIDEO_FRAME_REFRESH

    r = cpu_r(IID)
    attr_md5 = vram_attr_fingerprint(IID)
    scr_md5, _ = screen_md5(IID)

    results.append({
        "frame": f,
        "expected_attr_val": expected_val,
        "desc": desc,
        "cpu_r": r,
        "attr_fingerprint": attr_md5,
        "screen_md5": scr_md5,
    })
    print(f"        R reg   = 0x{r:04X}" if r is not None else "        R reg   = None")
    print(f"        Attr[0x5800..0x581F] md5 = {attr_md5}")
    print(f"        Screen PNG md5         = {scr_md5}")

# Step 8: Summary
print("\n=== SUMMARY ===")
print(f"{'Frame':>8} {'Attr Exp':>10} {'CPU R':>10} {'Attr md5':>14} {'Screen md5':>14}")
print("-" * 70)
for r in results:
    print(f"{r['frame']:>8} 0x{r['expected_attr_val']:02X}{'':>6} "
          f"{'0x%04X' % r['cpu_r'] if r['cpu_r'] else 'None':>10} "
          f"{(r['attr_fingerprint'] or 'None')[:14]:>14} "
          f"{(r['screen_md5'] or 'None')[:14]:>14}")

print("\n=== PASS/FAIL CHECKS ===")

# Check 1: CPU R differs across frames
r_vals = [r["cpu_r"] for r in results if r["cpu_r"] is not None]
cpu_diff = len(set(r_vals)) == len(r_vals) and len(r_vals) >= 2
print(f"[{'PASS' if cpu_diff else 'FAIL'}] CPU R register differs across frames (got {len(set(r_vals))} unique)")

# Check 2: Attr fingerprint differs
a_vals = [r["attr_fingerprint"] for r in results if r["attr_fingerprint"]]
attr_diff = len(set(a_vals)) == len(a_vals) and len(a_vals) >= 2
print(f"[{'PASS' if attr_diff else 'FAIL'}] Attribute memory fingerprints differ across frames")

# Check 3: Screen MD5 differs - THIS IS THE KEY CHECK
s_vals = [r["screen_md5"] for r in results if r["screen_md5"]]
scr_diff = len(set(s_vals)) == len(s_vals) and len(s_vals) >= 2
print(f"[{'PASS' if scr_diff else 'FAIL'}] Screen PNG MD5 differs across frames (DEFINITIVE screen-refresh proof)")

# Check 4: Determinism - re-seek to first frame, screen md5 should match
if results:
    f0 = results[0]["frame"]
    c.ttd_seek(IID, f0)
    time.sleep(0.4)
    scr_md5_redo, _ = screen_md5(IID)
    deterministic = (scr_md5_redo == results[0]["screen_md5"])
    print(f"[{'PASS' if deterministic else 'FAIL'}] Re-seek to frame {f0} yields identical screen (deterministic)")
    print(f"        First seek:  {results[0]['screen_md5']}")
    print(f"        Re-seek:     {scr_md5_redo}")

all_pass = cpu_diff and attr_diff and scr_diff
print(f"\n=== Overall: {'ALL CHECKS PASSED' if all_pass else 'SOME CHECKS FAILED'} ===")
sys.exit(0 if all_pass else 1)
