#!/usr/bin/env python3
"""gamestart.py - find the input that starts a ZX Spectrum game from its title screen.

Drives a running Unreal-NG instance over the WebAPI (default http://localhost:8090). The machine stands
at the game's title or menu screen. The script:

1. saves the state to a snapshot (the restart point of every attempt) and mutes the mixer;
2. probes which inputs the game reads: port watches on the Kempston port (#1F) and on each of the eight
   keyboard half-rows (#FE with A8..A15), counted over a few seconds without input;
3. builds the candidates the probe allows: a key whose half-row is never scanned cannot start the game,
   Kempston only when the game reads #1F and the machine answers it;
4. runs every candidate from the snapshot, plus one run without input (the baseline), and measures what the
   input changed: CPU addresses executed that the baseline never executed, and the screen digest;
5. prints the candidates, best first, and leaves the machine in the best one's state (or in the restart
   point with --restore).

A candidate is a sequence of steps: `key:<name>` (a tap), `joy:<buttons>` (a Kempston tap), `wait:<frames>`.
The built-in list covers the common menus: Space, Enter, 0, the joystick fire keys (Sinclair 1 fire = 0,
Sinclair 2 fire = 5, Cursor fire = 0), the menu digits 1..5 followed by a start key, S and P. --try adds
your own, e.g. --try "key:k,key:space" --try "key:2,wait:25,key:0".

Examples:
  gamestart.py --id <emulator-id>                       # probe, try, report, leave the best state
  gamestart.py --id <id> --frames 200 --json -o scratch/gamestart/dizzy.json
  gamestart.py --id <id> --probe-only                   # just the half-rows and ports the game reads

Recipe: .recipe/analysis/game-start-input.md
"""

import argparse
import json
import os
import struct
import sys
import urllib.error
import urllib.request

# Half-row n is selected by A(8+n) = 0. The keys from bit 0 (the outer key) to bit 4.
HALF_ROWS = [
    ("caps", "z", "x", "c", "v"),
    ("a", "s", "d", "f", "g"),
    ("q", "w", "e", "r", "t"),
    ("1", "2", "3", "4", "5"),
    ("0", "9", "8", "7", "6"),
    ("p", "o", "i", "u", "y"),
    ("enter", "l", "k", "j", "h"),
    ("space", "symbol", "m", "n", "b"),
]
ROW_OF = {key: row for row, keys in enumerate(HALF_ROWS) for key in keys}

# The built-in candidates, the most common first. A menu digit picks the controls, a start key starts.
START_KEYS = ["space", "enter", "0", "5", "s", "p", "1"]
MENU_DIGITS = ["1", "2", "3", "4", "5"]


class ApiError(RuntimeError):
    pass


class Api:
    def __init__(self, base, emulator_id):
        self.base = base.rstrip("/") + "/api/v1/emulator/" + emulator_id
        self.session = None

    def call(self, method, path, body=None, raw=False):
        data = json.dumps(body).encode() if body is not None else None
        headers = {"Content-Type": "application/json"} if data is not None else {}
        if self.session:
            headers["X-Unreal-Session"] = self.session
        request = urllib.request.Request(self.base + path, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                payload = response.read()
        except urllib.error.HTTPError as exc:
            text = exc.read().decode(errors="replace")
            raise ApiError(f"{method} {path}: HTTP {exc.code}: {text[:400]}") from None
        except urllib.error.URLError as exc:
            raise ApiError(f"{method} {path}: {exc.reason}") from None
        if raw:
            return payload
        return json.loads(payload) if payload else {}

    def get(self, path, raw=False):
        return self.call("GET", path, raw=raw)

    def post(self, path, body=None):
        return self.call("POST", path, body if body is not None else {})

    def put(self, path, body):
        return self.call("PUT", path, body)

    def take_control(self):
        """An explicit automation session: other clients' changes wait while the attempts run"""
        reply = self.post("/control/session", {"client": "gamestart.py", "surface": "webapi", "lease_seconds": 600})
        self.session = reply.get("session")

    def release_control(self):
        if self.session:
            try:
                self.call("DELETE", "/control/session")
            except ApiError:
                pass
            self.session = None

    def run_frames(self, count):
        if count > 0:
            self.post("/run_frames", {"count": int(count)})

    def start_memory_session(self):
        self.post("/profiler/memory/start")

    def executed(self):
        """The set of CPU addresses executed since the session started."""
        payload = self.get("/profiler/memory/counters?space=cpu&format=binary", raw=True)
        cells = len(payload) // 12
        executes = struct.unpack_from(f"<{cells}I", payload, 8 * cells)
        return {address for address, count in enumerate(executes) if count}

    def screen_digest(self):
        """The ULA screen's pixels and attributes with the border folded in (not page 5's program data)"""
        return self.get("/state/screen/digest?start=0x4000&end=0x5AFF").get("combined")


def parse_steps(text):
    steps = []
    for part in filter(None, (p.strip() for p in text.split(","))):
        kind, _, value = part.partition(":")
        if kind not in ("key", "joy", "wait") or not value:
            raise ValueError(f"bad step '{part}': use key:<name>, joy:<buttons> or wait:<frames>")
        steps.append((kind, value))
    return steps


def steps_text(steps):
    return ",".join(f"{kind}:{value}" for kind, value in steps) or "(no input)"


def run_steps(api, steps, tap_frames):
    for kind, value in steps:
        if kind == "key":
            api.post("/keyboard/tap", {"key": value, "frames": tap_frames})
            api.run_frames(tap_frames + 2)
        elif kind == "joy":
            api.post("/joystick/tap", {"buttons": value, "frames": tap_frames})
            api.run_frames(tap_frames + 2)
        else:
            api.run_frames(int(value, 0))


def probe(api, frames):
    """Port watches over the Kempston port and the eight half-rows, counted for `frames` frames."""
    api.call("DELETE", "/profiler/memory/watches")
    api.post("/profiler/memory/watches", {"name": "kempston", "port": "0x1F", "mask": "0x00FF", "values": True})
    for row in range(8):
        # A read with A(8+row) = 0 and A0 = 0 scans this half-row, whatever the other rows do
        api.post("/profiler/memory/watches",
                 {"name": f"row{row}", "port": "0x00FE", "mask": hex((1 << (8 + row)) | 0x0001)})
    api.start_memory_session()
    api.run_frames(frames)
    watches = {w["name"]: w for w in api.get("/profiler/memory/watches").get("watches", [])}
    api.call("DELETE", "/profiler/memory/watches")
    rows = [int(watches.get(f"row{row}", {}).get("reads", 0)) for row in range(8)]
    kempston = int(watches.get("kempston", {}).get("reads", 0))
    return rows, kempston


def any_key(rows):
    """Every half-row read as often as the others: one IN with A = 0 scans the whole keyboard ("press any key")"""
    return rows[0] > 0 and all(count == rows[0] for count in rows)


def build_candidates(rows, kempston_usable, extra):
    scanned = lambda key: rows[ROW_OF[key]] > 0
    candidates = []
    if kempston_usable:
        candidates.append([("joy", "fire")])
    if any_key(rows):
        # Any key does it: the start keys tell an "any key" wait from a menu that reads the keys one by one
        candidates.extend([[("key", "space")], [("key", "enter")], [("key", "0")]])
        candidates.extend(extra)
        return candidates
    for key in START_KEYS:
        if scanned(key):
            candidates.append([("key", key)])
    for digit in MENU_DIGITS:
        if not scanned(digit):
            continue
        for key in ("0", "space", "enter", "5"):
            if key != digit and scanned(key):
                candidates.append([("key", digit), ("wait", "25"), ("key", key)])
        if kempston_usable:
            candidates.append([("key", digit), ("wait", "25"), ("joy", "fire")])
    candidates.extend(extra)
    unique = []
    for steps in candidates:
        if steps not in unique:
            unique.append(steps)
    return unique


def save_restart_point(api, directory):
    os.makedirs(directory, exist_ok=True)
    errors = []
    for extension in ("szx", "z80", "sna"):
        path = os.path.abspath(os.path.join(directory, f"restart.{extension}"))
        try:
            api.post("/snapshot/save", {"path": path, "force": True})
            return path
        except ApiError as exc:
            errors.append(str(exc))
    raise ApiError("cannot save the restart point: " + "; ".join(errors))


def load_restart_point(api, snapshot):
    reply = api.post("/snapshot/load", {"path": snapshot, "switch_model": False})
    if reply.get("status") == "error":  # a refusal answers 200 with the reason
        raise ApiError(f"cannot load the restart point {snapshot}: {reply.get('message')}")


def attempt(api, snapshot, steps, frames, tap_frames):
    load_restart_point(api, snapshot)
    api.post("/pause")
    api.start_memory_session()
    run_steps(api, steps, tap_frames)
    api.run_frames(frames)
    return api.executed(), api.screen_digest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--url", default="http://localhost:8090", help="WebAPI base URL")
    parser.add_argument("--id", required=True, help="emulator id")
    parser.add_argument("--probe-frames", type=int, default=150, help="frames the probe counts the ports")
    parser.add_argument("--frames", type=int, default=150, help="frames each attempt runs after its input")
    parser.add_argument("--tap-frames", type=int, default=4, help="frames a key or the fire button is held")
    parser.add_argument("--min-new", type=int, default=64, help="new executed addresses that count as a start")
    parser.add_argument("--try", dest="extra", action="append", default=[], help="an extra candidate (steps)")
    parser.add_argument("--work", default="scratch/gamestart", help="directory of the restart snapshot")
    parser.add_argument("--probe-only", action="store_true", help="report the probe and stop")
    parser.add_argument("--restore", action="store_true", help="leave the machine at the restart point")
    parser.add_argument("--keep-sound", action="store_true", help="do not mute the mixer")
    parser.add_argument("--json", action="store_true", help="print the report as JSON")
    parser.add_argument("-o", "--output", help="also write the JSON report here")
    args = parser.parse_args()

    api = Api(args.url, args.id)
    try:
        extra = [parse_steps(text) for text in args.extra]
    except ValueError as exc:
        parser.error(str(exc))

    try:
        api.take_control()
        api.post("/pause")
        if not args.keep_sound:
            api.put("/audio/mixer/master", {"muted": True})
        snapshot = save_restart_point(api, args.work)
        rows, kempston_reads = probe(api, args.probe_frames)
        joystick = api.get("/joystick")
        kempston_usable = kempston_reads > 0 and bool(joystick.get("wired", False))
        report = {
            "restart_point": snapshot,
            "probe": {
                "frames": args.probe_frames,
                "half_rows": [{"row": n, "keys": list(HALF_ROWS[n]), "reads": rows[n]} for n in range(8)],
                "kempston_reads": kempston_reads,
                "kempston_wired": bool(joystick.get("wired", False)),
                "any_key": any_key(rows),
            },
            "candidates": [],
        }
        if kempston_reads and not joystick.get("wired", False):
            report["probe"]["hint"] = "the game reads #1F but no Kempston answers: slots plug edge.next kempston"

        if not args.probe_only:
            baseline_executed, baseline_digest = attempt(api, snapshot, [], args.frames, args.tap_frames)
            report["baseline"] = {"executed": len(baseline_executed), "digest": baseline_digest}
            for steps in build_candidates(rows, kempston_usable, extra):
                executed, digest = attempt(api, snapshot, steps, args.frames, args.tap_frames)
                new = len(executed - baseline_executed)
                report["candidates"].append({
                    "steps": steps_text(steps),
                    "new_executed": new,
                    "executed": len(executed),
                    "screen_changed": digest != baseline_digest,
                    "started": new >= args.min_new and digest != baseline_digest,
                })
            # The most new code wins (the game loop, not a key-definition screen); within 10 % of it the shorter input
            report["candidates"].sort(key=lambda c: (not c["started"], -c["new_executed"]))
            started = [c for c in report["candidates"] if c["started"]]
            if started:
                close = [c for c in started if c["new_executed"] * 10 >= started[0]["new_executed"] * 9]
                best = min(close, key=lambda c: c["steps"].count(","))
                report["candidates"].remove(best)
                report["candidates"].insert(0, best)
            best = report["candidates"][0] if report["candidates"] else None
            report["best"] = best["steps"] if best and best["started"] else None
            if report["best"] and not args.restore:
                attempt(api, snapshot, parse_steps(report["best"]), args.frames, args.tap_frames)
            else:
                load_restart_point(api, snapshot)
                api.post("/pause")
    except ApiError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        api.release_control()

    if args.output:
        os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
        with open(args.output, "w") as out:
            json.dump(report, out, indent=2)
    if args.json:
        print(json.dumps(report, indent=2))
        return 0

    print(f"restart point: {report['restart_point']}")
    scanned = [f"{'/'.join(r['keys'])}={r['reads']}" for r in report["probe"]["half_rows"] if r["reads"]]
    print(f"half-rows read: {', '.join(scanned) or 'none'}")
    if report["probe"]["any_key"]:
        print("every half-row read equally: an \"any key\" wait")
    print(f"kempston #1F reads: {kempston_reads} (wired: {report['probe']['kempston_wired']})")
    if "hint" in report["probe"]:
        print(f"hint: {report['probe']['hint']}")
    if args.probe_only:
        return 0
    print(f"baseline: {report['baseline']['executed']} addresses executed")
    for c in report["candidates"]:
        mark = "START" if c["started"] else "     "
        screen = "screen changed" if c["screen_changed"] else "same screen"
        print(f"{mark} {c['steps']:<36} new={c['new_executed']:<6} {screen}")
    print(f"best: {report['best'] or 'none started; try --frames, --try or the port trace'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
