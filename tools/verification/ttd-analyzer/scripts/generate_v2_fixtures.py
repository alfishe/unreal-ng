#!/usr/bin/env python3
"""Generate v2 .ttd fixtures with realistic ZX Spectrum workloads.

Produces two .ttd files that exercise the Phase 5 codec the way real
sessions do:

  * idle_session.ttd    — model idle for 200 frames. RAM barely changes.
                          Exercises the "all sub-pages clean" fast path.
  * active_demo.ttd     — model running a demo (BLI-like). Screen RAM,
                          attribute RAM, and a few code pages change every
                          frame; the rest stays put. Exercises the XOR-delta
                          codec at its designed-for workload.

Both fixtures use the same Pentagon-128K model (8 RAM pages × 16 KB) as
the canonical test model. Each one emits:
  - One I-frame at frame 0 (every emu page captured as Full sub-slots)
  - 199 P-frames anchored at frame 0 (only dirty 4 KB sub-pages re-captured)

The generator implements the codec faithfully:
  * 4 KB sub-pages (SUB_PAGE_SIZE = 4096)
  * 4 sub-pages per 16 KB emu page
  * Full / XorPrev / Zero encodings
  * zstd level 1 compression
  * CRC32C (Castagnoli) integrity tag
  - frame_kind discriminator (I-frame / P-frame)
  - keyframe_anchor chain rooting

Usage:
    python3 scripts/generate_v2_fixtures.py [--out-dir DIR]

Default output dir: tools/verification/ttd-analyzer/testdata/
"""

from __future__ import annotations

import argparse
import os
import random
import struct
import sys
from typing import Dict, List, Optional, Tuple

# Resolve the analyzer package path regardless of CWD.
_HERE = os.path.dirname(os.path.abspath(__file__))
_ANALYZER_ROOT = os.path.dirname(_HERE)
sys.path.insert(0, _ANALYZER_ROOT)

import zstandard as zstd  # noqa: E402

from src.ttd_format import (  # noqa: E402
    ENCODING_FULL,
    ENCODING_XOR_PREV,
    ENCODING_ZERO,
    FRAME_KIND_KEY_FRAME,
    FRAME_KIND_DELTA_FRAME,
    MAGIC,
    NEVER_TOUCHED_SLOT,
    SCHEMA_VERSION,
    SUB_PAGE_SIZE,
    SUB_PAGES_PER_EMU_PAGE,
    crc32c,
    parse_bytes,
)


# ===========================================================================
# Constants
# ===========================================================================

EMU_PAGE_SIZE = SUB_PAGE_SIZE * SUB_PAGES_PER_EMU_PAGE  # 16 KB
MODEL_RAM_PAGES = 8                                     # Pentagon-128K
TOTAL_SUB_SLOTS = MODEL_RAM_PAGES * SUB_PAGES_PER_EMU_PAGE  # 32

# Pentagon-128K model_id in the engine (matches eModelIds).
MODEL_PENTAGON_128K = 2

ZSTD_L1 = zstd.ZstdCompressor(level=1)


# ===========================================================================
# Codec helpers (mirror the C++ TTDCodecPageStore writer)
# ===========================================================================


def _zstd1_compress(raw: bytes) -> bytes:
    """zstd level 1 — matches ttd::codec::Compress defaults."""
    return ZSTD_L1.compress(raw)


def _make_slot_full(raw_4k: bytes) -> Tuple[int, int, bytes]:
    """Build a Full slot.

    Returns (encoding, prev_slot, payload).
    """
    assert len(raw_4k) == SUB_PAGE_SIZE
    payload = _zstd1_compress(raw_4k)
    return (ENCODING_FULL, NEVER_TOUCHED_SLOT, payload)


def _make_slot_xor(prev_raw_4k: bytes, new_raw_4k: bytes) -> Tuple[int, int, bytes]:
    """Build an XorPrev slot, choosing Zero when delta is all zeros.

    Returns (encoding, prev_slot, payload). Caller must supply prev_slot
    separately because we don't know it here.
    """
    assert len(prev_raw_4k) == len(new_raw_4k) == SUB_PAGE_SIZE
    xor_buf = bytes(a ^ b for a, b in zip(prev_raw_4k, new_raw_4k))
    if not any(xor_buf):
        # Page unchanged — caller should AddRef prev_slot instead.
        # Returning Zero here would be semantically wrong; signal by
        # returning None and let caller decide.
        return (ENCODING_ZERO, NEVER_TOUCHED_SLOT, b"")
    payload = _zstd1_compress(xor_buf)
    return (ENCODING_XOR_PREV, NEVER_TOUCHED_SLOT, payload)


def _make_slot_zero() -> Tuple[int, int, bytes]:
    """Build a Zero slot (all-zeros 4 KB page)."""
    return (ENCODING_ZERO, NEVER_TOUCHED_SLOT, b"")


# ===========================================================================
# .ttd binary writer
# ===========================================================================


class TtdV2Writer:
    """Streaming v2 .ttd writer.

    Usage:
        w = TtdV2Writer(model_ram_pages=8, model_id=2)
        slot_idx = w.intern_full(b"\\x00" * 4096)
        ...
        w.begin_checkpoint(frame=0, frame_kind=KEY_FRAME, anchor=0)
        w.emit_ram_sub_slots([...])  # 32 entries
        w.end_checkpoint()
        ...
        w.write_file("out.ttd")
    """

    def __init__(self, model_ram_pages: int, model_id: int):
        self.model_ram_pages = model_ram_pages
        self.model_id = model_id
        self._slots: List[Tuple[int, int, int, bytes]] = []  # (enc, refcount, prev_slot, payload)
        self._decompressed_cache: Dict[int, bytes] = {}  # slot_idx -> 4 KB raw
        self._checkpoints_buf = bytearray()
        self._n_checkpoints = 0
        self._emulator_id = b"unreal-ng-fxgen"

    # --- Slot interning ---------------------------------------------------

    def intern_full(self, raw_4k: bytes) -> int:
        enc, prev, payload = _make_slot_full(raw_4k)
        idx = len(self._slots)
        self._slots.append((enc, 1, prev, payload))
        self._decompressed_cache[idx] = raw_4k
        return idx

    def intern_xor(self, prev_slot: int, new_raw_4k: bytes) -> int:
        prev_raw = self._decompressed_cache[prev_slot]
        enc, _, payload = _make_slot_xor(prev_raw, new_raw_4k)
        if enc == ENCODING_ZERO:
            # Page unchanged — reuse prev_slot (this is what C++ does too).
            self._bump_refcount(prev_slot)
            self._decompressed_cache[prev_slot]  # already cached
            return prev_slot
        idx = len(self._slots)
        self._slots.append((enc, 1, prev_slot, payload))
        self._decompressed_cache[idx] = new_raw_4k
        return idx

    def intern_zero(self) -> int:
        idx = len(self._slots)
        self._slots.append((_make_slot_zero()[0], 1, NEVER_TOUCHED_SLOT, b""))
        self._decompressed_cache[idx] = bytes(SUB_PAGE_SIZE)
        return idx

    def addref(self, slot_idx: int) -> None:
        self._bump_refcount(slot_idx)

    def _bump_refcount(self, slot_idx: int) -> None:
        enc, rc, prev, payload = self._slots[slot_idx]
        self._slots[slot_idx] = (enc, rc + 1, prev, payload)

    # --- Checkpoint emission ----------------------------------------------

    def begin_checkpoint(
        self,
        frame: int,
        global_t: int,
        frame_kind: int,
        keyframe_anchor: int,
    ) -> None:
        self._cp_buf = bytearray()
        self._cp_buf += struct.pack("<Q", frame)
        self._cp_buf += struct.pack("<Q", global_t)
        self._cp_buf += struct.pack("<B", frame_kind)
        self._cp_buf += struct.pack("<Q", keyframe_anchor)
        # CPU state — 48 bytes of plausible Z80 state.
        self._cp_buf += self._pack_cpu()
        # Chipset — 128 bytes.
        self._cp_buf += self._pack_chipset(frame)

    def emit_ram_sub_slots(self, sub_slots: List[int]) -> None:
        assert len(sub_slots) == self.model_ram_pages * SUB_PAGES_PER_EMU_PAGE, \
            f"expected {self.model_ram_pages * SUB_PAGES_PER_EMU_PAGE} sub-slots, got {len(sub_slots)}"
        for ref in sub_slots:
            self._cp_buf += struct.pack("<I", ref)

    def emit_peripheral_blobs(self, ay=b"", fdc=b"", tape=b"", covox=b"") -> None:
        for blob in (ay, fdc, tape, covox):
            self._cp_buf += struct.pack("<I", len(blob))
            self._cp_buf += blob

    def end_checkpoint(self) -> None:
        self._checkpoints_buf += self._cp_buf
        self._n_checkpoints += 1
        self._cp_buf = None

    # --- File writer ------------------------------------------------------

    def write_file(self, path: str, captured_at_unix_ms: int = 0,
                   session_start: int = 0, session_end: int = 0) -> None:
        with open(path, "wb") as f:
            f.write(self._serialize(captured_at_unix_ms, session_start, session_end))
        # Round-trip verify.
        with open(path, "rb") as f:
            data = f.read()
        parse_bytes(data)  # raises on malformed

    def _serialize(self, captured_at: int, sess_start: int, sess_end: int) -> bytes:
        buf = bytearray()
        buf += self._pack_header(captured_at, sess_start, sess_end)
        for enc, rc, prev, payload in self._slots:
            buf += self._pack_slot(enc, rc, prev, payload)
        buf += self._checkpoints_buf
        return bytes(buf)

    # --- Internal: header + slot packers ---------------------------------

    def _pack_header(self, captured_at: int, sess_start: int, sess_end: int) -> bytes:
        h = bytearray()
        h += MAGIC
        h += struct.pack("<H", SCHEMA_VERSION)
        h += struct.pack("<H", 0x0001)  # little-endian flag
        h += struct.pack("<B", self.model_id)
        h += struct.pack("<B", self.model_ram_pages)
        h += struct.pack("<H", 48)   # cpu_state_size
        h += struct.pack("<H", 128)  # chipset_state_size (matches Python parser's expectation)
        h += struct.pack("<Q", captured_at)
        h += struct.pack("<B", len(self._emulator_id))
        h += self._emulator_id
        h += struct.pack("<B", 1)               # session_state = recording
        h += struct.pack("<Q", sess_start)
        h += struct.pack("<Q", sess_end)
        h += struct.pack("<I", len(self._slots))
        h += struct.pack("<I", self._n_checkpoints)
        h += b"\x00" * 8                        # reserved
        return bytes(h)

    def _pack_slot(self, enc: int, rc: int, prev: int, payload: bytes) -> bytes:
        out = bytearray()
        out += struct.pack("<B", enc)
        out += struct.pack("<I", rc)
        out += struct.pack("<I", prev)
        # CRC32C of the ORIGINAL raw page. The Python parser recomputes
        # this from decompressed bytes and stashes it on the slot. Writers
        # in C++ store 0 and let the reader recompute — we follow the same
        # convention so the parser's recomputed value matches what the
        # C++ writer would produce.
        out += struct.pack("<I", 0)
        out += struct.pack("<I", len(payload))
        out += payload
        return bytes(out)

    # --- Internal: CPU + chipset packers ---------------------------------

    def _pack_cpu(self) -> bytes:
        out = bytearray()
        # 12 u16: pc, sp, af, bc, de, hl, ix, iy, alt_af..alt_hl
        for v in [0x1234, 0xFFFE] + [0] * 10:
            out += struct.pack("<H", v)
        # 7 u8: i, r_low, r_hi, iff1, iff2, im, halted
        for v in [0x3F, 0x00, 0x00, 1, 1, 1, 0]:
            out += struct.pack("<B", v)
        out += b"\x00"                    # padding before memptr
        out += struct.pack("<H", 0)       # memptr
        out += struct.pack("<B", 0)       # q
        out += b"\x00"                    # padding before eipos
        out += struct.pack("<H", 0)       # eipos
        out += struct.pack("<H", 0)       # haltpos
        out += struct.pack("<B", 0) * 3   # nmi/int_pending/int_gate
        out += b"\x00"                    # padding before halt_cycle
        out += struct.pack("<I", 0)       # halt_cycle
        return bytes(out)

    def _pack_chipset(self, frame: int) -> bytes:
        out = bytearray()
        out += struct.pack("<Q", frame * 69888)  # t_states (~Pentagon frame)
        out += struct.pack("<Q", frame)          # frame_counter
        # 13 chipset u8 ports — keep p7FFD stable (bank 0, screen 5).
        for v in [0x18, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x07, 0x00]:
            out += struct.pack("<B", v)
        # 21 extended ports
        out += b"\x00" * 21
        out += b"\x00" * 4              # wd_shadow
        out += b"\x00" * 16             # comp_pal
        out += struct.pack("<B", 0)     # ulaplus_mode
        out += struct.pack("<B", 0)     # ulaplus_reg
        out += b"\x00" * 64             # ulaplus_cram
        out += b"\x00" * 32             # pfff7
        return bytes(out)


# ===========================================================================
# Workload models
# ===========================================================================


def _initial_ram_idle() -> List[bytearray]:
    """Idle session: RAM is mostly zero except screen + a few code pages.

    Pentagon-128K maps:
      page 0 (0x0000-0x3FFF phys 0): ROM (read-only; not dirty)
      page 5 (0x4000-0x7FFF phys 5): screen — 6912 bytes of demo content
      other pages: zero
    """
    pages = [bytearray(EMU_PAGE_SIZE) for _ in range(MODEL_RAM_PAGES)]
    # Populate screen page (page 5) with realistic-ish content.
    rng = random.Random(42)
    screen = pages[5]
    # 6144 bytes of pixel data — half random noise, half zeros.
    for i in range(6144):
        screen[i] = rng.randint(0, 255) if rng.random() < 0.5 else 0
    # 768 bytes of attribute data — each byte is FBBIIPPP.
    for i in range(6144, 6144 + 768):
        screen[i] = rng.randint(0, 0x3F)
    return pages


def _step_idle(pages: List[bytearray], frame: int) -> List[int]:
    """Idle step: nothing changes. Return list of dirty emu-page indices."""
    return []


def _initial_ram_active() -> List[bytearray]:
    """Active demo (BLI-like): screen + a few code pages populated."""
    pages = _initial_ram_idle()
    rng = random.Random(123)
    # Populate page 0 with a fake "loader" pattern (just for noise).
    for i in range(EMU_PAGE_SIZE):
        pages[0][i] = rng.randint(0, 255) if rng.random() < 0.3 else 0
    # Page 1: code that "executes" — sparse nonzero bytes.
    for i in range(EMU_PAGE_SIZE):
        pages[1][i] = rng.randint(0, 255) if rng.random() < 0.2 else 0
    return pages


def _step_active(pages: List[bytearray], frame: int) -> List[int]:
    """BLI-like step: screen + small code region change every frame.

    Returns list of dirty emu-page indices.

    Realistic demo behavior (tuned to match measured real-demo patterns
    from docs/inprogress/2026-07-19-time-travel/phase-5-codec-poc-results.md):
      - Pixel memory (6144 B): ~5% byte churn per frame. Real demos update
        small sprite regions, scroll a few rows, or rotate palettes — not
        full-screen repaints. 5% gives ~310 changed bytes per frame.
      - Attribute memory (768 B): ~15% byte churn per frame. Demos flip
        attributes more aggressively than pixels (color cycling, flash).
      - Code page: ~30 bytes change per frame. Self-modifying code is
        rare; most demos touch a handful of variables each frame.
      - Page 0 (ROM): never changes (no-op).
    """
    rng = random.Random(frame * 31 + 7)
    screen = pages[5]
    # Pixel churn — 5% of 6144 = ~310 bytes.
    for i in range(6144):
        if rng.random() < 0.05:
            screen[i] = rng.randint(0, 255)
    # Attribute churn — 15% of 768 = ~115 bytes.
    for i in range(6144, 6144 + 768):
        if rng.random() < 0.15:
            screen[i] = rng.randint(0, 0x3F)
    # Code page — 30 bytes per frame.
    code = pages[1]
    for _ in range(30):
        offset = rng.randint(0, EMU_PAGE_SIZE - 1)
        code[offset] = rng.randint(0, 255)
    return [1, 5]


# ===========================================================================
# Session builders
# ===========================================================================


def _capture_checkpoint(
    writer: TtdV2Writer,
    frame: int,
    frame_kind: int,
    keyframe_anchor: int,
    pages_prev: Optional[List[bytearray]],
    pages_now: List[bytearray],
    dirty_emu_pages: List[int],
    ever_dirty_emu_pages: set,
) -> List[bytearray]:
    """Emit one checkpoint into ``writer`` using the v2 codec.

    For each emu page:
      - If I-frame (frame_kind == KEY_FRAME): every sub-page gets InternFull
        (or InternZero for all-zero pages).
      - If P-frame and page was never dirty this session: emit
        NEVER_TOUCHED_SLOT for all 4 sub-slots.
      - If P-frame and page was dirty this frame: InternXor each of the 4
        sub-pages against the previous slot index.
      - If P-frame and page was ever dirty but not this frame: reuse the
        previous slot index (AddRef).

    Returns the new ``pages_prev`` (== pages_now for next iteration).
    """
    sub_slots: List[int] = [NEVER_TOUCHED_SLOT] * TOTAL_SUB_SLOTS

    for page_idx in range(MODEL_RAM_PAGES):
        base = page_idx * SUB_PAGES_PER_EMU_PAGE
        page_data = pages_now[page_idx]

        if frame_kind == FRAME_KIND_KEY_FRAME:
            # Capture every sub-page as Full (or Zero if all zeros).
            for sub in range(SUB_PAGES_PER_EMU_PAGE):
                chunk = bytes(page_data[sub * SUB_PAGE_SIZE:(sub + 1) * SUB_PAGE_SIZE])
                if not any(chunk):
                    slot = writer.intern_zero()
                else:
                    slot = writer.intern_full(chunk)
                sub_slots[base + sub] = slot
        else:
            # P-frame
            if page_idx not in ever_dirty_emu_pages:
                # Never touched this session — leave NEVER_TOUCHED.
                continue
            # Page was (or is) dirty. Need previous slot indices for XorPrev.
            # For simplicity, we look up pages_prev's sub-page slot indices
            # from the writer's last checkpoint — but since we don't track
            # that explicitly here, we re-intern based on diff:
            #   - If page not dirty this frame: reuse prev slot (we need it).
            #   - If page dirty this frame: InternXor.
            # The trick: we maintained prev_slot_state externally for this.
            prev_slots = _PREV_SLOTS_BY_PAGE.get(page_idx, [NEVER_TOUCHED_SLOT] * 4)

            if page_idx not in dirty_emu_pages:
                # Reuse prev slots (AddRef).
                for sub in range(SUB_PAGES_PER_EMU_PAGE):
                    if prev_slots[sub] != NEVER_TOUCHED_SLOT:
                        writer.addref(prev_slots[sub])
                        sub_slots[base + sub] = prev_slots[sub]
            else:
                # Page dirty — XOR each sub-page against prev.
                for sub in range(SUB_PAGES_PER_EMU_PAGE):
                    chunk = bytes(page_data[sub * SUB_PAGE_SIZE:(sub + 1) * SUB_PAGE_SIZE])
                    if prev_slots[sub] == NEVER_TOUCHED_SLOT:
                        # First time this sub-page is captured.
                        if not any(chunk):
                            slot = writer.intern_zero()
                        else:
                            slot = writer.intern_full(chunk)
                    else:
                        prev_chunk = bytes(
                            pages_prev[page_idx][sub * SUB_PAGE_SIZE:(sub + 1) * SUB_PAGE_SIZE]
                        )
                        if chunk == prev_chunk:
                            # Unchanged sub-page within dirty emu page — reuse.
                            writer.addref(prev_slots[sub])
                            slot = prev_slots[sub]
                        else:
                            slot = writer.intern_xor(prev_slots[sub], chunk)
                    sub_slots[base + sub] = slot

            # Persist new slots for next iteration.
            _PREV_SLOTS_BY_PAGE[page_idx] = list(sub_slots[base:base + SUB_PAGES_PER_EMU_PAGE])

    writer.begin_checkpoint(
        frame=frame,
        global_t=frame * 69888,
        frame_kind=frame_kind,
        keyframe_anchor=keyframe_anchor,
    )
    writer.emit_ram_sub_slots(sub_slots)
    writer.emit_peripheral_blobs()
    writer.end_checkpoint()
    # Return a SNAPSHOT of pages_now so the next iteration sees this
    # frame's content as "previous" when computing XOR deltas. If we
    # returned pages_now directly, the caller's in-place mutation would
    # make pages_prev alias pages_now, and every XOR would be zero.
    return [bytearray(p) for p in pages_now]


# Module-level state used by _capture_checkpoint to track previous slot
# indices per emu page across calls. (Reset per session.)
_PREV_SLOTS_BY_PAGE: Dict[int, List[int]] = {}


def build_session(
    n_frames: int,
    initial_pages_fn,
    step_fn,
    label: str,
    out_path: str,
) -> Dict:
    """Build a complete .ttd session and write to ``out_path``.

    Returns a dict of telemetry: total_bytes, n_checkpoints, per_frame_bytes,
    compression_ratio.
    """
    # Reset cross-call state.
    _PREV_SLOTS_BY_PAGE.clear()

    writer = TtdV2Writer(model_ram_pages=MODEL_RAM_PAGES, model_id=MODEL_PENTAGON_128K)
    pages = initial_pages_fn()
    ever_dirty: set = set()
    pages_prev: Optional[List[bytearray]] = None

    for frame in range(n_frames):
        if frame == 0:
            # Frame 0 is the I-frame: capture every page.
            frame_kind = FRAME_KIND_KEY_FRAME
            anchor = 0
            dirty_emu_pages = list(range(MODEL_RAM_PAGES))
            ever_dirty.update(dirty_emu_pages)
        else:
            # Step the workload, get dirty list.
            dirty_emu_pages = step_fn(pages, frame)
            ever_dirty.update(dirty_emu_pages)
            frame_kind = FRAME_KIND_DELTA_FRAME
            anchor = 0  # Anchor at frame 0 for the whole session.

        pages_prev = _capture_checkpoint(
            writer=writer,
            frame=frame,
            frame_kind=frame_kind,
            keyframe_anchor=anchor,
            pages_prev=pages_prev,
            pages_now=pages,
            dirty_emu_pages=dirty_emu_pages,
            ever_dirty_emu_pages=ever_dirty,
        )

    writer.write_file(
        out_path,
        captured_at_unix_ms=0,
        session_start=0,
        session_end=n_frames - 1,
    )

    # Telemetry.
    file_bytes = os.path.getsize(out_path)
    n_cps = writer._n_checkpoints
    n_slots = len(writer._slots)
    live_payload = sum(len(p) for _, _, _, p in writer._slots)
    raw_total = sum(SUB_PAGE_SIZE for enc, _, _, _ in writer._slots if enc != ENCODING_ZERO)
    ratio = (live_payload / raw_total) if raw_total else 1.0

    return {
        "label": label,
        "path": out_path,
        "n_frames": n_frames,
        "n_checkpoints": n_cps,
        "n_slots": n_slots,
        "file_bytes": file_bytes,
        "per_frame_bytes": file_bytes / n_cps,
        "live_payload_bytes": live_payload,
        "raw_payload_bytes": raw_total,
        "compression_ratio": ratio,
    }


# ===========================================================================
# CLI
# ===========================================================================


def _format_size(n: float) -> str:
    if n < 1024:
        return f"{n:.0f} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.2f} KB"
    return f"{n / (1024 * 1024):.2f} MB"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out-dir", default=os.path.join(_ANALYZER_ROOT, "testdata"),
        help="Where to write the .ttd fixtures (default: testdata/)",
    )
    parser.add_argument(
        "--frames", type=int, default=200,
        help="Number of frames per session (default: 200)",
    )
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    print(f"Generating v2 fixtures into {args.out_dir}/")
    print(f"  model: Pentagon-128K ({MODEL_RAM_PAGES} × 16 KB = {MODEL_RAM_PAGES * 16} KB RAM)")
    print(f"  frames per session: {args.frames}")
    print()

    results = []

    # 1. Idle session
    idle_path = os.path.join(args.out_dir, "idle_session.ttd")
    print(f"[1/2] Building idle session → {idle_path}")
    idle_res = build_session(
        n_frames=args.frames,
        initial_pages_fn=_initial_ram_idle,
        step_fn=_step_idle,
        label="idle",
        out_path=idle_path,
    )
    results.append(idle_res)

    # 2. Active demo session (BLI-like)
    active_path = os.path.join(args.out_dir, "active_demo.ttd")
    print(f"[2/2] Building active demo → {active_path}")
    active_res = build_session(
        n_frames=args.frames,
        initial_pages_fn=_initial_ram_active,
        step_fn=_step_active,
        label="active_demo",
        out_path=active_path,
    )
    results.append(active_res)

    # Report
    print()
    print("=" * 78)
    print("V2 CODEC EFFICIENCY REPORT")
    print("=" * 78)
    v1_baseline_kb = 46.5
    print(f"v1 baseline: {v1_baseline_kb:.1f} KB / frame")
    print()
    print(f"{'Workload':<16} {'File':>12} {'Frames':>8} {'Slots':>8} "
          f"{'Per-frame':>12} {'Ratio':>8} {'vs v1':>10}")
    print("-" * 78)
    for r in results:
        per_frame_kb = r["per_frame_bytes"] / 1024
        improvement = v1_baseline_kb / per_frame_kb if per_frame_kb > 0 else float("inf")
        print(f"{r['label']:<16} {_format_size(r['file_bytes']):>12} "
              f"{r['n_checkpoints']:>8} {r['n_slots']:>8} "
              f"{_format_size(r['per_frame_bytes']):>12} "
              f"{r['compression_ratio'] * 100:>7.2f}% "
              f"{improvement:>9.1f}×")

    print()
    target_lo = v1_baseline_kb / 17
    target_hi = v1_baseline_kb / 11
    print(f"Target efficiency: 11-17× better than v1 "
          f"= {_format_size(target_hi * 1024)}-{_format_size(target_lo * 1024)} / frame")
    print()
    for r in results:
        per_frame_kb = r["per_frame_bytes"] / 1024
        improvement = v1_baseline_kb / per_frame_kb if per_frame_kb > 0 else float("inf")
        status = "✓ MEETS" if improvement >= 11 else "✗ BELOW TARGET"
        if improvement >= 17:
            status = "✓✓ EXCEEDS (>=17×)"
        print(f"  {r['label']:<16} {improvement:>5.1f}×  {status}")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
