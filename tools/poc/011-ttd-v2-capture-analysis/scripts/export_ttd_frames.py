#!/usr/bin/env python3
"""Export real TTD frames to simple binary format for C++ benchmark."""
import sys
import os
import struct

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../../"))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools/verification/ttd-analyzer/src"))
import ttd_format

PAGE_SIZE = 4096

def export_ttd(filepath: str, outpath: str):
    dump = ttd_format.parse_file(filepath)
    num_frames = dump.header.checkpoint_count
    num_pages = dump.header.model_ram_pages
    frame_size = num_pages * PAGE_SIZE

    print(f"Exporting {os.path.basename(filepath)}: {num_frames} frames, {num_pages} pages")

    with open(outpath, 'wb') as f:
        # Header: num_frames (u32), num_pages (u32)
        f.write(struct.pack('<II', num_frames, num_pages))

        # Frames: raw bytes
        for cp_idx in range(num_frames):
            cp = dump.checkpoints[cp_idx]
            for p in range(num_pages):
                page_bytes = dump.get_sub_page(cp.ram_page_refs[p])
                f.write(page_bytes)

    print(f"  -> {outpath} ({os.path.getsize(outpath) // 1024} KB)")

def main():
    ttd_dir = "testdata/ttd"
    out_dir = "tools/poc/011-ttd-v2-capture-analysis/bin"
    os.makedirs(out_dir, exist_ok=True)

    files = ["demo_7threality.ttd", "demo_across-the-edge-second.ttd", "active_demo.ttd", "idle_session.ttd"]

    for fname in files:
        fpath = os.path.join(ttd_dir, fname)
        if os.path.exists(fpath):
            outname = fname.replace('.ttd', '.frames')
            export_ttd(fpath, os.path.join(out_dir, outname))

if __name__ == "__main__":
    main()
