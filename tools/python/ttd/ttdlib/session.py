"""A branched session file, read the way the engine's loader reads it.

TTDSessionFile::Load (core/src/debugger/ttd/engine/ttdsessionfile.cpp) with
branch reading on, rule for rule, so a verdict here is the emulator's:

- A part belongs to the line its part end names (0 the trunk). A branch's
  records are in the twins of the trunk streams (0x0200 + id, 0x0240 + frame
  stream); the branch table (0x0200) creates a line.
- A trunk part that is unreachable (damaged, or depends on a damaged part) or
  fails stops the load there; a branch part that does is the end of that branch
  ("stops at part N"), the trunk and other branches stand. A failure in a
  branch's first part leaves the branch out whole.
- A branch's create record must parse, carry the part's line id, number lines
  in increasing order, and name a loaded parent holding a checkpoint at the
  fork frame; the first checkpoint's frame is the fork frame. A copy without
  changes takes the parent's tables at the fork (the part must depend on the
  parent's part of the fork); a whole copy must match the parent's pieces.
- In a branch part a version's base is (line + 1, number in that line's file)
  and the line is this one or an ancestor; in a trunk part it is number + 1.
  A part with the file-start bit restarts the line's numbering.

The content CRC of a checkpoint (the one TTDBranchFixtures_Test writes to
testdata/ttd/branches/expected.json): CRC32C over the CPU state (48 bytes),
then for every (region, piece) holding a version, ascending, the region (u32),
the piece (u32) and the version's content CRC (u32).

Worked example:

    s = read_branched(open_branched(Path("session.ttd").read_bytes()))
    for line in s.lines.values():
        print(line.id, line.parent, [cp.frame for cp in line.checkpoints])
    crcs = content_crcs(s, 1)              # [(frame, crc), ...] of branch 1
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple

from .ttdcontainer import (ENCODING_RANGES, ENCODING_XOR, ENCODING_ZERO, PIECE, Checkpoint, Container, Reader,
                           TtdContainerError, TtdError, Version, crc32c, is_chunked_twin, open_container, read_record,
                           reachable, stream_of_chunked_twin)

BRANCH_TABLE = 0x0200
BRANCH_FIRST, BRANCH_LAST = 0x0201, 0x023F
BRANCH_FRAME_FIRST, BRANCH_FRAME_LAST = 0x0240, 0x027F
FRAME_FIRST, FRAME_LAST = 0x0100, 0x013F
ENGINE_STREAMS = {1, 2, 3, 5, 6, 7, 13, 14, 15, 16}   # 2: piece tables (a folder's segment files), read past
BRANCH_STATE = 18          # holderstream::kBranchState: U8 1, Varint active line, the trunk's branch record
TIME_ANCHOR_BYTES = 24
NO_LINE = 0xFFFFFFFF


def is_holder_stream(sid: int) -> bool:
    return 8 <= sid <= 12 or 17 <= sid <= 0xFF


def branch_twin(sid: int) -> int:
    if FRAME_FIRST <= sid <= FRAME_LAST:
        return BRANCH_FRAME_FIRST + sid - FRAME_FIRST
    return BRANCH_TABLE + sid if 1 <= sid <= 0x3F else 0


def trunk_of_twin(sid: int) -> int:
    if BRANCH_FRAME_FIRST <= sid <= BRANCH_FRAME_LAST:
        return FRAME_FIRST + sid - BRANCH_FRAME_FIRST
    return sid - BRANCH_TABLE if BRANCH_FIRST <= sid <= BRANCH_LAST else 0


def known_stream(sid: int) -> bool:
    """TTDSessionFile::KnownStream (branch reading on)."""
    if is_chunked_twin(sid):
        return known_stream(stream_of_chunked_twin(sid))
    if FRAME_FIRST <= sid <= FRAME_LAST or is_holder_stream(sid) or sid == BRANCH_TABLE:
        return True
    trunk = trunk_of_twin(sid)
    if trunk:
        return known_stream(trunk)
    return sid in ENGINE_STREAMS


def open_branched(data: bytes) -> Container:
    """open_container with every branch stream known. Raises TtdContainerError."""
    return open_container(data, known_stream)


# ---------------------------------------------------------------------------
# Records: the branch table's create, the controller's branch record
# ---------------------------------------------------------------------------

@dataclass
class Create:
    line: int
    parent: int
    fork_frame: int
    fork_t: int
    meta: bytes


def parse_create(payload: bytes) -> Create:
    """The branch table's create record. Raises ValueError when it is not one."""
    r = Reader(payload)
    if r.u8() != 1:
        raise ValueError("not a create record")
    line, parent, frame, t = r.varint(), r.varint(), r.varint(), r.varint()
    size = r.varint()
    if size > r.left():
        raise ValueError("damaged create record")
    return Create(line, parent, frame, t, r.take(size))


def encode_create(c: Create) -> bytes:
    from .writer import varint
    return b"\x01" + varint(c.line) + varint(c.parent) + varint(c.fork_frame) + varint(c.fork_t) + \
        varint(len(c.meta)) + c.meta


@dataclass
class BranchMeta:
    """The controller's branch record, version 1 (ttdcontrollersessionfile.cpp WriteBranchRecord)."""
    guid: bytes = b"\0" * 16
    name: str = ""
    created_micros: int = 0
    created_by: str = ""
    run: str = ""
    reason: str = ""
    tags: List[str] = field(default_factory=list)
    note: str = ""
    pinned: bool = False
    end_frame: int = 0
    end_t: int = 0

    @property
    def guid_text(self) -> str:
        h = self.guid.hex()
        return f"{h[:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:]}"


def parse_meta(payload: bytes) -> Optional[BranchMeta]:
    """The controller's record, None when absent or not version 1 (the engine keeps any bytes)."""
    if not payload:
        return None
    try:
        r = Reader(payload)
        if r.u8() != 1:
            return None
        m = BranchMeta(guid=r.take(16), name=r.string(), created_micros=r.u64(), created_by=r.string(),
                       run=r.string(), reason=r.string())
        m.tags = [r.string() for _ in range(r.varint())]
        m.note = r.string()
        m.pinned = r.u8() != 0
        m.end_frame, m.end_t = r.varint(), r.varint()
        return m
    except ValueError:
        return None


def encode_meta(m: BranchMeta) -> bytes:
    from .writer import string, varint
    out = b"\x01" + m.guid + string(m.name) + struct.pack("<Q", m.created_micros) + string(m.created_by) + \
        string(m.run) + string(m.reason) + varint(len(m.tags))
    for t in m.tags:
        out += string(t)
    return out + string(m.note) + bytes([1 if m.pinned else 0]) + varint(m.end_frame) + varint(m.end_t)


# ---------------------------------------------------------------------------
# The session
# ---------------------------------------------------------------------------

@dataclass
class Line:
    id: int
    parent: int = -1                    # -1 the trunk
    fork_frame: int = 0
    fork_t: int = 0
    fork_index: int = -1                # the parent's checkpoint (index in its list) the line forks at
    meta: bytes = b""
    parts: List[int] = field(default_factory=list)
    checkpoints: List[Checkpoint] = field(default_factory=list)
    base: Dict[Tuple[int, int], int] = field(default_factory=dict)   # tables under the first checkpoint's changes
    holder: Dict[int, bytes] = field(default_factory=dict)

    @property
    def info(self) -> Optional[BranchMeta]:
        return parse_meta(self.meta)

    @property
    def end_frame(self) -> int:
        return self.checkpoints[-1].frame if self.checkpoints else self.fork_frame


@dataclass
class BranchedSession:
    container: Container
    snapshot_interval: int = 0
    regions: List[dict] = field(default_factory=list)       # memory regions, then one per device state
    lines: Dict[int, Line] = field(default_factory=dict)     # in creation order
    versions: List[Version] = field(default_factory=list)
    holder: Dict[int, bytes] = field(default_factory=dict)
    notes: List[str] = field(default_factory=list)          # branches stopped or left out
    stopped_at: str = ""                                     # the trunk's stop
    parts_loaded: int = 0
    version_line: List[int] = field(default_factory=list)

    @property
    def complete(self) -> bool:
        return self.parts_loaded == len(self.container.parts)

    def lineage(self, line: int) -> List[int]:
        """The line and its ancestors, the trunk first."""
        out = []
        while line >= 0:
            out.append(line)
            line = self.lines[line].parent
        return out[::-1]

    def children(self, line: int) -> List[int]:
        return [l.id for l in self.lines.values() if l.parent == line]

    def subtree(self, line: int) -> List[int]:
        out, todo = [], [line]
        while todo:
            cur = todo.pop(0)
            out.append(cur)
            todo.extend(self.children(cur))
        return out

    def is_ancestor(self, a: int, b: int) -> bool:
        """@p a is @p b or one of its ancestors."""
        while b >= 0:
            if a == b:
                return True
            b = self.lines[b].parent if b in self.lines else -1
        return False

    def active_line(self) -> int:
        """The active branch the holder's branch state names (0 when none or not loaded)."""
        state = self.holder.get(BRANCH_STATE)
        try:
            r = Reader(state or b"")
            if r.u8() == 1:
                line = r.varint()
                return line if line in self.lines else 0
        except ValueError:
            pass
        return 0


def _read_tables(s: BranchedSession) -> List[dict]:
    r = Reader(s.container.session_tables)
    if r.u16() != 1:
        raise TtdContainerError("unknown session tables version")
    s.snapshot_interval = r.u32()
    regions = []
    for _ in range(r.varint()):
        regions.append({"id": r.u16(), "name": r.string(), "owner_type": r.u16(), "owner_instance": r.string(),
                        "pieces": r.u32(), "bytes": r.u32(), "dirty_granularity": r.u32(),
                        "block_pieces": r.u32()})
    for _ in range(r.varint()):
        d = {"type": r.u16(), "instance": r.string(), "legacy_id": r.u8(), "layout_version": r.u16(),
             "state_size": r.u32(), "variable_size": bool(r.u8()), "firmware": r.u64()}
        d["restore_after"] = [(r.u16(), r.string()) for _ in range(r.varint())]
        d["time_fields"] = [(r.u16(), r.u8()) for _ in range(r.varint())]
        d["runs_behind_cpu"] = bool(r.u8())
        size = 4 + d["state_size"] + len(d["time_fields"]) * TIME_ANCHOR_BYTES
        regions.append({"id": 0x8000 + len(regions), "name": f"device {d['instance'] or d['type']}",
                        "pieces": (size + PIECE - 1) // PIECE, "bytes": size, "device": d})
    return regions


def table_at(s: BranchedSession, line_id: int, index: int) -> Dict[Tuple[int, int], int]:
    """The piece table (region, piece) -> version number at a line's checkpoint @p index."""
    line = s.lines[line_id]
    table = dict(line.base)
    for cp in line.checkpoints[:index + 1]:
        for region, piece, number in cp.changes:
            table[(region, piece)] = number
    return table


def content_crcs(s: BranchedSession, line_id: int) -> List[Tuple[int, int]]:
    """(frame, content CRC) of every checkpoint of the line (the module comment)."""
    line = s.lines[line_id]
    table = dict(line.base)
    out = []
    for cp in line.checkpoints:
        for region, piece, number in cp.changes:
            table[(region, piece)] = number
        blob = bytearray(cp.cpu)
        for (region, piece) in sorted(table):
            blob += struct.pack("<III", region, piece, s.versions[table[(region, piece)]].crc)
        out.append((cp.frame, crc32c(bytes(blob))))
    return out


class _LineFailed(Exception):
    """A line fails to load; made: the engine had added the line (its id counts as used)."""

    def __init__(self, why: str, made: bool = False):
        super().__init__(why)
        self.made = made


def read_branched(c: Container) -> BranchedSession:
    """Every line of the file the loader would load (module comment). Raises TtdContainerError when the tables
    cannot be read; damage inside parts is reported in notes / stopped_at."""
    s = BranchedSession(c)
    try:
        s.regions = _read_tables(s)
    except ValueError as e:
        raise TtdContainerError(f"damaged session tables: {e}") from e
    items: Dict[int, List[int]] = {}
    lost: Set[int] = set()
    created: Set[int] = set()        # ids ever made or refused: a create must number above all of them
    max_ids = 0                      # the engine's line count (placeholders included)

    for part in c.parts:
        line_of = part.branch
        if line_of and line_of in lost:
            continue

        def lose(why: str) -> None:
            lost.add(line_of)
            s.notes.append(f"branch {line_of} stops at part {part.index}: {why}")

        if not reachable(c, part.index):
            why = part.damage or "it depends on a damaged part"
            if line_of:
                lose(why)
                continue
            s.stopped_at = f"part {part.index}: {why}"
            break
        records: Dict[int, bytes] = {}
        table: Optional[bytes] = None
        why, read_ok = "", True
        holder: Dict[int, bytes] = {}
        for rec in part.records:
            if not known_stream(rec.stream_id):
                continue
            stream = rec.stream_id
            if stream == BRANCH_TABLE:
                if line_of == 0:
                    continue
                try:
                    table = read_record(c, rec)
                except ValueError:
                    table = None
                continue
            mirrored = trunk_of_twin(stream)
            if mirrored:
                if line_of == 0:
                    continue
                stream = mirrored
            elif line_of and not is_holder_stream(stream):
                continue
            if is_holder_stream(stream):
                try:
                    payload = read_record(c, rec)
                    if line_of == 0 or rec.stream_id == stream:
                        s.holder[stream] = payload
                    else:
                        holder[stream] = payload
                except ValueError as e:
                    s.notes.append(f"holder stream {rec.stream_id} is damaged: left out ({e})")
                continue
            try:
                records[stream] = read_record(c, rec)
            except ValueError as e:
                why, read_ok = str(e), False
                break
        if not read_ok:
            if line_of:
                lose(why)
                continue
            s.stopped_at = f"part {part.index}: {why}"
            break

        create: Optional[Create] = None
        if line_of and line_of not in s.lines:
            problem = ""
            try:
                if table is None:
                    raise ValueError
                create = parse_create(table)
            except ValueError:
                problem = "its creation record is missing or damaged"
            if not problem and (create.line != line_of or create.line < max_ids):
                problem = "it is numbered out of order"
            elif not problem and (create.parent not in s.lines or create.parent in lost):
                problem = "its parent is not loaded"
            if problem:
                lost.add(line_of)
                s.notes.append(f"branch {line_of} is left out: {problem}")
                continue
        if line_of == 0 and 0 not in s.lines:
            s.lines[0] = Line(0)
            max_ids = max(max_ids, 1)
        if part.file_start:
            items[line_of] = []
        my_items = items.setdefault(line_of, [])
        if 1 not in records or 3 not in records:
            why = "its pieces or checkpoints are missing"
            if line_of:
                lose(why)
                continue
            s.stopped_at = f"part {part.index}: {why}"
            break

        line = s.lines.get(line_of)
        new_line: Optional[Line] = None
        mark_versions = len(s.versions)
        mark_cps = len(line.checkpoints) if line else 0
        try:
            p = Reader(records[1])
            r = Reader(records[3])
            frame = 0
            for i in range(r.varint()):
                frame += r.varint()
                flags = r.u8()
                start = r.u64()
                cpu = r.take(48)
                chipset = r.take(120)
                r.take(r.varint())
                cursors = (r.varint(), r.varint(), r.varint(), r.varint())
                changes: List[Tuple[int, int, int]] = []
                for _ in range(r.varint()):
                    region, count = r.varint(), r.varint()
                    if count > r.left():
                        raise ValueError("damaged checkpoints")
                    for _ in range(count):
                        piece = r.varint()
                        encoding, depth, base = p.u8(), p.varint(), p.varint()
                        base_number = -1
                        if line_of and base:
                            number = p.varint()
                            base_line = base - 1
                            anchor = create.parent if create else line_of
                            if base_line not in items or number >= len(items[base_line]) or not (
                                    base_line == line_of or (base_line in s.lines and s.is_ancestor(base_line, anchor))):
                                raise ValueError("damaged checkpoints")
                            base_number = items[base_line][number]
                        elif base:
                            if base - 1 >= len(my_items):
                                raise ValueError("damaged checkpoints")
                            base_number = my_items[base - 1]
                        crc = p.u32()
                        payload = p.take(p.varint())
                        if depth > 0xFFFF or encoding > ENCODING_RANGES or \
                                (encoding in (ENCODING_XOR, ENCODING_RANGES)) != (base_number >= 0) or \
                                (encoding == ENCODING_ZERO) != (len(payload) == 0):
                            raise ValueError("damaged checkpoints")
                        s.versions.append(Version(encoding, depth, base_number, crc, payload, part.index))
                        s.version_line.append(line_of)
                        my_items.append(len(s.versions) - 1)
                        changes.append((region, piece, len(s.versions) - 1))
                cp = Checkpoint(frame, start, bool(flags & 1), cpu, chipset, cursors, changes, part.index)
                if create and i == 0:
                    new_line = _make_line(s, c, part, create, cp, line_of)
                    line = new_line
                    continue
                _import_checkpoint(s, line, cp)
        except (ValueError, _LineFailed) as e:
            why = str(e) if isinstance(e, _LineFailed) else "damaged checkpoints"
            if create and (new_line is not None or (isinstance(e, _LineFailed) and e.made)):
                max_ids = max(max_ids, line_of + 1)   # ReleaseLine keeps the slot
            del s.versions[mark_versions:]
            del s.version_line[mark_versions:]
            if line_of:
                lost.add(line_of)
                if create:
                    s.lines.pop(line_of, None)
                    s.notes.append(f"branch {line_of} is left out: {why}")
                else:
                    del line.checkpoints[mark_cps:]
                    s.notes.append(f"branch {line_of} stops at part {part.index}: {why}")
                continue
            s.stopped_at = f"part {part.index}: {why}"
            break
        if create:
            max_ids = line_of + 1
        line.parts.append(part.index)
        line.holder.update(holder)
        s.parts_loaded += 1
    if 0 not in s.lines or not s.lines[0].checkpoints:
        raise TtdError(s.stopped_at or "the file holds no checkpoint")
    return s


def _import_checkpoint(s: BranchedSession, line: Line, cp: Checkpoint) -> None:
    """TimeTravelEngine::ImportCheckpoint's checks."""
    if line.checkpoints and (cp.frame <= line.checkpoints[-1].frame or cp.start < line.checkpoints[-1].start):
        raise _LineFailed(f"frame {cp.frame} does not follow the last recorded frame")
    previous = -1
    for region, piece, _ in cp.changes:
        if region < previous or region >= len(s.regions):
            raise _LineFailed("changes are not sorted by region")
        previous = region
        if piece >= s.regions[region]["pieces"]:
            raise _LineFailed(f"change out of range (region {region}, piece {piece})")
    line.checkpoints.append(cp)


def _make_line(s: BranchedSession, c: Container, part, create: Create, cp: Checkpoint, line_id: int) -> Line:
    """The branch's first checkpoint (Load's create branch: ImportForkLine, or ImportBranchLine + the copy's check)."""
    parent = s.lines[create.parent]
    index = next((k for k, x in enumerate(parent.checkpoints) if x.frame >= create.fork_frame), -1)
    if cp.frame != create.fork_frame:
        raise _LineFailed("its creation record names another fork than its first checkpoint's")
    line = Line(line_id, create.parent, create.fork_frame, create.fork_t, index, create.meta)
    if not cp.changes:
        from_parent = any(d < len(c.parts) and c.parts[d].branch == create.parent and
                          c.parts[d].first_frame <= create.fork_frame <= c.parts[d].first_frame + c.parts[d].frame_count
                          for d in part.dependencies)
        if not from_parent:
            raise _LineFailed("its first part does not depend on its parent's part of the fork")
        if index < 0 or parent.checkpoints[index].frame != create.fork_frame or line_id >= 0xFFFF:
            raise _LineFailed(f"the branch forks at frame {create.fork_frame}, which its parent does not hold")
        line.base = table_at(s, create.parent, index)
        fork = parent.checkpoints[index]
        cp = Checkpoint(fork.frame, fork.start, cp.baseline, cp.cpu, cp.chipset, (0, 0, 0, 0), [], cp.part)
    else:
        if index < 0 or parent.checkpoints[index].frame != create.fork_frame or line_id >= 0xFFFF:
            raise _LineFailed(f"the branch forks at frame {create.fork_frame}, which its parent does not hold")
        try:
            _import_checkpoint(s, Line(line_id), cp)
        except _LineFailed as e:
            raise _LineFailed(str(e), True) from e
        theirs = table_at(s, create.parent, index)
        mine = {(r, p): n for r, p, n in cp.changes}
        if set(theirs) != set(mine) or any(s.versions[theirs[k]].crc != s.versions[mine[k]].crc for k in mine):
            raise _LineFailed("its fork's copy is not its parent's checkpoint at the fork", True)
    line.checkpoints.append(cp)
    s.lines[line_id] = line
    return line


def decode_piece(s: BranchedSession, number: int, cache: Dict[int, bytes]) -> bytes:
    """The 4 KB content of version @p number through its chain (any line). Raises ValueError when damaged."""
    from .ttdcontainer import decode_version
    return decode_version(s, number, cache)   # BranchedSession has the same `versions` list


def validate_branched(data: bytes) -> List[str]:
    """Every problem a branch-aware load finds, and the content of every version ([] = sound)."""
    try:
        c = open_branched(data)
    except TtdContainerError as e:
        return [str(e)]
    problems = [n for n in c.notes if "skipped" not in n or "damaged" in n]
    if not c.finalized:
        problems.append("not finalized (no valid index): an unfinished or damaged file")
    try:
        s = read_branched(c)
    except TtdError as e:
        return problems + [f"session: {e}"]
    if s.stopped_at:
        problems.append(f"session: stopped at {s.stopped_at}")
    problems += [f"session: {n}" for n in s.notes]
    cache: Dict[int, bytes] = {}
    for n in range(len(s.versions)):
        try:
            decode_piece(s, n, cache)
        except Exception as e:   # zstd errors included: reported, not raised
            problems.append(f"version {n}: {e}")
        if len(cache) > 4096:
            cache.clear()
    return problems
