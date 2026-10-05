#!/usr/bin/env python3
"""Build the write journal of a saved .ttd session by replay, and save it.

The write journal answers "who wrote this address last" at once. Sessions
record it only where asked (D40: off by default, switched on and off during a
recording). This tool adds it to a saved session for any span, without the
GUI: it drives a running emulator through the WebAPI.

  1. reads the file's recorded machine (GET /ttd/file-info): model and
     General Sound card;
  2. creates an instance of that model, fits the card, loads the session;
  3. builds the journal for frames FROM..TO by replaying them
     (POST /ttd/journal/build; about 2-4 ms per frame);
  4. saves the session with its journal (POST /ttd/dump) and removes the
     instance.

The offline analyzer (src/main.py) reads .ttd files but cannot replay them;
this is the replaying half, so it needs the emulator.

Usage: start the desktop app with the WebAPI, then

    python3 tools/verification/ttd-analyzer/scripts/build_write_journal.py \
        session.ttd --out session-journal.ttd [--from 1200] [--to 1500]

Prints what was built and the spans the journal covers; exit status 1 when
the emulator refuses (another model needed, a recording running, ...).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any, Dict
from urllib.parse import quote

sys.path.insert(0, str(Path(__file__).resolve().parent))
from record_fixtures import DEFAULT_BASE_URL, ApiError, EmulatorApi, run_frames  # noqa: E402


def describe_spans(state: Dict[str, Any]) -> str:
    spans = state.get("write_journal_segments") or []
    if not spans:
        return "nothing"
    if state.get("write_journal_complete"):
        prefix = "the whole session"
    else:
        prefix = f"{len(spans)} span(s)"
    return prefix + ": " + ", ".join(
        f"frame {s['from_frame']}:{s['from_tinframe']} .. {s['to_frame']}:{s['to_tinframe']}" for s in spans)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("input", help="the .ttd session to read")
    parser.add_argument("--out", help="where to save it with the journal (default: <input>-journal.ttd)")
    parser.add_argument("--from", dest="from_frame", type=int, help="first frame (default: the session start)")
    parser.add_argument("--to", dest="to_frame", type=int, help="last frame (default: the session end)")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL, help=f"WebAPI base URL (default: {DEFAULT_BASE_URL})")
    args = parser.parse_args()

    source = Path(args.input).resolve()
    out = Path(args.out).resolve() if args.out else source.with_name(source.stem + "-journal.ttd")
    api = EmulatorApi(args.base_url, timeout=3600.0)   # a long build answers when done

    try:
        info = api.get(f"/ttd/file-info?path={quote(str(source))}")
        if not info or not info.get("ok", True):
            print(f"cannot read {source}: {(info or {}).get('error')}", file=sys.stderr)
            return 1
        machine = info.get("machine") or {}
        model = machine.get("model")
        if not model:
            print(f"{source}: the file does not name its model", file=sys.stderr)
            return 1

        emu_id = api.create_instance(model)
        base = f"/emulator/{emu_id}"
        try:
            card = machine.get("general_sound")
            if card and card != "none":
                # A slot change (owner decision Q10): the machine restarts with the card, under a new id
                reply = api.post(f"{base}/control/audio/gs", {"action": "switch_personality", "personality": card})
                emu_id = reply.get("restart", {}).get("emulatorId", emu_id)
                base = f"/emulator/{emu_id}"
            api.post(f"{base}/ttd/load", {"path": str(source)})

            body: Dict[str, Any] = {}
            if args.from_frame is not None:
                body["from_frame"] = args.from_frame
            if args.to_frame is not None:
                body["to_frame"] = args.to_frame
            built = api.post(f"{base}/ttd/journal/build", body)
            print(f"{source.name} ({model}): built {built['frames_built']} frame(s), {built['records']} writes"
                  + (f"; {built['frames_covered']} already covered" if built.get("frames_covered") else "")
                  + (f"; {built['frames_refused']} not replayable" if built.get("frames_refused") else "")
                  + ("; cancelled" if built.get("cancelled") else ""))
            print(f"the journal covers {describe_spans(built)}")

            api.post(f"{base}/ttd/dump", {"path": str(out)})
            print(f"saved {out}")
        finally:
            api.delete(f"/emulator/{emu_id}")
    except ApiError as exc:
        print(exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
