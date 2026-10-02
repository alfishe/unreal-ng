"""Per-change piece statistics of a session, shared by the Phase 1 experiments.

For every real content change of a piece (see ttdhistory.py) it records the
zstd level 1 size of the XOR difference and of the full piece - the two
candidates the v1 encoder compresses (ttdcodecpagestore.cpp InternXor). It also
records the full size of every non-zero piece at checkpoint 0 and at every
50th checkpoint, which is what the v1 key frame stores again.

Sizes come from the Python zstandard binding (zstd 1.5.6; the emulator links
1.5.7). check_against_file() compares them with the Full slots the file itself
stores, so each experiment can state how close they are.

Results are cached in <repo>/scratch/ttd-experiments/cache/ (git-ignored).
"""

from __future__ import annotations

import os
import pickle
from dataclasses import dataclass, field
from typing import Dict, List, Tuple

import numpy as np
import zstandard

import ttdhistory as th
from datasets import ROOT

CACHE = os.path.join(ROOT, "scratch", "ttd-experiments", "cache")
V1_KEY_FRAME_INTERVAL = 50
_zc = zstandard.ZstdCompressor(level=1)
_ZERO = bytes(th.PIECE)


def zsize(data: bytes) -> int:
    return 0 if data == _ZERO else len(_zc.compress(data))


def xor(a: bytes, b: bytes) -> bytes:
    return (np.frombuffer(a, np.uint8) ^ np.frombuffer(b, np.uint8)).tobytes()


@dataclass
class RegionStats:
    name: str
    pieces: int
    checkpoints: int
    # one row per real change: checkpoint, piece, xor size, full size
    change_cp: np.ndarray = None
    change_piece: np.ndarray = None
    change_xor: np.ndarray = None
    change_full: np.ndarray = None
    # full size of every piece at checkpoint 0 and at each v1 key frame:
    # key_full[k] = array(pieces) for checkpoint k * 50 (0 = baseline); 0 = all-zero piece
    key_full: Dict[int, np.ndarray] = field(default_factory=dict)

    def changes_at(self) -> List[np.ndarray]:
        """Changed piece indices per checkpoint."""
        out = [np.empty(0, np.int64) for _ in range(self.checkpoints)]
        order = np.argsort(self.change_cp, kind="stable")
        cps = self.change_cp[order]
        bounds = np.searchsorted(cps, np.arange(self.checkpoints + 1))
        for i in range(self.checkpoints):
            out[i] = self.change_piece[order[bounds[i]:bounds[i + 1]]]
        return out


@dataclass
class SessionStats:
    name: str
    frames: List[int]
    model_ram_pages: int
    regions: Dict[str, RegionStats]


def compute(name: str, path: str) -> SessionStats:
    rows: Dict[str, List[Tuple[int, int, int, int]]] = {}
    contents: Dict[str, Dict[int, bytes]] = {}
    key_full: Dict[str, Dict[int, np.ndarray]] = {}
    sampled = {"next": 0}   # next checkpoint whose key-frame sample is still due

    def sample_until(upto: int) -> None:
        # The scan reports changes in checkpoint order, so when the first change
        # of checkpoint `upto` arrives, every change of the checkpoints before it
        # is applied: the contents are the state of checkpoint upto - 1, and of
        # any earlier checkpoint without changes since.
        while sampled["next"] < upto:
            k = sampled["next"]
            if k % V1_KEY_FRAME_INTERVAL == 0:
                for region, cmap in contents.items():
                    key_full.setdefault(region, {})[k] = np.array(
                        [zsize(cmap[j]) for j in range(len(cmap))], np.int64)
            sampled["next"] += 1

    def on_baseline(region, j, data):
        contents.setdefault(region, {})[j] = data

    def on_change(region, i, j, prev, new):
        sample_until(i)
        rows.setdefault(region, []).append((i, j, zsize(xor(prev, new)), zsize(new)))
        contents[region][j] = new

    hist = th.scan(path, on_change=on_change, on_baseline=on_baseline)
    sample_until(hist.checkpoints)

    regions = {}
    for rname, rh in hist.regions.items():
        r = np.array(rows.get(rname, []), np.int64).reshape(-1, 4)
        regions[rname] = RegionStats(
            name=rname, pieces=rh.pieces, checkpoints=hist.checkpoints,
            change_cp=r[:, 0], change_piece=r[:, 1], change_xor=r[:, 2], change_full=r[:, 3],
            key_full=key_full.get(rname, {}))
    return SessionStats(name=name, frames=hist.frames, model_ram_pages=hist.model_ram_pages, regions=regions)


def load(name: str, path: str) -> SessionStats:
    os.makedirs(CACHE, exist_ok=True)
    key = name.replace(":", "_").replace("/", "_")
    cache = os.path.join(CACHE, key + ".pkl")
    if os.path.exists(cache) and os.path.getmtime(cache) >= os.path.getmtime(path):
        with open(cache, "rb") as f:
            return pickle.load(f)
    stats = compute(name, path)
    with open(cache, "wb") as f:
        pickle.dump(stats, f)
    return stats


def check_against_file(path: str, samples: int = 400) -> Tuple[float, float]:
    """Mean and max relative difference between this module's zstd-1 size of a
    Full piece and the payload the emulator stored for it."""
    dump = th.tf.parse_file(path)
    diffs = []
    for slot in dump.slots:
        if slot.encoding != th.tf.ENCODING_FULL:
            continue
        mine = zsize(dump.get_sub_page(slot.index))
        if mine and len(slot.payload):
            diffs.append(abs(mine - len(slot.payload)) / len(slot.payload))
        if len(diffs) >= samples:
            break
    return (float(np.mean(diffs)) if diffs else 0.0, float(np.max(diffs)) if diffs else 0.0)
