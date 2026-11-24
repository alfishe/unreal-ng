#!/usr/bin/env python3
"""The video wall's smooth motion (`tilegrid.smooth_motion`, docs/emulator/design/gui-control) checked on a running
wall through its WebAPI: every mode in turn, what the tiles did with it, and what it cost.

    videowall_smooth.py --port 8090 [--snapshot game.sna] [--seconds 8] [--modes off,blend,interpolate-lite,interpolate]

Per mode: the grid's state (`smooth_motion`, `smooth {tiles, moving, stepped_down, too_slow, pictures, between,
frames_only, wall_steps, step_ms, max_step_ms, threads}`), pictures a second and the wall process's CPU (one core = 100 %).
Checked: `off` holds no tile engine (`smooth` is null) and the wall is `off` again at the end; a mode other than off
draws pictures between the frames when the machines' pictures move.
--snapshot loads it on every machine first (a moving picture). Exit code 1 when a check fails.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request


def call(base, method, path, body=None, timeout=30):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + path, method=method, data=data, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        try:
            return json.loads(e.read() or b"{}")
        except ValueError:
            return {"ok": False, "error": f"http {e.code}"}


def cpu_seconds(pid):
    """User + system CPU time of the process so far (psutil-free: /proc on Linux, ps elsewhere)"""
    try:
        with open(f"/proc/{pid}/stat") as f:
            parts = f.read().rsplit(")", 1)[1].split()
        return (int(parts[11]) + int(parts[12])) / os.sysconf("SC_CLK_TCK")
    except OSError:
        out = os.popen(f"ps -o time= -p {pid}").read().strip()
        if not out:
            return None
        days, _, rest = out.rpartition("-")
        fields = [float(x) for x in rest.split(":")]
        while len(fields) < 3:
            fields.insert(0, 0.0)
        return (int(days) if days else 0) * 86400 + fields[0] * 3600 + fields[1] * 60 + fields[2]


def grid_state(base):
    answer = call(base, "GET", "/gui/state?target=main/tilegrid")
    if not answer.get("ok"):
        raise SystemExit(f"no tile grid on the GUI plane: {answer}")
    return answer["body"]["state"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, required=True, help="the video wall's WebAPI port")
    parser.add_argument("--snapshot", help="loaded on every machine first")
    parser.add_argument("--seconds", type=float, default=8.0, help="measured per mode, after 3 s to settle")
    parser.add_argument("--modes", default="off,blend,interpolate-lite,interpolate,off")
    parser.add_argument("--pid", type=int, help="the wall's process, for its CPU (default: from the GUI plane's hosts)")
    args = parser.parse_args()
    base = f"http://127.0.0.1:{args.port}/api/v1"

    pid = args.pid
    if pid is None:
        hosts = (call(base, "GET", "/gui").get("body") or {}).get("hosts") or []
        pid = next((h.get("pid") for h in hosts if h.get("program") == "unreal-videowall"), None)
    state = grid_state(base)
    tiles = [t["instance"] for t in state.get("tiles", [])]
    print(f"wall: {len(tiles)} tiles, pid {pid}, smooth_motion {state.get('smooth_motion')}")
    if args.snapshot:
        for emulator in tiles:
            call(base, "POST", f"/emulator/{emulator}/snapshot/load", {"path": os.path.abspath(args.snapshot)}, 60)
            call(base, "PUT", f"/emulator/{emulator}/audio/mixer/master", {"muted": True})

    failures = []
    before = state.get("smooth_motion", "off")
    try:
        for mode in args.modes.split(","):
            answer = call(base, "POST", "/gui/tilegrid.smooth_motion", {"mode": mode, "target": "main/tilegrid"})
            if not answer.get("ok"):
                failures.append(f"{mode}: {answer}")
                print(f"{mode}: refused: {answer}")
                continue
            time.sleep(3)
            s0, c0, t0 = grid_state(base), cpu_seconds(pid) if pid else None, time.time()
            time.sleep(args.seconds)
            s1, c1, dt = grid_state(base), cpu_seconds(pid) if pid else None, time.time() - t0
            smooth0, smooth1 = s0.get("smooth") or {}, s1.get("smooth")
            cpu = None if c0 is None or c1 is None else round(100 * (c1 - c0) / dt, 1)
            if smooth1 is None:
                print(f"{mode}: in effect {s1.get('smooth_motion')}, no tile engines, wall CPU {cpu} %")
            else:
                pictures = (smooth1["pictures"] - smooth0.get("pictures", 0)) / dt
                between = (smooth1["between"] - smooth0.get("between", 0)) / dt
                print(f"{mode}: in effect {s1.get('smooth_motion')}, {smooth1['tiles']} engines on "
                      f"{smooth1['threads']} threads, {smooth1['moving']} moving, {pictures:.0f} pictures/s "
                      f"({between:.0f} between frames), a refresh {smooth1['step_ms']:.2f} ms (max "
                      f"{smooth1['max_step_ms']:.2f}), stepped down {smooth1['stepped_down']}, too slow "
                      f"{smooth1['too_slow']}, frames only {smooth1['frames_only']} (the wall stepped "
                      f"{smooth1['wall_steps']} times), wall CPU {cpu} %")
            if s1.get("smooth_motion") != mode:
                failures.append(f"{mode}: the grid says {s1.get('smooth_motion')}")
            if mode == "off" and smooth1 is not None:
                failures.append("off: tile engines are still there")
            if mode != "off":
                if smooth1 is None or smooth1["tiles"] != len(tiles):
                    failures.append(f"{mode}: {0 if smooth1 is None else smooth1['tiles']} engines for {len(tiles)} tiles")
                elif args.snapshot and smooth1["between"] == smooth0.get("between", 0):
                    failures.append(f"{mode}: no picture between frames in {dt:.0f} s")
    finally:
        call(base, "POST", "/gui/tilegrid.smooth_motion", {"mode": before, "target": "main/tilegrid"})
    for failure in failures:
        print("FAILED", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
