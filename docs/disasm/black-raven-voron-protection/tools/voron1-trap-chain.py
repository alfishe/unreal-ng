#!/usr/bin/env python3
"""Replay VORON1 boot WITH fastdisk+traps ON (the working path), tracing:
- every ROM service call (bp 0x3D13): A=service, BC/DE/HL params
- execution reaching the loader stages (bp 0x5F10, 0x6000, 0x6200)
Produces the ground-truth boot chain (the 8.3 "breakpoint on 0x6000-0x60FF"
next step). Boot is the deterministic voron1-detboot.py sequence (no keystroke
strings). Creates its own instance; prints the id. Transient outputs -> scratch/.
"""
import json
import sys
import time
import urllib.error
import urllib.request

ROOT = "http://localhost:8090/api/v1"
UDI = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/testdata/loaders/udi/VORON1.UDI"
OUT = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch/voron1-trap-chain.json"


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


def call(m, p, b=None):
    return http(m, f"{ROOT}/emulator/{EMU}{p}", b)


def ocr():
    try:
        return (call("GET", "/capture/ocr").get("text") or "").strip()
    except Exception:
        return ""


for b in (call("GET", "/breakpoints").get("breakpoints") or []):
    call("DELETE", f"/breakpoints/{b['id']}")
call("DELETE", "/analyzer/trdos/events")
print("== traps ON (working-path conditions) ==")
call("POST", "/feature/fastdisk", {"enabled": True})
call("POST", "/settings/trdos_traps", {"name": "trdos_traps", "value": True})
for addr, note in ((0x3D13, "service"), (0x5F10, "loader-entry"), (0x6000, "stage"), (0x6200, "stage2")):
    r = call("POST", "/breakpoints", {"type": "execution", "address": addr})
    print(f"  bp {addr:#06x} {note}: id={r.get('id')}")

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

log = []
step = 0
while step < 60:
    hit = None
    t0 = time.time()
    call("POST", "/resume", {})
    while time.time() - t0 < 8:
        st = call("GET", "/breakpoints/status")
        if st.get("is_paused"):
            hit = st
            break
        time.sleep(0.12)
    if not hit:
        print(f"[{step}] no more hits (idle/finished)")
        break
    regs = call("GET", "/registers")
    pc = regs["special"]["pc"]
    a = (regs["main"]["af"] >> 8) & 0xFF
    bc, de, hl = regs["main"]["bc"], regs["main"]["de"], regs["main"]["hl"]
    print(f"[{step:2}] PC={pc:#06x} A={a:02X} BC={bc:04X} DE={de:04X} HL={hl:04X} SP={regs['special']['sp']:04X}")
    log.append({"pc": pc, "a": a, "bc": bc, "de": de, "hl": hl})
    step += 1
    if pc == 0x6000 or pc == 0x5F10:
        print("     *** LOADER ENTRY REACHED ***")
        break

json.dump(log, open(OUT, "w"))
print("screen:", repr(ocr()[:200]))
print("saved", OUT, "EMU ID:", EMU)
