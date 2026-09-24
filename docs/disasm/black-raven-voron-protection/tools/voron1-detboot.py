#!/usr/bin/env python3
"""VORON1: deterministic TTD boot - insert disk + enter TR-DOS + RUN"boot".

Research harness for docs/disasm/black-raven-voron-protection (solves the
8.3 "menu does not respond to scripted keys" blocker). No keystroke strings,
no menu timing (128K-editor injection mangles lines):
  1. create PENTAGON, fastdisk/trdos_traps off (real FDC)
  2. /reset -> 128K menu -> POST /basic/mode {"mode":"48k"} -> 48K editor
     (VORON1's forged track 0 fails IsTrdos(), so /disk insert autostart
     refuses with "not TR-DOS formatted" - do not use autostart here)
  3. plain /disk/0/insert, then /ttd/start
  4. POST /basic/run "RANDOMIZE USR 15616"  ($3D00 hook pages TR-DOS in;
     the udi_zvezdnoe_boot_test pattern)
  5. poll OCR for the TR-DOS "A>" prompt
  6. POST /basic/run 'RUN"boot"'  (tokenized, closing quote included)
  7. watch OCR + /analyzer/trdos/events, screen capture (GIF), ttd/stop
  8. origin-controlled find-exec markers for the TR-DOS error/FDC blocks
Known outcome (2026-09-22 journal, instance fba5f11d, frames 0-6361): loader
runs, screen clears ~f2700, protection check stub at 0x5D8C-0x5E30 loops at
0x5DE0, ret at 0x5DE5 pops 0xF5C9, NOP march to 0xFFFF, silent TR-DOS
re-init, blank screen, idle command loop. Error entries 0x1D29/0x1D1A never
execute; last real sector-read block exec is 0x3F17 at f2265.
usage: voron1-detboot.py [stale-instance-id ...]   (creates a new instance,
prints its id; transient outputs go to scratch/)
"""
import base64
import json
import sys
import time
import urllib.error
import urllib.request

ROOT = "http://localhost:8090/api/v1"
UDI = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/testdata/loaders/udi/VORON1.UDI"
OUT = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch/voron1-detboot.json"
SHOT = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch/voron1-detboot-screen.gif"


def http(method, url, body=None, timeout=60):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as ex:
        detail = ""
        try:
            detail = ex.read().decode()[:200]
        except Exception:
            pass
        print(f"  !! {method} {url.split('/emulator')[-1]}: {ex} {detail}")
        return None
    except (urllib.error.URLError, json.JSONDecodeError) as ex:
        print(f"  !! {method} {url}: {ex}")
        return None


def call(emu, method, path, body=None):
    return http(method, f"{ROOT}/emulator/{emu}{path}", body)


def ocr(emu):
    d = call(emu, "GET", "/capture/ocr")
    return (d.get("text") or "").strip() if isinstance(d, dict) else ""


# --- 1. optional cleanup of stale instances (ids passed on argv) ---------------
health = http("GET", f"{ROOT}/emulator")
insts = ((health or {}).get("emulators") or [])
print("instances:", [it.get("id") for it in insts if isinstance(it, dict)])
for iid in sys.argv[1:]:
    print(" deleting stale", iid)
    http("DELETE", f"{ROOT}/emulator/{iid}")

# --- 2. create + real FDC ------------------------------------------------------
r = http("POST", f"{ROOT}/emulator/start", {"model": "PENTAGON"})
EMU = (r or {}).get("id")
print("created:", EMU)
if not EMU:
    sys.exit(1)
report = {"emu": EMU}

call(EMU, "POST", "/feature/fastdisk", {"enabled": False})
call(EMU, "POST", "/settings/trdos_traps", {"name": "trdos_traps", "value": False})
print("fastdisk/trdos_traps off")

# --- 3. reset -> 128K BASIC via the menu ---------------------------------------
# 128k, NEVER 48k: the menu's 48K transition ends with OUT 0x7FFD,0x30 which
# latches the paging lock bit (D5) - every later loader bank write would be
# silently ignored until the next reset (boot recipe bug found 2026-09-23).
call(EMU, "POST", "/reset", {})
time.sleep(2.0)
st = call(EMU, "GET", "/basic/state") or {}
state = st.get("state")
print("state after reset:", state)
if state == "menu128k":
    r = call(EMU, "POST", "/basic/mode", {"mode": "128k"})
    print("mode 128k:", json.dumps(r))
    time.sleep(1.0)

# 48K banner dismissal: entering 48K BASIC from the 128K menu passes through up
# to two wait states that each need a keypress - the menu selection itself
# (navigateToBasic48K injects ENTER, asynchronously) and then the 48K ROM's
# copyright banner. One blind tap races the menu transition; instead keep
# tapping ENTER while E_LINE (#5C59) still holds boot garbage, and only then
# proceed (each tap advances whichever wait state the machine is in).
e_line = 0
t0 = time.time()
while time.time() - t0 < 12:
    m = call(EMU, "GET", "/memory/read/0x5C59?length=2&format=full") or {}
    e = (m or {}).get("data") or [0, 0]
    e_line = e[0] | e[1] << 8 if len(e) == 2 else 0
    if 0x5B00 <= e_line <= 0xFF00:
        break
    call(EMU, "POST", "/keyboard/tap", {"key": "ENTER"})
    time.sleep(1.5)
print(f"E_LINE settled: 0x{e_line:04X} after {time.time() - t0:.1f}s")
report["e_line"] = e_line
st = call(EMU, "GET", "/basic/state") or {}
print("state now:", st.get("state"))
report["state_before"] = st.get("state")
if st.get("state") != "basic48k":
    print("!! not in 48K BASIC - aborting")
    open(OUT, "w").write(json.dumps(report, indent=1))
    sys.exit(3)

# --- 4. insert + record --------------------------------------------------------
r = call(EMU, "POST", "/disk/0/insert", {"path": UDI})
print("insert:", json.dumps(r))
report["insert"] = r

r = call(EMU, "POST", "/ttd/start", {})
print("ttd/start:", json.dumps(r))
report["ttd_start"] = r

# --- 5. enter TR-DOS deterministically -----------------------------------------
r = call(EMU, "POST", "/basic/run", {"command": "RANDOMIZE USR 15616"})
print("basic/run USR 15616:", json.dumps(r))
report["run_usr15616"] = r

prompt_seen = False
txt = ""
t0 = time.time()
while time.time() - t0 < 15:
    time.sleep(0.7)
    txt = ocr(EMU)
    if "A>" in txt or "TR-DOS" in txt:
        prompt_seen = True
        break
print(f"TR-DOS prompt after {time.time() - t0:.1f}s:", prompt_seen, repr(txt[:100]))
report["trdos_prompt_seen"] = prompt_seen
if not prompt_seen:
    open(OUT, "w").write(json.dumps(report, indent=1))
    call(EMU, "POST", "/ttd/stop", {})
    sys.exit(3)

# --- 6. RUN"boot" ---------------------------------------------------------------
r = call(EMU, "POST", "/basic/run", {"command": 'RUN"boot"'})
print("basic/run RUN\"boot\":", json.dumps(r))
report["run_boot"] = r

# --- 7. watch the load ----------------------------------------------------------
snaps = []
events_all = []
t0 = time.time()
while time.time() - t0 < 60:
    time.sleep(1.0)
    txt = ocr(EMU)
    evs = (call(EMU, "GET", "/analyzer/trdos/events") or {}).get("events", [])
    for e in evs[len(events_all):]:
        events_all.append(e)
        print(f"  [{time.time() - t0:4.1f}s] FDC {e.get('formatted', json.dumps(e)[:100])}")
    if txt and (not snaps or txt != snaps[-1][1]):
        snaps.append((round(time.time() - t0, 1), txt[:300]))
        print(f"  [{time.time() - t0:4.1f}s] screen: {repr(txt[:160])}")
report["snaps"] = snaps
report["events"] = events_all
report["final_screen"] = snaps[-1][1] if snaps else ""
print("final screen:", repr(report["final_screen"][-160:]))

# --- 8. screen capture (endpoint returns GIF) -----------------------------------
try:
    req = urllib.request.Request(f"{ROOT}/emulator/{EMU}/capture/screen")
    with urllib.request.urlopen(req, timeout=30) as resp:
        raw = resp.read()
    j = None
    try:
        j = json.loads(raw)
    except Exception:
        pass
    if isinstance(j, dict):
        b64 = j.get("data") or j.get("image") or ""
        raw = base64.b64decode(b64) if b64 else b""
    open(SHOT, "wb").write(raw)
    print("screen saved:", SHOT, len(raw), "bytes")
except Exception as ex:
    print("screen capture failed:", ex)

# --- 9. stop recording + origin-controlled markers -------------------------------
r = call(EMU, "POST", "/ttd/stop", {})
print("ttd/stop:", json.dumps(r))
report["ttd_stop"] = r
st = call(EMU, "GET", "/ttd/status") or {}
report["ttd_status_stop"] = st


def find_exec(addr, before=None):
    call(EMU, "POST", "/ttd/seek", {"frame": 10 ** 9, "tinframe": 0})
    q = {"addr": addr, "access": "execute"}
    if before:
        q["before_frame"], q["before_tin"] = before
    r = call(EMU, "POST", "/ttd/find-last", q) or {}
    print(f" exec {hex(addr)}:", json.dumps(r)[:240])
    return r


report["markers"] = {name: find_exec(a) for name, a in
                     [("err_print_1d29", 0x1D29), ("err_entry_1d1a", 0x1D1A),
                      ("read_sector_3f17", 0x3F17), ("read_next_3f22", 0x3F22)]}

open(OUT, "w").write(json.dumps(report, indent=1))
print("saved", OUT)
print("EMU ID:", EMU)
