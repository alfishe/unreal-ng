"""Writing schema-2 containers, byte for byte as the C++ writer does.

Mirrors TTDContainerWriter (core/src/debugger/ttd/engine/ttdcontainer.cpp):
the header (fixed 64 bytes, stream table, session tables, CRC over it with the
CRC field zero), records (32-byte header with its own CRC, payload zstd level 1
when that is smaller), part-end records (stream 0), the index (part table,
per-stream totals in order of first appearance, index extra) and the trailer.

The tools copy parts verbatim (storage.md S-6): a record is added as stored
(add_stored), with its flags and raw size kept; only new records (an index
record of branches.tdx) are encoded here (add_record).

Worked example:

    w = ContainerWriter.like(container)          # the input's header
    for part in container.parts:
        for rec in part.records:
            w.add_stored(rec.stream_id, rec.flags, stored_payload(container, rec), rec.raw_size)
        w.end_part(part.first_frame, part.frame_count, part.branch, part.dependencies, part.extra)
    data = w.finalize()                           # the same bytes as the input
"""

from __future__ import annotations

import struct
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

import zstandard

from .ttdcontainer import (FIXED_HEADER, FLAG_COMPRESSED, FLAG_PART_END, MAGIC, RECORD_HEADER, SCHEMA, SYNC,
                           TRAILER_MAGIC, Container, Record, StreamDesc, TtdError, crc32c)

ZSTD_LEVEL = 1          # codec::kDefaultZstdLevel
INDEX_VERSION = 1


def _varint(value: int) -> bytes:
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def varint(value: int) -> bytes:
    """TTDByteWriter::Varint: 7 bits per byte, low first."""
    if value < 0:
        raise TtdError("a varint cannot be negative")
    return _varint(value)


def string(text: str) -> bytes:
    """TTDByteWriter::Str: varint length, UTF-8 bytes."""
    raw = text.encode("utf-8")
    return varint(len(raw)) + raw


def stored_payload(c: Container, rec: Record) -> bytes:
    """A record's payload as stored, CRC checked (TTDContainerReader::ReadStored). Raises TtdError when damaged."""
    start = rec.offset + RECORD_HEADER
    stored = c.data[start:start + rec.stored_size]
    if len(stored) != rec.stored_size or crc32c(stored) != rec.payload_crc:
        raise TtdError(f"damaged record at {rec.offset} (CRC)")
    return stored


def compress(data: bytes) -> bytes:
    """codec::Compress: zstd level 1 with the content size in the frame."""
    return zstandard.ZstdCompressor(level=ZSTD_LEVEL).compress(data)


class ContainerWriter:
    """TTDContainerWriter into memory."""

    def __init__(self, flags: int, uuid: bytes, created_micros: int, zstd_version: int,
                 streams: Sequence[StreamDesc], session_tables: bytes):
        if len(uuid) != 16:
            raise TtdError("a session UUID has 16 bytes")
        head = bytearray()
        head += MAGIC
        head += struct.pack("<HHII", SCHEMA, flags, 0, 0)
        head += uuid
        head += struct.pack("<QI", created_micros, zstd_version)
        head += b"\0" * (FIXED_HEADER - len(head))
        head += struct.pack("<H", len(streams))
        for s in streams:
            name = s.name.encode("utf-8")[:255]
            head += struct.pack("<HHBB", s.id, s.layout, 0 if s.required else 1, len(name))
            head += name
        head += struct.pack("<I", len(session_tables))
        head += session_tables
        struct.pack_into("<I", head, 8, len(head))
        struct.pack_into("<I", head, 12, crc32c(bytes(head)))
        self.out = bytearray(head)
        self.streams = list(streams)
        self._part = 0
        self._sequence = 0
        self._part_records: List[int] = []
        self._parts: List[Tuple[int, int, int, int]] = []        # part end offset, first frame, frames, branch
        self._totals: Dict[int, List[int]] = {}                   # stream -> records, stored, raw (first-seen order)
        self._finalized = False

    @classmethod
    def like(cls, c: Container, streams: Optional[Sequence[StreamDesc]] = None) -> "ContainerWriter":
        """A writer with @p c's header (flags, UUID, time, zstd version, tables); @p streams replaces its table."""
        return cls(c.flags, c.uuid, c.created_micros, c.zstd_version,
                   list(c.streams.values()) if streams is None else streams, c.session_tables)

    @property
    def part_count(self) -> int:
        return self._part

    def _write_record(self, stream_id: int, flags: int, stored: bytes, raw_size: int) -> int:
        if self._finalized:
            raise TtdError("the container is finalized")
        offset = len(self.out)
        h = bytearray(SYNC)
        h += struct.pack("<HHIIIII", stream_id, flags, self._part, self._sequence, len(stored), raw_size,
                         crc32c(stored))
        h += struct.pack("<I", crc32c(bytes(h)))
        self._sequence += 1
        self.out += h
        self.out += stored
        return offset

    def _count(self, stream_id: int, stored: int, raw: int) -> None:
        t = self._totals.setdefault(stream_id, [0, 0, 0])
        t[0] += 1
        t[1] += stored
        t[2] += raw

    def add_stored(self, stream_id: int, flags: int, stored: bytes, raw_size: int) -> None:
        """A record as another file stored it (TTDContainerWriter::AddStoredRecord)."""
        if stream_id == 0:
            raise TtdError("stream 0 holds part ends only")
        self._part_records.append(self._write_record(stream_id, flags & FLAG_COMPRESSED, stored, raw_size))
        self._count(stream_id, len(stored), raw_size)

    def add_record(self, stream_id: int, payload: bytes, allow_compress: bool = True) -> None:
        """A new record, compressed when that is smaller (TTDContainerWriter::AddRecord)."""
        if stream_id == 0:
            raise TtdError("stream 0 holds part ends only")
        packed = compress(payload) if allow_compress and payload else b""
        use = bool(packed) and len(packed) < len(payload)
        stored = packed if use else payload
        self._part_records.append(self._write_record(stream_id, FLAG_COMPRESSED if use else 0, stored, len(payload)))
        self._count(stream_id, len(stored), len(payload))

    def end_part(self, first_frame: int = 0, frame_count: int = 0, branch: int = 0,
                 dependencies: Iterable[int] = (), extra: bytes = b"") -> None:
        body = bytearray(struct.pack("<QIHI", first_frame, frame_count, branch, len(self._part_records)))
        for off in self._part_records:
            body += struct.pack("<Q", off)
        deps = sorted(set(dependencies))
        body += varint(len(deps))
        previous = 0
        for d in deps:
            body += varint(d - previous)
            previous = d
        body += varint(len(extra)) + extra
        offset = self._write_record(0, FLAG_PART_END, bytes(body), len(body))
        self._parts.append((offset, first_frame, frame_count, branch))
        self._part_records = []
        self._part += 1

    def finalize(self, index_extra: bytes = b"") -> bytes:
        """The index and the trailer; returns the whole file."""
        index = bytearray(struct.pack("<HI", INDEX_VERSION, len(self._parts)))
        for off, first, count, branch in self._parts:
            index += struct.pack("<QQIH", off, first, count, branch)
        index += struct.pack("<H", len(self._totals))
        for sid, (records, stored, raw) in self._totals.items():
            index += struct.pack("<HQQQ", sid, records, stored, raw)
        index += struct.pack("<I", len(index_extra)) + index_extra
        index_offset = len(self.out)
        trailer = bytearray(struct.pack("<QII", index_offset, len(index), crc32c(bytes(index))))
        trailer += TRAILER_MAGIC
        trailer += struct.pack("<I", crc32c(bytes(trailer)))
        self.out += index
        self.out += trailer
        self._finalized = True
        return bytes(self.out)

    def unfinalized(self) -> bytes:
        """The file without index and trailer (branches.tdx is written so)."""
        return bytes(self.out)


def copy_parts(c: Container, keep: Sequence[int], streams: Optional[Sequence[StreamDesc]] = None) -> bytes:
    """A new finalized file of @p c's parts @p keep (indices, in order), copied verbatim; dependencies renumbered.
    Raises TtdError when a kept part depends on a part left out, or a part is damaged."""
    new_index = {old: new for new, old in enumerate(keep)}
    w = ContainerWriter.like(c, streams)
    for old in keep:
        part = c.parts[old]
        if part.damaged:
            raise TtdError(f"part {old} is damaged: {part.damage}")
        for rec in part.records:
            w.add_stored(rec.stream_id, rec.flags, stored_payload(c, rec), rec.raw_size)
        deps = []
        for d in part.dependencies:
            if d not in new_index:
                raise TtdError(f"part {old} depends on part {d}, which is left out")
            deps.append(new_index[d])
        w.end_part(part.first_frame, part.frame_count, part.branch, deps, part.extra)
    return w.finalize(c.index_extra)
