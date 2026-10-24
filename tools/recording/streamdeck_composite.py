#!/usr/bin/env python3
"""The emulator window and the Stream Deck in one video frame, frame by frame: a scripted demo on Stream Deck Studio.

The clock is the machine's: the machine stays paused and the script runs it one frame at a time, so every video frame
is one emulated frame (48.828 fps on a Pentagon) whatever the host's load. After each frame the window's picture (as
drawn: CRT shader and all, no HUD, GET /capture/screen?source=window) and the deck's (Stream Deck Studio's
GET /screenshot?area=deck) are taken; then the two are put side by side, the step's caption under them, and encoded
to H.264 at the machine's rate.

Time travel goes by frames too: a FRAMES dial turned one detent per video frame plays the history backwards (or
forwards) at the machine's rate; a TIME dial's jump of a second holds its picture for a set number of frames.

    tools/recording/streamdeck_composite.py --demo ttd --snapshot "testdata/loaders/sna/Dizzy X.sna" \\
        --out ~/Downloads/streamdeck-ttd.mp4
    tools/recording/streamdeck_composite.py --demo menus --out ~/Downloads/streamdeck-menus.mp4

Needs: a GUI unreal-qt (--webapi), a Stream Deck Studio with a deck of its own playing data/streamdeck/scenarios/
unreal.deck.yaml through a daemon paired with that unreal-qt (deckflow run unreal --host <webapi>), ffmpeg, Pillow.
The deck's keys and dials go through the studio like a finger; the game's keys go to the emulator with the companion
token (no session, no remote-control lock). The window's settings (HUD, CRT) and the run state are put back after.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

from PIL import Image, ImageDraw, ImageFont


class Http:
    def __init__(self, base, token=None):
        self.base, self.token = base.rstrip("/"), token

    def call(self, method, path, body=None, raw=False, timeout=30):
        headers = {"Content-Type": "application/json"}
        if self.token:
            headers["Authorization"] = "Bearer " + self.token
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, method=method, data=data, headers=headers)
        out = urllib.request.urlopen(req, timeout=timeout).read()
        return out if raw else json.loads(out or b"{}")


class Take:
    """The timeline in frames: each shoot() writes one video frame (the window's picture, the deck's, the caption)."""

    def __init__(self, root, emu, studio, deck, folder):
        self.root, self.emu, self.studio, self.deck, self.folder = root, emu, studio, deck, folder
        self.frames = []  # (window png, deck png, caption)
        self.text = ""
        self.window = None
        self.held = set()  # game keys down

    def caption(self, text):
        self.text = text

    def shoot(self, window=True):
        # the window (the emulator) and the deck (the studio) are two servers: both pictures taken at once
        n = len(self.frames)
        deck = os.path.join(self.folder, f"d{n:06d}.png")

        def take_deck():
            with open(deck, "wb") as f:
                f.write(self.studio.call("GET", "/screenshot?area=deck", raw=True))

        th = threading.Thread(target=take_deck)
        th.start()
        if window or self.window is None:
            self.window = os.path.join(self.folder, f"w{n:06d}.png")
            self.emu.call("GET", f"/capture/screen?source=window&path={self.window}")
        th.join()
        self.frames.append((self.window, deck, self.text))

    def run_state(self):
        return self.root.call("GET", "/surfaces/readouts?names=run.state")["values"]["run.state"]["state"]

    def paused(self):
        if self.run_state() != "paused":
            self.root.call("POST", "/controllers/actions/run.pause", {})
        for _ in range(50):
            if self.run_state() == "paused":
                return
            time.sleep(0.02)

    def position(self):
        t = self.root.call("GET", "/surfaces/readouts?names=ttd.position")["values"].get("ttd.position", {})
        c = t.get("current") or {}
        return c.get("frame"), c.get("tinframe"), t.get("state")

    def play(self, frames, keys=()):
        """`frames` machine frames, a video frame each; keys: (from, to, [game keys]) held over frames [from, to)."""
        for i in range(frames):
            want = set()
            for a, b, ks in keys:
                if a <= i < b:
                    want |= set(ks)
            for k in sorted(self.held - want):
                self.emu.call("POST", "/keyboard/release", {"key": k})
            for k in sorted(want - self.held):
                self.emu.call("POST", "/keyboard/press", {"key": k})
            self.held = want
            self.emu.call("POST", "/run_frame", {})
            self.shoot()
        for k in sorted(self.held):
            self.emu.call("POST", "/keyboard/release", {"key": k})
        self.held = set()

    def hold(self, frames, window=False):
        """The picture stays (time stands, as after a seek); the deck goes on (its labels, its readouts)."""
        for _ in range(frames):
            self.shoot(window)

    def key(self, row, col, then_frames=0, settle=0.8):
        """A deck key like a finger: down for two frames, up; the action lands, the machine stays paused."""
        self.studio.call("POST", "/input/key", {"row": row, "col": col, "action": "down"})
        self.hold(2, window=True)
        self.studio.call("POST", "/input/key", {"row": row, "col": col, "action": "up"})
        time.sleep(settle)
        self.paused()
        self.shoot()
        if then_frames:
            self.hold(then_frames, window=True)

    def dial(self, index, ticks, frames, hold_each=0):
        """A detent turn per video frame (ticks each), each waited for until the history's position moved."""
        for _ in range(frames):
            before = self.position()
            self.studio.call("POST", "/input/dial", {"index": index, "action": "turn", "ticks": ticks})
            for _ in range(100):
                if self.position()[:2] != before[:2]:
                    break
                time.sleep(0.01)
            time.sleep(0.03)  # the strip follows the position
            self.shoot()
            if hold_each:
                self.hold(hold_each)


def demo_ttd(t):
    """Play Dizzy with history recording, then the history backwards and forwards by frames, jumps by seconds, live."""
    run_right, run_left, jump_right, jump_left, jump = ["7"], ["6"], ["7", "9"], ["6", "9"], ["9"]
    t.caption("Home page: the game runs, time travel is off")
    t.play(50, [(10, 45, run_right)])
    t.caption("TTD REC: the history records every frame")
    t.key(2, 2)
    t.caption("Playing: Dizzy runs and jumps, every frame goes into the history")
    t.play(600, [(0, 60, run_right), (60, 130, jump_right), (130, 170, run_right), (170, 240, jump_left),
                 (240, 300, run_left), (300, 360, jump), (360, 430, jump_right), (430, 500, jump_right),
                 (500, 560, run_right)])
    t.caption("FRAMES dial left: the history plays backwards, frame by frame")
    t.dial(0, -1, 220)
    t.caption("FRAMES dial right: forwards again")
    t.dial(0, 1, 110)
    t.caption("TIME dial left: a second back per detent")
    for _ in range(4):
        t.dial(1, -1, 1, hold_each=30)
    t.caption("TIME dial right: a second forward")
    for _ in range(2):
        t.dial(1, 1, 1, hold_each=30)
    t.caption("LIVE: back to the present, the game goes on")
    t.key(2, 1)
    t.play(150, [(10, 80, jump_right), (80, 150, run_right)])
    t.caption("TTD REC: recording off")
    t.key(2, 2, then_frames=40)


# home keys that open a page, and the pages inside a page (sound > AY channels)
MENU_WALK = [((0, 6), "SPEED"), ((1, 2), "STATE"), ((1, 3), "MEDIA"), ((1, 4), "MODEL"), ((1, 5), "REC"),
             ((1, 6), "SOUND"), ((1, 7), "VIDEO"), ((1, 8), "STATS"), ((2, 4), "GAME"), ((2, 5), "BASIC"),
             ((3, 7), "TIME"), ((3, 8), "DEBUG")]
MENU_INNER = {"SOUND": [((0, 2), "AY CHANNELS")]}


def demo_menus(t, pause=98):
    """Every page of the scenario: open it from home, back again; each page held `pause` frames (2 s)."""
    t.caption("Home page")
    t.play(pause)
    for (row, col), name in MENU_WALK:
        t.caption(f"{name} page")
        t.key(row, col)
        t.play(pause)
        for (r2, c2), inner in MENU_INNER.get(name, []):
            t.caption(f"{inner} page")
            t.key(r2, c2)
            t.play(pause)
            t.caption(f"Back to {name}")
            t.key(0, 0)
            t.play(pause)
        t.caption("Back to the home page")
        t.key(0, 0)
        t.play(pause)


DEMOS = {"ttd": demo_ttd, "menus": demo_menus}


def font(size):
    for f in ("/System/Library/Fonts/HelveticaNeue.ttc", "/System/Library/Fonts/Helvetica.ttc",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def compose(frames, out, size, rate):
    """The frame: the window left, the deck right, both the same height, the caption centred under them."""
    W, H = size
    margin, gap, band = W // 40, W // 40, H // 9
    win0, deck0 = Image.open(frames[0][0]), Image.open(frames[0][1])
    ch = H - 2 * margin - band
    ww, dw = win0.width * ch / win0.height, deck0.width * ch / deck0.height
    k = min(1.0, (W - 2 * margin - gap) / (ww + dw))
    ch, ww, dw = int(ch * k) // 2 * 2, int(ww * k) // 2 * 2, int(dw * k) // 2 * 2
    x0 = (W - ww - gap - dw) // 2
    y0 = margin + (H - 2 * margin - band - ch) // 2
    cap_font, tag_font = font(band * 34 // 100), font(band * 20 // 100)
    enc = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                            "-r", rate, "-i", "-", "-c:v", "libx264", "-preset", "slow", "-crf", "16",
                            "-pix_fmt", "yuv420p", "-movflags", "+faststart", out], stdin=subprocess.PIPE)
    cache = {}
    for wpath, dpath, text in frames:
        if wpath not in cache:
            cache.clear()
            cache[wpath] = Image.open(wpath).convert("RGB").resize((ww, ch), Image.LANCZOS)
        frame = Image.new("RGB", (W, H), (16, 18, 22))
        frame.paste(cache[wpath], (x0, y0))
        frame.paste(Image.open(dpath).convert("RGB").resize((dw, ch), Image.LANCZOS), (x0 + ww + gap, y0))
        dr = ImageDraw.Draw(frame)
        dr.text((x0, y0 + ch + band // 8), "unreal-ng", font=tag_font, fill=(120, 126, 140))
        dr.text((x0 + ww + gap + dw, y0 + ch + band // 8), "Stream Deck + XL", font=tag_font, fill=(120, 126, 140),
                anchor="ra")
        if text:
            dr.text((W // 2, y0 + ch + band * 62 // 100), text, font=cap_font, fill=(236, 238, 242), anchor="mm")
        enc.stdin.write(frame.tobytes())
    enc.stdin.close()
    enc.wait()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--demo", choices=sorted(DEMOS), default="ttd")
    ap.add_argument("--out", required=True)
    ap.add_argument("--webapi", default="http://127.0.0.1:8690", help="the unreal-qt the deck drives")
    ap.add_argument("--emulator", help="instance id (default: the first)")
    ap.add_argument("--studio", type=int, default=37152, help="Stream Deck Studio's automation port")
    ap.add_argument("--deck", default="EMU00C6", help="the studio deck's serial")
    ap.add_argument("--companion", default=os.path.expanduser("~/.config/unreal-streamdeck/companion.json"))
    ap.add_argument("--size", default="2560x1440")
    ap.add_argument("--crt", default="crt-lottes", help="the window's CRT profile while recording ('' = as it is)")
    ap.add_argument("--snapshot", help="a snapshot loaded before the take (off camera), path on the emulator's host")
    ap.add_argument("--keep", action="store_true", help="keep the pictures of every frame")
    a = ap.parse_args()

    token = json.load(open(a.companion))["token"]
    root = Http(a.webapi + "/api/v1", token)
    eid = a.emulator or root.call("GET", "/emulator")["emulators"][0]["id"]
    emu = Http(f"{a.webapi}/api/v1/emulator/{eid}", token)
    studio = Http(f"http://127.0.0.1:{a.studio}/api/v1")
    out = os.path.abspath(os.path.expanduser(a.out))
    scratch = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scratch", "streamdeck-composite")
    os.makedirs(scratch, exist_ok=True)
    work = tempfile.mkdtemp(prefix=a.demo + "-", dir=scratch)  # the pictures of every frame

    before = emu.call("GET", "/video/window")
    t = Take(root, emu, studio, a.deck, work)
    was_running = t.run_state() != "paused"
    change = {"hud": False}
    if a.crt:
        change.update(gpu=True, crt=True, crt_profile=a.crt)
    emu.call("PUT", "/video/window", change)
    try:
        root.call("POST", "/controllers/actions/run.pace_set", {"params": {"preset": "full"}})
        if a.snapshot:
            emu.call("POST", "/snapshot/load", {"path": os.path.abspath(os.path.expanduser(a.snapshot))})
        t.paused()
        if a.demo == "ttd":  # start from no history (off camera): a recording stopped, the history dropped
            if t.position()[2] in ("recording", "detached"):
                root.call("POST", "/controllers/actions/ttd.record_toggle", {})
                t.paused()
            root.call("POST", "/controllers/actions/ttd.invalidate", {})
        studio.call("POST", f"/runtime/navigate?deck={a.deck}", {"to": "home"})
        time.sleep(0.5)
        started = time.monotonic()
        DEMOS[a.demo](t)
        print(f"{len(t.frames)} frames taken in {time.monotonic() - started:.0f} s")
    finally:
        emu.call("PUT", "/video/window", {k: before[k] for k in ("hud", "gpu", "crt", "crt_profile") if k in before})
        if was_running and t.run_state() == "paused":
            root.call("POST", "/controllers/actions/run.pause_toggle", {})
    # the machine's rate: t-states per second / t-states per frame (3500000 / 71680 = 48.828 on a Pentagon)
    rate = root.call("GET", "/surfaces/readouts?names=ttd.position")["values"].get("ttd.position", {}).get("fps", 50)
    num, den = (3125, 64) if abs(rate - 48.828125) < 1e-6 else (int(round(rate * 1000)), 1000)
    W, H = (int(v) for v in a.size.split("x"))
    compose(t.frames, out, (W, H), f"{num}/{den}")
    print(f"wrote {out}: {W}x{H}, {len(t.frames)} frames at {num / den:.3f} fps, {len(t.frames) * den / num:.1f} s")
    if a.keep:
        print(f"kept {work}")
    else:
        shutil.rmtree(work)


if __name__ == "__main__":
    sys.exit(main())
