#!/usr/bin/env python3
"""Record .ttd fixtures from a running emulator over the WebAPI.

This replaces an earlier script that *synthesised* .ttd files byte by byte in
Python. That approach had two problems that are worth remembering, because both
are easy to reintroduce:

  1. It was a second writer for the format. Every change to the C++ writer or to
     ttd.ksy had to be mirrored here by hand, and nothing enforced it — a
     forgotten update silently produced files in the old layout that the reader
     still accepted.

  2. Its page content was random bytes over zeros. Downstream consumers treated
     the output as real emulator memory (the compression PoC labelled its
     workloads "REAL_full" / "REAL_xor" and described them as "real XOR-delta
     buffers"), so codec decisions were calibrated against noise.

Recording through the API removes both: the file is produced by the same writer
that production uses, and it contains a real machine's memory.

Usage
-----
Start the emulator app with the WebAPI enabled (any instances in it are left
alone - every fixture is recorded on a fresh instance of its model, created and
removed by this script), then from anywhere:

    python3 tools/verification/ttd-analyzer/scripts/record_fixtures.py

That re-records the whole fixture corpus into ``testdata/ttd/``. One fixture:

    python3 tools/verification/ttd-analyzer/scripts/record_fixtures.py --only active_demo

An ad-hoc recording from another snapshot:

    python3 tools/verification/ttd-analyzer/scripts/record_fixtures.py \\
        --out-dir scratch --snapshot "testdata/loaders/sna/Dizzy Y.sna" \\
        --frames 600 --name dizzy

Relative paths (``--out-dir``, ``--snapshot``) are taken from the PROJECT ROOT,
not from the current directory, and are turned into absolute paths before they
are sent: the emulator resolves every path itself, so it must run on this
machine.

Every step that decides the content runs an exact number of frames on a paused
machine (``/run_frames``): the settle after loading a snapshot and the recording
itself. Wall-clock pacing made the fixtures unreproducible - the emulator runs
unthrottled at up to ~9x realtime, so "N frames' worth of sleep" was anything
from N to 9N frames.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


DEFAULT_BASE_URL = "http://localhost:8090"

# tools/verification/ttd-analyzer/scripts/record_fixtures.py -> project root
PROJECT_ROOT = Path(__file__).resolve().parents[4]

# The fixture corpus: (name, model, snapshot relative to the project root or
# None for a cold boot, settle frames run after loading it). Every fixture is
# recorded on a fresh instance of its model, so none depends on what ran
# before. Change a fixture here, in one place, never by passing different
# arguments by hand.
CORPUS: List[Tuple[str, str, Optional[str], int]] = [
    ("idle_session", "PENTAGON", None, 0),
    ("active_demo", "PENTAGON", "testdata/loaders/sna/Dizzy Y.sna", 0),
    ("demo_7threality", "PENTAGON", "testdata/loaders/sna/7threality.sna", 100),
    ("demo_across-the-edge-second", "PENTAGON", "testdata/loaders/sna/across-the-edge-second.sna", 100),
    # TurboSound FM (the Pentagon's TurboSound slot is TSFM): a tune already
    # playing in the snapshot, so FM and SSG registers change every frame
    ("tsfm_tech_support", "PENTAGON", "testdata/sound/tsfm/tech_support.sna", 0),
]
CORPUS_DIR = "testdata/ttd"

# /run_frames runs at most this many frames per call
RUN_FRAMES_LIMIT = 10000

# The sound configuration every fixture is recorded in. The TurboSound render
# loop schedules the SSG generator ticks by the output sample rate and by the
# decimator of the quality mode, so the recorded TSFM/AY state - and a replay
# that must reproduce it - depends on both. "auto" would follow the recording
# machine's audio device (48 kHz on one, 44.1 kHz on another).
CORE_RATE = 44100
FEATURES_ON = ("soundhq", "screenhq")  # the product defaults, set explicitly


def from_root(path: str) -> str:
    """Absolute path for the emulator: relative paths are project-root based."""
    p = Path(path)
    return str(p if p.is_absolute() else PROJECT_ROOT / p)


class ApiError(RuntimeError):
    pass


class EmulatorApi:
    """Thin WebAPI wrapper — only the calls this script needs."""

    def __init__(self, base_url: str = DEFAULT_BASE_URL, timeout: float = 30.0):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def _request(self, method: str, path: str,
                 body: Optional[Dict[str, Any]] = None) -> Any:
        url = f"{self.base_url}/api/v1{path}"
        data = json.dumps(body).encode("utf-8") if body is not None else None
        req = urllib.request.Request(url, data=data, method=method)
        if data is not None:
            req.add_header("Content-Type", "application/json")

        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                raw = resp.read()
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise ApiError(f"{method} {path} -> HTTP {exc.code}: {detail}") from exc
        except urllib.error.URLError as exc:
            raise ApiError(
                f"cannot reach the emulator at {self.base_url} ({exc.reason}). "
                f"Start one with the WebAPI enabled."
            ) from exc

        if not raw:
            return None
        try:
            return json.loads(raw)
        except json.JSONDecodeError:
            return raw.decode("utf-8", errors="replace")

    def get(self, path: str) -> Any:
        return self._request("GET", path)

    def post(self, path: str, body: Optional[Dict[str, Any]] = None) -> Any:
        return self._request("POST", path, body)

    def delete(self, path: str) -> Any:
        return self._request("DELETE", path)

    def create_instance(self, model: str) -> str:
        """A fresh, never-started instance: power-on state, nothing run yet.

        /emulator/create does not start the run loop, so nothing advances
        before /run_frames does - unlike /emulator/start, which runs the
        machine until it is paused.
        """
        info = self.post("/emulator/create", {"model": model}) or {}
        emu_id = info.get("id") or info.get("emulator_id")
        if not emu_id:
            raise ApiError(f"/emulator/create returned no id: {info}")
        return str(emu_id)

    def instance_info(self, emu_id: str) -> Dict[str, Any]:
        """Current record for one instance, from the instance list.

        There is no per-instance state endpoint; the list is the only place
        is_paused is exposed.
        """
        for info in self._instance_list():
            for key in ("id", "emulator_id", "instance_id", "uuid"):
                if str(info.get(key, "")) == emu_id:
                    return info
        return {}

    def set_running(self, emu_id: str, running: bool) -> None:
        """Resume or pause, tolerating the emulator already being in that state.

        /resume answers HTTP 400 when the instance is not paused, and /pause
        does the same in reverse, so issuing them blindly turns a no-op into a
        failure.
        """
        info = self.instance_info(emu_id)
        is_paused = bool(info.get("is_paused", False))

        if running and is_paused:
            self.post(f"/emulator/{emu_id}/resume")
        elif not running and not is_paused:
            self.post(f"/emulator/{emu_id}/pause")

    def _instance_list(self) -> list:
        # GET /api/v1/emulator returns either a JSON array or an object with
        # an "emulators" field, depending on server version.
        instances = self.get("/emulator") or []
        if isinstance(instances, dict):
            for key in ("emulators", "instances", "data"):
                if isinstance(instances.get(key), list):
                    instances = instances[key]
                    break
            else:
                instances = []
        return instances if isinstance(instances, list) else []



def run_frames(api: EmulatorApi, base: str, frames: int) -> None:
    """Run exactly `frames` frames on a paused machine."""
    while frames > 0:
        chunk = min(frames, RUN_FRAMES_LIMIT)
        api.post(f"{base}/run_frames", {"count": chunk})
        frames -= chunk


def pin_sound_configuration(api: EmulatorApi, base: str) -> None:
    """Pinned core rate and quality mode (see CORE_RATE); the rate switch is
    applied at a frame boundary, so one frame is run to apply it"""
    for feature in FEATURES_ON:
        api._request("PUT", f"{base}/feature/{feature}", {"enabled": True})
    api._request("PUT", f"{base}/settings/audio_rate", {"value": CORE_RATE})
    run_frames(api, base, 1)
    rate = ((api.get(f"{base}/settings") or {}).get("settings", {}).get("audio", {}).get("core_rate_hz"))
    if rate != CORE_RATE:
        raise ApiError(f"core rate is {rate}, expected the pinned {CORE_RATE}")


def record_session(api: EmulatorApi, emu_id: str, out_path: str,
                   frames: int, snapshot: Optional[str],
                   settle_frames: int, fresh: bool) -> None:
    """Record one session and dump it to `out_path` (absolute paths)."""
    base = f"/emulator/{emu_id}"

    if not fresh:
        # An existing instance: pause and reset. A reset keeps RAM and the
        # frame position (as the real reset button does), so such a recording
        # still depends on what the instance ran before - ad-hoc use only
        print("  resetting machine")
        api.set_running(emu_id, False)
        api.post(f"{base}/reset")

    print(f"  sound: {CORE_RATE} Hz, {', '.join(FEATURES_ON)} on")
    pin_sound_configuration(api, base)

    if snapshot:
        print(f"  loading snapshot: {snapshot}")
        api.post(f"{base}/snapshot/load", {"path": snapshot})
        # Let the loaded program reach a steady state before recording, so the
        # fixture is not dominated by its startup - an exact frame count
        if settle_frames:
            print(f"  settling {settle_frames} frames")
            run_frames(api, base, settle_frames)

    print("  starting TTD recording")
    api.post(f"{base}/ttd/start")
    run_frames(api, base, frames)

    status = api.get(f"{base}/ttd/status") or {}
    captured = int(status.get("checkpoint_count", status.get("checkpointCount", 0)) or 0)
    print(f"  captured {captured} checkpoints ({frames} frames + the baseline)")

    api.post(f"{base}/ttd/stop")

    print(f"  writing {out_path}")
    api.post(f"{base}/ttd/dump", {"path": out_path})


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Record .ttd fixtures from a running emulator via the WebAPI. "
                    "Relative paths are taken from the project root.")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL,
                        help=f"WebAPI base URL (default: {DEFAULT_BASE_URL})")
    parser.add_argument("--emulator-id", default=None,
                        help="Record on this existing instance (reset first) instead of a fresh "
                             "instance per fixture - ad-hoc use, not reproducible")
    parser.add_argument("--model", default="PENTAGON",
                        help="Ad-hoc: model of the fresh instance for --snapshot (default: PENTAGON)")
    parser.add_argument("--out-dir", default=CORPUS_DIR,
                        help=f"Directory for the .ttd files (default: {CORPUS_DIR})")
    parser.add_argument("--frames", type=int, default=300,
                        help="Frames to record per fixture (default: 300)")
    parser.add_argument("--only", default=None,
                        help="Re-record one corpus fixture by name")
    parser.add_argument("--snapshot", default=None,
                        help="Ad-hoc: record a single fixture from this snapshot instead of the corpus")
    parser.add_argument("--name", default=None,
                        help="Ad-hoc: output basename (default: the snapshot's)")
    parser.add_argument("--settle-frames", type=int, default=100,
                        help="Ad-hoc: frames to run after loading --snapshot (default: 100)")
    args = parser.parse_args()

    if args.snapshot:
        name = args.name or os.path.splitext(os.path.basename(args.snapshot))[0]
        fixtures = [(name, args.model, args.snapshot, args.settle_frames)]
    else:
        fixtures = [f for f in CORPUS if args.only in (None, f[0])]
        if not fixtures:
            print(f"error: no corpus fixture named {args.only!r} "
                  f"(have: {', '.join(f[0] for f in CORPUS)})", file=sys.stderr)
            return 1

    api = EmulatorApi(args.base_url)
    out_dir = from_root(args.out_dir)
    os.makedirs(out_dir, exist_ok=True)  # the emulator's dump does not create directories

    for name, model, snapshot, settle in fixtures:
        out_path = os.path.join(out_dir, f"{name}.ttd")
        print(f"\n[{name}]")
        emu_id = args.emulator_id
        try:
            if emu_id is None:
                emu_id = api.create_instance(model)
                print(f"  fresh {model} instance: {emu_id}")
            record_session(api, emu_id, out_path, args.frames,
                           from_root(snapshot) if snapshot else None, settle,
                           fresh=args.emulator_id is None)
        except ApiError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1
        finally:
            if args.emulator_id is None and emu_id is not None:
                try:
                    api.delete(f"/emulator/{emu_id}")
                except ApiError:
                    pass

    print("\nDone. Verify with:  tools/verification/ttd-analyzer/run.sh validate <file.ttd>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
