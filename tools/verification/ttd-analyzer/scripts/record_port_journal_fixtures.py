#!/usr/bin/env python3
"""Record the port-journal fixtures and the emulator's answers to them.

Two real sessions (testdata/ttd/port-journals/, see testdata/ttd/README.md):

  dizzyx.ttd           Dizzy X (128K snapshot, AY music) on a Pentagon while
                       keys the game polls are pressed: 8, 0, 5, Q, SPACE
  greenberet-load.ttd  a 128K machine loading Green Beret from tape through
                       the ROM loader (Tape Loader in the 128K menu), 1000
                       frames of it

For each, a set of "when did the program ..." questions is asked of the live
session (POST /ttd/port-events) and of the saved file ("file": no load), the
two answers are checked equal, and the answers are written to expected.json.
The analyzer's test (tools/verification/ttd-analyzer/tests/test_port_search.py)
and the C++ test (TimeTravelManager_PortJournalFixture_Test) ask the same
questions of the files and must get these answers.

Usage: start the desktop app with the WebAPI (port 8090), then

    python3 tools/verification/ttd-analyzer/scripts/record_port_journal_fixtures.py

Every step runs an exact number of frames on a machine that is not running
(/run_frames), and keys are pressed between those steps, so a recording
depends on nothing but the build (record_fixtures.py explains why wall-clock
pacing is not used). The shipped configs fit NeoGS in the General Sound slot;
its ZX-DMA is not isolated by the port journals, so the classic card is fitted.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from record_fixtures import PROJECT_ROOT, EmulatorApi, run_frames  # noqa: E402

OUT_DIR = PROJECT_ROOT / "testdata/ttd/port-journals"

DIZZY_KEYS: List[Tuple[str, int, int]] = [
    # key, frames held, frames after the release
    ("8", 75, 25),
    ("0", 15, 75),
    ("5", 60, 40),
    ("q", 15, 50),
    ("space", 15, 100),
]

DIZZY_QUERIES: List[Dict[str, Any]] = [
    {"event": "key", "arg": "8"}, {"event": "key", "arg": "0"}, {"event": "key", "arg": "5"},
    {"event": "key", "arg": "q"}, {"event": "key", "arg": "space"}, {"event": "key", "arg": "p"},
    {"event": "key"},
    {"event": "border"}, {"event": "beeper", "limit": 20}, {"event": "beeper", "limit": 3, "newest": True},
    {"event": "ay-write", "arg": "7", "limit": 10}, {"event": "ay-write", "arg": "8", "limit": 10},
    {"event": "ay-select", "limit": 16}, {"event": "ay-read", "limit": 5},
    {"event": "ear"}, {"event": "in", "limit": 10},
    {"event": "in", "port": "0xEFFE", "match": "any-clear", "value_mask": "0x1F", "trigger": "rising"},
    {"event": "out", "port": "0x7FFD", "limit": 5},
    {"event": "ay-write", "arg": "7", "from": "200", "to": "260:0", "limit": 100},
]

TAPE_QUERIES: List[Dict[str, Any]] = [
    {"event": "ear", "limit": 50}, {"event": "ear", "limit": 20, "newest": True},
    {"event": "ear", "from": "900", "limit": 30},
    {"event": "key", "arg": "enter"}, {"event": "key"},
    {"event": "border", "limit": 50}, {"event": "in", "limit": 10}, {"event": "out", "limit": 10},
]


def fit_classic_gs(api: EmulatorApi, base: str) -> None:
    api.post(f"{base}/control/audio/gs", {"action": "switch_personality", "personality": "lle"})
    run_frames(api, base, 1)  # the switch is applied at a frame boundary


def ask(api: EmulatorApi, base: str, path: Path, queries: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    answers = []
    for q in queries:
        live = api.post(f"{base}/ttd/port-events", q)
        from_file = api.post(f"{base}/ttd/port-events", dict(q, file=str(path)))
        if live != from_file:
            raise SystemExit(f"{path.name} {q}: the file answered differently from the live session")
        answers.append({"query": q, "answer": live})
        print(f"  {json.dumps(q)}: {live['count']} hit(s)")
    return answers


def dump(api: EmulatorApi, base: str, path: Path) -> None:
    api.post(f"{base}/ttd/stop")
    status = api.get(f"{base}/ttd/status") or {}
    if not status.get("port_journal_active"):
        raise SystemExit(f"port journals are off: {status.get('port_journal_off_reason')}")
    print(f"  {status['port_read_count']} IN, {status['port_write_count']} OUT, "
          f"{status['checkpoint_count']} checkpoints -> {path}")
    api.post(f"{base}/ttd/dump", {"path": str(path)})


def record_dizzy(api: EmulatorApi) -> Dict[str, Any]:
    emu = api.create_instance("PENTAGON")
    base = f"/emulator/{emu}"
    try:
        fit_classic_gs(api, base)
        api.post(f"{base}/snapshot/load", {"path": str(PROJECT_ROOT / "testdata/loaders/z80/dizzyx.z80")})
        run_frames(api, base, 50)
        api.post(f"{base}/ttd/start")
        run_frames(api, base, 25)
        for key, held, after in DIZZY_KEYS:
            api.post(f"{base}/keyboard/press", {"key": key})
            run_frames(api, base, held)
            api.post(f"{base}/keyboard/release", {"key": key})
            run_frames(api, base, after)
        path = OUT_DIR / "dizzyx.ttd"
        dump(api, base, path)
        return {"file": path.name, "answers": ask(api, base, path, DIZZY_QUERIES)}
    finally:
        api.delete(base)


def record_tape(api: EmulatorApi) -> Dict[str, Any]:
    emu = api.create_instance("128K")
    base = f"/emulator/{emu}"
    try:
        fit_classic_gs(api, base)
        api.post(f"{base}/tape/load", {"path": str(PROJECT_ROOT / "testdata/loaders/tap/greenberet.tap")})
        run_frames(api, base, 150)  # the 128K menu is up
        api.post(f"{base}/ttd/start")
        api.post(f"{base}/keyboard/press", {"key": "enter"})  # Tape Loader
        run_frames(api, base, 10)
        api.post(f"{base}/keyboard/release", {"key": "enter"})
        run_frames(api, base, 1000)
        path = OUT_DIR / "greenberet-load.ttd"
        dump(api, base, path)
        return {"file": path.name, "answers": ask(api, base, path, TAPE_QUERIES)}
    finally:
        api.delete(base)


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    api = EmulatorApi(timeout=300.0)
    fixtures = [record_dizzy(api), record_tape(api)]
    (OUT_DIR / "expected.json").write_text(json.dumps({"fixtures": fixtures}, indent=1) + "\n")
    print(f"wrote {OUT_DIR / 'expected.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
