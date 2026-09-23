#!/usr/bin/env python3
"""TTD-precise disasm (at the exact time each region executed) + long forward
walk compressing loop repeats. /disasm decodes linearly from the address, so
disassemble at the exact (frame,tin) each region executed via /ttd/seek first;
the TIMES constants document the 2026-09-22 journal - adjust per journal.
usage: voron1-fwdwalk2.py <emu-id>
"""
import json
import sys
import urllib.error
import urllib.request

ROOT = "http://localhost:8090/api/v1"
OUT = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch/voron1-fwdwalk2.json"
if len(sys.argv) < 2:
    sys.exit("usage: voron1-fwdwalk2.py <emu-id>")
EMU = sys.argv[1]


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


def regs():
    r = call("GET", "/registers") or {}
    sp = r.get("special") or {}
    m = r.get("main") or {}
    return {"pc": sp.get("pc"), "af": m.get("af"), "bc": m.get("bc"),
            "de": m.get("de"), "hl": m.get("hl"), "ix": (r.get("index") or {}).get("ix")}


report = {"disasm_at_time": {}, "forward": []}

# --- 1. disasm each region at the exact moment it executed --------------------
TIMES = [
    ((341, 63173), 0x3DF8, 12, "post-RA helper 3DF8"),
    ((341, 63173), 0x3E01, 6, "wait loop 3E01"),
    ((346, 41737), 0x0056, 8, "low hook 0056"),
    ((346, 41816), 0x16C5, 8, "RUN parser 16C5"),
    ((346, 41826), 0x3D13, 8, "dispatcher 3D13"),
    ((346, 41850), 0x2F69, 8, "SP-fix trampoline 2F69"),
    ((346, 41884), 0x1D2F, 14, "error routine 1D2F"),
]
print("== disasm at exact TTD times ==")
for (f, t), addr, count, name in TIMES:
    call("POST", "/ttd/seek", {"frame": f, "tinframe": t})
    d = call("GET", f"/disasm?address={addr}&count={count}") or {}
    ins = d.get("instructions", [])
    print(f"-- {name} @ f={f} t={t} --")
    for i in ins:
        if isinstance(i, dict):
            print(f"   {i.get('address_hex', hex(i.get('address', 0)))}: {i.get('bytes', '')}  {i.get('mnemonic', i.get('disasm', ''))}")
    report["disasm_at_time"][name] = ins

# --- 2. long forward walk from the wait loop ----------------------------------
print("== forward walk from (341, 63173), 6000 instrs, compress loops ==")
call("POST", "/ttd/seek", {"frame": 341, "tinframe": 63173})
fwd = []
last_pc = None
run = 0
for i in range(6000):
    r = call("POST", "/ttd/step-instruction", {"dir": "forward"}) or {}
    if not r.get("stepped"):
        print(f"  stalled at {i + 1}: {json.dumps(r)[:120]}")
        break
    g = regs()
    pc = g["pc"]
    if pc == last_pc:
        run += 1
        continue
    if last_pc is not None:
        fwd[-1]["reps"] = run
    g["frame"], g["tin"] = r.get("frame"), r.get("tinframe")
    g["reps"] = 1
    fwd.append(g)
    last_pc = pc
    if len(fwd) <= 160:
        print(f"   {hex(pc)} f={g['frame']} t={g['tin']} af={g['af'] and hex(g['af'])} bc={g['bc'] and hex(g['bc'])} de={g['de'] and hex(g['de'])} hl={g['hl'] and hex(g['hl'])}")
    if pc is not None and pc in (0x1D29, 0x1D1A):
        print("   *** ERROR PRINT REACHED ***")
        break

report["forward"] = fwd
print(f"distinct pcs: {len(fwd)}")
open(OUT, "w").write(json.dumps(report, indent=1))
print("saved", OUT)
