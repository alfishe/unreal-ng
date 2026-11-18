#!/usr/bin/env python3
"""ttdtool: inspect, extract, prune, join and repair branched TTD sessions and recording folders without an emulator.

Design: docs/inprogress/2026-11-08-ttd-branches/tools.md. Format: storage.md there.

Commands (every one takes a .ttd file or a recording folder unless noted):

    info <path>                       header, streams, parts, sizes per stream and per branch, segments
    tree <path>                       the branch tree with the controller's records
    validate <path> [--json]          everything the loader checks, the branch table, dependencies, the folder index
    extract <path> --branch B (--lineage | --subtree) -o OUT [--apply]
    prune <path> <criteria> [-o OUT] [--apply] [--force]
    join <folder> -o OUT [--apply]    what Save does, for a crashed or unsaved folder
    reindex <folder> [--apply]        branches.tdx written again from the segment files
    recover <folder> [-o OUT] [--apply]
    gc [--root DIR] [--older-than DAYS] [--apply]
    stats <path> [--json]             per branch: frames, bytes, unique bytes (freed by deleting it), shared bytes
    flatten <path> --branch B -o OUT [--url URL]   the C++ engine's `ttd save lineage=` over the WebAPI

Rules for every writing command: a dry run unless --apply (the plan printed first); never in place (a new file,
written as OUT.partial and renamed; a folder's branches.tdx replaced by rename, tombstones before files go); a folder
whose owner.pid names a running process is refused; the output is validated before success is reported.

Exit codes: 0 done (or a sound file), 1 problems found or the command refused, 2 bad arguments.

Worked example:

    ttdtool.py tree session.ttd
    ttdtool.py prune session.ttd --run analyzer-7 --untagged -o pruned.ttd            # the plan
    ttdtool.py prune session.ttd --run analyzer-7 --untagged -o pruned.ttd --apply    # writes pruned.ttd
"""

from __future__ import annotations

import argparse
import datetime
import fnmatch
import json
import os
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ttdlib import folder as fld                                                      # noqa: E402
from ttdlib.session import (BranchedSession, Line, content_crcs, open_branched, read_branched,  # noqa: E402
                            validate_branched)
from ttdlib.ttdcontainer import RECORD_HEADER, STREAM_NAMES, Container, TtdError     # noqa: E402
from ttdlib.writer import ContainerWriter, copy_parts, stored_payload               # noqa: E402


# ---------------------------------------------------------------------------
# Loading a path
# ---------------------------------------------------------------------------

@dataclass
class Loaded:
    path: Path
    data: bytes                       # the file, or the folder joined in memory
    container: Container
    session: BranchedSession
    folder: bool
    files: List[Path]


def load(path: Path) -> Loaded:
    """A file, or a recording folder joined in memory (its files as Save would join them). Raises TtdError."""
    if path.is_dir():
        if not fld.is_recording_folder(path):
            raise TtdError(f"{path} is not a recording folder (no {fld.OWNER_FILE})")
        files = fld.join_list(path)
        data = fld.join_files(files)
        is_folder = True
    else:
        try:
            data = path.read_bytes()
        except OSError as e:
            raise TtdError(f"{path}: {e.strerror}") from e
        files, is_folder = [path], False
    c = open_branched(data)
    return Loaded(path, data, c, read_branched(c), is_folder, files)


def stream_name(c: Container, sid: int) -> str:
    desc = c.streams.get(sid)
    if desc:
        return desc.name
    return STREAM_NAMES.get(sid, f"stream {sid:#06x}")


def micros_text(micros: int) -> str:
    if not micros:
        return "-"
    return datetime.datetime.fromtimestamp(micros / 1e6, datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


def line_label(line: Line) -> str:
    info = line.info
    if line.id == 0:
        return "trunk"
    return f"branch {line.id}" + (f" '{info.name}'" if info and info.name else "")


def find_branch(s: BranchedSession, key: str) -> int:
    """A branch by its id, GUID (or a unique prefix of it) or name. Raises TtdError."""
    if key.isdigit() and int(key) in s.lines:
        return int(key)
    matches = []
    for line in s.lines.values():
        info = line.info
        if info and (info.name == key or info.guid_text == key.lower() or
                     (len(key) >= 8 and info.guid_text.startswith(key.lower()))):
            matches.append(line.id)
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise TtdError(f"no branch '{key}' (ids: {', '.join(str(i) for i in s.lines)})")
    raise TtdError(f"'{key}' names {len(matches)} branches ({matches}): use the id")


def part_bytes(c: Container, index: int) -> int:
    part = c.parts[index]
    return sum(RECORD_HEADER + r.stored_size for r in part.physical) + RECORD_HEADER   # its part end too (about)


# ---------------------------------------------------------------------------
# info, tree, stats
# ---------------------------------------------------------------------------

def cmd_info(args) -> int:
    ld = load(Path(args.path))
    c, s = ld.container, ld.session
    print(f"{ld.path}: {'recording folder' if ld.folder else 'session file'}, {len(ld.data)} bytes"
          + ("" if not ld.folder else " joined"))
    print(f"  uuid {c.uuid.hex()}  created {micros_text(c.created_micros)}  zstd {c.zstd_version}  "
          f"{'finalized' if c.finalized else 'NOT finalized (scanned)'}")
    print(f"  regions {len(s.regions)}  snapshot interval {s.snapshot_interval}  parts {len(c.parts)} "
          f"(loaded {s.parts_loaded})  versions {len(s.versions)}")
    if ld.folder:
        print("  segments:")
        for f in ld.files:
            seg, _ = fld.describe_segment(f)
            if seg:
                print(f"    {f.name:24s} line {seg.line:<3d} frames {seg.first_frame}+{seg.frame_span:<6d} "
                      f"parts {seg.parts:<4d} {seg.bytes} bytes")
            else:
                print(f"    {f.name:24s} unreadable")
    per_stream: Dict[int, List[int]] = {}
    for part in c.parts:
        for r in part.physical:
            t = per_stream.setdefault(r.stream_id, [0, 0, 0])
            t[0] += 1
            t[1] += r.stored_size + RECORD_HEADER
            t[2] += r.raw_size
    print("  streams:              records      stored         raw")
    for sid in sorted(per_stream):
        n, stored, raw = per_stream[sid]
        print(f"    {sid:#06x} {stream_name(c, sid)[:14]:14s} {n:7d} {stored:11d} {raw:11d}")
    print("  lines:")
    for line in s.lines.values():
        size = sum(part_bytes(c, p) for p in line.parts)
        print(f"    {line_label(line):32s} parent {line.parent:>3d}  frames {line.checkpoints[0].frame}.."
              f"{line.end_frame}  checkpoints {len(line.checkpoints):5d}  parts {len(line.parts):4d}  {size} bytes")
    for note in c.notes + s.notes + ([f"stopped at {s.stopped_at}"] if s.stopped_at else []):
        print(f"  note: {note}")
    return 0


def tree_rows(s: BranchedSession) -> List[Tuple[int, str]]:
    rows: List[Tuple[int, str]] = []

    def walk(line_id: int, prefix: str, last: bool, depth: int) -> None:
        line = s.lines[line_id]
        info = line.info
        if line_id == 0:
            text = f"trunk  frames {line.checkpoints[0].frame}..{line.end_frame}"
        else:
            text = (f"[{line_id}] {info.name if info else '(no record)'}  fork {line.fork_frame}:{line.fork_t}  "
                    f"length {line.end_frame - line.fork_frame}")
            if info:
                text += f"  created {micros_text(info.created_micros)} by {info.created_by or '-'}"
                text += f"  run {info.run or '-'}  reason {info.reason or '-'}"
                if info.tags:
                    text += f"  tags {','.join(info.tags)}"
                if info.pinned:
                    text += "  PINNED"
                text += f"  guid {info.guid_text}"
        if s.active_line() == line_id:
            text += "  (active)"
        rows.append((line_id, (prefix + ("`-- " if last else "|-- ") if depth else "") + text))
        kids = s.children(line_id)
        for k, kid in enumerate(kids):
            walk(kid, prefix + ("    " if last else "|   ") if depth else "", k == len(kids) - 1, depth + 1)

    walk(0, "", True, 0)
    return rows


def cmd_tree(args) -> int:
    ld = load(Path(args.path))
    for _, row in tree_rows(ld.session):
        print(row)
    for note in ld.session.notes:
        print(f"note: {note}")
    return 0


def branch_stats(ld: Loaded) -> List[dict]:
    c, s = ld.container, ld.session
    out = []
    for line in s.lines.values():
        own = sum(part_bytes(c, p) for p in line.parts)
        subtree = [l for l in s.subtree(line.id)]
        unique = sum(part_bytes(c, p) for l in subtree for p in s.lines[l].parts)
        shared = sum(part_bytes(c, p) for l in s.lineage(line.id)[:-1] for p in s.lines[l].parts)
        info = line.info
        out.append({"line": line.id, "name": info.name if info else ("trunk" if line.id == 0 else ""),
                    "parent": line.parent, "frames": line.end_frame - line.checkpoints[0].frame + 1,
                    "checkpoints": len(line.checkpoints), "parts": len(line.parts), "bytes": own,
                    "unique_bytes": unique, "shared_bytes": shared})
    return out


def cmd_stats(args) -> int:
    ld = load(Path(args.path))
    rows = branch_stats(ld)
    if args.json:
        print(json.dumps({"path": str(ld.path), "bytes": len(ld.data), "branches": rows}, indent=2))
        return 0
    print(f"{'line':>5} {'name':20s} {'parent':>6} {'frames':>7} {'parts':>6} {'bytes':>10} {'unique':>10} "
          f"{'shared':>10}")
    for r in rows:
        print(f"{r['line']:5d} {r['name'][:20]:20s} {r['parent']:6d} {r['frames']:7d} {r['parts']:6d} "
              f"{r['bytes']:10d} {r['unique_bytes']:10d} {r['shared_bytes']:10d}")
    return 0


# ---------------------------------------------------------------------------
# validate
# ---------------------------------------------------------------------------

def branch_problems(ld: Loaded) -> List[str]:
    """The branch table and dependency checks beyond the loader's."""
    c, s = ld.container, ld.session
    problems = []
    guids: Dict[bytes, int] = {}
    for line in s.lines.values():
        if line.id == 0:
            continue
        seen, cur = set(), line.id
        while cur > 0:
            if cur in seen:
                problems.append(f"branch {line.id}: its parents make a cycle")
                break
            seen.add(cur)
            cur = s.lines[cur].parent if cur in s.lines else -1
        parent = s.lines.get(line.parent)
        if parent is None:
            problems.append(f"branch {line.id}: parent {line.parent} is not in the file")
        elif not (parent.checkpoints[0].frame <= line.fork_frame <= parent.end_frame):
            problems.append(f"branch {line.id}: fork frame {line.fork_frame} is outside its parent")
        info = line.info
        if info is not None:
            if info.guid in guids and info.guid != b"\0" * 16:
                problems.append(f"branch {line.id}: GUID {info.guid_text} is branch {guids[info.guid]}'s too")
            guids.setdefault(info.guid, line.id)
    for line in s.lines.values():
        allowed = set()
        for l in s.lineage(line.id):
            allowed.update(s.lines[l].parts)
        for p in line.parts:
            for d in c.parts[p].dependencies:
                if d >= p:
                    problems.append(f"part {p}: depends on a later part {d}")
                elif d not in allowed:
                    problems.append(f"part {p} (line {line.id}): depends on part {d} outside its lineage")
    return problems


def folder_problems(folder: Path) -> List[str]:
    problems = []
    tdx = folder / fld.INDEX_FILE
    files = fld.segments(folder)
    if any(fld.parse_segment_name(f.name)[1] for f in files) and not tdx.is_file():
        problems.append(f"{fld.INDEX_FILE} is missing (reindex rebuilds it)")
    if tdx.is_file():
        try:
            index = fld.read_index(tdx.read_bytes())
        except TtdError as e:
            return problems + [f"{fld.INDEX_FILE}: {e}"]
        if index.damaged:
            problems.append(f"{fld.INDEX_FILE}: ends early ({index.damaged})")
        from_files = fld.index_from_files(folder)
        names = {seg.file for seg in index.segments}
        for seg in from_files.segments:
            if seg.file not in names:
                problems.append(f"{fld.INDEX_FILE}: no segment record for {seg.file}")
        listed = {seg.file: seg for seg in index.segments}
        for seg in from_files.segments:
            old = listed.get(seg.file)
            if old and old != seg:
                problems.append(f"{fld.INDEX_FILE}: the record of {seg.file} differs from the file")
        for seg in index.segments:
            if not (folder / seg.file).is_file() and seg.line not in index.deleted:
                problems.append(f"{fld.INDEX_FILE}: {seg.file} is listed but missing")
        made = {fld.create_of(cr).line for cr in index.creates if fld.create_of(cr)}
        for cr in from_files.creates:
            created = fld.create_of(cr)
            if created and created.line not in made:
                problems.append(f"{fld.INDEX_FILE}: no create record for branch {created.line}")
    for f in files:
        seg, _ = fld.describe_segment(f)
        if seg is None:
            problems.append(f"{f.name}: no complete part (cut, or not a session file)")
    return problems


def cmd_validate(args) -> int:
    path = Path(args.path)
    problems: List[str] = []
    if path.is_dir():
        problems += folder_problems(path)
        try:
            ld = load(path)
            problems += validate_branched(ld.data) + branch_problems(ld)
        except TtdError as e:
            problems.append(str(e))
    else:
        data = path.read_bytes()
        problems += validate_branched(data)
        try:
            c = open_branched(data)
            ld = Loaded(path, data, c, read_branched(c), False, [path])
            problems += branch_problems(ld)
        except TtdError:
            pass
    if args.json:
        print(json.dumps({"path": str(path), "sound": not problems, "problems": problems}, indent=2))
    else:
        print(f"{path}: {'sound' if not problems else f'{len(problems)} problem(s)'}")
        for p in problems:
            print(f"  {p}")
    return 0 if not problems else 1


# ---------------------------------------------------------------------------
# Writing: plan, check, write
# ---------------------------------------------------------------------------

def check_output(out: bytes, original: BranchedSession, keep: Sequence[int]) -> List[str]:
    """The output loads whole, holds exactly @p keep, and each kept line gives the original's positions and content
    CRCs. Returns the problems ([] = good)."""
    problems = validate_branched(out)
    if problems:
        return problems
    s = read_branched(open_branched(out))
    if sorted(s.lines) != sorted(keep):
        return [f"the output holds lines {sorted(s.lines)}, not {sorted(keep)}"]
    for line in keep:
        a, b = s.lines[line], original.lines[line]
        if (a.parent, a.fork_frame, a.fork_t) != (b.parent, b.fork_frame, b.fork_t):
            problems.append(f"line {line}: another parent or fork")
        elif content_crcs(s, line) != content_crcs(original, line):
            problems.append(f"line {line}: a checkpoint differs from the original's")
    return problems


def refuse_target(out: Path, inputs: Iterable[Path]) -> None:
    resolved = out.resolve()
    for p in inputs:
        if resolved == p.resolve() or (p.is_dir() and p.resolve() in resolved.parents):
            raise TtdError(f"{out}: never written in place (it is the input or inside it)")
    if out.exists():
        raise TtdError(f"{out} exists: choose another name")


def write_new(out: Path, data: bytes) -> None:
    """OUT.partial, synced, renamed to OUT."""
    partial = out.with_name(out.name + ".partial")
    with open(partial, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(partial, out)


def replace_file(target: Path, data: bytes) -> None:
    """A file in a folder replaced whole: written beside it, synced, renamed over it (a crash leaves the old or the
    new, never half)."""
    write_new(target.with_name(target.name + ".new"), data)
    os.replace(target.with_name(target.name + ".new"), target)


def finish_write(args, out: Path, data: bytes, original: BranchedSession, keep: Sequence[int], what: str) -> int:
    problems = check_output(data, original, keep)
    if problems:
        print(f"refused: the {what} would not validate:")
        for p in problems:
            print(f"  {p}")
        return 1
    if not args.apply:
        print(f"dry run: would write {out} ({len(data)} bytes, lines {sorted(keep)}); --apply writes it")
        return 0
    write_new(out, data)
    again = check_output(out.read_bytes(), original, keep)
    if again:
        print(f"{out}: written but does not validate: {again}")
        return 1
    print(f"wrote {out}: {len(data)} bytes, lines {sorted(keep)}; validated")
    return 0


def keep_parts(ld: Loaded, keep: Sequence[int]) -> bytes:
    keep_set = set(keep)
    return copy_parts(ld.container, [p.index for p in ld.container.parts if p.branch in keep_set])


def cmd_extract(args) -> int:
    path, out = Path(args.path), Path(args.output)
    fld.refuse_live(path)
    refuse_target(out, [path])
    ld = load(path)
    s = ld.session
    branch = find_branch(s, args.branch)
    if args.subtree:
        keep = sorted(set(s.lineage(branch)) | set(s.subtree(branch)))
    else:
        keep = s.lineage(branch)
    print(f"extract {'subtree' if args.subtree else 'lineage'} of {line_label(s.lines[branch])}: lines {sorted(keep)}")
    return finish_write(args, out, keep_parts(ld, keep), s, keep, "extract")


def prune_matches(s: BranchedSession, args, now_micros: int) -> List[int]:
    """The branches every given criterion matches (the trunk never)."""
    keep_lineage = set(s.lineage(find_branch(s, args.not_lineage_of))) if args.not_lineage_of else None
    out = []
    for line in s.lines.values():
        if line.id == 0:
            continue
        info = line.info
        length = line.end_frame - line.fork_frame
        checks = []
        if args.older_than is not None:
            checks.append(info is not None and info.created_micros < now_micros - args.older_than * 86_400_000_000)
        if args.created_by is not None:
            checks.append(info is not None and fnmatch.fnmatchcase(info.created_by, args.created_by))
        if args.run is not None:
            checks.append(info is not None and fnmatch.fnmatchcase(info.run, args.run))
        if args.tag is not None:
            checks.append(info is not None and args.tag in info.tags)
        if args.untagged:
            checks.append(info is None or not info.tags)
        if args.unpinned:
            checks.append(info is None or not info.pinned)
        if args.shorter_than is not None:
            checks.append(length < args.shorter_than)
        if args.longer_than is not None:
            checks.append(length > args.longer_than)
        if keep_lineage is not None:
            checks.append(line.id not in keep_lineage)
        if checks and all(checks):
            out.append(line.id)
    return out


def prune_plan(s: BranchedSession, matched: List[int], force: bool) -> Tuple[List[int], List[str]]:
    """The lines to delete (each match with its descendants), and the matches kept (pinned, or over a pinned one)."""
    remove, kept = set(), []
    for line in matched:
        sub = s.subtree(line)
        pinned = [l for l in sub if s.lines[l].info and s.lines[l].info.pinned]
        if pinned and not force:
            kept.append(f"{line_label(s.lines[line])}: " +
                        ("pinned" if pinned == [line] or line in pinned else f"over pinned {pinned}") +
                        " (--force deletes it)")
            continue
        remove.update(sub)
    return sorted(remove), kept


def cmd_prune(args) -> int:
    path = Path(args.path)
    criteria = [args.older_than, args.created_by, args.run, args.tag, args.shorter_than, args.longer_than,
                args.not_lineage_of]
    if all(c is None for c in criteria) and not args.untagged and not args.unpinned:
        print("prune needs at least one criterion", file=sys.stderr)
        return 2
    fld.refuse_live(path)
    ld = load(path)
    s = ld.session
    now = int((args.now if args.now is not None else time.time()) * 1e6)
    matched = prune_matches(s, args, now)
    remove, kept = prune_plan(s, matched, args.force)
    for k in kept:
        print(f"kept: {k}")
    if not remove:
        print("nothing to prune")
        return 0
    keep = [l for l in s.lines if l not in remove]
    freed = sum(part_bytes(ld.container, p) for l in remove for p in s.lines[l].parts)
    print(f"prune {len(remove)} branch(es) {remove}: about {freed} bytes")
    for l in remove:
        print(f"  {line_label(s.lines[l])}")
    data = keep_parts(ld, keep)
    if not ld.folder:
        if not args.output:
            print("a file prune writes a new file: give -o OUT", file=sys.stderr)
            return 2
        out = Path(args.output)
        refuse_target(out, [path])
        return finish_write(args, out, data, s, keep, "pruned file")
    # A folder: tombstones (one part of delete records in branches.tdx), then the lines' files go
    problems = check_output(data, s, keep)
    if problems:
        print(f"refused: the folder without them would not validate: {problems}")
        return 1
    files = [f for f in fld.segments(path) if fld.parse_segment_name(f.name)[1] in remove]
    tdx = path / fld.INDEX_FILE
    index_ok = False
    if tdx.is_file():
        try:
            index_ok = not fld.read_index(tdx.read_bytes()).damaged
        except TtdError:
            index_ok = False
    base = tdx.read_bytes() if index_ok else fld.rebuilt_index(path)
    new_index = fld.append_deletes(base, remove)
    if not args.apply:
        print(f"dry run: would append {len(remove)} delete record(s) to {tdx}"
              + ("" if index_ok else " (rebuilt first: it is missing or damaged)")
              + f", then delete {len(files)} file(s):")
        for f in files:
            print(f"  {f.name}")
        print("--apply does it")
        return 0
    replace_file(tdx, new_index)
    for f in files:
        f.unlink()
    again = load(path)
    problems = check_output(again.data, s, keep)
    if problems or sorted(fld.read_index(tdx.read_bytes()).deleted) != sorted(
            set(remove) | fld.read_index(base).deleted):
        print(f"{path}: pruned but does not validate: {problems}")
        return 1
    print(f"pruned {path}: {len(remove)} branch(es), {len(files)} file(s) deleted; validated")
    return 0


def cmd_join(args) -> int:
    path, out = Path(args.path), Path(args.output)
    if not fld.is_recording_folder(path):
        print(f"{path} is not a recording folder", file=sys.stderr)
        return 2
    fld.refuse_live(path)
    refuse_target(out, [path])
    files = fld.join_list(path)
    print(f"join {len(files)} file(s):")
    for f in files:
        print(f"  {f}")
    data = fld.join_files(files)
    s = read_branched(open_branched(data))
    return finish_write(args, out, data, s, list(s.lines), "joined file")


def cmd_reindex(args) -> int:
    path = Path(args.path)
    if not fld.is_recording_folder(path):
        print(f"{path} is not a recording folder", file=sys.stderr)
        return 2
    fld.refuse_live(path)
    data = fld.rebuilt_index(path)
    index = fld.read_index(data)
    tdx = path / fld.INDEX_FILE
    same = tdx.is_file() and tdx.read_bytes() == data
    print(f"{fld.INDEX_FILE} from the files: {index.records} record(s) ({len(index.segments)} segment(s), "
          f"{len(index.creates)} create(s), {len(index.deleted)} delete(s)), {len(data)} bytes"
          + (": the same as the folder's" if same else ""))
    if same:
        return 0
    if not args.apply:
        print(f"dry run: would replace {tdx}" + (f" (the old one kept as {tdx.name}.old)" if tdx.is_file() else "")
              + "; --apply does it")
        return 0
    if tdx.is_file():
        old = tdx.with_name(tdx.name + ".old")
        if old.exists():
            print(f"{old} exists: move it away first", file=sys.stderr)
            return 1
        os.link(tdx, old)
        replace_file(tdx, data)
    else:
        write_new(tdx, data)
    again = fld.read_index(tdx.read_bytes())
    if again.damaged or again.records != index.records:
        print(f"{tdx}: written but reads back wrong ({again.damaged})")
        return 1
    print(f"wrote {tdx}; validated")
    return 0


def recoverable(files: List[Path]) -> Tuple[List[Tuple[Path, Container, List[int]]], List[str]]:
    """Per file the parts that can be kept: each up to its first damaged part (and none of a file after a branch's
    first file is lost). Notes say what was left."""
    out, notes = [], []
    lost_lines = set()
    for f in files:
        parsed = fld.parse_segment_name(f.name)
        line = parsed[1] if parsed else 0
        if line in lost_lines:
            notes.append(f"{f.name}: left out (an earlier file of branch {line} is lost)")
            continue
        try:
            c = fld.open_container(f.read_bytes(), fld.known_stream)
        except TtdError as e:
            notes.append(f"{f.name}: unreadable ({e})")
            lost_lines.add(line)
            continue
        keep = []
        for part in c.parts:
            why = part.damage if part.damaged else ""
            for r in part.physical if not why else []:
                try:
                    stored_payload(c, r)
                except TtdError as e:
                    why = str(e)
                    break
            if why:
                notes.append(f"{f.name}: kept up to part {part.index} ({why})")
                lost_lines.add(line)
                break
            keep.append(part.index)
        notes += [f"{f.name}: {n}" for n in c.notes if "incomplete" in n or "no valid index" in n or "tail" in n]
        out.append((f, c, keep))
    return out, notes


def cmd_recover(args) -> int:
    path = Path(args.path)
    if not fld.is_recording_folder(path):
        print(f"{path} is not a recording folder", file=sys.stderr)
        return 2
    fld.refuse_live(path)
    files = fld.join_list(path)
    plan, notes = recoverable(files)
    for n in notes:
        print(f"note: {n}")
    first = plan[0][1] if plan else None
    if first is None:
        print("nothing recoverable")
        return 1
    streams = list(first.streams.values())
    have = {d.id for d in streams}
    for _, c, _ in plan[1:]:
        for d in c.streams.values():
            if d.id not in have:
                have.add(d.id)
                streams.append(d)
    w = ContainerWriter.like(first, streams)
    base = 0
    for f, c, keep in plan:
        if c.session_tables != first.session_tables:
            print(f"{f} is not of the same recording: left out")
            continue
        mapping = {old: base + k for k, old in enumerate(keep)}
        for old in keep:
            part = c.parts[old]
            for r in part.physical:
                w.add_stored(r.stream_id, r.flags, stored_payload(c, r), r.raw_size)
            w.end_part(part.first_frame, part.frame_count, part.branch,
                       [mapping[d] for d in part.dependencies if d in mapping], part.extra)
        base += len(keep)
    data = w.finalize()
    s = read_branched(open_branched(data))
    print("recoverable:")
    for line in s.lines.values():
        print(f"  {line_label(line):32s} frames {line.checkpoints[0].frame}..{line.end_frame} "
              f"({len(line.checkpoints)} checkpoints)")
    for n in s.notes:
        print(f"  note: {n}")
    if not args.output:
        return 0
    out = Path(args.output)
    refuse_target(out, [path])
    # A dropped part may take later parts' checkpoints with it: keep exactly what loads
    return finish_write(args, out, data, s, list(s.lines), "recovered file")


def cmd_gc(args) -> int:
    root = Path(args.root) if args.root else fld.recordings_root()
    now = args.now if args.now is not None else time.time()
    found = fld.crashed_folders(root, args.older_than, now)
    if not found:
        print(f"{root}: no crashed recording older than {args.older_than} day(s)")
        return 0
    for age in found:
        print(f"{age.path.name}: owner {age.owner or '-'} not running, newest file "
              f"{datetime.datetime.fromtimestamp(age.newest).strftime('%Y-%m-%d %H:%M')} ({age.age_days:.1f} days)")
    if not args.apply:
        print(f"dry run: {len(found)} folder(s) would be deleted; --apply deletes them")
        return 0
    failed = 0
    for age in found:
        try:
            fld.delete_folder(age)
            print(f"deleted {age.path}")
        except (OSError, TtdError) as e:
            failed += 1
            print(f"kept {age.path}: {e}")
    return 1 if failed else 0


# ---------------------------------------------------------------------------
# flatten (over the WebAPI)
# ---------------------------------------------------------------------------

class WebApi:
    def __init__(self, url: str, timeout: float = 30.0):
        self.url = url.rstrip("/")
        self.timeout = timeout
        self.session: Optional[str] = None

    def call(self, method: str, path: str, body: Optional[dict] = None) -> dict:
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.url + path, data=data, method=method)
        req.add_header("Content-Type", "application/json")
        if self.session:
            req.add_header("X-Unreal-Session", self.session)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                text = r.read().decode() or "{}"
        except urllib.error.HTTPError as e:
            text = e.read().decode(errors="replace")
            raise TtdError(f"{method} {path}: HTTP {e.code} {text[:300]}") from e
        return json.loads(text)


def emulator_reachable(url: str) -> bool:
    try:
        WebApi(url, timeout=2.0).call("GET", "/api/v1/emulator")
        return True
    except (TtdError, OSError, ValueError):
        return False


def cmd_flatten(args) -> int:
    path, out = Path(args.path).resolve(), Path(args.output).resolve()
    refuse_target(out, [path])
    ld = load(path)
    branch = find_branch(ld.session, args.branch)
    if not emulator_reachable(args.url):
        print(f"no emulator answers at {args.url}: flatten needs the C++ engine (`ttd save lineage=`). Start "
              f"unreal-qt (or pass --url) and run again; `extract --lineage` gives a branched file without it.")
        return 1
    if not args.apply:
        print(f"dry run: the emulator at {args.url} would load {path}, switch to branch {branch} and write its "
              f"lineage linear to {out}; --apply does it")
        return 0
    api = WebApi(args.url)
    info = api.call("GET", f"/api/v1/ttd/file-info?path={urllib.request.quote(str(path))}")
    model = (info.get("machine") or {}).get("model") or "PENTAGON"
    created = api.call("POST", "/api/v1/emulator/create", {"model": model})
    emu = created.get("id") or created.get("emulator_id")
    if not emu:
        raise TtdError(f"the emulator was not created: {created}")
    try:
        api.session = api.call("POST", f"/api/v1/emulator/{emu}/control/session",
                               {"client": "ttdtool-flatten", "surface": "webapi", "lease_seconds": 120}).get("session")
        api.call("POST", f"/api/v1/emulator/{emu}/ttd/load", {"path": str(path)})
        if branch:
            api.call("POST", f"/api/v1/emulator/{emu}/ttd/branches/{branch}/switch", {})
        dumped = api.call("POST", f"/api/v1/emulator/{emu}/ttd/dump", {"path": str(out), "lineage": "true"})
        if not dumped.get("ok", False):
            raise TtdError(f"the dump failed: {dumped}")
    finally:
        try:
            if api.session:
                api.call("DELETE", f"/api/v1/emulator/{emu}/control/session")
            api.call("DELETE", f"/api/v1/emulator/{emu}")
        except TtdError:
            pass
    problems = validate_branched(out.read_bytes())
    if problems:
        print(f"{out}: written by the emulator but does not validate: {problems}")
        return 1
    print(f"wrote {out} ({out.stat().st_size} bytes): branch {branch}'s lineage, linear; validated")
    return 0


# ---------------------------------------------------------------------------

def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="ttdtool.py", description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="command", required=True)

    for name, fn, helptext in (("info", cmd_info, "header, streams, parts, sizes"),
                               ("tree", cmd_tree, "the branch tree")):
        s = sub.add_parser(name, help=helptext)
        s.add_argument("path")
        s.set_defaults(fn=fn)
    s = sub.add_parser("validate", help="everything the loader checks, plus branches and the folder index")
    s.add_argument("path")
    s.add_argument("--json", action="store_true")
    s.set_defaults(fn=cmd_validate)
    s = sub.add_parser("stats", help="per branch frames and bytes")
    s.add_argument("path")
    s.add_argument("--json", action="store_true")
    s.set_defaults(fn=cmd_stats)

    s = sub.add_parser("extract", help="a new file with a branch's lineage or subtree")
    s.add_argument("path")
    s.add_argument("--branch", required=True, help="id, GUID (or 8+ leading hex digits) or name")
    g = s.add_mutually_exclusive_group()
    g.add_argument("--lineage", action="store_true", help="the trunk and every ancestor with the branch (default)")
    g.add_argument("--subtree", action="store_true", help="the lineage plus every descendant")
    s.add_argument("-o", "--output", required=True)
    s.add_argument("--apply", action="store_true")
    s.set_defaults(fn=cmd_extract)

    s = sub.add_parser("prune", help="delete branches by criteria (all given must match)")
    s.add_argument("path")
    s.add_argument("--older-than", type=float, metavar="DAYS", help="created more than DAYS days ago")
    s.add_argument("--created-by", metavar="WHO", help="creator (glob: 'mcp:*')")
    s.add_argument("--run", metavar="RUN", help="run id (glob)")
    s.add_argument("--tag", metavar="TAG", help="has the tag")
    s.add_argument("--untagged", action="store_true")
    s.add_argument("--unpinned", action="store_true")
    s.add_argument("--shorter-than", type=int, metavar="FRAMES", help="fewer frames from the fork to its end")
    s.add_argument("--longer-than", type=int, metavar="FRAMES")
    s.add_argument("--not-lineage-of", metavar="BRANCH", help="not the branch nor one of its ancestors")
    s.add_argument("--force", action="store_true", help="pinned branches too")
    s.add_argument("--now", type=float, help=argparse.SUPPRESS)
    s.add_argument("-o", "--output", help="a file prune's new file")
    s.add_argument("--apply", action="store_true")
    s.set_defaults(fn=cmd_prune)

    s = sub.add_parser("join", help="a folder's files as one file (what Save does)")
    s.add_argument("path")
    s.add_argument("-o", "--output", required=True)
    s.add_argument("--apply", action="store_true")
    s.set_defaults(fn=cmd_join)

    s = sub.add_parser("reindex", help="branches.tdx again from the segment files")
    s.add_argument("path")
    s.add_argument("--apply", action="store_true")
    s.set_defaults(fn=cmd_reindex)

    s = sub.add_parser("recover", help="what a crashed recording keeps, per branch")
    s.add_argument("path")
    s.add_argument("-o", "--output")
    s.add_argument("--apply", action="store_true")
    s.set_defaults(fn=cmd_recover)

    s = sub.add_parser("gc", help="crashed recording folders older than N days")
    s.add_argument("--root", help="default ~/.unreal-ng/ttd")
    s.add_argument("--older-than", type=float, default=fld.KEEP_DAYS, metavar="DAYS")
    s.add_argument("--now", type=float, help=argparse.SUPPRESS)
    s.add_argument("--apply", action="store_true")
    s.set_defaults(fn=cmd_gc)

    s = sub.add_parser("flatten", help="a branch's lineage as one linear file (needs a running emulator)")
    s.add_argument("path")
    s.add_argument("--branch", required=True)
    s.add_argument("-o", "--output", required=True)
    s.add_argument("--url", default="http://127.0.0.1:8090")
    s.add_argument("--apply", action="store_true")
    s.set_defaults(fn=cmd_flatten)
    return p


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parser().parse_args(argv)
    try:
        return args.fn(args)
    except TtdError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
