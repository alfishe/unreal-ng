"""Piece-change history of a recorded TTD session, the common input of the Phase 1 experiments.

A .ttd file stores memory as 4 KB pieces. This module replays a session file
and answers, for every checkpoint: which pieces of each memory region really
changed content since the previous checkpoint. "Really" matters: v1 stores
every non-zero piece again at each key frame (every 50 frames) under a new slot
id although the content did not change, so slot ids alone overstate change.

Regions:
- "ram": machine RAM, from the checkpoints' piece references;
- "gs.ram": the classic General Sound card RAM, which v1 keeps inside the GS
  device blob (fixed state, then the RAM) - the only device memory present in
  v1 files; MoonSound wave memory, NeoGS memory and the EEPROMs are not
  recorded by v1 at all.

The file reader is the project's analyzer (tools/verification/ttd-analyzer).
"""

from __future__ import annotations

import hashlib
import os
import sys
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "verification", "ttd-analyzer"))
from src import ttd_format as tf  # noqa: E402

PIECE = 4096
GS_PERIPHERAL_ID = 5          # PeripheralId::GeneralSound
GS_FIXED_STATE_BYTES = 95     # SoundChip_GeneralSound::TTD_FIXED_STATE_SIZE (soundchip_gs.cpp:835-844)

# on_change(region, checkpoint_index, piece_index, previous_bytes, new_bytes)
ChangeCallback = Callable[[str, int, int, bytes, bytes], None]


@dataclass
class RegionHistory:
    name: str
    pieces: int
    # changes[i] = sorted piece indices whose content differs between checkpoints i-1 and i
    # (changes[0] is empty: checkpoint 0 is the baseline)
    changes: List[List[int]] = field(default_factory=list)
    # version[i][p] would be large; instead version_at(i, p) is answered from change lists
    nonzero_at_baseline: List[bool] = field(default_factory=list)

    def change_counts(self) -> List[int]:
        return [len(c) for c in self.changes]


@dataclass
class SessionHistory:
    path: str
    model_ram_pages: int
    frames: List[int]
    regions: Dict[str, RegionHistory]

    @property
    def checkpoints(self) -> int:
        return len(self.frames)


def _split(data: bytes) -> List[bytes]:
    out = [data[i:i + PIECE] for i in range(0, len(data), PIECE)]
    if out and len(out[-1]) < PIECE:
        out[-1] = out[-1] + bytes(PIECE - len(out[-1]))
    return out


def scan(path: str, on_change: Optional[ChangeCallback] = None,
         on_baseline: Optional[Callable[[str, int, bytes], None]] = None) -> SessionHistory:
    """Read a session and build the change history of every region.

    on_baseline(region, piece_index, bytes) is called once per piece for checkpoint 0.
    on_change is called for every real content change, with the previous and new bytes.
    """
    dump = tf.parse_file(path)
    pages = dump.header.model_ram_pages
    ram_pieces = pages * 4
    zero = bytes(PIECE)

    def ram_piece(cp, j, slot_cache):
        ref = cp.ram_sub_slots[j]
        if ref == tf.NEVER_TOUCHED_SLOT:
            return zero
        return dump.get_sub_page(ref)

    frames = [cp.frame for cp in dump.checkpoints]
    regions: Dict[str, RegionHistory] = {"ram": RegionHistory("ram", ram_pieces)}

    # baseline
    cp0 = dump.checkpoints[0]
    cur_ram = [ram_piece(cp0, j, None) for j in range(ram_pieces)]
    cur_slot = list(cp0.ram_sub_slots)
    regions["ram"].changes.append([])
    regions["ram"].nonzero_at_baseline = [p != zero for p in cur_ram]
    if on_baseline:
        for j, p in enumerate(cur_ram):
            on_baseline("ram", j, p)

    def gs_blob(cp):
        raw = cp.peripheral_blobs.get(GS_PERIPHERAL_ID)
        return None if raw is None else tf.decode_peripheral_blob(GS_PERIPHERAL_ID, raw)

    gs0 = gs_blob(cp0)
    cur_gs: Optional[List[bytes]] = None
    if gs0 is not None and len(gs0) > GS_FIXED_STATE_BYTES:
        cur_gs = _split(gs0[GS_FIXED_STATE_BYTES:])
        regions["gs.ram"] = RegionHistory("gs.ram", len(cur_gs))
        regions["gs.ram"].changes.append([])
        regions["gs.ram"].nonzero_at_baseline = [p != zero for p in cur_gs]
        if on_baseline:
            for j, p in enumerate(cur_gs):
                on_baseline("gs.ram", j, p)

    for i in range(1, len(dump.checkpoints)):
        cp = dump.checkpoints[i]
        changed = []
        for j in range(ram_pieces):
            ref = cp.ram_sub_slots[j]
            if ref == cur_slot[j]:
                continue
            cur_slot[j] = ref
            new = zero if ref == tf.NEVER_TOUCHED_SLOT else dump.get_sub_page(ref)
            if new != cur_ram[j]:
                if on_change:
                    on_change("ram", i, j, cur_ram[j], new)
                cur_ram[j] = new
                changed.append(j)
        regions["ram"].changes.append(changed)

        if cur_gs is not None:
            blob = gs_blob(cp)
            pieces = _split(blob[GS_FIXED_STATE_BYTES:]) if blob is not None else cur_gs
            gchanged = []
            for j, new in enumerate(pieces):
                if new != cur_gs[j]:
                    if on_change:
                        on_change("gs.ram", i, j, cur_gs[j], new)
                    cur_gs[j] = new
                    gchanged.append(j)
            regions["gs.ram"].changes.append(gchanged)

    return SessionHistory(path=path, model_ram_pages=pages, frames=frames, regions=regions)


def content_id(data: bytes) -> bytes:
    """Short identity of a piece's content (for counting differing pieces)."""
    return hashlib.blake2b(data, digest_size=12).digest()
