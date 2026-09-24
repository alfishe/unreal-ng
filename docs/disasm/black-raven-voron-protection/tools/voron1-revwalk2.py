#!/usr/bin/env python3
"""Backward execution analysis v2 - controls the find-last search origin.

TTD technique harness: find-last searches from the CURRENT ttd position and
each hit MOVES the position, so every query must re-seek its origin first
(seeking to the recording end for "last execution per region", or to a fixed
moment for "last before X"). Addresses in journal queries are decimal.
The ERR_TIME / REGIONS constants document the 2026-09-22 journal (frames
0-6361); adjust them per journal before reuse.
usage: voron1-revwalk2.py <emu-id>
"""
import json
import sys
import urllib.error
import urllib.request

ROOT = "http://localhost:8090/api/v1"
OUT = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch/voron1-revwalk2.json"
if len(sys.argv) < 2:
    sys.exit("usage: voron1-revwalk2.py <emu-id>")
EMU = sys.argv[1]
ERR_TIME = (346, 42005)  # example: last exec of 0x1D1A in the 2026-09-22 journal


def call(method, path, body=None):
    url = f"{ROOT}/emulator/{EMU}{path}"
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as ex:
        print(f"  !! {method} {path}: {ex} {ex.read().decode()[:200]}")
        return None


def seek(frame, tin=0):
    r = call("POST", "/ttd/seek", {"frame": frame, "tinframe": tin}) or {}
    return r.get("reached")


def find_exec(lo, hi, before=None, access="execute"):
    q = {"addr_from": lo, "addr_to": hi, "access": access}
    if before:
        q["before_frame"], q["before_tin"] = before
    r = call("POST", "/ttd/find-last", q) or {}
    if r.get("found"):
        return (r["frame"], r.get("tinframe", 0), r.get("pc"), r.get("phys_page"))
    if r.get("blocked"):
        print(f"  BLOCKED by marker: {r.get('marker_kind')} f={r.get('marker_frame')} {r.get('marker_reason')}")
    return None


report = {}

# --- establish origin at the recording end -----------------------------------
st = call("GET", "/ttd/status") or {}
end = st.get("current_end_frame", 0)
print("ttd status: state=%s end_frame=%s checkpoints=%s" %
      (st.get("state"), end, st.get("checkpoint_count")))
if not end:
    sys.exit(1)
ok = seek(max(0, end - 1), 0)
print(f"seek to end ({end - 1},0): {ok}")

# --- Part A: region timeline, fixed origin at end ------------------------------
REGIONS = [
    ("0x1D1A error entry", 0x1D1A, 0x1D1A),
    ("0x1E36 read-address+resync", 0x1E36, 0x1E42),
    ("0x1E43 sector prep / ReadOnly chk", 0x1E43, 0x1E61),
    ("0x1E62 sector prep part2 / dispatch", 0x1E62, 0x1EA0),
    ("0x3D00-0x3FFF any FDC block", 0x3D00, 0x3FFF),
    ("0x3E16 geometry probe", 0x3E16, 0x3E43),
    ("0x3E44 seek helper", 0x3E44, 0x3E62),
    ("0x3E63 drive-prep", 0x3E63, 0x3E7F),
    ("0x3EC7 READ ADDRESS cmd", 0x3EC7, 0x3EC7),
    ("0x3EDD RA timeout FORCE INT", 0x3EDD, 0x3EEF),
    ("0x3EF2 ID completion loop", 0x3EF2, 0x3F05),
    ("0x3F06 store dest+sector#", 0x3F06, 0x3F16),
    ("0x3F17 sector reg load", 0x3F17, 0x3F21),
    ("0x3F22 command OUT (0x80/0xA0)", 0x3F22, 0x3F30),
    ("0x3F31 status/result handling", 0x3F31, 0x3F6F),
    ("0x3FEC INI sector-read loop", 0x3FEC, 0x3FF5),
    ("0x1D80-0x1E35 misc rom", 0x1D80, 0x1E35),
    ("0x5F10 game loader entry", 0x5F10, 0x5F1F),
    ("0x5F74-0x5F7C loader fail helper", 0x5F74, 0x5F7C),
    ("0x6004 loader raw FDC read", 0x6004, 0x6010),
    ("0x6200 next stage", 0x6200, 0x6210),
]
print("== Part A: last execution per region (origin=end) ==")
timeline = []
for name, lo, hi in REGIONS:
    hit = find_exec(lo, hi)
    timeline.append({"region": name, "hit": hit and list(hit)})
    if hit:
        print(f"  {name:36s} LAST f={hit[0]:5d} t={hit[1]:6d} pc={hex(hit[2])}")
    else:
        print(f"  {name:36s} NEVER")
    # restore origin: each hit moved the position - re-seek to end
    seek(max(0, end - 1), 0)
report["timeline"] = timeline

# --- Part B: deepest FDC progress just before the error ------------------------
print("== Part B: last FDC-block exec before the error ==")
seek(ERR_TIME[0], ERR_TIME[1])
for name, lo, hi in [
    ("any 0x3D00-3FFF", 0x3D00, 0x3FFF),
    ("0x3EC7 READ ADDRESS", 0x3EC7, 0x3EC7),
    ("0x3EF2 ID loop", 0x3EF2, 0x3F05),
    ("0x3F17 sector reg", 0x3F17, 0x3F21),
    ("0x3F22 cmd OUT", 0x3F22, 0x3F30),
    ("0x1E62-1EA0 prep2", 0x1E62, 0x1EA0),
    ("0x1E43-1E61 prep1", 0x1E43, 0x1E61),
    ("0x1D80-1E35", 0x1D80, 0x1E35),
]:
    hit = find_exec(lo, hi, before=ERR_TIME)
    msg = f"f={hit[0]} t={hit[1]} pc={hex(hit[2])}" if hit else "NEVER"
    print(f"  {name:22s} -> {msg}")
    report.setdefault("before_error", {})[name] = hit and list(hit)
    seek(ERR_TIME[0], ERR_TIME[1])

# --- Part C: backward instruction walk from the error ---------------------------
print("== Part C: backward walk from error entry ==")
f, t = ERR_TIME
seek(f, t)
walk = []
for i in range(60):
    hit = find_exec(0x0000, 0xFFFF, before=(f, t))
    if hit is None:
        print(f"  walk ended after {i} steps")
        break
    nf, nt, npc, npp = hit
    if (nf, nt) >= (f, t):
        print(f"  inclusive-semantics stall at ({f},{t}); aborting walk")
        break
    walk.append({"i": i + 1, "frame": nf, "tin": nt, "pc": npc, "page": npp})
    f, t = nf, nt
report["walk"] = walk

print("== reverse path (error-first) ==")
for w in walk:
    print(f"   -{w['i']:2d} f={w['frame']:4d} t={w['tin']:6d} pc={hex(w['pc'])} page={w['page']}")

open(OUT, "w").write(json.dumps(report, indent=1))
print("saved", OUT)
