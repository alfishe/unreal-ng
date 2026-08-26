#!/usr/bin/env python3
"""
Final end-to-end TTD verification: WebAPI writes to VRAM are captured AND
produce different rendered screens on seek.

This proves the complete fix chain:
  1. WebAPI POST /memory/write -> DirectWriteToZ80Memory
  2. DirectWriteToZ80Memory now calls MarkDirty
  3. MarkDirty marks the VRAM page dirty
  4. Next OnFrameBoundary captures the VRAM page in a checkpoint
  5. Seek restores the VRAM page AND rebuilds the framebuffer
  6. Screen pixel MD5 reflects the restored VRAM content
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

# Attribute bytes (0x5800-0x5AFF) are part of VRAM and directly affect
# the rendered screen colors. Writing different values here will produce
# different pixel content.
ATTR_BASE = 0x5800


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


def read_byte(iid, addr):
    st, body = http("GET", f"/api/v1/emulator/{iid}/memory/0x{addr:X}?len=1")
    if st != 200:
        return None
    return json.loads(body).get("data", [None])[0]


def write_block(iid, addr, values):
    body = {"address": f"0x{addr:X}", "data": values}
    st, _ = http("POST", f"/api/v1/emulator/{iid}/memory/write", body)
    return st == 200


def screen_md5(iid):
    st, body = http("GET", f"/api/v1/emulator/{iid}/capture/screen")
    return hashlib.md5(body).hexdigest() if st == 200 else None


def get_pos(iid):
    pos = c.ttd_position(iid)
    if isinstance(pos, dict):
        cur = pos.get("current", {})
        if isinstance(cur, dict):
            return cur.get("frame")
    return None


def get_cpu(iid):
    st, body = http("GET", f"/api/v1/emulator/{iid}/registers")
    if st != 200:
        return None, None
    d = json.loads(body)
    sp = d.get("special", {})
    return sp.get("r"), sp.get("pc")


print("=== WebAPI VRAM Write -> TTD Capture -> Seek Restore ===\n")

# Create 48K instance
print("[1] Creating 48K emulator...")
res = c.create_and_start_emulator(model="48K")
IID = res["id"]
print(f"    ID: {IID}")
time.sleep(1.0)

# Start recording
print("[2] Starting TTD recording...")
c.ttd_start(IID)
time.sleep(0.5)

# Write 4 distinct attribute patterns
# Attribute byte format: PPIIIIIF (paper, ink, flash)
# We'll write a solid color block to make the screen visibly different.
PATTERNS = [
    (0x10, "BLUE ink on BLACK paper"),    # 0b00010000
    (0x20, "RED ink on BLACK paper"),     # 0b00100000
    (0x30, "YELLOW ink on BLACK paper"),  # 0b00110000
    (0x47, "BLACK ink on GREEN paper"),   # 0b01000111
]

# Fill first 32 attribute bytes (first row of screen)
samples = []
for idx, (val, desc) in enumerate(PATTERNS):
    # Write 32 bytes of the pattern
    ok = write_block(IID, ATTR_BASE, [val] * 32)
    time.sleep(0.1)
    # Verify write took
    live_first = read_byte(IID, ATTR_BASE)
    # Sample full state
    f = get_pos(IID)
    r, pc = get_cpu(IID)
    scr = screen_md5(IID)
    print(f"    [{idx}] Wrote 0x{val:02X} ({desc})")
    print(f"        Live at frame {f}: attr[0]={live_first:02X}, R=0x{r:04X}, PC=0x{pc:04X}, scr={scr[:16]}")
    samples.append({"frame": f, "val": val, "desc": desc, "live_attr": live_first, "live_r": r, "live_scr": scr})
    # Wait for recording to advance
    time.sleep(1.0)

# Stop and pause
print("\n[3] Stopping recording and pausing...")
c.ttd_stop(IID)
time.sleep(0.3)
c.pause_emulator(IID)
time.sleep(0.3)

# Seek and verify
print("\n[4] === SEEK VERIFICATION ===\n")
results = []
for s in samples:
    if s["frame"] is None:
        continue
    print(f"    >>> Seek to frame {s['frame']} (expected attr=0x{s['val']:02X}, {s['desc']})")
    c.ttd_seek(IID, s["frame"])
    time.sleep(0.5)
    got_attr = read_byte(IID, ATTR_BASE)
    got_r, _ = get_cpu(IID)
    got_scr = screen_md5(IID)
    print(f"        Got: attr={got_attr:02X}, R=0x{got_r:04X}, scr={got_scr[:16]}")
    attr_match = got_attr == s["val"]
    scr_match = got_scr == s["live_scr"]
    r_match = got_r == s["live_r"]
    print(f"        attr match: {attr_match}, screen match: {scr_match}, R match: {r_match}")
    results.append({
        "frame": s["frame"], "val": s["val"], "desc": s["desc"],
        "got_attr": got_attr, "attr_match": attr_match,
        "got_scr": got_scr, "live_scr": s["live_scr"], "scr_match": scr_match,
        "got_r": got_r, "live_r": s["live_r"], "r_match": r_match,
    })

# Summary
print("\n=== SUMMARY ===")
print(f"{'Pattern':<32} {'Frame':>6} {'Attr':>6} {'ScrMatch':>9} {'RMatch':>7}")
print("-" * 70)
for r in results:
    print(f"{r['desc']:<32} {r['frame']:>6} "
          f"{'OK' if r['attr_match'] else 'FAIL':>6} "
          f"{'Y' if r['scr_match'] else 'N':>9} "
          f"{'Y' if r['r_match'] else 'N':>7}")

print("\n=== VERDICT ===")
attr_ok = all(r["attr_match"] for r in results)
scr_unique = len({r["got_scr"] for r in results}) == len(results)
live_unique = len({r["live_scr"] for r in results}) == len(results)
scr_match = all(r["scr_match"] for r in results)

print(f"[{'PASS' if attr_ok else 'FAIL'}] WebAPI VRAM writes restored on seek")
print(f"[{'PASS' if live_unique else 'FAIL'}] Live screens differed during recording (4 patterns visible)")
print(f"[{'PASS' if scr_unique else 'FAIL'}] Seeked screens differ across frames")
print(f"[{'PASS' if scr_match else 'FAIL'}] Seeked screen matches live sample at recording time")

overall = attr_ok and live_unique and scr_unique and scr_match
print(f"\n=== {'FULL CHAIN VERIFIED: WebAPI write -> MarkDirty -> capture -> seek restore -> screen refresh' if overall else 'PARTIAL - see above'} ===")
sys.exit(0 if overall else 1)
