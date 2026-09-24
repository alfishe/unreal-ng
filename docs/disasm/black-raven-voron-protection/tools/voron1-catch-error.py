#!/usr/bin/env python3
"""Boot VORON1 (traps OFF, real FDC) with execution breakpoints on the TR-DOS
ROM error path: 0x1D29 (error print), 0x1E36 (read-address callers),
0x1E5A (readonly branch). On each hit dump registers, stack, the message
string at HL (from data/rom/trdos504t.rom when HL < 0x4000) and FDC state.
In the 2026-09-22 journal the error entries never executed - the loader
derails silently - so this harness exists to catch a hit on other builds.
Boot is the deterministic voron1-detboot.py sequence (no keystroke strings).
Creates its own instance; prints the id. Transient outputs -> scratch/.
"""
import json
import sys
import time
import urllib.error
import urllib.request

ROOT = "http://localhost:8090/api/v1"
UDI = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/testdata/loaders/udi/VORON1.UDI"
ROM = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/data/rom/trdos504t.rom"
OUT = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch/voron1-catch-error.json"


def http(method, url, body=None, timeout=60):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as ex:
        print(f"  !! {method} {url.split('/emulator')[-1]}: {ex}")
        return None


r = http("POST", f"{ROOT}/emulator/start", {"model": "PENTAGON"})
EMU = (r or {}).get("id")
print("created:", EMU)
if not EMU:
    sys.exit(1)


def call(method, path, body=None):
    return http(method, f"{ROOT}/emulator/{EMU}{path}", body)


def ocr():
    try:
        return (call("GET", "/capture/ocr").get("text") or "").strip()
    except Exception:
        return ""


# clean: breakpoints + analyzer; real FDC
for b in (call("GET", "/breakpoints").get("breakpoints") or []):
    call("DELETE", f"/breakpoints/{b['id']}")
call("DELETE", "/analyzer/trdos/events")
call("POST", "/feature/fastdisk", {"enabled": False})
call("POST", "/settings/trdos_traps", {"name": "trdos_traps", "value": False})

print("bp on 0x1D29 (error print) and 0x1E36 (read-addr callers) and 0x1E5A (readonly branch)")
for addr in (0x1D29, 0x1E36, 0x1E5A):
    r = call("POST", "/breakpoints", {"type": "execution", "address": addr})
    print(f"  {addr:#06x}: {json.dumps(r)[:80]}")

# --- deterministic boot: reset -> 128K BASIC -> USR 15616 -> RUN"boot" ----------
# 128k, NEVER 48k: the menu's 48K transition ends with OUT 0x7FFD,0x30 which
# latches the paging lock bit (D5) - loader bank writes would be ignored.
call("POST", "/reset", {})
time.sleep(2.0)
st = call("GET", "/basic/state") or {}
if st.get("state") == "menu128k":
    call("POST", "/basic/mode", {"mode": "128k"})
    time.sleep(1.0)
st = call("GET", "/basic/state") or {}
print("state now:", st.get("state"))
call("POST", "/disk/0/insert", {"path": UDI})
call("POST", "/basic/run", {"command": "RANDOMIZE USR 15616"})
t0 = time.time()
prompt = False
while time.time() - t0 < 15:
    time.sleep(0.7)
    txt = ocr()
    if "A>" in txt or "TR-DOS" in txt:
        prompt = True
        break
print("TR-DOS prompt:", prompt, repr(txt[:80]))
call("POST", "/basic/run", {"command": 'RUN"boot"'})


# --- stack window: physical RAM page behind each visible bank (Pentagon) --------
def page_for(addr):
    if 0x4000 <= addr < 0x8000:
        return 5, 0x4000
    if 0x8000 <= addr < 0xC000:
        return 2, 0x8000
    return 0, 0xC000  # bank3 default = RAM page 0


def dump_state(tag):
    regs = call("GET", "/registers") or {}
    sp = (regs.get("special") or {}).get("sp", 0)
    pc = (regs.get("special") or {}).get("pc", 0)
    m = regs.get("main") or {}
    print(f"--- {tag}: PC={pc:#06x} SP={sp:#06x} HL={m.get('hl', 0):#06x} "
          f"DE={m.get('de', 0):#06x} BC={m.get('bc', 0):#06x} "
          f"A={(m.get('af', 0) >> 8) & 0xFF:#04x}")
    print("    fdc:", json.dumps(call("GET", "/state/fdc"))[:200])
    pg, base = page_for(sp)
    try:
        d = call("GET", f"/memory/ram/{pg}/{sp - base}?len=64")
        bts = bytes.fromhex(d.get("hex") or d.get("data") or "")
        for i in range(0, min(32, len(bts) - 1), 2):
            print(f"   stack {sp + i:#06x}: {bts[i] | (bts[i + 1] << 8):#06x}")
    except Exception as ex:
        print("   stack walk failed:", ex)
    hl = m.get("hl", 0)
    if 0 <= hl < 0x4000:
        rom = open(ROM, "rb").read()
        print("string @HL:", rom[hl:hl + 40])
    return regs


log = []
step = 0
while step < 12:
    hit = None
    t0 = time.time()
    call("POST", "/resume", {})
    while time.time() - t0 < 20:
        st = call("GET", "/breakpoints/status") or {}
        if st.get("is_paused"):
            hit = st
            break
        time.sleep(0.3)
    if not hit:
        print(f"[{step}] no more hits within 20s (load may have derailed silently)")
        break
    regs = dump_state(f"HIT {step}")
    pc = (regs.get("special") or {}).get("pc", 0)
    log.append({"pc": pc, "regs": regs})
    step += 1
    if pc == 0x1D29:
        print("*** ERROR PRINT REACHED ***")
        break

print("screen:", repr(ocr()[:200]))
print("analyzer tail:")
for e in (call("GET", "/analyzer/trdos/events").get("events") or [])[-8:]:
    print("  ", e.get("formatted"))
json.dump(log, open(OUT, "w"), indent=1)
print("saved", OUT, "EMU ID:", EMU)
