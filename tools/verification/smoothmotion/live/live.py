#!/usr/bin/env python3
"""Smooth motion live: what the unreal-qt window really shows, refresh by refresh, replayed and scored (README.md).

For each clip: launch unreal-qt with UNREAL_SMOOTH_TRACE (its own ports, muted, view settings off), load the clip
through the WebAPI, run it in real time with the GPU screen and smooth motion on, warm up, trace the refreshes, quit;
then replay.cpp (the core on the CPU: every plan and picture must be the window's; the timing) and smoothness.py
(the window's pictures at their own times, `off` and `blend` on the same refreshes, the same mode on a perfect clock).

    python3 -I live.py --bin cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt \\
        --replay scratch/live/replay --out scratch/live testdata/loaders/sna/aleste1.sna@8 \\
        scratch/2026-10-28-poc-047/rzx/zynaps.rzx@30

A clip is `<file>[@<warm-up seconds>]` (.sna / .z80 / .rzx). Writes <out>/<clip>/trace (the trace), <clip>/score (the
inputs of smoothness.py), <clip>.json (its results) and prints a table.
"""
import argparse
import json
import os
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SMOOTHNESS = os.path.join(HERE, "..", "smoothness.py")


class Api:
    def __init__(self, port):
        self.base = f"http://localhost:{port}/api/v1"
        self.session = None
        self.emu = None

    def call(self, method, path, body=None, timeout=60):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method)
        if self.session:
            req.add_header("X-Unreal-Session", self.session)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return json.loads(r.read() or b"{}")
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


def wait_for(predicate, seconds, what):
    end = time.time() + seconds
    while time.time() < end:
        try:
            if predicate():
                return
        except (OSError, RuntimeError, urllib.error.URLError):
            pass
        time.sleep(0.25)
    raise RuntimeError(f"timed out waiting for {what}")


def bring_to_front(pid):
    """A covered window gets no refreshes on macOS (occluded: the presenter rests), so nothing is traced"""
    if sys.platform == "darwin":
        subprocess.run(["osascript", "-e", 'tell application "System Events" to set frontmost of (first process whose '
                        f'unix id is {pid}) to true'], capture_output=True)


def run_clip(args, spec):
    path, _, warm = spec.partition("@")
    warm = float(warm) if warm else 5.0
    name = os.path.splitext(os.path.basename(path))[0].replace(" ", "_")
    clip_dir = os.path.join(args.out, name)
    trace = os.path.join(clip_dir, "trace")
    os.makedirs(trace, exist_ok=True)
    for f in ("start", "events.tsv", "frames.rgba", "refreshes.rgba"):
        if os.path.exists(os.path.join(trace, f)):
            os.remove(os.path.join(trace, f))
    env = dict(os.environ, UNREAL_WEBAPI_PORT=str(args.port), UNREAL_CLI_PORT=str(args.port + 1),
               UNREAL_MCP_PORT=str(args.port + 2), UNREAL_QT_VIEW_SETTINGS="off", UNREAL_SMOOTH_TRACE=trace,
               UNREAL_SMOOTH_TRACE_REFRESHES=str(args.refreshes))
    log = open(os.path.join(clip_dir, "unreal-qt.log"), "w")
    proc = subprocess.Popen([args.bin], env=env, stdout=log, stderr=subprocess.STDOUT)
    api = Api(args.port)
    try:
        wait_for(lambda: api.call("GET", "/emulator/models", timeout=2) is not None, 60, "the WebAPI")
        emus = api.call("GET", "/emulator").get("emulators", [])
        emu = emus[0]["id"] if emus else api.call("POST", "/emulator/start", {"model": args.model})["id"]
        api.attach(emu)
        if path.lower().endswith(".rzx"):
            answer = api.call("POST", api.e("/rzx/play"), {"path": os.path.abspath(path)})
            if answer.get("status") != "success":
                raise RuntimeError(f"rzx/play: {answer.get('message')}")
        else:
            answer = api.call("POST", api.e("/snapshot/load"), {"path": os.path.abspath(path), "switch_model": True})
            if answer.get("status") == "error":
                raise RuntimeError(f"snapshot/load: {answer.get('message')}")
        if answer.get("emulator_id") and answer["emulator_id"] != api.emu:
            api.attach(answer["emulator_id"])
        window = api.call("PUT", api.e("/video/window"), {"gpu": True, "smooth_motion": args.mode})
        if args.front:
            bring_to_front(proc.pid)
        try:
            api.call("POST", api.e("/resume"))
        except RuntimeError as err:
            if "not paused" not in str(err):
                raise
        time.sleep(warm)
        before = api.call("GET", api.e("/video/window"))["present"]
        open(os.path.join(trace, "start"), "w").close()
        events = os.path.join(trace, "events.tsv")

        def ended():
            with open(events) as f:
                f.seek(max(0, os.path.getsize(events) - 8))
                return f.read().strip().endswith("end")
        wait_for(ended, args.refreshes / 30 + 60, "the trace's end")
        after = api.call("GET", api.e("/video/window"))["present"]
        present = {k: after.get(k) for k in ("presenter", "vrr", "refresh_hz", "lead_ms", "error_ms", "gpu_wait_ms")}
        present["late"] = after.get("late", 0) - before.get("late", 0)
        present["dropped"] = after.get("dropped", 0) - before.get("dropped", 0)
        present["smooth"] = after.get("smooth", {}).get("mode"), after.get("smooth", {}).get("device")
        try:
            api.call("DELETE", api.e("/control/session"))
        except RuntimeError:
            pass
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(15)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()

    score = os.path.join(clip_dir, "score")
    os.makedirs(os.path.join(score, "ideal"), exist_ok=True)
    replay = subprocess.run([args.replay, trace, score], capture_output=True, text=True)
    print(f"== {name} ({window.get('backend')}, {present['presenter']}, smooth {present['smooth']})")
    print(replay.stdout.rstrip())
    print(f"present: late {present['late']}, dropped {present['dropped']}, error {present['error_ms']}, "
          f"lead {present['lead_ms']} ms")
    src_hz, disp_hz = (float(x) for x in open(os.path.join(score, "ideal", "rates.txt")).read().split())
    common = [sys.executable, "-I", SMOOTHNESS, "--source", os.path.join(score, "source.rgba"), "--src-hz",
              f"{src_hz:.4f}", "--jobs", str(args.jobs)]
    subprocess.run(common + ["--out-dir", score, "--filters", f"live,off,blend", "--times",
                             os.path.join(score, "times.txt"), "--json", os.path.join(clip_dir, "live.json")],
                   capture_output=True, text=True, check=True)
    subprocess.run(common + ["--out-dir", os.path.join(score, "ideal"), "--filters", args.mode, "--disp-hz",
                             f"{disp_hz:.4f}", "--json", os.path.join(clip_dir, "ideal.json")],
                   capture_output=True, text=True, check=True)
    live = json.load(open(os.path.join(clip_dir, "live.json")))["summary"]
    ideal = json.load(open(os.path.join(clip_dir, "ideal.json")))["summary"]
    rows = [("live " + args.mode, live["live"]), ("ideal clock " + args.mode, ideal[args.mode]),
            ("live off", live["off"]), ("live blend", live["blend"])]
    print(f"{'':24}{'jstep':>7}{'ghost':>7}{'tear%':>7}{'pic%':>7}   floor {live['live']['jfloor']:.2f}")
    for label, s in rows:
        print(f"{label:24}{s['jstep']:7.2f}{s['ghost']:7.2f}{100 * s['tear']:7.1f}{100 * s['pic']:7.1f}")
    result = {"clip": spec, "replay": replay.stdout, "replay_exit": replay.returncode, "present": present,
              "scores": {label: s for label, s in rows}}
    json.dump(result, open(os.path.join(args.out, name + ".json"), "w"), indent=1)
    if not args.keep:
        for d, files in ((trace, ("frames.rgba", "refreshes.rgba")),
                         (score, ("source.rgba", "live.rgba", "off.rgba", "blend.rgba")),
                         (os.path.join(score, "ideal"), (args.mode + ".rgba",))):
            for f in files:
                if os.path.exists(os.path.join(d, f)):
                    os.remove(os.path.join(d, f))
    return replay.returncode == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True, help="unreal-qt")
    ap.add_argument("--replay", required=True, help="replay.cpp built (README.md)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--port", type=int, default=18870, help="WebAPI port (CLI +1, MCP +2)")
    ap.add_argument("--model", default="pentagon", help="the instance's model before a snapshot switches it")
    ap.add_argument("--mode", default="interpolate", choices=["blend", "interpolate"])
    ap.add_argument("--refreshes", type=int, default=600)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--keep", action="store_true", help="keep the pixel files")
    ap.add_argument("--no-front", dest="front", action="store_false",
                    help="leave the window where it opens (macOS: a covered window traces nothing)")
    ap.add_argument("clips", nargs="+")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    ok = True
    for spec in args.clips:
        try:
            ok &= run_clip(args, spec)
        except (RuntimeError, subprocess.CalledProcessError) as err:
            print(f"{spec}: {err}", file=sys.stderr, flush=True)
            ok = False
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
