#!/usr/bin/env python3
"""
Clean TTD memory restoration test.

The previous test was flawed: writes during pause don't trigger
OnFrameBoundary, so no checkpoint captures them. Seek correctly
returns the most recent pre-write checkpoint state.

This test:
  1. Uses address 0x8000 (bank 2, untouched by ROM)
  2. Writes while emulator is RUNNING (so checkpoints capture them)
  3. Reads live state at each step
  4. Seeks back to those frames and verifies memory matches
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


def read_byte(iid, addr):
    """Read a single byte from memory."""
    st, body = http("GET", f"/api/v1/emulator/{iid}/memory/0x{addr:X}?len=1")
    if st != 200:
        return None
    d = json.loads(body)
    data = d.get("data", [])
    return data[0] if data else None


def write_byte(iid, addr, val):
    body = {"address": f"0x{addr:X}", "data": [val]}
    st, _ = http("POST", f"/api/v1/emulator/{iid}/memory/write", body)
    return st == 200


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


def get_screen_md5(iid):
    st, body = http("GET", f"/api/v1/emulator/{iid}/capture/screen")
    return hashlib.md5(body).hexdigest() if st == 200 else None


# Use address 0x8000 — bank 2, not touched by ROM at idle
ADDR = 0x8000

print("=== Clean TTD memory restoration test ===\n")

# 1. Create 48K instance
print("[1] Creating 48K emulator...")
res = c.create_and_start_emulator(model="48K")
IID = res["id"]
print(f"    ID: {IID}")
time.sleep(0.8)

# 2. Pre-flight: read default at 0x8000, then write a sentinel
default_val = read_byte(IID, ADDR)
print(f"[2] Default value at 0x{ADDR:X} = 0x{default_val:02X}" if default_val is not None else f"[2] Default at 0x{ADDR:X} = None")

# 3. Start TTD recording
print("[3] Starting TTD recording...")
c.ttd_start(IID)
time.sleep(0.5)

# 4. ERA A: let it record for 1s with default value at 0x8000
print("[4] ERA A: recording 1s with default value at 0x8000...")
time.sleep(1.0)
frame_a = get_pos(IID)
val_a = read_byte(IID, ADDR)
r_a, pc_a = get_cpu(IID)
scr_a = get_screen_md5(IID)
print(f"    Frame {frame_a}: 0x{ADDR:X}=0x{val_a:02X}, R=0x{r_a:04X}, PC=0x{pc_a:04X}, scr={scr_a[:12] if scr_a else 'None'}")

# 5. ERA B: write 0xAA, let it record 1s
print("[5] ERA B: writing 0xAA to 0x8000, recording 1s...")
write_byte(IID, ADDR, 0xAA)
val_check = read_byte(IID, ADDR)
print(f"    Live read after write: 0x{val_check:02X}")
time.sleep(1.0)
frame_b = get_pos(IID)
val_b = read_byte(IID, ADDR)
r_b, pc_b = get_cpu(IID)
scr_b = get_screen_md5(IID)
print(f"    Frame {frame_b}: 0x{ADDR:X}=0x{val_b:02X}, R=0x{r_b:04X}, PC=0x{pc_b:04X}, scr={scr_b[:12] if scr_b else 'None'}")

# 6. ERA C: write 0xBB, let it record 1s
print("[6] ERA C: writing 0xBB to 0x8000, recording 1s...")
write_byte(IID, ADDR, 0xBB)
val_check = read_byte(IID, ADDR)
print(f"    Live read after write: 0x{val_check:02X}")
time.sleep(1.0)
frame_c = get_pos(IID)
val_c = read_byte(IID, ADDR)
r_c, pc_c = get_cpu(IID)
scr_c = get_screen_md5(IID)
print(f"    Frame {frame_c}: 0x{ADDR:X}=0x{val_c:02X}, R=0x{r_c:04X}, PC=0x{pc_c:04X}, scr={scr_c[:12] if scr_c else 'None'}")

# 7. ERA D: write 0xCC, let it record 1s
print("[7] ERA D: writing 0xCC to 0x8000, recording 1s...")
write_byte(IID, ADDR, 0xCC)
val_check = read_byte(IID, ADDR)
print(f"    Live read after write: 0x{val_check:02X}")
time.sleep(1.0)
frame_d = get_pos(IID)
val_d = read_byte(IID, ADDR)
r_d, pc_d = get_cpu(IID)
scr_d = get_screen_md5(IID)
print(f"    Frame {frame_d}: 0x{ADDR:X}=0x{val_d:02X}, R=0x{r_d:04X}, PC=0x{pc_d:04X}, scr={scr_d[:12] if scr_d else 'None'}")

# 8. Stop recording
print("\n[8] Stopping TTD recording...")
c.ttd_stop(IID)
time.sleep(0.3)

markers = c.ttd_markers(IID)
if isinstance(markers, dict):
    print(f"    Markers: {markers.get('count', 0)} total")

# 9. Pause emulator for scrub testing
print("\n[9] Pausing emulator for scrub testing...")
c.pause_emulator(IID)
time.sleep(0.3)

# 10. Seek to each frame and check what's there
print("\n[10] === SEEK AND VERIFY ===\n")
sample_results = []
samples = [
    ("ERA_A", frame_a, val_a, r_a, pc_a, scr_a),
    ("ERA_B", frame_b, val_b, r_b, pc_b, scr_b),
    ("ERA_C", frame_c, val_c, r_c, pc_c, scr_c),
    ("ERA_D", frame_d, val_d, r_d, pc_d, scr_d),
]
for label, target_frame, expected_val, expected_r, expected_pc, expected_scr in samples:
    if target_frame is None:
        print(f"    [SKIP] {label}: no frame recorded")
        continue
    print(f"    >>> {label}: seek to frame {target_frame}")
    print(f"        Expected: val=0x{expected_val:02X}, R=0x{expected_r:04X}, PC=0x{expected_pc:04X}, scr={expected_scr[:12] if expected_scr else 'None'}")
    c.ttd_seek(IID, target_frame)
    time.sleep(0.4)
    got_val = read_byte(IID, ADDR)
    got_r, got_pc = get_cpu(IID)
    got_scr = get_screen_md5(IID)
    print(f"        Got:      val=0x{got_val:02X}, R=0x{got_r:04X}, PC=0x{got_pc:04X}, scr={got_scr[:12] if got_scr else 'None'}")
    mem_match = got_val == expected_val
    print(f"        Memory matches? {'YES' if mem_match else 'NO'}")
    sample_results.append({
        "label": label, "frame": target_frame,
        "expected_val": expected_val, "got_val": got_val,
        "mem_match": mem_match,
        "expected_scr": expected_scr, "got_scr": got_scr,
        "scr_match": expected_scr == got_scr,
    })

# 11. Determinism check: re-seek to ERA_B
print(f"\n[11] Determinism: re-seek to ERA_B (frame {frame_b})")
c.ttd_seek(IID, frame_b)
time.sleep(0.4)
got_val = read_byte(IID, ADDR)
got_scr = get_screen_md5(IID)
b_result = next((r for r in sample_results if r["label"] == "ERA_B"), None)
if b_result:
    print(f"    First seek:  val=0x{b_result['got_val']:02X}, scr={b_result['got_scr'][:12] if b_result['got_scr'] else 'None'}")
    print(f"    Re-seek:     val=0x{got_val:02X}, scr={got_scr[:12] if got_scr else 'None'}")
    deterministic = (got_val == b_result['got_val'] and got_scr == b_result['got_scr'])
    print(f"    Deterministic? {'YES' if deterministic else 'NO'}")

# 12. Summary
print("\n=== SUMMARY ===")
print(f"{'Era':<8} {'Frame':>8} {'Exp':>6} {'Got':>6} {'Match':>6}")
print("-" * 40)
for r in sample_results:
    print(f"{r['label']:<8} {r['frame']:>8} 0x{r['expected_val']:02X}{'':>2} 0x{r['got_val']:02X}{'':>2} {'Y' if r['mem_match'] else 'N':>6}")

print("\n=== VERDICT ===")
mem_vals = [r["got_val"] for r in sample_results]
mem_unique = len(set(mem_vals)) == len(mem_vals)
mem_correct = all(r["mem_match"] for r in sample_results)
scr_unique = len({r["got_scr"] for r in sample_results}) == len(sample_results)
print(f"[{'PASS' if mem_unique else 'FAIL'}] Memory at 0x{ADDR:X} differs across seeked frames ({len(set(mem_vals))} unique values)")
print(f"[{'PASS' if mem_correct else 'FAIL'}] Seeked memory matches what was live at recording time")
print(f"[{'PASS' if scr_unique else 'FAIL'}] Screen md5 differs across seeked frames ({len(set(r['got_scr'] for r in sample_results))} unique)")

all_ok = mem_unique and mem_correct
print(f"\n=== Overall: {'DEFINITIVE PROOF: seek restores memory state' if all_ok else 'PARTIAL — see above'} ===")
sys.exit(0 if all_ok else 1)
