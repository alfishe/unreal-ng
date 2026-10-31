"""Command-line entry point for the TTD analyzer: the time-travel engine's
session file (schema 2, core/src/debugger/ttd/ttd.ksy).

Subcommands
-----------
``info FILE``
    Regions, devices, checkpoints, segments, events and bytes per stream.

``validate FILE``
    Every CRC, every piece version decoded, every part's dependency list.
    Exits 0 if no problems, 1 otherwise. Suitable for CI gates.

``parts FILE``
    The session file part by part: frames, records, bytes per stream, the
    parts each depends on, damage.

``recover FILE``
    What a file without its index (an unfinished recording) still holds: the
    complete parts a scan finds, the frames they cover, what was dropped.

v1's files (schema 1) are refused: their reader went with v1 (Phase 6 of the
TTD v2 migration, 2026-10-30). The emulator answers the questions the v1
commands answered (port searches, frames, coverage) on engine files itself:
``ttd port-events file=...``, ``ttd export-clip``, the coverage verbs.

Examples
--------
    ./run.sh info testdata/ttd/engine/active_demo.ttd
    ./run.sh validate testdata/ttd/engine/active_demo.ttd && echo "OK"
    ./run.sh parts testdata/ttd/v2/synthetic.ttd

Exit codes
----------
* 0 - success.
* 1 - integrity problems (only for ``validate``).
* 2 - file could not be parsed (bad magic, truncated, not schema 2).
* 4 - I/O error.
"""

from __future__ import annotations

import argparse
import sys
from typing import List, Optional

from . import __version__


def _format_size(n: int) -> str:
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n / (1024 * 1024):.2f} MB"


def _schema_of(path: str) -> int:
    try:
        with open(path, "rb") as f:
            head = f.read(6)
    except OSError as e:
        print(f"error: cannot read {path}: {e}", file=sys.stderr)
        sys.exit(4)
    if len(head) < 6 or head[:4] != b"TTDD":
        return 0
    return int.from_bytes(head[4:6], "little")


def _require_schema_2(path: str) -> None:
    schema = _schema_of(path)
    if schema == 2:
        return
    what = "a v1 file (schema 1; v1's reader was deleted with v1)" if schema == 1 else "not a TTD session file"
    print(f"error: {path}: {what}", file=sys.stderr)
    sys.exit(2)


def _open_v2_or_die(path: str):
    from .ttdcontainer import TtdContainerError, open_container
    data = open(path, "rb").read()
    try:
        return data, open_container(data)
    except TtdContainerError as e:
        print(f"error: cannot parse {path}: {e}", file=sys.stderr)
        sys.exit(2)


def cmd_info(args: argparse.Namespace) -> int:
    from .ttdcontainer import read_session
    _require_schema_2(args.file)
    data, c = _open_v2_or_die(args.file)
    s = read_session(c)
    print(f"=== {args.file} ===")
    print(f"file size: {_format_size(len(data))}")
    print(f"schema: 2 (the time-travel engine's session file){', converted from schema 1' if s.converted_from_v1 else ''}")
    print(f"finished: {'yes' if c.finalized else 'no (opened by scanning)'}, parts: {len(c.parts)}")
    print(f"regions: " + ", ".join(f"{r['name']} ({r['bytes']} B)" for r in s.regions))
    print(f"devices: {len(s.devices)}" + (": " + ", ".join(d["instance"] for d in s.devices) if s.devices else ""))
    if s.checkpoints:
        first, last = s.checkpoints[0].frame, s.checkpoints[-1].frame
        segments = sum(1 for cp in s.checkpoints if cp.baseline)
        print(f"checkpoints: {len(s.checkpoints)} (frames {first}-{last}), segments: {segments}, "
              f"piece versions: {len(s.versions)}")
    print(f"events: {s.events}, bus reads: {s.bus_reads}, bus writes: {s.bus_writes}, "
          f"vectors: {s.bus_vectors}, sector reads: {s.media_reads}, write journal: {s.write_journal}")
    for name, n in sorted(s.stream_bytes.items(), key=lambda kv: -kv[1]):
        print(f"  {name:14} {_format_size(n)}")
    for note in c.notes:
        print(f"note: {note}")
    if s.stopped_at:
        print(f"stopped at {s.stopped_at}")
    return 0


def cmd_validate(args: argparse.Namespace) -> int:
    from .ttdcontainer import validate
    _require_schema_2(args.file)
    data = open(args.file, "rb").read()
    problems = validate(data)
    if not problems:
        print(f"{args.file}: OK")
        return 0
    print(f"{args.file}: FAIL ({len(problems)} problem(s))")
    for p in problems:
        print(f"  {p}")
    return 1


def cmd_parts(args: argparse.Namespace) -> int:
    from .ttdcontainer import RECORD_HEADER, STREAM_NAMES, reachable
    _require_schema_2(args.file)
    _, c = _open_v2_or_die(args.file)
    for part in c.parts:
        sizes = {}
        for r in part.records:
            name = STREAM_NAMES.get(r.stream_id, f"stream {r.stream_id}")
            sizes[name] = sizes.get(name, 0) + r.stored_size + RECORD_HEADER
        state = "damaged: " + part.damage if part.damaged else ("ok" if reachable(c, part.index) else "unreachable")
        start = " [file start]" if part.file_start else ""
        print(f"part {part.index}: frames {part.first_frame}+{part.frame_count}, {len(part.records)} records, "
              f"depends on {part.dependencies or 'nothing'}, {state}{start}")
        print("    " + ", ".join(f"{k} {_format_size(v)}" for k, v in sizes.items()))
    return 0


def cmd_recover(args: argparse.Namespace) -> int:
    from .ttdcontainer import reachable
    _require_schema_2(args.file)
    _, c = _open_v2_or_die(args.file)
    kept = [p for p in c.parts if reachable(c, p.index)]
    if c.finalized:
        print(f"{args.file}: finished; nothing to recover ({len(c.parts)} parts)")
    elif kept:
        print(f"{args.file}: {len(kept)} complete part(s) kept, frames {kept[0].first_frame}-"
              f"{kept[-1].first_frame + kept[-1].frame_count - 1}")
    else:
        print(f"{args.file}: no complete part")
    for note in c.notes:
        print(f"  {note}")
    return 0


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="ttd-analyzer",
        description="Inspector for Unreal-NG .ttd session files (the engine's schema 2).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--version", action="version",
                   version=f"ttd-analyzer {__version__}")
    sub = p.add_subparsers(dest="cmd", required=True, metavar="<command>")

    for name, func, text in (("info", cmd_info, "Regions, checkpoints, events, bytes per stream."),
                             ("validate", cmd_validate, "Integrity checks (CI-friendly)."),
                             ("parts", cmd_parts, "Parts, bytes per stream, dependencies."),
                             ("recover", cmd_recover, "What a scan keeps of an unfinished file.")):
        sp = sub.add_parser(name, help=text)
        sp.add_argument("file", help="Path to .ttd file")
        sp.set_defaults(func=func)
    return p


def main(argv: Optional[List[str]] = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
