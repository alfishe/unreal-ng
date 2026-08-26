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
Start an emulator with the WebAPI enabled, then:

    python3 record_fixtures.py --out-dir "$(pwd)/testdata/ttd"

    python3 record_fixtures.py --out-dir /tmp \\
        --snapshot testdata/loaders/sna/"Dizzy Y.sna" \\
        --frames 600 --name dizzy

Fixtures live in the repository's shared corpus at ``testdata/ttd/``, alongside
the snapshots and disk images the rest of the suite uses.

Paths are resolved by the *emulator*, not by this script: the snapshot path and
the output path are both interpreted on the machine running the emulator.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from typing import Any, Dict, Optional


DEFAULT_BASE_URL = "http://localhost:8090"


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

    def first_instance_id(self) -> str:
        for info in self._instance_list():
            if info.get("destroying"):
                continue
            for key in ("id", "emulator_id", "instance_id", "uuid"):
                value = info.get(key)
                if value:
                    return str(value)
        raise ApiError("no running emulator instance found")


def record_session(api: EmulatorApi, emu_id: str, out_path: str,
                   frames: int, snapshot: Optional[str],
                   settle_frames: int) -> None:
    """Record one session and dump it to `out_path` on the emulator's host."""
    base = f"/emulator/{emu_id}"

    # Always start from a known machine. Without this the "idle" fixture simply
    # captures whatever the instance happened to be running - in practice the
    # program left over from a previous recording, which made the two default
    # fixtures render identical screens and neither of them reproducible.
    print("  resetting machine")
    api.set_running(emu_id, False)
    api.post(f"{base}/reset")

    if snapshot:
        print(f"  loading snapshot: {snapshot}")
        api.post(f"{base}/snapshot/load", {"path": snapshot})
        # Let the loaded program reach a steady state before recording, so the
        # fixture is not dominated by its startup.
        if settle_frames:
            api.set_running(emu_id, True)
            time.sleep(settle_frames / 50.0)
            api.set_running(emu_id, False)

    print("  starting TTD recording")
    api.set_running(emu_id, False)  # recording starts from a paused machine
    api.post(f"{base}/ttd/start")

    # Drive the length by checkpoint count, not by wall clock. The emulator is
    # not obliged to run at 50 Hz - unthrottled it ran ~9x realtime here, which
    # turned a "6 second" recording into 2714 frames instead of 300 and left the
    # fixtures unreproducible.
    api.set_running(emu_id, True)
    captured = 0
    deadline = time.monotonic() + max(30.0, frames / 10.0)
    while captured < frames and time.monotonic() < deadline:
        time.sleep(0.05)
        status = api.get(f"{base}/ttd/status") or {}
        captured = int(status.get("checkpoint_count",
                                  status.get("checkpointCount", 0)) or 0)
    api.set_running(emu_id, False)

    if captured < frames:
        print(f"  warning: wanted {frames} checkpoints, timed out at {captured}")
    print(f"  captured {captured} checkpoints")

    api.post(f"{base}/ttd/stop")

    print(f"  writing {out_path}")
    api.post(f"{base}/ttd/dump", {"path": out_path})


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Record .ttd fixtures from a running emulator via the WebAPI.")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL,
                        help=f"WebAPI base URL (default: {DEFAULT_BASE_URL})")
    parser.add_argument("--emulator-id", default=None,
                        help="Instance id (default: first running instance)")
    parser.add_argument("--out-dir", required=True,
                        help="Directory for the .ttd files, as seen by the EMULATOR host "
                             "(the repository keeps them in testdata/ttd)")
    parser.add_argument("--frames", type=int, default=300,
                        help="Frames to record per fixture (default: 300)")
    parser.add_argument("--settle-frames", type=int, default=100,
                        help="Frames to run after loading a snapshot, before recording")
    parser.add_argument("--snapshot", default=None,
                        help="Record a single fixture from this snapshot instead of the default set")
    parser.add_argument("--name", default=None,
                        help="Output basename when --snapshot is used")
    args = parser.parse_args()

    api = EmulatorApi(args.base_url)

    try:
        emu_id = args.emulator_id or api.first_instance_id()
    except ApiError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"emulator instance: {emu_id}")

    if args.snapshot:
        name = args.name or os.path.splitext(os.path.basename(args.snapshot))[0]
        fixtures = [(name, args.snapshot)]
    else:
        # The default set: one idle machine and one running program, which
        # bracket the range of what the analyzer and the codec work have to
        # cope with.
        fixtures = [
            ("idle_session", None),
            ("active_demo", "testdata/loaders/sna/Dizzy Y.sna"),
        ]

    for name, snapshot in fixtures:
        out_path = os.path.join(args.out_dir, f"{name}.ttd")
        print(f"\n[{name}]")
        try:
            record_session(api, emu_id, out_path, args.frames, snapshot,
                           args.settle_frames)
        except ApiError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    print("\nDone. Verify with:  ./run.sh info <file.ttd>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
