"""E6 - data model of TTD v1 and v2 on a recorded session.

For one .ttd file this module computes, per stream, how many bytes v1 holds
in memory and writes to the file, and how many v2 would, with the reason for
each number. Speed is not modeled; the cost of a seek is counted as work
(pieces and chain links to decode).

v1 is modeled from what the engine does today (ttdcodecpagestore.cpp,
timetravelmanager.cpp, ttdcompression.h) and checked against the benchmark's
measured heap split (bm4_heap_*). v2 follows the migration roadmap
(docs/inprogress/2026-09-25-ttd-v2-migration/) with the parameters E1-E4
measured:

- RAM pieces (Phase 1): every content change stored once (no key frames),
  encoded once (XOR; the full piece is compressed too only when the XOR is
  larger than T = 128 B), a chain of at most K = 50 links per piece, payloads
  in an arena at their exact size plus a 16-byte slot header;
- reference table (Phase 1, Step 4): copy-on-write blocks of 8 pages
  (32 pieces, 128 B), two levels;
- device state (Phase 2): a device's state stored only when it changed, as the
  compressed XOR against its previous version;
- journals and coverage (Phases 4-5): kept in memory as the compressed blocks
  the file already uses, at their exact size.
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "common"))
import piecestats as ps  # noqa: E402
import ttdhistory as th  # noqa: E402

tf = th.tf

PIECE = 4096
# --- v1 engine constants (see the files named above) ---
ZSTD_BOUND_PIECE = PIECE + (PIECE >> 8) + ((128 * 1024 - PIECE) >> 11)  # ZSTD_compressBound(4096) = 4174
V1_SLOT_STRUCT = 40          # TTDCodecPageStore::Slot: encoding, refcount, prevSlot, crc32c, std::vector
V1_CHECKPOINT_STRUCT = 288   # sizeof(TTDCheckpoint), measured (bm4_heap_checkpoints_bpf)
V1_PAGE_REF = 16             # TTDPageRef: 4 x u32 per 16 KB page
V1_KEY_INTERVAL = 50
V1_JOURNAL_RECORD = 12       # sizeof(TTDWriteRecord)
V1_JOURNAL_CAPACITY = 1 << 23  # 64 MB rounded up to a power of two in records (8,388,608 = 100.7 MB)
V1_JOURNAL_CHUNK = 1 << 16   # records per committed ring chunk

# --- v2 parameters (E1-E4) ---
V2_K = 50                    # chain limit per piece (E1)
V2_T = 128                   # compress the full piece only when the XOR is larger (E2)
V2_SLOT_HEADER = 16          # arena slot: offset, size, previous version, CRC
V2_BLOCK_PIECES = 32         # 8 pages of 16 KB (E3)
V2_BLOCK_BYTES = V2_BLOCK_PIECES * 4
V2_POINTER = 8
V2_FILE_BLOCK_RECORD = 10    # region, position, block index


def zstd_bound(n: int) -> int:
    return n + (n >> 8) + (((128 * 1024 - n) >> 11) if n < 128 * 1024 else 0)


@dataclass
class Stream:
    """One stream of a session: bytes in memory and in the file, v1 and v2."""
    name: str
    v1_mem: float = 0.0
    v1_file: float = 0.0
    v2_mem: float = 0.0
    v2_file: float = 0.0
    why: str = ""


@dataclass
class SeekWork:
    """Mean work to restore memory at a random position, per piece that is not zero."""
    v1_links: float = 0.0
    v2_links: float = 0.0


@dataclass
class SessionModel:
    name: str
    frames: int
    checkpoints: int
    file_bytes: int
    streams: Dict[str, Stream] = field(default_factory=dict)
    seek: SeekWork = field(default_factory=SeekWork)

    def total(self, attr: str) -> float:
        return sum(getattr(s, attr) for s in self.streams.values())


# ---------------------------------------------------------------- RAM pieces

def _region_pieces(r: "ps.RegionStats", checkpoints: int) -> Dict[str, float]:
    """Bytes and seek work of one memory region, v1 and v2."""
    base = r.key_full[min(r.key_full)]                 # full sizes of every piece at checkpoint 0
    nonzero0 = base > 0

    # v1: a Full slot for every non-zero piece at every key frame, one XorPrev or
    # Full slot per change in between (the smaller of the two encodings)
    v1_payload = 0
    v1_slots = 0
    v1_alloc_slots = 0                                  # slots whose payload came from Compress()
    for cp, sizes in r.key_full.items():
        v1_payload += int(sizes.sum())
        v1_slots += len(sizes)
        v1_alloc_slots += int((sizes > 0).sum())
    best = np.minimum(r.change_xor, r.change_full)
    is_key = (r.change_cp % V1_KEY_INTERVAL) == 0
    v1_payload += int(best[~is_key].sum())               # changes on a key frame are in key_full already
    v1_slots += int((~is_key).sum())
    v1_alloc_slots += int(((r.change_full > 0) & ~is_key).sum())

    # v2: baseline once, then each change once; K links per piece at most
    depth = np.zeros(r.pieces, np.int64)
    v2_payload = int(base.sum())
    v2_slots = int(nonzero0.sum())
    order = np.argsort(r.change_cp, kind="stable")
    for idx in order:
        p = r.change_piece[idx]
        x, f = int(r.change_xor[idx]), int(r.change_full[idx])
        if f == 0:                                      # new content is all zeros: no payload
            depth[p] = 0
            v2_slots += 1
            continue
        if depth[p] >= V2_K:
            v2_payload += f
            depth[p] = 0
        elif x <= V2_T or x <= f:
            v2_payload += x
            depth[p] += 1
        else:
            v2_payload += f
            depth[p] = 0
        v2_slots += 1

    # seek work: mean chain depth over all checkpoints of the pieces not zero
    v1_links, v2_links = _mean_depths(r, checkpoints)

    return {
        "v1_payload": v1_payload, "v1_slots": v1_slots, "v1_alloc_slots": v1_alloc_slots,
        "v2_payload": v2_payload, "v2_slots": v2_slots,
        "v1_links": v1_links, "v2_links": v2_links,
    }


def _mean_depths(r: "ps.RegionStats", checkpoints: int):
    """Mean number of XOR links a restore decodes per non-zero piece, at a uniformly random checkpoint."""
    changes = r.changes_at()
    d1 = np.zeros(r.pieces, np.int64)
    d2 = np.zeros(r.pieces, np.int64)
    nonzero = r.key_full[min(r.key_full)] > 0
    full_at = {}
    for k, cp in enumerate(r.change_cp):
        full_at[(int(cp), int(r.change_piece[k]))] = int(r.change_full[k])
    xor_at = {}
    for k, cp in enumerate(r.change_cp):
        xor_at[(int(cp), int(r.change_piece[k]))] = int(r.change_xor[k])
    s1 = s2 = 0.0
    n = 0
    for cp in range(checkpoints):
        if cp % V1_KEY_INTERVAL == 0:
            d1[:] = 0
        for p in changes[cp] if cp < len(changes) else ():
            f = full_at[(cp, p)]
            x = xor_at[(cp, p)]
            nonzero[p] = f > 0
            if cp % V1_KEY_INTERVAL != 0:
                d1[p] = d1[p] + 1 if x < f else 0
            if f == 0 or d2[p] >= V2_K or not (x <= V2_T or x <= f):
                d2[p] = 0
            else:
                d2[p] += 1
        cnt = int(nonzero.sum())
        if cnt:
            s1 += float(d1[nonzero].sum())
            s2 += float(d2[nonzero].sum())
            n += cnt
    return (s1 / n, s2 / n) if n else (0.0, 0.0)


def _ref_table(r: "ps.RegionStats", checkpoints: int, v1_ref_bytes_per_cp: float):
    """Reference table bytes: v1 dense per checkpoint; v2 copy-on-write blocks, two levels."""
    blocks = (r.pieces + V2_BLOCK_PIECES - 1) // V2_BLOCK_PIECES
    changes = r.changes_at()
    mem = file = 0
    for cp in range(checkpoints):
        changed = {p // V2_BLOCK_PIECES for p in (changes[cp] if cp < len(changes) else ())}
        if cp == 0:
            changed = set(range(blocks))
        if changed:
            mem += V2_POINTER + blocks * V2_POINTER + len(changed) * V2_BLOCK_BYTES
            file += 4 + len(changed) * (V2_FILE_BLOCK_RECORD + V2_BLOCK_BYTES)
        else:
            mem += V2_POINTER
            file += 4
    return v1_ref_bytes_per_cp * checkpoints, mem, file


# ------------------------------------------------------------- device state

def _device_state(dump) -> Dict[str, float]:
    """v1 stores every device blob in every checkpoint; v2 a changed blob as its compressed XOR."""
    prev: Dict[int, bytes] = {}
    v1 = v2 = 0
    changes = 0
    for cp in dump.checkpoints:
        for pid, raw in cp.peripheral_blobs.items():
            blob = raw
            if pid == th.GS_PERIPHERAL_ID:               # GS RAM is a memory region of its own in v2
                dec = tf.decode_peripheral_blob(pid, raw)
                blob = dec[:th.GS_FIXED_STATE_BYTES]
            v1 += len(raw)
            old = prev.get(pid)
            if old == blob:
                v2 += 4                                  # version reference only
                continue
            changes += 1
            if old is not None and len(old) == len(blob):
                v2 += 4 + _zsize_any(_xor_any(old, blob))
            else:
                v2 += 4 + _zsize_any(blob)
            prev[pid] = blob
    return {"v1": v1, "v2": v2, "changes": changes}


def _xor_any(a: bytes, b: bytes) -> bytes:
    return (int.from_bytes(a, "little") ^ int.from_bytes(b, "little")).to_bytes(len(a), "little")


_zc = None


def _zsize_any(data: bytes) -> int:
    global _zc
    if _zc is None:
        import zstandard
        _zc = zstandard.ZstdCompressor(level=1)
    return len(_zc.compress(data)) if data else 0


# ------------------------------------------------------------------ session

def model_session(name: str, path: str, journal_records_per_frame: Optional[float] = None,
                  coverage_working_set: float = 0.0) -> SessionModel:
    """Model one session.

    journal_records_per_frame overrides the journal rate when the v1 ring
    wrapped (the file then holds only the last V1_JOURNAL_CAPACITY records).
    coverage_working_set is the coverage index's in-memory working state
    (seen-bitmaps, open block, caches), which no file records: taken from the
    benchmark's measured heap split, the same for v1 and v2."""
    st = ps.load(name, path)
    dump = tf.parse_file(path)
    cps = len(dump.checkpoints)
    frames = cps - 1
    sm = SessionModel(name=name, frames=frames, checkpoints=cps, file_bytes=os.path.getsize(path))

    # RAM and GS RAM pieces
    piece = Stream("memory pieces")
    refs = Stream("reference table")
    links1 = links2 = 0.0
    nregions = 0
    for rname, r in st.regions.items():
        x = _region_pieces(r, cps)
        piece.v1_file += x["v1_payload"] + x["v1_slots"] * 17
        piece.v1_mem += x["v1_alloc_slots"] * ZSTD_BOUND_PIECE + x["v1_slots"] * V1_SLOT_STRUCT
        piece.v2_mem += x["v2_payload"] + x["v2_slots"] * V2_SLOT_HEADER
        piece.v2_file += x["v2_payload"] + x["v2_slots"] * V2_SLOT_HEADER
        links1 += x["v1_links"]
        links2 += x["v2_links"]
        nregions += 1
        if rname == "ram":
            v1_refs, m2, f2 = _ref_table(r, cps, dump.header.model_ram_pages * V1_PAGE_REF)
            refs.v1_mem += v1_refs
            refs.v1_file += dump.header.model_ram_pages * 16 * cps
            refs.v2_mem += m2
            refs.v2_file += f2
        else:                                           # GS RAM: v1 copies it whole in every blob
            _, m2, f2 = _ref_table(r, cps, 0)
            refs.v2_mem += m2
            refs.v2_file += f2
    piece.why = (f"v1: a full copy of every non-zero piece every {V1_KEY_INTERVAL} frames; each payload "
                 f"allocated at ZSTD_compressBound ({ZSTD_BOUND_PIECE} B). v2: each change once, exact size")
    refs.why = "v1: 16 B per 16 KB page per frame; v2: 8-page copy-on-write blocks, two levels"
    sm.seek = SeekWork(v1_links=links1 / max(1, nregions), v2_links=links2 / max(1, nregions))
    sm.streams[piece.name] = piece
    sm.streams[refs.name] = refs

    dev = _device_state(dump)
    s = Stream("device state", v1_mem=dev["v1"], v1_file=dev["v1"], v2_mem=dev["v2"], v2_file=dev["v2"],
               why=f"v1: every blob in every frame; v2: {dev['changes']} changed versions, compressed XOR")
    sm.streams[s.name] = s

    core = 168 * cps
    s = Stream("CPU + chipset", v1_mem=V1_CHECKPOINT_STRUCT * cps, v1_file=core, v2_mem=core, v2_file=core,
               why="v1: the checkpoint struct (288 B); v2: CPU + chipset only (168 B), not yet delta-coded")
    sm.streams[s.name] = s

    j = dump.journal
    if j is not None and j.record_count:
        stored = j.record_count
        rate = journal_records_per_frame if journal_records_per_frame else stored / frames
        written = rate * frames
        held = min(written, V1_JOURNAL_CAPACITY)
        chunks = -(-int(held) // V1_JOURNAL_CHUNK)
        per_record_file = j.section_bytes / stored
        s = Stream("write journal",
                   v1_mem=chunks * V1_JOURNAL_CHUNK * V1_JOURNAL_RECORD,
                   v1_file=held * per_record_file,
                   v2_mem=written * per_record_file, v2_file=written * per_record_file,
                   why=(f"{rate:.0f} writes per frame. v1: {V1_JOURNAL_RECORD} B per write in memory, ring capped "
                        f"at {V1_JOURNAL_CAPACITY:,} writes (older history is lost); v2: the file's compressed "
                        f"blocks ({per_record_file:.2f} B per write) kept in memory, whole history"))
        sm.streams[s.name] = s

    cov = dump.coverage
    if cov is not None:
        comp = cov.compressed_bytes
        bound = sum(zstd_bound(b.raw_size) for k in cov.kinds for b in k.blocks)
        s = Stream("coverage", v1_mem=bound + coverage_working_set, v1_file=comp,
                   v2_mem=comp + coverage_working_set, v2_file=comp,
                   why=(f"v1: blocks allocated at ZSTD_compressBound of their raw size; v2: exact size. Both hold "
                        f"a working set of {coverage_working_set / 1e6:.1f} MB (measured, not in the file)"))
        sm.streams[s.name] = s

    pj = 0
    for jr in (dump.port_reads, dump.port_writes):
        if jr is not None:
            pj += getattr(jr, "section_bytes", 0) or 0
    if pj:
        s = Stream("port journals", v1_mem=pj, v1_file=pj, v2_mem=pj, v2_file=pj, why="unchanged")
        sm.streams[s.name] = s
    return sm
