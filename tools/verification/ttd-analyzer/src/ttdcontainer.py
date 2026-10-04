"""TTD session files of schema 2: the time-travel engine's container.

The format is described in core/src/debugger/ttd/engine/ttdsession.ksy and
written by core/src/debugger/ttd/engine/ttdcontainer.cpp (the container) and
ttdsessionfile.cpp (the session in it). Design:
docs/inprogress/2026-09-25-ttd-v2-migration/phase-4-session-file-tdd.md.

A file is a header (magic "TTDD", schema 2, stream table, the session's
tables), records grouped into parts (each part closed by a part-end record),
then an index and a 24-byte trailer. Every record carries a CRC32C of its
header and of its payload; payloads are zstd-compressed when that was smaller.

This reader checks what the C++ reader checks (header, index and trailer CRCs,
every record's CRC, complete parts, unknown required streams) and more: it
decodes every piece version through its chain and checks the content CRC, and
checks each part's dependency list against the parts it really needs.

Worked example:

    data = Path("session.ttd").read_bytes()
    c = open_container(data)               # header, parts (index or scan)
    s = read_session(c)                    # checkpoints, versions, journals
    problems = validate(data)              # [] when the file is sound
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Tuple

import zstandard

MAGIC = b"TTDD"
SYNC = b"TREC"
TRAILER_MAGIC = b"TTDX"
SCHEMA = 2
FIXED_HEADER = 64
RECORD_HEADER = 32
TRAILER = 24
FLAG_COMPRESSED = 1
FLAG_PART_END = 2
PART_FILE_START = 1                  # part-end extra, bit 0
HEADER_CONVERTED_FROM_V1 = 1         # header flags, bit 0
PIECE = 4096
MAX_RAW = 1 << 30

STREAM_NAMES = {
    0: "container", 1: "pieces", 3: "checkpoints", 5: "events", 6: "configuration",
    7: "write-journal", 13: "bus-reads", 14: "bus-writes", 15: "bus-vectors", 16: "media-reads",
}
FRAME_STREAM_FIRST, FRAME_STREAM_LAST = 0x0100, 0x013F    # frame-boundary streams (D19); 0x0100 = screenshot
KNOWN_STREAMS = (set(STREAM_NAMES) - {0}) | set(range(FRAME_STREAM_FIRST, FRAME_STREAM_LAST + 1))

ENCODING_FULL, ENCODING_XOR, ENCODING_ZERO, ENCODING_RANGES = 0, 1, 2, 3
ENCODING_NAMES = {0: "full", 1: "xor", 2: "zero", 3: "ranges"}


class TtdContainerError(Exception):
    """The file cannot be opened (not a session file, damaged header or tables)."""


# ---------------------------------------------------------------------------
# CRC32C (Castagnoli), as codec::Crc32C in ttdcompression.h
# ---------------------------------------------------------------------------

def _make_table() -> List[int]:
    table = []
    for i in range(256):
        crc = i
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
        table.append(crc)
    return table


_CRC_TABLE = _make_table()


def crc32c(data: bytes, crc: int = 0) -> int:
    crc ^= 0xFFFFFFFF
    table = _CRC_TABLE
    for b in data:
        crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF


# ---------------------------------------------------------------------------
# Bytes
# ---------------------------------------------------------------------------

class Reader:
    """Bounds-checked little-endian reading; every overrun raises ValueError."""

    def __init__(self, data: bytes, pos: int = 0, end: Optional[int] = None):
        self.data = data
        self.pos = pos
        self.end = len(data) if end is None else end

    def left(self) -> int:
        return self.end - self.pos

    def take(self, n: int) -> bytes:
        if n < 0 or n > self.left():
            raise ValueError("past the end")
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def u8(self) -> int:
        return self.take(1)[0]

    def u16(self) -> int:
        return struct.unpack("<H", self.take(2))[0]

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]

    def u64(self) -> int:
        return struct.unpack("<Q", self.take(8))[0]

    def varint(self) -> int:
        value, shift = 0, 0
        while shift < 64:
            b = self.u8()
            value |= (b & 0x7F) << shift
            if not b & 0x80:
                return value
            shift += 7
        raise ValueError("varint too long")

    def string(self) -> str:
        return self.take(self.varint()).decode("utf-8", "replace")


# ---------------------------------------------------------------------------
# The container
# ---------------------------------------------------------------------------

@dataclass
class StreamDesc:
    id: int
    layout: int
    required: bool
    name: str


@dataclass
class Record:
    offset: int
    stream_id: int
    flags: int
    part_index: int
    sequence: int
    stored_size: int
    raw_size: int
    payload_crc: int


@dataclass
class Part:
    index: int
    first_frame: int = 0
    frame_count: int = 0
    branch: int = 0
    records: List[Record] = field(default_factory=list)
    dependencies: List[int] = field(default_factory=list)
    extra: bytes = b""
    damaged: bool = False
    damage: str = ""

    @property
    def file_start(self) -> bool:
        return bool(self.extra) and bool(self.extra[0] & PART_FILE_START)


@dataclass
class Container:
    data: bytes
    flags: int
    uuid: bytes
    created_micros: int
    zstd_version: int
    streams: Dict[int, StreamDesc]
    session_tables: bytes
    data_start: int
    parts: List[Part] = field(default_factory=list)
    finalized: bool = False
    index_extra: bytes = b""
    notes: List[str] = field(default_factory=list)


def _record_header(data: bytes, offset: int) -> Optional[Record]:
    if offset + RECORD_HEADER > len(data):
        return None
    h = data[offset:offset + RECORD_HEADER]
    if h[:4] != SYNC or crc32c(h[:28]) != struct.unpack_from("<I", h, 28)[0]:
        return None
    stream_id, flags, part, seq, stored, raw, pcrc = struct.unpack_from("<HHIIIII", h, 4)
    r = Record(offset, stream_id, flags, part, seq, stored, raw, pcrc)
    if raw > MAX_RAW or offset + RECORD_HEADER + stored > len(data):
        return None
    if not flags & FLAG_COMPRESSED and raw != stored:
        return None
    return r


def _parse_part_end(c: Container, rec: Record) -> Part:
    if rec.stream_id != 0 or not rec.flags & FLAG_PART_END:
        raise ValueError("not a part-end record")
    payload = c.data[rec.offset + RECORD_HEADER: rec.offset + RECORD_HEADER + rec.stored_size]
    if crc32c(payload) != rec.payload_crc:
        raise ValueError("damaged part-end record")
    r = Reader(payload)
    part = Part(index=rec.part_index)
    part.first_frame = r.u64()
    part.frame_count = r.u32()
    part.branch = r.u16()
    count = r.u32()
    if r.left() // 8 < count:
        raise ValueError("damaged part-end record (fields)")
    for _ in range(count):
        offset = r.u64()
        member = _record_header(c.data, offset)
        if member is None or member.part_index != part.index or member.stream_id == 0:
            if not part.damaged:
                part.damaged = True
                part.damage = f"a record header of the part is damaged (at {offset})"
            member = Record(offset, 0xFFFF, 0, part.index, 0, 0, 0, 0)
        part.records.append(member)
    deps, prev = r.varint(), 0
    for _ in range(deps):
        prev += r.varint()
        part.dependencies.append(prev)
    part.extra = r.take(r.varint())
    return part


def _read_index(c: Container) -> None:
    data = c.data
    if len(data) < c.data_start + TRAILER:
        raise ValueError("no trailer")
    t = data[-TRAILER:]
    if t[16:20] != TRAILER_MAGIC or crc32c(t[:20]) != struct.unpack_from("<I", t, 20)[0]:
        raise ValueError("no trailer")
    index_offset, index_size, index_crc = struct.unpack_from("<QII", t, 0)
    if index_offset < c.data_start or index_offset + index_size != len(data) - TRAILER:
        raise ValueError("damaged trailer")
    index = data[index_offset:index_offset + index_size]
    if crc32c(index) != index_crc:
        raise ValueError("damaged index")
    r = Reader(index)
    if r.u16() != 1:
        raise ValueError("unknown index version")
    parts = []
    for i in range(r.u32()):
        part_end, first, count, branch = r.u64(), r.u64(), r.u32(), r.u16()
        rec = _record_header(data, part_end)
        try:
            if rec is None:
                raise ValueError("damaged part-end record header")
            part = _parse_part_end(c, rec)
        except ValueError as e:
            part = Part(index=i, first_frame=first, frame_count=count, branch=branch, damaged=True, damage=str(e))
        if part.damaged:
            c.notes.append(f"part {i}: {part.damage}")
        parts.append(part)
    r.take(r.u16() * 26)
    c.index_extra = r.take(r.u32())
    c.parts = parts


def _scan(c: Container) -> None:
    data, offset, pending, found = c.data, c.data_start, 0, []
    while offset + RECORD_HEADER <= len(data):
        rec = _record_header(data, offset)
        if rec is not None:
            if rec.stream_id == 0:
                try:
                    part = _parse_part_end(c, rec)
                    if part.damaged:
                        c.notes.append(f"part {part.index}: {part.damage}")
                    found.append(part)
                except ValueError as e:
                    c.notes.append(f"part {rec.part_index}: {e}")
                pending = 0
            else:
                pending += 1
            offset += RECORD_HEADER + rec.stored_size
            continue
        nxt = data.find(SYNC, offset + 1)
        while nxt >= 0 and _record_header(data, nxt) is None:
            nxt = data.find(SYNC, nxt + 1)
        if nxt < 0:
            break
        c.notes.append(f"damaged bytes {offset}-{nxt} skipped")
        offset = nxt
    if pending:
        c.notes.append(f"incomplete last part dropped ({pending} record(s) without a part end)")
    found.sort(key=lambda p: p.index)
    parts: List[Part] = []
    for part in found:
        if parts and parts[-1].index == part.index:
            continue
        while len(parts) < part.index:
            parts.append(Part(index=len(parts), damaged=True, damage="part-end record lost"))
        parts.append(part)
    c.parts = parts


def open_container(data: bytes, known: Optional[Callable[[int], bool]] = None) -> Container:
    """Open a schema-2 file: header, then the index, or a scan without one.
    Raises TtdContainerError when the file cannot be opened at all."""
    if len(data) < FIXED_HEADER:
        raise TtdContainerError("too short for a session file")
    if data[:4] != MAGIC:
        raise TtdContainerError("not a TTD session file")
    schema = struct.unpack_from("<H", data, 4)[0]
    if schema != SCHEMA:
        raise TtdContainerError(f"unsupported schema v{schema}")
    size = struct.unpack_from("<I", data, 8)[0]
    if size < FIXED_HEADER or size > len(data):
        raise TtdContainerError("damaged header (size)")
    header = bytearray(data[:size])
    stored_crc = struct.unpack_from("<I", header, 12)[0]
    header[12:16] = b"\0\0\0\0"
    if crc32c(bytes(header)) != stored_crc:
        raise TtdContainerError("damaged header (CRC)")
    flags = struct.unpack_from("<H", data, 6)[0]
    uuid = data[16:32]
    created, zstd_version = struct.unpack_from("<QI", data, 32)
    r = Reader(data, FIXED_HEADER, size)
    try:
        streams = {}
        for _ in range(r.u16()):
            sid, layout, kind, length = r.u16(), r.u16(), r.u8(), r.u8()
            streams[sid] = StreamDesc(sid, layout, kind == 0, r.take(length).decode("utf-8", "replace"))
        tables = r.take(r.u32())
    except ValueError as e:
        raise TtdContainerError(f"damaged header (tables): {e}") from e
    c = Container(data, flags, uuid, created, zstd_version, streams, tables, size)
    known = known or (lambda sid: sid in KNOWN_STREAMS)
    for s in streams.values():
        if known(s.id):
            continue
        if s.required:
            raise TtdContainerError(f"the file needs stream '{s.name}' (id {s.id}) that this version does not know")
        c.notes.append(f"ancillary stream '{s.name}' (id {s.id}) skipped")
    try:
        _read_index(c)
        c.finalized = True
    except ValueError as e:
        c.notes.append(f"no valid index ({e}): opened by scanning")
        _scan(c)
    return c


def read_record(c: Container, rec: Record) -> bytes:
    """A record's payload: CRC checked, decompressed. Raises ValueError when damaged."""
    stored = c.data[rec.offset + RECORD_HEADER: rec.offset + RECORD_HEADER + rec.stored_size]
    if len(stored) != rec.stored_size or crc32c(stored) != rec.payload_crc:
        raise ValueError(f"damaged record at {rec.offset} (CRC)")
    if not rec.flags & FLAG_COMPRESSED:
        return stored
    try:
        out = zstandard.ZstdDecompressor().decompress(stored, max_output_size=rec.raw_size)
    except zstandard.ZstdError as e:
        raise ValueError(f"damaged record at {rec.offset} (decompression)") from e
    if len(out) != rec.raw_size:
        raise ValueError(f"damaged record at {rec.offset} (size)")
    return out


def reachable(c: Container, index: int) -> bool:
    """Neither the part nor any part it depends on (transitively) is damaged."""
    state: Dict[int, bool] = {}

    def walk(i: int) -> bool:
        if i in state:
            return state[i]
        ok = 0 <= i < len(c.parts) and not c.parts[i].damaged
        for d in c.parts[i].dependencies if ok else []:
            if d >= i or not walk(d):
                ok = False
                break
        state[i] = ok
        return ok

    return walk(index)


# ---------------------------------------------------------------------------
# The session in it
# ---------------------------------------------------------------------------

@dataclass
class Version:
    encoding: int
    depth: int
    base: int          # global version number, -1 for none
    crc: int
    payload: bytes
    part: int


@dataclass
class Checkpoint:
    frame: int
    start: int
    baseline: bool
    cpu: bytes
    chipset: bytes
    cursors: Tuple[int, int, int, int]       # bus reads, bus writes, media reads, bus vectors
    changes: List[Tuple[int, int, int]]      # region, piece, global version number
    part: int


@dataclass
class Session:
    container: Container
    snapshot_interval: int = 0
    regions: List[dict] = field(default_factory=list)
    devices: List[dict] = field(default_factory=list)
    checkpoints: List[Checkpoint] = field(default_factory=list)
    versions: List[Version] = field(default_factory=list)
    events: int = 0
    bus_reads: int = 0
    bus_writes: int = 0
    bus_vectors: int = 0
    media_reads: int = 0
    write_journal: int = 0
    stopped_at: str = ""
    stream_bytes: Dict[str, int] = field(default_factory=dict)

    @property
    def converted_from_v1(self) -> bool:
        return bool(self.container.flags & HEADER_CONVERTED_FROM_V1)


def _read_tables(s: Session) -> None:
    r = Reader(s.container.session_tables)
    if r.u16() != 1:
        raise TtdContainerError("unknown session tables version")
    s.snapshot_interval = r.u32()
    for _ in range(r.varint()):
        s.regions.append({
            "id": r.u16(), "name": r.string(), "owner_type": r.u16(), "owner_instance": r.string(),
            "pieces": r.u32(), "bytes": r.u32(), "dirty_granularity": r.u32(), "block_pieces": r.u32(),
        })
    for _ in range(r.varint()):
        d = {"type": r.u16(), "instance": r.string(), "legacy_id": r.u8(), "layout_version": r.u16(),
             "state_size": r.u32(), "variable_size": bool(r.u8()), "firmware": r.u64()}
        d["restore_after"] = [(r.u16(), r.string()) for _ in range(r.varint())]
        d["time_fields"] = [(r.u16(), r.u8()) for _ in range(r.varint())]
        d["runs_behind_cpu"] = bool(r.u8())
        s.devices.append(d)


def _count_ports(payload: bytes) -> int:
    return Reader(payload).varint()


def read_session(c: Container) -> Session:
    """The session's checkpoints and versions, part by part, up to the first
    unreachable part (stopped_at says why)."""
    s = Session(c)
    _read_tables(s)
    local: List[int] = []                  # number in the current file -> global version number
    for part in c.parts:
        if not reachable(c, part.index):
            s.stopped_at = f"part {part.index}: {part.damage or 'it depends on a damaged part'}"
            break
        if part.file_start:
            local = []
        payloads: Dict[int, bytes] = {}
        try:
            for rec in part.records:
                if rec.stream_id in KNOWN_STREAMS:
                    payloads[rec.stream_id] = read_record(c, rec)
                    desc = c.streams.get(rec.stream_id)
                    name = STREAM_NAMES.get(rec.stream_id, desc.name if desc else f"stream {rec.stream_id}")
                    s.stream_bytes[name] = s.stream_bytes.get(name, 0) + rec.stored_size + RECORD_HEADER
        except ValueError as e:
            s.stopped_at = f"part {part.index}: {e}"
            break
        p = Reader(payloads.get(1, b""))
        cps = Reader(payloads.get(3, b""))
        frame = 0
        for _ in range(cps.varint()):
            frame += cps.varint()
            flags = cps.u8()
            start = cps.u64()
            cpu = cps.take(48)
            chipset = cps.take(120)
            cps.take(cps.varint())         # unclaimed devices
            cursors = (cps.varint(), cps.varint(), cps.varint(), cps.varint())
            changes = []
            for _ in range(cps.varint()):
                region, count = cps.varint(), cps.varint()
                for _ in range(count):
                    piece = cps.varint()
                    encoding, depth, base, crc = p.u8(), p.varint(), p.varint(), p.u32()
                    payload = p.take(p.varint())
                    base_global = -1 if base == 0 else local[base - 1]
                    s.versions.append(Version(encoding, depth, base_global, crc, payload, part.index))
                    local.append(len(s.versions) - 1)
                    changes.append((region, piece, len(s.versions) - 1))
            s.checkpoints.append(Checkpoint(frame, start, bool(flags & 1), cpu, chipset, cursors, changes, part.index))
        if 5 in payloads:
            s.events += Reader(payloads[5]).varint()
        s.bus_reads += _count_ports(payloads[13]) if 13 in payloads else 0
        s.bus_writes += _count_ports(payloads[14]) if 14 in payloads else 0
        s.bus_vectors += _count_ports(payloads[15]) if 15 in payloads else 0
        s.media_reads += Reader(payloads[16]).varint() if 16 in payloads else 0
        if 7 in payloads:
            s.write_journal += Reader(payloads[7]).varint()
    return s


def decode_version(s: Session, number: int, cache: Dict[int, bytes]) -> bytes:
    """The 4 KB content of a version, through its chain. Raises ValueError when damaged."""
    if number in cache:
        return cache[number]
    chain, cur = [], number
    while s.versions[cur].encoding in (ENCODING_XOR, ENCODING_RANGES):
        chain.append(cur)
        cur = s.versions[cur].base
        if cur < 0 or len(chain) > 1024:
            raise ValueError(f"version {number}: broken chain")
    start = s.versions[cur]
    if start.encoding == ENCODING_ZERO:
        out = bytearray(PIECE)
    else:
        out = bytearray(zstandard.ZstdDecompressor().decompress(start.payload, max_output_size=PIECE))
    for v in reversed(chain):
        version = s.versions[v]
        if version.encoding == ENCODING_RANGES:
            r = Reader(version.payload)
            while r.left():
                at = r.u16()
                n = r.u8()
                for k, b in enumerate(r.take(n)):
                    out[at + k] ^= b
        else:
            diff = zstandard.ZstdDecompressor().decompress(version.payload, max_output_size=PIECE)
            out = bytearray(a ^ b for a, b in zip(out, diff))
    if len(out) != PIECE or crc32c(bytes(out)) != s.versions[number].crc:
        raise ValueError(f"version {number}: content CRC mismatch")
    cache[number] = bytes(out)
    return cache[number]


def validate(data: bytes) -> List[str]:
    """Everything the C++ reader checks, the content of every version and the
    dependency lists. Returns the problems found ([] = sound)."""
    try:
        c = open_container(data)
    except TtdContainerError as e:
        return [str(e)]
    problems = list(n for n in c.notes if "skipped" not in n or "damaged" in n)
    if not c.finalized:
        problems.append("not finalized (no valid index): an unfinished or damaged file")
    for part in c.parts:
        if part.damaged:
            problems.append(f"part {part.index}: {part.damage}")
        for rec in part.records:
            try:
                read_record(c, rec)
            except ValueError as e:
                problems.append(f"part {part.index}: {e}")
    try:
        s = read_session(c)
    except (ValueError, TtdContainerError) as e:
        return problems + [f"session: {e}"]
    if s.stopped_at:
        problems.append(f"session: stopped at {s.stopped_at}")

    # Every version decodes to its CRC
    cache: Dict[int, bytes] = {}
    for n in range(len(s.versions)):
        try:
            decode_version(s, n, cache)
        except (ValueError, zstandard.ZstdError) as e:
            problems.append(str(e))
        if len(cache) > 4096:
            cache.clear()

    # Each part lists the parts of its versions' bases and of every piece's current version at its end
    live: Dict[Tuple[int, int], int] = {}
    by_part: Dict[int, List[Checkpoint]] = {}
    for cp in s.checkpoints:
        by_part.setdefault(cp.part, []).append(cp)
    for part in c.parts:
        if part.index not in by_part:
            continue
        if part.file_start:
            live = {}
        need = set()
        for cp in by_part[part.index]:
            for region, piece, number in cp.changes:
                live[(region, piece)] = number
                base = s.versions[number].base
                if base >= 0 and s.versions[base].part != part.index:
                    need.add(s.versions[base].part)
        need |= {s.versions[n].part for n in live.values() if s.versions[n].part != part.index}
        missing = need - set(part.dependencies)
        if missing:
            problems.append(f"part {part.index}: dependencies {sorted(missing)} not listed")
    return problems
