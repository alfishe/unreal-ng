#!/usr/bin/env python3
"""POC 047: capture consecutive frames of snapshots and RZX recordings through the WebAPI.

Like POC 046's capture.py, plus .rzx: played with rzx/play (a recording for another model switches the model and
the instance id changes; the script follows it). For each file: load or play, run `--warmup` frames, then `--frames`
frames one by one, each followed by `capture/framebuffer?format=rgba`. Writes <out>/<name>.rgba and <name>.json.
The emulator instance is the caller's (its own ports); the script mutes every instance it captures from.

    python3 -I capture.py --port 18750 --out scratch/2026-10-28-poc-039/long --warmup 1500 --frames 1500 a.rzx b.sna@300@space
"""
import argparse
import json
import os
import sys
import urllib.error
import urllib.request


class Api:
    def __init__(self, port):
        self.base = f"http://localhost:{port}/api/v1"
        self.session = None
        self.emu = None

    def call(self, method, path, body=None, raw=False):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method)
        if self.session:
            req.add_header("X-Unreal-Session", self.session)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                payload = r.read()
                return (payload, dict(r.headers)) if raw else json.loads(payload or b"{}")
        except urllib.error.HTTPError as err:
            raise RuntimeError(f"{method} {path}: {err.code} {err.read().decode(errors='replace')[:300]}")

    def e(self, path=""):
        return f"/emulator/{self.emu}{path}"

    def attach(self, emu):
        if self.session and self.emu:
            try:
                self.call("DELETE", self.e("/control/session"))
            except RuntimeError:
                pass
        self.session = None
        self.emu = emu
        self.session = self.call("POST", self.e("/control/session"), {})["session"]
        self.call("POST", self.e("/audio/mixer/master"), {"muted": True})


def capture(api, spec, args):
    path, warmup, key = (spec.split("@") + ["", ""])[:3]
    warmup = int(warmup) if warmup else args.warmup
    name = os.path.splitext(os.path.basename(path))[0].replace(" ", "_")
    api.call("POST", api.e("/pause"))
    if path.lower().endswith(".rzx"):
        answer = api.call("POST", api.e("/rzx/play"), {"path": os.path.abspath(path)})
        if answer.get("status") != "success":
            raise RuntimeError(f"rzx/play: {answer.get('message')}")
        if answer.get("emulator_id") and answer["emulator_id"] != api.emu:
            api.attach(answer["emulator_id"])
    else:
        answer = api.call("POST", api.e("/snapshot/load"), {"path": os.path.abspath(path), "switch_model": True})
        if answer.get("status") == "error":
            raise RuntimeError(f"snapshot/load: {answer.get('message')}")
        if answer.get("emulator_id") and answer["emulator_id"] != api.emu:
            api.attach(answer["emulator_id"])
    api.call("POST", api.e("/pause"))
    if key:
        api.call("POST", api.e("/run_frames"), {"frames": 50})
        api.call("POST", api.e("/keyboard/press"), {"key": key})
        api.call("POST", api.e("/run_frames"), {"frames": 5})
        api.call("POST", api.e("/keyboard/release"), {"key": key})
    if warmup:
        api.call("POST", api.e("/run_frames"), {"frames": warmup})
    width = height = 0
    with open(os.path.join(args.out, name + ".rgba"), "wb") as f:
        for _ in range(args.frames):
            api.call("POST", api.e("/run_frame"))
            pixels, headers = api.call("GET", api.e("/capture/framebuffer?format=rgba"), raw=True)
            headers = {k.lower(): v for k, v in headers.items()}
            width, height = int(headers["x-width"]), int(headers["x-height"])
            f.write(pixels)
    with open(os.path.join(args.out, name + ".json"), "w") as f:
        json.dump({"name": name, "source": path, "width": width, "height": height, "count": args.frames,
                   "warmup": warmup}, f)
    print(f"{name}: {args.frames} frames {width}x{height}", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=18750)
    ap.add_argument("--out", required=True)
    ap.add_argument("--warmup", type=int, default=150)
    ap.add_argument("--frames", type=int, default=200)
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    api = Api(args.port)
    api.attach(api.call("GET", "/emulator")["emulators"][0]["id"])
    try:
        for spec in args.files:
            try:
                capture(api, spec, args)
            except RuntimeError as err:
                print(f"{spec}: {err}", file=sys.stderr, flush=True)
    finally:
        try:
            api.call("DELETE", api.e("/control/session"))
        except RuntimeError:
            pass


if __name__ == "__main__":
    main()
