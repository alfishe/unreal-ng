"""Experiment E7: write journal retention policies, modeled on recorded sessions.

Input: a v1 session file whose write journal holds the session's whole write
history (recorded with a ring that did not wrap, record-sessions.sh).

For every policy the model computes:
- bytes per minute of history kept, in memory and in the file, with the
  engine's record format (memory writes only: machine-time delta u32, region
  u16, offset u32 (two 16-bit columns: in-page offset, page), cpu u16, pc u16,
  value u8; columns, zstd level 1, blocks of
  2,048 records - v1's block size and level);
- per find-last query: whether the kept journal answers it, and otherwise how
  many frames the coverage index walks (no emulation) and how many frames are
  replayed (at most one: the coverage index is whole history, so the newest
  candidate frame is the answer's frame).

Policies (phase-3-replay-inputs-tdd.md §4.8):
- Ring: the newest 8,388,608 records (v1's ring);
- WholeHistory: every record;
- Window(W): the records within W frames before the current position (a query
  looks back from the position, so the frames after it do not answer it);
  W = 0 keeps no journal at all. A window is not saved: the file holds none.

Query workload: 1,000 random positions per session; at each one the address
comes from one of three classes, a third each:
- A: written within the last frame before the position;
- B: last written 1-60 seconds earlier (50-3,000 frames);
- C: last written more than 60 seconds earlier, or never (half each).
Reverse continue to a write watchpoint asks the same question (the newest
write of an address before the position), so it is the same workload.
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional

import numpy as np
import zstandard

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "verification", "ttd-analyzer"))
from src import ttd_format as tf  # noqa: E402

V1_RING_RECORDS = 8_388_608
BLOCK_RECORDS = 2048
ZSTD_LEVEL = 1
FRAMES_PER_SECOND = 50          # the benchmark's minute is 3,000 frames
WINDOWS = (0, 50, 250, 1500)
QUERIES = 1000
SEED = 0xE7


@dataclass
class Journal:
    """A session's memory writes, sorted by time."""
    t: np.ndarray        # uint64 machine time (v1 GlobalT)
    key: np.ndarray      # uint32 physical address: page << 14 | offset
    pc: np.ndarray       # uint16
    value: np.ndarray    # uint8
    io_records: int      # port OUTs v1 also keeps in its journal (not here: the OUT bus journal has them)
    v1_compressed: int   # v1's journal section bytes (memory + port records)
    span: int            # machine time per frame
    frames: int          # frames recorded
    first_frame: int     # the session's first frame
    ram_pages: int


def _varints(buf: np.ndarray) -> np.ndarray:
    """Decode a run of LEB128 varints (the journal's time deltas)."""
    ends = np.flatnonzero((buf & 0x80) == 0)
    starts = np.concatenate(([0], ends[:-1] + 1))
    lengths = ends - starts + 1
    shift = (np.arange(buf.size) - np.repeat(starts, lengths)).astype(np.uint64) * np.uint64(7)
    parts = (buf & 0x7F).astype(np.uint64) << shift
    return np.add.reduceat(parts, starts)


def load(path: str) -> Journal:
    dump = tf.parse_file(path)
    if not dump.header.flags & tf.FLAGS_WRITE_JOURNAL_COMPLETE:
        raise ValueError(f"{path}: the journal does not hold the whole session (ring wrapped)")
    cps = dump.checkpoints
    # Machine time per frame: the base T-states per frame (chipset) times the
    # TTD clock unit (models with a hardware turbo count finer units)
    a, b = cps[0].chipset, cps[-1].chipset
    base = round((b.t_states - a.t_states) / (b.frame_counter - a.frame_counter))
    units = max(1, round(dump.journal.blocks[-1].last_global_t / (base * b.frame_counter))) if dump.journal.blocks else 1
    span = base * units
    dctx = zstandard.ZstdDecompressor()
    ts, addrs, pcs, values, pages, ios = [], [], [], [], [], []
    for block, payload in zip(dump.journal.blocks, dump.journal.payloads):
        raw = np.frombuffer(dctx.decompress(payload, max_output_size=block.raw_size), dtype=np.uint8)
        n = block.record_count
        # Header: two varints (time column bytes, base time), then the columns
        pos = 0
        vals = []
        for _ in range(2):
            v, shift = 0, 0
            while True:
                b = int(raw[pos]); pos += 1
                v |= (b & 0x7F) << shift
                shift += 7
                if not b & 0x80:
                    break
            vals.append(v)
        gt_bytes, base_t = vals
        deltas = _varints(raw[pos:pos + gt_bytes])
        assert deltas.size == n, (path, deltas.size, n)
        deltas[0] = 0
        t = (np.uint64(base_t) + np.cumsum(deltas, dtype=np.uint64)) & np.uint64((1 << 40) - 1)
        a = pos + gt_bytes
        addr = raw[a:a + 2 * n].view("<u2")
        pc = raw[a + 2 * n:a + 4 * n].view("<u2")
        value = raw[a + 4 * n:a + 5 * n]
        page = raw[a + 5 * n:a + 6 * n]
        io = np.unpackbits(raw[a + 6 * n:a + 6 * n + (n + 7) // 8], bitorder="little")[:n].astype(bool)
        ts.append(t); addrs.append(addr); pcs.append(pc); values.append(value); pages.append(page); ios.append(io)
    t = np.concatenate(ts); addr = np.concatenate(addrs); pc = np.concatenate(pcs)
    value = np.concatenate(values); page = np.concatenate(pages); io = np.concatenate(ios)
    mem = ~io
    key = (page[mem].astype(np.uint32) << 14) | (addr[mem].astype(np.uint32) & 0x3FFF)
    return Journal(t=t[mem], key=key, pc=pc[mem], value=value[mem], io_records=int(io.sum()),
                   v1_compressed=dump.journal.section_bytes, span=span,
                   frames=cps[-1].frame - cps[0].frame, first_frame=cps[0].frame,
                   ram_pages=dump.header.model_ram_pages)


def _varint_bytes(values: np.ndarray) -> bytes:
    """LEB128 varints, as v1's journal writes its time deltas."""
    v = values.astype(np.uint64)
    nb = np.ones(v.size, dtype=np.int64)
    for k in range(1, 10):
        nb += (v >= np.uint64(1) << np.uint64(7 * k)).astype(np.int64)
    out = np.zeros(int(nb.sum()), dtype=np.uint8)
    start = np.concatenate(([0], np.cumsum(nb)[:-1]))
    for k in range(int(nb.max()) if v.size else 0):
        m = nb > k
        byte = ((v[m] >> np.uint64(7 * k)) & np.uint64(0x7F)).astype(np.uint8)
        byte |= np.where(nb[m] > k + 1, 0x80, 0).astype(np.uint8)
        out[start[m] + k] = byte
    return out.tobytes()


def encode_blocks(j: Journal) -> np.ndarray:
    """Compressed bytes of each 2,048-record block in the engine's format."""
    cctx = zstandard.ZstdCompressor(level=ZSTD_LEVEL)
    n = j.t.size
    delta = np.diff(j.t, prepend=j.t[:1]).astype(np.uint64)
    zeros16 = np.zeros(n, dtype="<u2")
    sizes = []
    for s in range(0, n, BLOCK_RECORDS):
        e = min(n, s + BLOCK_RECORDS)
        raw = b"".join((_varint_bytes(delta[s:e]), zeros16[s:e].tobytes(),             # time, region
                        (j.key[s:e] & 0xFFFF).astype("<u2").tobytes(),                 # offset: low half,
                        (j.key[s:e] >> 16).astype("<u2").tobytes(), zeros16[s:e].tobytes(),   # high half; cpu
                        j.pc[s:e].astype("<u2").tobytes(), j.value[s:e].tobytes()))
        sizes.append(len(cctx.compress(raw)))
    return np.array(sizes, dtype=np.int64)


@dataclass
class Query:
    cls: str
    frame: int                   # the position's frame
    answer_frame: Optional[int]  # the newest write's frame, None = never written


@dataclass
class PolicyResult:
    name: str
    memory_bytes: float          # held (Window: mean over the query positions)
    memory_max_bytes: float
    file_bytes: float
    hits: int = 0                # answered from the kept journal
    walked: List[int] = field(default_factory=list)    # coverage frames walked per query
    replayed: List[int] = field(default_factory=list)  # frames replayed per query


class Index:
    """The newest write of an address before a time."""

    def __init__(self, j: Journal):
        order = np.lexsort((j.t, j.key))
        self.key = j.key[order]
        self.t = j.t[order]

    def last_before(self, key: int, t: int) -> Optional[int]:
        lo = int(np.searchsorted(self.key, key, "left"))
        hi = int(np.searchsorted(self.key, key, "right"))
        if lo == hi:
            return None
        i = lo + int(np.searchsorted(self.t[lo:hi], t, "left")) - 1
        return int(self.t[i]) if i >= lo else None


def make_queries(j: Journal, idx: Index, rng: np.random.Generator) -> List[Query]:
    span = j.span
    second, minute = FRAMES_PER_SECOND * span, 60 * FRAMES_PER_SECOND * span
    queries: List[Query] = []
    classes = ["A", "B", "C"] * (QUERIES // 3) + ["A"] * (QUERIES % 3)
    for cls in classes:
        for _ in range(200):   # tries for a position where the class exists
            frame = j.first_frame + int(rng.integers(1, j.frames))
            pt = frame * span + int(rng.integers(0, span))
            if cls == "A":
                lo, hi = np.searchsorted(j.t, [pt - span, pt])
            elif cls == "B":
                lo, hi = np.searchsorted(j.t, [pt - minute, pt - second])
            else:
                if rng.random() < 0.5:   # never written: a RAM address with no write before the position
                    key = int(rng.integers(0, j.ram_pages << 14))
                    if idx.last_before(key, pt) is None:
                        queries.append(Query(cls, frame, None))
                        break
                    continue
                lo, hi = 0, int(np.searchsorted(j.t, pt - minute))
            if hi <= lo:
                continue
            key = int(j.key[int(rng.integers(lo, hi))])
            last = idx.last_before(key, pt)
            ok = {"A": last is not None and last >= pt - span,
                  "B": last is not None and pt - minute <= last < pt - second,
                  "C": last is not None and last < pt - minute}[cls]
            if ok:
                queries.append(Query(cls, frame, last // span))
                break
    return queries


def evaluate(j: Journal, blocks: np.ndarray, queries: List[Query]) -> List[PolicyResult]:
    n = j.t.size
    total = float(blocks.sum())
    # Bytes per frame: each block's bytes spread over its records' frames
    rec_frame = (j.t // np.uint64(j.span)).astype(np.int64) - j.first_frame
    per_record = np.repeat(blocks / BLOCK_RECORDS, BLOCK_RECORDS)[:n]
    if n % BLOCK_RECORDS:
        per_record[-(n % BLOCK_RECORDS):] = blocks[-1] / (n % BLOCK_RECORDS)
    per_frame = np.bincount(rec_frame, weights=per_record, minlength=j.frames + 1)
    cum = np.concatenate(([0.0], np.cumsum(per_frame)))
    first_frame = j.first_frame

    results: List[PolicyResult] = []

    whole = PolicyResult("WholeHistory", total, total, total)
    ring_n = min(n, V1_RING_RECORDS)
    ring_bytes = float(blocks[-((ring_n + BLOCK_RECORDS - 1) // BLOCK_RECORDS):].sum()) if ring_n else 0.0
    ring = PolicyResult("Ring", ring_bytes, ring_bytes, ring_bytes)
    ring_start = int(j.t[n - ring_n] // np.uint64(j.span)) if n > V1_RING_RECORDS else None
    windows = []
    for w in WINDOWS:
        held = [cum[q.frame - first_frame] - cum[max(0, q.frame - first_frame - w)] for q in queries]
        windows.append((w, PolicyResult(f"Window {w}", float(np.mean(held)), float(np.max(held)), 0.0)))

    for q in queries:
        a = q.answer_frame
        whole.hits += 1; whole.walked.append(0); whole.replayed.append(0)
        if ring_start is None or (a is not None and a >= ring_start):
            ring.hits += 1; ring.walked.append(0); ring.replayed.append(0)
        else:   # walk the coverage index down from the ring's oldest frame
            ring.walked.append(ring_start - (a if a is not None else first_frame))
            ring.replayed.append(1 if a is not None else 0)
        for w, r in windows:
            start = q.frame - w if w else q.frame + 1   # W = 0: no journal, the position's frame too is walked
            if w and a is not None and a >= start:
                r.hits += 1; r.walked.append(0); r.replayed.append(0)
            else:
                r.walked.append(start - (a if a is not None else first_frame))
                r.replayed.append(1 if a is not None else 0)
    return [whole, ring] + [r for _, r in windows]
