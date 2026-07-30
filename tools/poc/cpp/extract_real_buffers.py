#!/usr/bin/env python3
"""Extract real codec-input buffers from a .ttd file into a binary blob.

The .ttd writer stores, per non-Zero slot, a zstd-compressed payload that
the codec actually saw. For Full slots the decompressed bytes are a raw
4 KB emulator page snapshot; for XorPrev slots the decompressed bytes are
the XOR-delta buffer (`new XOR prev`) that the compressor was handed.

This script walks every slot in the input .ttd, decompresses it, and
writes the 4 KB buffer into one of two output buckets:

  - full    : raw 4 KB page snapshots (real memory content)
  - xor     : real XOR-delta buffers (the dominant TTD workload)

The output file is a flat binary with a tiny header so the C++ PoC can
mmap / read it without any parser dependency.

Output layout (all little-endian):

  u32 magic            = 0x54544442  ('TBDG' = TTD Buffers)
  u32 version          = 1
  u32 sub_page_size    = 4096
  u32 full_count       = N_full
  u32 xor_count        = N_xor
  u32 reserved         = 0
  u8  full_payload[N_full  * 4096]   -- concatenation
  u8  xor_payload [N_xor   * 4096]   -- concatenation

Usage:
    python3 extract_real_buffers.py \\
        --ttd tools/verification/ttd-analyzer/testdata/active_demo.ttd \\
        --out tools/poc/cpp/real_buffers.bin
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

# Make the ttd-analyzer package importable without installation.
REPO_ROOT = Path(__file__).resolve().parents[3]
ANALYZER_SRC = REPO_ROOT / "tools" / "verification" / "ttd-analyzer" / "src"
sys.path.insert(0, str(ANALYZER_SRC))

from ttd_format import (  # noqa: E402
    ENCODING_FULL,
    ENCODING_XOR_PREV,
    SUB_PAGE_SIZE,
    parse_file,
)

MAGIC = 0x54544442
VERSION = 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ttd", required=True, type=Path,
                    help="input .ttd file")
    ap.add_argument("--out", required=True, type=Path,
                    help="output binary blob (.bin)")
    args = ap.parse_args()

    dump = parse_file(str(args.ttd))

    full_buffers: list[bytes] = []
    xor_buffers: list[bytes] = []

    for slot in dump.slots:
        if slot.encoding == ENCODING_FULL:
            full_buffers.append(dump.get_sub_page(slot.index))
        elif slot.encoding == ENCODING_XOR_PREV:
            # get_sub_page returns the reconstructed 4 KB page (XOR'd with prev).
            # But what we want is what the COMPRESSOR SAW, which is the delta
            # itself. The .ttd writer applies XOR before compressing, so the
            # payload stored on disk is `zstd(new XOR prev)`. We need to
            # decompress that payload directly to obtain the delta buffer,
            # NOT walk the XorPrev chain (which would give us the page content).
            xor_buffers.append(_decompress_slot_payload(slot.payload))

    # Sanity-check sizes.
    for b in full_buffers:
        if len(b) != SUB_PAGE_SIZE:
            raise SystemExit(f"full buffer not {SUB_PAGE_SIZE} B: {len(b)}")
    for b in xor_buffers:
        if len(b) != SUB_PAGE_SIZE:
            raise SystemExit(f"xor buffer not {SUB_PAGE_SIZE} B: {len(b)}")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("wb") as f:
        f.write(struct.pack("<IIIIII",
                            MAGIC, VERSION, SUB_PAGE_SIZE,
                            len(full_buffers), len(xor_buffers), 0))
        for b in full_buffers:
            f.write(b)
        for b in xor_buffers:
            f.write(b)

    total_bytes = (len(full_buffers) + len(xor_buffers)) * SUB_PAGE_SIZE
    print(f"wrote {args.out}")
    print(f"  full slots : {len(full_buffers):5d}  ({len(full_buffers) * SUB_PAGE_SIZE:>8d} B)")
    print(f"  xor  slots : {len(xor_buffers):5d}  ({len(xor_buffers)  * SUB_PAGE_SIZE:>8d} B)")
    print(f"  total      : {len(full_buffers) + len(xor_buffers):5d}  ({total_bytes:>8d} B)")
    return 0


def _decompress_slot_payload(payload: bytes) -> bytes:
    """Decompress the raw on-disk zstd payload of a slot.

    We need this because the public ``TtdDump.get_sub_page`` walks the
    XorPrev chain to reconstruct the *page content*, but the codec-input
    buffer we want is the *delta itself* (the compressed payload is
    `zstd(new XOR prev)`, and that delta is what every contender codec
    would actually see in production).
    """
    import zstandard as zstd
    dctx = zstd.ZstdDecompressor()
    out = dctx.decompress(payload)
    if len(out) != SUB_PAGE_SIZE:
        raise SystemExit(
            f"decompressed payload is {len(out)} B, expected {SUB_PAGE_SIZE}"
        )
    return out


if __name__ == "__main__":
    raise SystemExit(main())
