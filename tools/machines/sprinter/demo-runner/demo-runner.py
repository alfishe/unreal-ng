#!/usr/bin/env python3
"""Run Sprinter programs from a hard-disk image one by one and classify what they do.

Three layers, each usable on its own:
  boot      create a SPRINTER instance (BIOS of choice), insert the HDD, run frames at full
            speed (paused machine + run_frames) until Flex Navigator idles
  navigate  walk Flex Navigator's left panel to a file, from the disk's own directory
            listing (folders first, then files, by name), and press Enter
  probe     run the program for a few seconds of emulated time and classify it:
            exited (back in FN), waits-for-int (HALT loop, with the IM2 vector it waits for),
            static (no picture change), running (picture changes), plus the Covox-Blaster state

Needs a running unreal-qt with the WebAPI (UNREAL_WEBAPI_PORT), mtools (mdir) for the listing
and Pillow for the contact sheet. Example:

    tools/machines/sprinter/demo-runner/demo-runner.py --port 8097 \\
        --hdd ~/Downloads/mame_release_v306_25.05.2025/IMG/sp_hdd_sys.chd \\
        --listing-image ~/Downloads/sprinter/hdd/sp_hdd_sys.img \\
        --out scratch/demos DEMOS/BALLS/balls.exe DEMOS/BADAPPLE/badapple.exe

`--all DEMOS` runs every .exe below a folder. The listing image is a raw copy of the same disk
(the FAT partition at byte 32256); the CHD itself is what the emulator mounts.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

FN_IDLE_PC = 0xA441          # Flex Navigator 1.15's idle HALT (its main loop waits for INT here)
FRAMES_PER_SECOND = 49       # the Sprinter's 320-line frame is 48.83 Hz


class Emu:
    def __init__(self, port, emu_id=None):
        self.base = f"http://localhost:{port}/api/v1"
        self.id = emu_id

    def call(self, method, path, body=None, timeout=120):
        url = f"{self.base}/emulator/{self.id}{path}" if self.id and not path.startswith("/emulator") else f"{self.base}{path}"
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(url, data=data, method=method, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read()
            try:
                return json.loads(raw)
            except ValueError:
                return raw

    def quiet(self, method, path):
        """A call whose 'already in that state' error does not matter (pause / resume)."""
        try:
            self.call(method, path)
        except urllib.error.HTTPError:
            pass

    def frames(self, n):
        while n > 0:
            step = min(n, 10000)
            self.call("POST", "/run_frames", {"count": step}, timeout=600)
            n -= step

    def tap(self, key, frames=3):
        self.call("POST", "/keyboard/tap", {"key": key, "frames": frames})
        self.frames(frames + 6)  # let the keyboard stream and the program see it

    def pc(self):
        return self.call("GET", "/registers")["special"]["pc"]

    def regs(self):
        return self.call("GET", "/registers")

    def mem(self, addr, n):
        return self.call("GET", f"/memory/read/{hex(addr)}?length={n}&format=full")["data"]

    def digest(self):
        """The picture's digest: video memory + border (`combined`). The response also carries the frame number and
        the previous digest, so the whole JSON differs on every call and must not be compared"""
        d = self.call("GET", "/state/screen/digest")
        value = d.get("combined") or (d.get("active_surface") or {}).get("digest")
        if not value:
            raise RuntimeError(f"no digest in /state/screen/digest: {d}")
        return value

    def shot(self, path):
        self.call("GET", f"/capture/screen?area=full&format=png&path={path}")

    def journal_seq(self):
        """The PLD journal's count of events so far (the next event gets this sequence number)"""
        return self.call("GET", "/state/sprinter/pld-journal").get("appended", 0)

    def journal_since(self, seq, kinds):
        events = self.call("GET", "/state/sprinter/pld-journal").get("events", [])
        return [e for e in events if e.get("seq", -1) >= seq and e.get("kind") in kinds]

    def cbl(self):
        cb = self.call("GET", "/state/sprinter").get("sound", {}).get("covox_blaster", {})
        return {k: cb.get(k) for k in ("mode", "rate_hz", "int_requests", "ticks")}


def boot(emu, bios, hdd, max_seconds=120):
    if not emu.id:
        emu.id = emu.call("POST", "/emulator/start", {"model": "SPRINTER", "sprinter": {"bios": bios}})["id"]
    slots = emu.call("GET", "/media").get("slots", [])
    mounted = next((s for s in slots if s.get("id") == "ide0.master"), {})
    if os.path.basename(str((mounted.get("medium") or {}).get("source") or "")) != os.path.basename(hdd):
        emu.call("POST", "/media/ide0.master/insert", {"path": hdd})
    emu.quiet("POST", "/pause")
    emu.call("POST", "/reset")
    stable = 0
    for _ in range(max_seconds):
        emu.frames(FRAMES_PER_SECOND)
        stable = stable + 1 if emu.pc() == FN_IDLE_PC else 0
        if stable >= 2:
            return True
    return False


def listing(image, folder):
    """Flex Navigator's order of a folder: '..' (not at the root), folders, then files, by name.
    Folder names end with '/' so a file without an extension is never taken for one."""
    out = subprocess.run(["mdir", "-i", f"{image}@@32256", "::/" + folder.strip("/")],
                         capture_output=True, text=True, check=True).stdout
    dirs, files = [], []
    for line in out.splitlines():
        m = re.match(r"^(\S+)\s+(\S*)\s+(<DIR>|\d[\d ]*)\s", line)
        if not m or m.group(1) in (".", ".."):
            continue
        name, ext, kind = m.group(1), m.group(2), m.group(3)
        if kind == "<DIR>":
            dirs.append(name + "/")
        elif not ext.isdigit():
            files.append(f"{name}.{ext}" if ext else name)
    key = lambda s: s.lower()
    return ([".."] if folder.strip("/") else []) + sorted(dirs, key=key) + sorted(files, key=key)


def navigate(emu, image, path, before_enter=None):
    """From FN's freshly booted left panel at C:\\ to `path`, then Enter on the file. `before_enter` runs once the
    cursor is on the file, right before that last Enter (the TTD recording starts there)."""
    parts = path.strip("/").split("/")
    folder = ""
    for k, name in enumerate(parts):
        entries = listing(image, folder)
        lower = [e.rstrip("/").lower() for e in entries]
        if name.lower() not in lower:
            raise ValueError(f"{name} not in {folder or 'C:'}: {entries}")
        for _ in range(lower.index(name.lower())):
            emu.tap("down")
        if k == len(parts) - 1 and before_enter:
            before_enter()
        emu.tap("enter")
        emu.frames(FRAMES_PER_SECOND)  # the panel re-reads the folder
        folder = f"{folder}/{name}".strip("/")


class Ttd:
    """TTD recording around one program: started right before the Enter that launches it, stopped after the probe.
    A run that did not end `running` (a hang, waits-for-int, static, an error) keeps its session as a .ttd file, so
    the hang can be rewound and inspected (port events, the PLD journal); see .recipe/analysis/ttd-recording.md"""

    def __init__(self, emu, history_frames):
        self.emu = emu
        self.history_frames = history_frames

    def start(self):
        self.emu.call("POST", "/ttd/invalidate")  # a clean session per program
        self.emu.call("POST", "/ttd/start", {"mode": "development", "history_limit_frames": self.history_frames})

    def finish(self, keep_path=None):
        """Stop; save to `keep_path` when given; drop the history. Returns the saved path or None"""
        saved = None
        try:
            self.emu.call("POST", "/ttd/stop")
            if keep_path:
                r = self.emu.call("POST", "/ttd/dump", {"path": keep_path}, timeout=600)
                saved = keep_path if not isinstance(r, dict) or r.get("success", True) else None
        finally:
            self.emu.call("POST", "/ttd/invalidate")
        return saved


def probe(emu, name, out_dir, seconds=8, step=2, journal_from=None):
    pcs, digests = [], []
    for i in range(0, seconds, step):
        emu.frames(step * FRAMES_PER_SECOND)
        pcs.append(emu.pc())
        digests.append(emu.digest())
        emu.shot(os.path.join(out_dir, f"{name}-{i // step}.png"))
    r = emu.regs()
    im, i_reg, pc = r["interrupt"]["im"], r["special"]["i"], r["special"]["pc"]
    opcode = emu.mem(pc, 1)[0]
    result = {"program": name, "pc": hex(pc), "im": im, "cbl": emu.cbl(),
              "picture_changes": len(set(digests)) - 1}
    reloads = emu.journal_since(journal_from, ("pld_load",)) if journal_from is not None else []
    if reloads:
        # The program reloaded the PLD (code #2E: back to the ROM loader, e.g. a custom .acx bitstream): the
        # machine restarts, so the picture and the PC say nothing about the program itself
        result["verdict"] = "pld-reload"
        result["pld_load"] = [{k: e.get(k) for k in ("frame", "pc", "text")} for e in reloads[:2]]
    elif pc == FN_IDLE_PC and len(set(pcs)) == 1:
        result["verdict"] = "exited-to-fn"
    elif opcode == 0x76 and len(set(pcs)) == 1 and result["picture_changes"] == 0:
        result["verdict"] = "waits-for-int"
        if im == 2:
            table = emu.mem(i_reg * 256, 256)
            result["im2_vectors"] = {hex(n * 2): hex(table[n * 2] | table[n * 2 + 1] << 8)
                                     for n in range(128) if table[n * 2] or table[n * 2 + 1]}
    elif result["picture_changes"] == 0:
        result["verdict"] = "static"
    else:
        result["verdict"] = "running"
    return result


def contact_sheet(out_dir, results, cols=4, thumb=(276, 108)):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return None
    rows = []
    for r in results:
        frames = sorted(f for f in os.listdir(out_dir) if f.startswith(r["program"] + "-") and f.endswith(".png"))
        rows.append((r, [os.path.join(out_dir, f) for f in frames[:cols]]))
    sheet = Image.new("RGB", (thumb[0] * cols + 220, thumb[1] * len(rows)), "black")
    draw = ImageDraw.Draw(sheet)
    for y, (r, files) in enumerate(rows):
        draw.text((4, y * thumb[1] + 4), f"{r['program']}\n{r['verdict']}\ncbl {r['cbl'].get('mode')}", fill="white")
        for x, f in enumerate(files):
            sheet.paste(Image.open(f).convert("RGB").resize(thumb), (220 + x * thumb[0], y * thumb[1]))
    path = os.path.join(out_dir, "contact-sheet.png")
    sheet.save(path)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("programs", nargs="*", help="paths on the disk, e.g. DEMOS/BALLS/balls.exe")
    ap.add_argument("--all", help="every .exe below this folder on the disk")
    ap.add_argument("--port", type=int, default=8097)
    ap.add_argument("--id", help="an existing SPRINTER instance (default: create one)")
    ap.add_argument("--bios", default="3.07")
    ap.add_argument("--hdd", required=True, help="the image the emulator mounts (CHD or raw)")
    ap.add_argument("--listing-image", required=True, help="a raw copy of the same disk, for mdir")
    ap.add_argument("--seconds", type=int, default=8, help="emulated seconds per program")
    ap.add_argument("--start-at", help="skip the programs before this path (resume a broken run)")
    ap.add_argument("--out", default="scratch/demos")
    ap.add_argument("--ttd", dest="ttd", action="store_true", default=True,
                    help="record TTD from the Enter on each program; keep a .ttd of every run that is not 'running' "
                         "(default on)")
    ap.add_argument("--no-ttd", dest="ttd", action="store_false", help="do not record TTD")
    ap.add_argument("--ttd-keep", choices=("failures", "all"), default="failures",
                    help="which sessions to save as .ttd: runs that are not 'running' (default), or every run "
                         "(a 'running' verdict can still hide a reset or a wrong picture)")
    ap.add_argument("--ttd-history-frames", type=int, default=FRAMES_PER_SECOND * 180,
                    help="rolling TTD history limit in frames (default: about 3 minutes)")
    a = ap.parse_args()

    programs = list(a.programs)
    if a.all:
        def walk(folder):
            for e in listing(a.listing_image, folder):
                if e == "..":
                    continue
                full = f"{folder}/{e.rstrip('/')}"
                if e.endswith("/"):
                    walk(full)
                elif e.lower().endswith(".exe"):
                    programs.append(full)
        walk(a.all.strip("/"))
    if a.start_at:
        lower = [p.lower() for p in programs]
        programs = programs[lower.index(a.start_at.lower()):]
    os.makedirs(a.out, exist_ok=True)
    out_dir = os.path.abspath(a.out)

    emu = Emu(a.port, a.id)
    ttd = Ttd(emu, a.ttd_history_frames) if a.ttd else None
    results = []
    names = set()
    for prog in programs:
        # FBIRD.EXE, FBIRD_.EXE and _FBIRD.EXE must not share a name (their screenshots would overwrite each other)
        base = re.sub(r"[^A-Za-z0-9_]+", "-", prog).strip("-").lower()
        name, n = base, 2
        while name in names:
            name, n = f"{base}-{n}", n + 1
        names.add(name)
        t0 = time.time()
        if not boot(emu, a.bios, a.hdd):
            results.append({"program": name, "verdict": "fn-not-ready", "cbl": {}})
            continue
        try:
            mark = {}

            def before_enter():
                mark["seq"] = emu.journal_seq()
                if ttd:
                    ttd.start()

            navigate(emu, a.listing_image, prog, before_enter=before_enter)
            r = probe(emu, name, out_dir, a.seconds, journal_from=mark.get("seq"))
        except Exception as e:  # keep going: one broken program must not stop the run
            r = {"program": name, "verdict": f"error: {e}", "cbl": {}}
        if ttd:
            try:
                keep = os.path.join(out_dir, f"{name}.ttd") if a.ttd_keep == "all" or r["verdict"] != "running" else None
                saved = ttd.finish(keep)
                if saved:
                    r["ttd"] = saved
            except Exception as e:
                r["ttd_error"] = str(e)
        r["path"] = prog
        r["wall_seconds"] = round(time.time() - t0, 1)
        results.append(r)
        print(json.dumps(r), flush=True)

    with open(os.path.join(out_dir, "results.json"), "w") as f:
        json.dump(results, f, indent=1)
    sheet = contact_sheet(out_dir, results)
    print(f"results: {os.path.join(out_dir, 'results.json')}" + (f", sheet: {sheet}" if sheet else ""))
    emu.quiet("POST", "/resume")


if __name__ == "__main__":
    sys.exit(main())
