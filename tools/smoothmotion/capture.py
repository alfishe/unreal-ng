#!/usr/bin/env python3
"""Capture consecutive emulated frames of snapshots and RZX recordings through the WebAPI: the input of
draft-video-profile.py, the smooth-motion measurements (tools/verification/smoothmotion) and POC 049 / 047.

For each file: load it (.rzx: played with rzx/play; a recording or snapshot for another model switches the model and
the instance id changes: the script follows it), tap a key if given, run `--warmup` frames, then `--frames` frames one
by one (`run_frame`), each followed by `capture/framebuffer?format=rgba`. Writes <out>/<name>.rgba (frames back to
back) and <name>.json (width, height, count). It captures from the caller's first instance, or with `--model` from a
new instance of that model (removed at the end); it mutes every instance it captures from.

    python3 -I tools/smoothmotion/capture.py --port 8390 --out scratch/video-profiles game.sna@300 demo.sna@300@space
    python3 -I tools/smoothmotion/capture.py --port 8390 --model PENTAGON:128 --out scratch/smoothmotion \
        --warmup 50 --frames 400 testdata/video/smoothmotion/bin/smooth-speeds.sna

A file is `<path>[@<warm-up frames>[@<key>]]`.
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
    ap.add_argument("--port", type=int, default=8390)
    ap.add_argument("--model", help="capture from a new instance of this model, MODEL[:ram KB] (e.g. PENTAGON:128)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--warmup", type=int, default=150)
    ap.add_argument("--frames", type=int, default=200)
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    api = Api(args.port)
    created = None
    if args.model:
        model, _, ram = args.model.partition(":")
        body = {"model": model} | ({"ram_size": int(ram)} if ram else {})
        answer = api.call("POST", "/emulator/start", body)
        if not answer.get("id"):
            raise SystemExit(f"emulator/start {args.model}: {answer}")
        created = answer["id"]
        api.attach(created)
    else:
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
        if created:
            try:
                # the instance captured from last (a model switch replaced the one started)
                api.session = None
                api.call("DELETE", api.e())
            except RuntimeError:
                pass


if __name__ == "__main__":
    main()
