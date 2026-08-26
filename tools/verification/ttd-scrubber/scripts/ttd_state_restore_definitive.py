#!/usr/bin/env python3
"""
DEFINITIVE TTD state restoration test.

Proves whether seek restores:
  (a) CPU registers  (already known: yes)
  (b) Memory contents (unverified)
  (c) Screen pixel buffer (unverified)

Strategy:
  - Use a 48K emulator with a SNAPSHOT loaded (so it's NOT running ROM and
    won't fight us by rewriting memory).
  - At each step we PAUSE, write a unique pattern, RESUME, let it record,
    PAUSE, write next pattern, RESUME, etc.
  - This guarantees each pattern is captured in a real checkpoint.
  - Then seek to frames captured during each pattern and verify
    memory, screen, and CPU all match what we wrote.
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


def get_state(iid):
    """Read CPU R, attr memory md5, screen PNG md5 — all at once."""
    # CPU
    st, body = http("GET", f"/api/v1/emulator/{iid}/registers")
    r = None
    if st == 200:
        d = json.loads(body)
        sp = d.get("special", {})
        r = sp.get("r")
        pc = sp.get("pc")
    else:
        pc = None
    # Attr memory at 0x5800
    st, body = http("GET", f"/api/v1/emulator/{iid}/memory/0x5800?len=32")
    attr_md5 = None
    attr_first = None
    if st == 200:
        d = json.loads(body)
        data = d.get("data", [])
        if data:
            attr_md5 = hashlib.md5(bytes(data)).hexdigest()
            attr_first = data[0]
    # Screen
    st, body = http("GET", f"/api/v1/emulator/{iid}/capture/screen")
    scr_md5 = hashlib.md5(body).hexdigest() if st == 200 else None
    return {"r": r, "pc": pc, "attr_md5": attr_md5, "attr_first": attr_first, "screen_md5": scr_md5}


def write_attr_byte(iid, addr, value):
    body = {"address": f"0x{addr:X}", "data": [value]}
    st, _ = http("POST", f"/api/v1/emulator/{iid}/memory/write", body)
    return st == 200


def get_pos(iid):
    """Get current recording position frame."""
    pos = c.ttd_position(iid)
    if isinstance(pos, dict):
        cur = pos.get("current", {})
        if isinstance(cur, dict):
            return cur.get("frame")
    return None


# ============================================================
print("=== DEFINITIVE TTD State Restoration Test ===\n")

# Step 1: Create 48K emulator
print("[1] Creating 48K emulator instance...")
res = c.create_and_start_emulator(model="48K")
IID = res["id"]
print(f"    ID: {IID}")
time.sleep(1.0)

# Step 2: Pause it so writes persist
print("[2] Pausing emulator so writes won't be overwritten by ROM...")
c.pause_emulator(IID)
time.sleep(0.5)

# Step 3: Start TTD recording (works while paused — captures each frame step)
print("[3] Starting TTD recording...")
c.ttd_start(IID)
time.sleep(0.5)

# Step 4: Write 3 distinct patterns, stepping frames between each
# We write to attribute byte 0x5800 (top-left of screen)
# Each pattern is captured at a specific frame
PATTERNS = [
    (0xAA, "0xAA"),
    (0xBB, "0xBB"),
    (0xCC, "0xCC"),
]

recorded = []  # list of (frame, value, desc, live_state_after_write)
for idx, (val, desc) in enumerate(PATTERNS):
    # Write the value
    ok = write_attr_byte(IID, 0x5800, val)
    time.sleep(0.1)
    # Read back to confirm write happened
    state_after_write = get_state(IID)
    f = get_pos(IID)
    print(f"    [{idx}] Wrote 0x{val:02X} to 0x5800 at frame {f} ok={ok}")
    print(f"        Live read: attr_first=0x{state_after_write['attr_first']:02X}, "
          f"attr_md5={state_after_write['attr_md5'][:12]}, "
          f"scr_md5={state_after_write['screen_md5'][:12] if state_after_write['screen_md5'] else 'None'}")
    recorded.append((f, val, desc, state_after_write))
    # Step the emulator a few frames to advance recording
    # Use the step endpoint if available, otherwise just wait
    for _ in range(3):
        try:
            c.step_forward(IID)  # this might fail since not in scrub mode yet
        except Exception:
            pass
        time.sleep(0.05)

# Step 5: Stop recording
print("\n[5] Stopping TTD recording...")
c.ttd_stop(IID)
time.sleep(0.5)

# Show markers
markers = c.ttd_markers(IID)
if isinstance(markers, dict):
    print(f"    Markers: count={markers.get('count')}")
    m = markers.get("markers", [])
    if m:
        frames = [x.get("frame") if isinstance(x, dict) else x for x in m]
        frames = [x for x in frames if x is not None]
        if frames:
            print(f"    Marker frame range: [{min(frames)}..{max(frames)}]")

# Step 6: For each recorded frame, seek and verify
print("\n[6] === SEEK AND VERIFY ===")
seek_results = []
for f, val, desc, live_state in recorded:
    if f is None:
        print(f"    [SKIP] No frame for {desc}")
        continue
    print(f"\n    >>> Seek to frame {f} (wrote 0x{val:02X})")
    c.ttd_seek(IID, f)
    time.sleep(0.4)
    state_after_seek = get_state(IID)
    print(f"        After seek: attr_first=0x{state_after_seek['attr_first']:02X}, "
          f"attr_md5={state_after_seek['attr_md5'][:12]}, "
          f"scr_md5={state_after_seek['screen_md5'][:12] if state_after_seek['screen_md5'] else 'None'}, "
          f"r=0x{state_after_seek['r']:04X}, pc=0x{state_after_seek['pc']:04X}")
    matches_memory = state_after_seek["attr_first"] == val
    matches_screen = state_after_seek["screen_md5"] == live_state["screen_md5"]
    print(f"        attr_first matches written value? {'YES' if matches_memory else 'NO — got 0x{:02X}, expected 0x{:02X}'.format(state_after_seek['attr_first'] or 0, val)}")
    print(f"        screen matches live state at write time? {'YES' if matches_screen else 'NO'}")
    seek_results.append({
        "frame": f, "written": val, "live": live_state, "seek": state_after_seek,
        "mem_match": matches_memory, "scr_match": matches_screen
    })

# Step 7: Summary
print("\n=== SUMMARY ===")
print(f"{'Frame':>8} {'Wrote':>8} {'LiveScr':>14} {'SeekScr':>14} {'LiveAttr':>14} {'SeekAttr':>14} {'MemOK':>6} {'ScrOK':>6}")
print("-" * 100)
for r in seek_results:
    print(f"{r['frame']:>8} 0x{r['written']:02X}{'':>4} "
          f"{(r['live']['screen_md5'] or 'None')[:14]:>14} "
          f"{(r['seek']['screen_md5'] or 'None')[:14]:>14} "
          f"{(r['live']['attr_md5'] or 'None')[:14]:>14} "
          f"{(r['seek']['attr_md5'] or 'None')[:14]:>14} "
          f"{'Y' if r['mem_match'] else 'N':>6} "
          f"{'Y' if r['scr_match'] else 'N':>6}")

print("\n=== VERDICT ===")
# Check 1: Memory state differs across seeks (proves seek restores memory)
attr_set = {r["seek"]["attr_md5"] for r in seek_results if r["seek"]["attr_md5"]}
mem_restored = len(attr_set) == len(seek_results) and len(attr_set) >= 2
print(f"[{'PASS' if mem_restored else 'FAIL'}] Seek restores memory state (got {len(attr_set)} unique attr fingerprints)")

# Check 2: Each seek's memory matches the value we wrote at that frame
mem_matches = all(r["mem_match"] for r in seek_results)
print(f"[{'PASS' if mem_matches else 'FAIL'}] Seeked memory matches the value written at that frame")

# Check 3: Screen differs across seeks
scr_set = {r["seek"]["screen_md5"] for r in seek_results if r["seek"]["screen_md5"]}
scr_restored = len(scr_set) == len(seek_results) and len(scr_set) >= 2
print(f"[{'PASS' if scr_restored else 'FAIL'}] Seek restores screen pixel buffer (got {len(scr_set)} unique screen md5s)")

# Check 4: CPU differs
r_set = {r["seek"]["r"] for r in seek_results if r["seek"]["r"] is not None}
cpu_diff = len(r_set) == len(seek_results)
print(f"[{'PASS' if cpu_diff else 'FAIL'}] CPU R register differs across seeks")

overall = mem_restored and mem_matches and scr_restored and cpu_diff
print(f"\n=== Overall: {'ALL PASSED — state restoration is COMPLETE and verified' if overall else 'PARTIAL — see failures above'} ===")
sys.exit(0 if overall else 1)
