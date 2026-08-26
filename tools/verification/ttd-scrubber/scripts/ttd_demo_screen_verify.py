#!/usr/bin/env python3
"""
TTD screen-refresh verification using a REAL animated demo snapshot.

Previous attempt failed because WebAPI memory writes go through
DirectWriteToZ80Memory, which bypasses the TTD dirty tracker. Demo
snapshots animate via CPU-executed code, whose writes go through
MemoryWriteFast/MemoryWriteDebug -> MarkDirty -> captured by TTD.

Strategy:
  1. Load Binary Love I (animated ZX Spectrum demo)
  2. Record for several seconds while it animates
  3. Sample frame positions + screen MD5s at various points
  4. Stop recording, seek back to each sampled frame
  5. Verify the seeked screen MD5 matches the live sample MD5
  6. Verify different frames produce different screens
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

DEMO_PATH = "/Users/dev/Projects/Test/unreal-ng/data/testsnapshots/z80/Binary Love I.z80"


def http(method, path, body=None):
    url = BASE + path
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    if body is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def get_screen_md5(iid):
    st, body = http("GET", f"/api/v1/emulator/{iid}/capture/screen")
    if st != 200:
        return None, st
    return hashlib.md5(body).hexdigest(), st


def get_cpu(iid):
    st, body = http("GET", f"/api/v1/emulator/{iid}/registers")
    if st != 200:
        return None, None, None
    d = json.loads(body)
    sp = d.get("special", {})
    main = d.get("main", {})
    return sp.get("r"), sp.get("pc"), main.get("hl")


def get_pos(iid):
    pos = c.ttd_position(iid)
    if isinstance(pos, dict):
        cur = pos.get("current", {})
        if isinstance(cur, dict):
            return cur.get("frame")
    return None


def get_vram_fingerprint(iid):
    """Read first 256 bytes of pixel memory + 32 bytes of attr."""
    pix_st, pix_body = http("GET", f"/api/v1/emulator/{iid}/memory/0x4000?len=256")
    attr_st, attr_body = http("GET", f"/api/v1/emulator/{iid}/memory/0x5800?len=32")
    if pix_st != 200 or attr_st != 200:
        return None
    pix = json.loads(pix_body).get("data", [])
    attr = json.loads(attr_body).get("data", [])
    return hashlib.md5(bytes(pix + attr)).hexdigest()


def snapshot_state(iid, label):
    """Sample current state."""
    f = get_pos(iid)
    r, pc, hl = get_cpu(iid)
    scr, _ = get_screen_md5(iid)
    vram = get_vram_fingerprint(iid)
    r_str = f"0x{r:04X}" if r is not None else "None"
    pc_str = f"0x{pc:04X}" if pc is not None else "None"
    scr_str = scr[:12] if scr else "None"
    vram_str = vram[:12] if vram else "None"
    print(f"    [{label}] frame={f}, R={r_str}, PC={pc_str}, scr={scr_str}, vram={vram_str}")
    return {"label": label, "frame": f, "r": r, "pc": pc, "hl": hl, "scr": scr, "vram": vram}


# ============================================================
print("=== TTD Screen Refresh Verification (Binary Love I demo) ===\n")

# 1. Create 128K instance (Binary Love needs 128K)
print("[1] Creating 128K emulator instance...")
res = c.create_and_start_emulator(model="128K")
IID = res["id"]
print(f"    ID: {IID}")
time.sleep(0.5)

# 2. Load the demo snapshot
print(f"[2] Loading Binary Love I demo snapshot...")
st, body = http("POST", f"/api/v1/emulator/{IID}/snapshot/load", {"path": DEMO_PATH})
print(f"    Status: {st}, Response: {body[:200]}")
if st != 200:
    print("    FAILED to load snapshot")
    sys.exit(1)
time.sleep(1.0)

# 3. Tap SPACE to start the demo (Binary Love shows intro until keypress)
print("\n[3] Tapping SPACE/ENTER to start demo...")
http("POST", f"/api/v1/emulator/{IID}/keyboard/tap", {"key": "Space", "frames": 3})
time.sleep(0.5)
http("POST", f"/api/v1/emulator/{IID}/keyboard/tap", {"key": "Space", "frames": 3})
time.sleep(0.5)
http("POST", f"/api/v1/emulator/{IID}/keyboard/tap", {"key": "Enter", "frames": 3})
time.sleep(1.5)

# 4. Sample initial screen to make sure demo is animating
print("[4] Verifying demo animates (screen should change over time)...")
scr1, _ = get_screen_md5(IID)
time.sleep(1.0)
scr2, _ = get_screen_md5(IID)
time.sleep(1.0)
scr3, _ = get_screen_md5(IID)
print(f"    t=0s: {scr1[:16]}")
print(f"    t=1s: {scr2[:16]}")
print(f"    t=2s: {scr3[:16]}")
if scr1 == scr2 == scr3:
    print("    WARNING: Screen is still static. Demo may need different trigger.")
    print("    Continuing anyway - seek determinism check still meaningful.")

# 5. Start TTD recording
print("\n[5] Starting TTD recording...")
c.ttd_start(IID)
time.sleep(0.3)

# 6. Sample state at multiple points during recording
print("\n[6] Recording samples at various points...")
samples = []
for i in range(5):
    time.sleep(1.5)
    s = snapshot_state(IID, f"REC_{i}")
    samples.append(s)

# 7. Stop recording
print("\n[7] Stopping TTD recording...")
c.ttd_stop(IID)
time.sleep(0.5)

markers = c.ttd_markers(IID)
if isinstance(markers, dict):
    print(f"    Total markers: {markers.get('count', 0)}")
    m = markers.get("markers", [])
    if m:
        frames = [x.get("frame") if isinstance(x, dict) else x for x in m]
        frames = [x for x in frames if x is not None]
        if frames:
            print(f"    Marker frame range: [{min(frames)}..{max(frames)}]")

# 8. Pause for scrub testing
print("\n[8] Pausing emulator...")
c.pause_emulator(IID)
time.sleep(0.3)

# 9. Seek to each sampled frame and verify
print("\n[9] === SEEK VERIFICATION ===")
seek_results = []
for s in samples:
    if s["frame"] is None:
        print(f"    [SKIP] {s['label']}: no frame")
        continue
    print(f"\n    >>> {s['label']}: seek to frame {s['frame']}")
    print(f"        Expected (live): scr={s['scr'][:16]}, vram={s['vram'][:16]}, R=0x{s['r']:04X}, PC=0x{s['pc']:04X}")
    try:
        c.ttd_seek(IID, s["frame"])
    except Exception as e:
        print(f"        SEEK FAILED: {e}")
        continue
    time.sleep(0.5)  # let NC_VIDEO_FRAME_REFRESH dispatch

    r_after, pc_after, _ = get_cpu(IID)
    scr_after, _ = get_screen_md5(IID)
    vram_after = get_vram_fingerprint(IID)
    r_str = f"0x{r_after:04X}" if r_after is not None else "None"
    pc_str = f"0x{pc_after:04X}" if pc_after is not None else "None"
    scr_str = scr_after[:16] if scr_after else "None"
    vram_str = vram_after[:16] if vram_after else "None"
    print(f"        Got (after seek): scr={scr_str}, vram={vram_str}, R={r_str}, PC={pc_str}")

    scr_match = scr_after == s["scr"]
    vram_match = vram_after == s["vram"]
    r_match = r_after == s["r"]
    print(f"        Screen match: {scr_match}, VRAM match: {vram_match}, R match: {r_match}")
    seek_results.append({
        "label": s["label"], "frame": s["frame"],
        "live_scr": s["scr"], "seek_scr": scr_after, "scr_match": scr_match,
        "live_vram": s["vram"], "seek_vram": vram_after, "vram_match": vram_match,
        "live_r": s["r"], "seek_r": r_after, "r_match": r_match,
    })

# 10. Determinism: re-seek to first sample
if seek_results:
    first = seek_results[0]
    print(f"\n[10] Determinism: re-seek to {first['label']} (frame {first['frame']})")
    c.ttd_seek(IID, first["frame"])
    time.sleep(0.5)
    scr_redo, _ = get_screen_md5(IID)
    print(f"    First seek:  {first['seek_scr'][:16]}")
    print(f"    Re-seek:     {scr_redo[:16]}")
    det = (scr_redo == first["seek_scr"])
    print(f"    Deterministic? {det}")

# 11. Summary
print("\n=== SUMMARY ===")
print(f"{'Label':<10} {'Frame':>8} {'LiveScr':>18} {'SeekScr':>18} {'Match':>6} {'VRAM':>6} {'R':>4}")
print("-" * 80)
for r in seek_results:
    print(f"{r['label']:<10} {r['frame']:>8} "
          f"{(r['live_scr'] or 'None')[:18]:>18} "
          f"{(r['seek_scr'] or 'None')[:18]:>18} "
          f"{'Y' if r['scr_match'] else 'N':>6} "
          f"{'Y' if r['vram_match'] else 'N':>6} "
          f"{'Y' if r['r_match'] else 'N':>4}")

print("\n=== VERDICT ===")
# Check 1: Live screens differed during recording (proves demo animates)
live_scrs = [r["live_scr"] for r in seek_results if r["live_scr"]]
live_unique = len(set(live_scrs))
print(f"[{'PASS' if live_unique >= 2 else 'FAIL'}] Demo animated during recording ({live_unique} unique live screens)")

# Check 2: Seek screens differ across frames
seek_scrs = [r["seek_scr"] for r in seek_results if r["seek_scr"]]
seek_unique = len(set(seek_scrs))
print(f"[{'PASS' if seek_unique >= 2 else 'FAIL'}] Seeked screens differ across frames ({seek_unique} unique)")

# Check 3: Seek screen matches live sample (THIS IS THE KEY)
matches = sum(1 for r in seek_results if r["scr_match"])
total = len(seek_results)
print(f"[{'PASS' if matches == total else 'FAIL'}] Seeked screen matches live sample at recording time ({matches}/{total})")

# Check 4: VRAM matches
vram_matches = sum(1 for r in seek_results if r["vram_match"])
print(f"[{'PASS' if vram_matches == total else 'FAIL'}] Seeked VRAM matches live VRAM ({vram_matches}/{total})")

# Check 5: CPU R matches
r_matches = sum(1 for r in seek_results if r["r_match"])
print(f"[{'PASS' if r_matches == total else 'FAIL'}] Seeked CPU R matches live ({r_matches}/{total})")

overall = (live_unique >= 2 and seek_unique >= 2 and matches == total and vram_matches == total)
print(f"\n=== Overall: {'SCREEN REFRESH VERIFIED - seek restores animated frames correctly' if overall else 'PARTIAL - see above'} ===")
sys.exit(0 if overall else 1)
