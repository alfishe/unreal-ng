#!/usr/bin/env python3
"""Pentagon 128 demo with the on-screen keyboard: walk the boot menu, enter 128 BASIC, type a program, run it.

The machine runs frame by frame (paused + run_frame) while the window recording writes one video frame per emulated
frame, so the file plays at the machine's speed whatever the host's load.

    python3 tools/recording/pentagon_osd_demo.py --port 8090 --out /abs/path/scratch/demo.mov [--profile crt-lottes]
    # 4K for YouTube: the CRT drawn at 1080, doubled in blocks, the keyboard right of it
    python3 tools/recording/pentagon_osd_demo.py --out /abs/path/scratch/demo-4k.mov \
        --scale 2 --picture-height 1080 --frame 3840x2160 --layout side

Recipe: .recipe/media/window-recording.md
"""
import argparse
import json
import sys
import time
import urllib.request

HOLD = 6   # frames a key stays down
GAP = 5    # frames between keys
AFTER_ENTER = 40  # the editor enters the line meanwhile and drops keys
SHIFT_LEAD = 2  # a shift goes down this many frames before its key

SYMBOL = {"=": "l", ":": "z", ";": "o", '"': "p", "+": "k", "-": "j", ",": "n", ".": "m"}


class Api:
    def __init__(self, port):
        self.base = f"http://127.0.0.1:{port}/api/v1"
        self.session = None  # X-Unreal-Session once the script holds control

    def call(self, method, path, body=None, timeout=180):
        data = json.dumps(body).encode() if body is not None else None
        headers = {"Content-Type": "application/json"}
        if self.session:
            headers["X-Unreal-Session"] = self.session
        req = urllib.request.Request(self.base + path, method=method, data=data, headers=headers)
        try:
            return json.loads(urllib.request.urlopen(req, timeout=timeout).read() or b"{}")
        except urllib.error.HTTPError as e:
            sys.exit(f"{method} {path}: HTTP {e.code} {e.read().decode()}")


def schedule_keys(start, keys):
    """[(frame, 'press'|'release', key)] for a list of key groups: a group is pressed together (shift first)"""
    events = []
    t = start
    for group in keys:
        if len(group) == 1:
            events += [(t, "press", group[0]), (t + HOLD, "release", group[0])]
            t += HOLD + GAP + (AFTER_ENTER if group[0] == "enter" else 0)
        else:
            shift, key = group
            events += [(t, "press", shift), (t + SHIFT_LEAD, "press", key),
                       (t + SHIFT_LEAD + HOLD, "release", key), (t + SHIFT_LEAD + HOLD + 1, "release", shift)]
            t += SHIFT_LEAD + HOLD + 1 + GAP
    return events, t


def text_keys(text):
    keys = []
    for ch in text:
        if ch == " ":
            keys.append(["space"])
        elif ch == "\n":
            keys.append(["enter"])
        elif ch in SYMBOL:
            keys.append(["ss", SYMBOL[ch]])
        elif ch.isupper():
            keys.append(["cs", ch.lower()])
        else:
            keys.append([ch])
    return keys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8090)
    ap.add_argument("--out", required=True)
    ap.add_argument("--profile", default="crt-lottes")
    ap.add_argument("--codec", default="prores")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--picture-height", type=int, default=0)
    ap.add_argument("--frame", default="", help="WxH, e.g. 3840x2160")
    ap.add_argument("--layout", default="below")
    ap.add_argument("--no-audio", action="store_true", help="the picture only (the sound track is on by default)")
    args = ap.parse_args()
    api = Api(args.port)

    emulators = api.call("GET", "/emulator")["emulators"]
    if not emulators:
        emu = api.call("POST", "/emulator/start", {"model": "PENTAGON"})
        emu_id = emu.get("id") or emu.get("emulator", {}).get("id")
    else:
        emu_id = emulators[0]["id"]
    e = f"/emulator/{emu_id}"
    # Take control for the whole demo: the GUI shows who drives it and locks; released at the end
    api.session = api.call("POST", f"{e}/control/session", {"client": "pentagon_osd_demo"})["session"]
    api.call("PUT", f"{e}/audio/mixer/master", {"muted": True})
    api.call("PUT", f"{e}/video/window", {"gpu": True, "crt": True, "crt_profile": args.profile})
    time.sleep(3)  # the boot menu up
    api.call("POST", f"{e}/pause")
    time.sleep(0.5)

    # The menu: down x3, up x2 (Tape Loader -> 128 BASIC), then Enter; then the program
    events = []
    t = 50  # a second of the menu first
    menu = [["down"], ["down"], ["down"], ["up"], ["up"]]
    ev, t = schedule_keys(t, menu)
    events += ev
    t += 25
    ev, t = schedule_keys(t, [["enter"]])
    events += ev
    t += 60  # the editor up
    program = ('10 for i=1 to 6\n'
               '20 border i: print ink i;"Unreal NG on a Pentagon"\n'
               '30 next i\n')
    ev, t = schedule_keys(t, text_keys(program))
    events += ev
    t += 25
    ev, t = schedule_keys(t, text_keys("run\n"))
    events += ev
    total = t + 200  # the result on screen

    print(f"{len(events)} key events, {total} frames ({total / 50:.1f} s)")
    status = api.call("POST", f"{e}/video/window/record",
                      dict({"path": args.out, "keyboard": "pentagon2plus", "codec": args.codec, "scale": args.scale,
                            "picture_height": args.picture_height, "layout": args.layout, "audio": not args.no_audio},
                           **({"width": int(args.frame.split("x")[0]), "height": int(args.frame.split("x")[1])}
                              if args.frame else {})))
    print("recording", status)
    by_frame = {}
    for frame, action, key in events:
        by_frame.setdefault(frame, []).append((action, key))
    started = time.time()
    frame = 0
    for at in sorted(by_frame) + [total]:
        if at > frame:  # run and record up to the next key change: every frame lands in the file
            api.call("POST", f"{e}/video/window/record/frames", {"frames": at - frame})
            if at // 250 != frame // 250 or at == total:
                print(f"frame {at}/{total} ({time.time() - started:.0f} s)", flush=True)
            frame = at
        for action, key in by_frame.get(at, []) if at < total else []:
            api.call("POST", f"{e}/keyboard/{action}", {"key": key})
    print("stopped", api.call("POST", f"{e}/video/window/record/stop", {}))
    api.call("DELETE", f"{e}/control/session")


if __name__ == "__main__":
    main()
