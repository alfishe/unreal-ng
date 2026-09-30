#!/usr/bin/env python3
"""Record a whole program run as a TTD session and dump it to a .ttd file.

Needs a running unreal-qt (WebAPI on :8090). Creates its own emulator
instance, inserts the disk with autostart, starts TTD recording right AFTER
the autostart reset (the reset would otherwise invalidate the session), lets
the program run in real time and stops when the screen stays unchanged for
--static-seconds after --min-frame, or at --max-frame.

TTD recording locks turbo and the tape/disk loader shortcuts off (commits
306c596e, 005771c8), so parts load at real speed and the recording shows
real-speed code.

Example (Across the Edge):
  python3 capture/record_ttd.py --disk testdata/loaders/trd/across_the_edge_by_demarche.trd \
      --out data/across_the_edge_full.ttd --min-frame 15500 --max-frame 17500
"""
import argparse
import hashlib
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from common.webapi import Emulator  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--disk", required=True, help="disk image (absolute or repo-relative path)")
ap.add_argument("--out", required=True, help=".ttd output path")
ap.add_argument("--model", default="PENTAGON")
ap.add_argument("--ram", type=int, default=512)
ap.add_argument("--min-frame", type=int, default=0, help="never stop before this frame (static intros)")
ap.add_argument("--max-frame", type=int, default=20000)
ap.add_argument("--static-seconds", type=float, default=20.0)
args = ap.parse_args()

emu = Emulator.create(args.model, args.ram)
print("emulator", emu.id)
r = emu.call("POST", "/disk/A/insert", {"path": os.path.abspath(args.disk), "autostart": True})
print("autostart:", r.get("autostart_message", r))
print("ttd:", emu.call("POST", "/ttd/start", {}))

last_digest, static_since = None, time.time()
while True:
    time.sleep(1.0)
    st = emu.call("GET", "/ttd/status")
    frame = st["current_end_frame"]
    digest = hashlib.md5(emu.frame_rgb().tobytes()).hexdigest()
    if digest != last_digest:
        last_digest, static_since = digest, time.time()
    static = time.time() - static_since
    print(f"\rframe {frame}  static {static:4.1f}s  page_store {st['page_store_used_bytes'] >> 20} MB", end="", flush=True)
    if frame >= args.max_frame or (frame >= args.min_frame and static >= args.static_seconds):
        break

print()
emu.call("POST", "/pause")
emu.call("POST", "/ttd/stop")
os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
print("dump:", emu.call("POST", "/ttd/dump", {"path": os.path.abspath(args.out)}, timeout=600))
