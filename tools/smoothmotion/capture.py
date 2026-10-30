#!/usr/bin/env python3
"""Capture consecutive emulated frames through the WebAPI (from POC 038), the input of draft-video-profile.py.

For each snapshot: load it, run `--warmup` frames, then `--frames` frames one by one (`run_frame`), each followed by
`capture/framebuffer?format=rgba`. Writes <out>/<name>.rgba (frames back to back) and <name>.json (width, height,
count). The emulator instance is the caller's (its own ports); the script mutes it.

    python3 -I tools/smoothmotion/capture.py --port 8390 --out scratch/video-profiles game.sna@300 demo.sna@300@space ...
"""
import argparse
import json
import os
import sys
import urllib.error
import urllib.request


SESSION = {}


def call(base, method, path, body=None, raw=False):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(base + path, data=data, method=method)
    if SESSION.get("id"):
        req.add_header("X-Unreal-Session", SESSION["id"])
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            payload = r.read()
            return (payload, dict(r.headers)) if raw else json.loads(payload or b"{}")
    except urllib.error.HTTPError as err:
        raise SystemExit(f"{method} {path}: {err.code} {err.read().decode(errors='replace')}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8390)
    ap.add_argument("--out", required=True)
    ap.add_argument("--warmup", type=int, default=150)
    ap.add_argument("--frames", type=int, default=200)
    ap.add_argument("snapshots", nargs="+")
    args = ap.parse_args()

    base = f"http://localhost:{args.port}/api/v1"
    emu = call(base, "GET", "/emulator")["emulators"][0]["id"]
    e = f"/emulator/{emu}"
    # Run control: an explicit session, so every request of the script is one client
    SESSION["id"] = call(base, "POST", f"{e}/control/session", {})["session"]
    try:
        capture_all(base, e, args)
    finally:
        call(base, "DELETE", f"{e}/control/session")


def capture_all(base, e, args):
    call(base, "POST", f"{e}/audio/mixer/master", {"muted": True})
    os.makedirs(args.out, exist_ok=True)

    for spec in args.snapshots:
        # path[@warmup[@key]]: a per-snapshot warmup, and a key tapped after the load (a demo's start key)
        snap, warmup, key = (spec.split("@") + ["", ""])[:3]
        warmup = int(warmup) if warmup else args.warmup
        name = os.path.splitext(os.path.basename(snap))[0].replace(" ", "_")
        call(base, "POST", f"{e}/pause")
        try:
            call(base, "POST", f"{e}/snapshot/load", {"path": os.path.abspath(snap)})
        except SystemExit as err:
            print(f"{name}: load failed: {err}", file=sys.stderr)
            continue
        call(base, "POST", f"{e}/pause")
        if key:
            call(base, "POST", f"{e}/run_frames", {"frames": 50})
            call(base, "POST", f"{e}/keyboard/press", {"key": key})
            call(base, "POST", f"{e}/run_frames", {"frames": 5})
            call(base, "POST", f"{e}/keyboard/release", {"key": key})
        call(base, "POST", f"{e}/run_frames", {"frames": warmup})
        width = height = 0
        with open(os.path.join(args.out, name + ".rgba"), "wb") as f:
            for _ in range(args.frames):
                call(base, "POST", f"{e}/run_frame")
                pixels, headers = call(base, "GET", f"{e}/capture/framebuffer?format=rgba", raw=True)
                headers = {k.lower(): v for k, v in headers.items()}
                width, height = int(headers["x-width"]), int(headers["x-height"])
                f.write(pixels)
        with open(os.path.join(args.out, name + ".json"), "w") as f:
            json.dump({"name": name, "source": snap, "width": width, "height": height, "count": args.frames}, f)
        print(f"{name}: {args.frames} frames {width}x{height}")


if __name__ == "__main__":
    main()
