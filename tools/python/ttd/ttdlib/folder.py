"""Recording folders: segment files, branches.tdx, the owner file, the join.

Mirrors core/src/debugger/ttd/ttdrecordingfolders.cpp (storage.md §4) and
JoinSessionFiles (core/src/debugger/ttd/engine/ttdrecordingwriter.cpp):

- A folder holds owner.pid (the recording process's id; it marks a recording
  folder), segment-NNNN.ttd (the trunk) and segment-NNNN-bK.ttd (branch line K),
  numbered in capture order, and from the first fork on branches.tdx.
- branches.tdx is a container without trailer: one stream 0x0200, each record
  a part of its own (a prune's delete records share one). Kinds: 1 create (the
  branch table's create record), 2 source {Str path, Varint size, 16-byte UUID},
  3 delete {Varint line}, 4 segment {Str file, Varint line, Varint firstFrame,
  Varint frameSpan, Varint parts, Varint bytes}. A cut or damaged part ends
  the list; the complete records before it count.
- The join copies every part as stored, renumbering parts; the header is the
  first file's plus the streams later files declare; a damaged part refuses.

Worked example:

    files = segments(Path("~/.unreal-ng/ttd/2026-11-15-101500-pentagon"))
    data = join_files(files)               # the bytes "Save as" writes
"""

from __future__ import annotations

import errno
import os
import re
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional, Set, Tuple

from .session import BRANCH_TABLE, Create, known_stream, parse_create
from .ttdcontainer import Reader, StreamDesc, TtdContainerError, TtdError, open_container, read_record
from .writer import ContainerWriter, stored_payload, string, varint

OWNER_FILE = "owner.pid"
INDEX_FILE = "branches.tdx"
KEEP_DAYS = 7                              # kCrashedRecordingKeep

INDEX_CREATE, INDEX_SOURCE, INDEX_DELETE, INDEX_SEGMENT = 1, 2, 3, 4
_TAIL = "cut or damaged tail"
_SEGMENT = re.compile(r"^segment-(\d{1,9})(?:-b(\d{1,9}))?\.ttd$")


def recordings_root() -> Path:
    """<user data>/ttd: ~/.unreal-ng/ttd (FileHelper::GetUserDataPath)."""
    return Path.home() / ".unreal-ng" / "ttd"


def parse_segment_name(name: str) -> Optional[Tuple[int, int]]:
    """(number, line) of a segment file name, None for any other name (ParseSegmentFileName)."""
    m = _SEGMENT.match(name)
    if not m:
        return None
    line = int(m.group(2)) if m.group(2) is not None else 0
    if m.group(2) is not None and line == 0:
        return None
    return int(m.group(1)), line


def segment_name(number: int, line: int) -> str:
    return f"segment-{number:04d}.ttd" if line == 0 else f"segment-{number:04d}-b{line}.ttd"


def is_recording_folder(path: Path) -> bool:
    return path.is_dir() and (path / OWNER_FILE).is_file()


def read_owner(folder: Path) -> int:
    """The process id owner.pid names (0: missing or unreadable)."""
    try:
        text = (folder / OWNER_FILE).read_text().split()
        pid = int(text[0]) if text else 0
        return pid if 0 < pid <= 0xFFFFFFFF else 0
    except (OSError, ValueError):
        return 0


def process_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except OSError as e:
        return e.errno == errno.EPERM      # it exists, owned by someone else
    return True


def owner_alive(folder: Path) -> int:
    """The owner's pid when that process runs (a live recording), else 0."""
    pid = read_owner(folder)
    return pid if process_alive(pid) else 0


def refuse_live(path: Path) -> None:
    """Raise TtdError when @p path (a folder or a file in one) belongs to a recording that runs."""
    folder = path if path.is_dir() else path.parent
    pid = owner_alive(folder) if (folder / OWNER_FILE).is_file() else 0
    if pid:
        raise TtdError(f"{folder} is being recorded by process {pid}: refused")


# ---------------------------------------------------------------------------
# branches.tdx
# ---------------------------------------------------------------------------

@dataclass
class Segment:
    file: str
    line: int
    first_frame: int
    frame_span: int
    parts: int
    bytes: int


@dataclass
class Source:
    path: str
    size: int
    uuid: bytes


@dataclass
class BranchIndex:
    source: Optional[Source] = None
    creates: List[bytes] = field(default_factory=list)
    deleted: Set[int] = field(default_factory=set)
    segments: List[Segment] = field(default_factory=list)
    records: int = 0
    damaged: str = ""                       # why the list ended early ("" when it did not)
    zstd_version: int = 0


def read_index(data: bytes) -> BranchIndex:
    """branches.tdx read back (TTDRecordingFolder::ReadBranchIndex). Raises TtdContainerError without a header."""
    c = open_container(data, lambda sid: sid == BRANCH_TABLE)
    out = BranchIndex(zstd_version=c.zstd_version)
    for part in c.parts:
        if part.damaged:
            out.damaged = f"part {part.index}: {part.damage}"
            break
        ok = True
        for rec in part.records:
            try:
                if rec.stream_id != BRANCH_TABLE:
                    raise ValueError("not an index record")
                payload = read_record(c, rec)
                r = Reader(payload)
                kind = r.u8()
                if kind == INDEX_CREATE:
                    out.creates.append(payload)
                elif kind == INDEX_DELETE:
                    out.deleted.add(r.varint())
                elif kind == INDEX_SOURCE:
                    out.source = Source(r.string(), r.varint(), r.take(16))
                elif kind == INDEX_SEGMENT:
                    out.segments.append(Segment(r.string(), r.varint(), r.varint(), r.varint(), r.varint(),
                                                r.varint()))
                out.records += 1
            except ValueError as e:
                out.damaged = f"part {part.index}: {e}"
                ok = False
                break
        if not ok:
            break
    if not out.damaged and any(_TAIL in n or "damaged bytes" in n for n in c.notes):
        out.damaged = "; ".join(n for n in c.notes if _TAIL in n or "damaged bytes" in n)
    return out


def source_record(s: Source) -> bytes:
    return bytes([INDEX_SOURCE]) + string(s.path) + varint(s.size) + s.uuid


def segment_record(s: Segment) -> bytes:
    return bytes([INDEX_SEGMENT]) + string(s.file) + varint(s.line) + varint(s.first_frame) + \
        varint(s.frame_span) + varint(s.parts) + varint(s.bytes)


def delete_record(line: int) -> bytes:
    return bytes([INDEX_DELETE]) + varint(line)


def index_writer(zstd_version: int) -> ContainerWriter:
    """An empty branches.tdx (TTDRecordingFolder::OpenIndex's header)."""
    return ContainerWriter(0, b"\0" * 16, 0, zstd_version, [StreamDesc(BRANCH_TABLE, 1, True, "branch-index")], b"")


def write_index(content: BranchIndex, zstd_version: int) -> bytes:
    """branches.tdx with @p content in OpenIndex's order: the source, the deletes, then in the files' order each
    branch's create before its first segment, then any create left."""
    w = index_writer(zstd_version)

    def add(record: bytes) -> None:
        w.add_record(BRANCH_TABLE, record)
        w.end_part()

    if content.source:
        add(source_record(content.source))
    for line in sorted(content.deleted):
        add(delete_record(line))
    create, seen = 0, set()
    for seg in content.segments:
        if seg.line != 0 and seg.line not in seen and create < len(content.creates):
            seen.add(seg.line)
            add(content.creates[create])
            create += 1
        elif seg.line != 0:
            seen.add(seg.line)
        add(segment_record(seg))
    for rest in content.creates[create:]:
        add(rest)
    return w.unfinalized()


def append_deletes(data: bytes, lines: List[int]) -> bytes:
    """@p data (branches.tdx) with one more part holding a delete record per line (TTDRecordingFolder::DeleteLines).
    The new part continues the file's numbering; a damaged tail is dropped first."""
    c = open_container(data, lambda sid: sid == BRANCH_TABLE)
    good = [p for p in c.parts if not p.damaged]
    # Re-emit the complete parts as stored, then the new one: the result is what an append to an intact file gives
    w = ContainerWriter.like(c)
    for part in good:
        for rec in part.physical:
            w.add_stored(rec.stream_id, rec.flags, stored_payload(c, rec), rec.raw_size)
        w.end_part(part.first_frame, part.frame_count, part.branch, part.dependencies, part.extra)
    for line in lines:
        w.add_record(BRANCH_TABLE, delete_record(line))
    w.end_part()
    return w.unfinalized()


# ---------------------------------------------------------------------------
# Segment files
# ---------------------------------------------------------------------------

def segments(folder: Path, deleted: Optional[Set[int]] = None) -> List[Path]:
    """The segment files by number, without deleted lines' (TTDRecordingFolder::Segments)."""
    if deleted is None:
        deleted = set()
        if (folder / INDEX_FILE).is_file():
            try:
                deleted = read_index((folder / INDEX_FILE).read_bytes()).deleted
            except TtdContainerError:
                pass
    found = []
    for entry in folder.iterdir():
        parsed = parse_segment_name(entry.name)
        if parsed and parsed[1] not in deleted:
            found.append((parsed, entry))
    return [p for _, p in sorted(found, key=lambda x: x[0])]


def describe_segment(path: Path) -> Tuple[Optional[Segment], bytes]:
    """A segment's index record and its first part's create (b"" when none); (None, b"") when unreadable
    (DescribeSegment)."""
    data = path.read_bytes()
    try:
        c = open_container(data, known_stream)
    except TtdContainerError:
        return None, b""
    if not c.parts:
        return None, b""
    parsed = parse_segment_name(path.name)
    first, last = c.parts[0], c.parts[-1]
    seg = Segment(path.name, parsed[1] if parsed else 0, first.first_frame,
                  last.first_frame + last.frame_count - first.first_frame, len(c.parts), len(data))
    create = b""
    for rec in first.records:
        if rec.stream_id == BRANCH_TABLE:
            try:
                create = read_record(c, rec)
            except ValueError:
                create = b""
            break
    return seg, create


def index_from_files(folder: Path) -> BranchIndex:
    """What branches.tdx would hold if written from the files (IndexFromFiles)."""
    out = BranchIndex()
    created = set()
    for path in segments(folder):
        seg, create = describe_segment(path)
        if seg is None:
            continue
        if seg.line != 0 and create and seg.line not in created:
            created.add(seg.line)
            out.creates.append(create)
            out.records += 1
        out.segments.append(seg)
        out.records += 1
    return out


def rebuilt_index(folder: Path) -> bytes:
    """branches.tdx written again from the files, keeping the old one's deletes and source (RebuildBranchIndex).
    The zstd version is the old index's, else the first segment's (what the emulator that recorded wrote)."""
    content = index_from_files(folder)
    zstd_version = 0
    tdx = folder / INDEX_FILE
    if tdx.is_file():
        try:
            old = read_index(tdx.read_bytes())
            content.deleted, content.source = old.deleted, old.source
            zstd_version = old.zstd_version
        except TtdContainerError:
            pass
    if not zstd_version:
        files = segments(folder, content.deleted)
        if not files:
            raise TtdError(f"{folder}: no segment file")
        zstd_version = open_container(files[0].read_bytes(), known_stream).zstd_version
    return write_index(content, zstd_version)


def join_files(files: List[Path]) -> bytes:
    """One finalized file of @p files' parts (JoinSessionFiles). Raises TtdError on a damaged part or another
    recording's file."""
    if not files:
        raise TtdError("nothing to join")
    containers = []
    for f in files:
        try:
            containers.append(open_container(f.read_bytes(), known_stream))
        except TtdContainerError as e:
            raise TtdError(f"{f}: {e}") from e
    first = containers[0]
    streams = list(first.streams.values())
    have = {s.id for s in streams}
    for c in containers[1:]:
        for s in c.streams.values():
            if s.id not in have:
                have.add(s.id)
                streams.append(s)
    w = ContainerWriter.like(first, streams)
    part_base = 0
    for f, c in zip(files, containers):
        if c.session_tables != first.session_tables:
            raise TtdError(f"{f} is not of the same recording")
        for part in c.parts:
            if part.damaged:
                raise TtdError(f"{f}, part {part.index}: {part.damage}")
            for rec in part.physical:
                try:
                    stored = stored_payload(c, rec)
                except TtdError as e:
                    raise TtdError(f"{f}: {e}") from e
                w.add_stored(rec.stream_id, rec.flags, stored, rec.raw_size)
            w.end_part(part.first_frame, part.frame_count, part.branch, [part_base + d for d in part.dependencies],
                       part.extra)
        part_base += len(c.parts)
    return w.finalize()


def join_list(folder: Path, depth: int = 0) -> List[Path]:
    """The files "Save as" joins: the source's (checked), then the segments (TTDRecordingFolder::JoinList)."""
    out: List[Path] = []
    tdx = folder / INDEX_FILE
    index = None
    if tdx.is_file():
        try:
            index = read_index(tdx.read_bytes())
        except TtdContainerError:
            index = None
    if index and index.source:
        src = Path(index.source.path)
        if depth > 16:
            raise TtdError("the recording's sources go too deep")
        if src.is_dir():
            if not is_recording_folder(src):
                raise TtdError(f"the recording continues {src}, which is not a recording folder")
            out += join_list(src, depth + 1)
        else:
            try:
                data = src.read_bytes()
                c = open_container(data, known_stream)
                same = len(data) == index.source.size and c.uuid == index.source.uuid
            except (OSError, TtdContainerError) as e:
                raise TtdError(f"the recording continues the session {src}, which is missing ({e})") from e
            if not same:
                raise TtdError(f"the recording continues the session {src}, which is changed")
            out.append(src)
    deleted = index.deleted if index else set()
    out += segments(folder, deleted)
    return out


# ---------------------------------------------------------------------------
# Crashed folders
# ---------------------------------------------------------------------------

@dataclass
class FolderAge:
    path: Path
    owner: int
    alive: bool
    newest: float                    # the newest modification time (s since the epoch), the folder's own included
    age_days: float


def folder_age(folder: Path, now: Optional[float] = None) -> FolderAge:
    """The folder's newest write by os.stat on each name it lists (one level of subfolders deep is walked too:
    os.scandir, never `find`)."""
    now = time.time() if now is None else now
    newest = folder.stat().st_mtime
    stack = [folder]
    while stack:
        cur = stack.pop()
        with os.scandir(cur) as it:
            for entry in it:
                st = entry.stat(follow_symlinks=False)
                newest = max(newest, st.st_mtime)
                if entry.is_dir(follow_symlinks=False):
                    stack.append(Path(entry.path))
    pid = read_owner(folder)
    return FolderAge(folder, pid, process_alive(pid), newest, (now - newest) / 86400.0)


def crashed_folders(root: Path, older_than_days: float, now: Optional[float] = None) -> List[FolderAge]:
    """The recording folders directly under @p root whose owner does not run and whose newest file is older than
    the limit (CleanCrashedRecordings); files in @p root (saved recordings) never count."""
    out = []
    if not root.is_dir():
        return out
    for entry in sorted(root.iterdir()):
        if not entry.is_dir() or entry.is_symlink() or not (entry / OWNER_FILE).is_file():
            continue
        age = folder_age(entry, now)
        if not age.alive and age.age_days >= older_than_days:
            out.append(age)
    return out


def delete_folder(age: FolderAge) -> None:
    """Delete one crashed folder by its explicit names: each file, then each subfolder, then the folder; the owner is
    checked again first."""
    if process_alive(read_owner(age.path)):
        raise TtdError(f"{age.path}: its owner {read_owner(age.path)} runs now: kept")
    for dirpath, dirnames, filenames in os.walk(age.path, topdown=False):
        for name in filenames:
            os.unlink(os.path.join(dirpath, name))
        for name in dirnames:
            full = os.path.join(dirpath, name)
            if os.path.islink(full):
                os.unlink(full)
            else:
                os.rmdir(full)
    os.rmdir(age.path)


def create_of(payload: bytes) -> Optional[Create]:
    try:
        return parse_create(payload)
    except ValueError:
        return None
