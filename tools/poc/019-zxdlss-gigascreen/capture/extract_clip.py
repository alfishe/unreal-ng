#!/usr/bin/env python3
"""Walk a TTD session frame by frame and save every frame as a lossless clip.

Each frame is the frame's FINAL beam-rendered picture: positioning by frame
number (ttd/seek, ttd/step-forward) shows exactly what the beam drew for that
frame, border stripes and multicolor included (display rule of
docs/inprogress/2026-09-28-ttd-positioning-and-display/design.md). Builds
before that fix showed a static memory decode on frame steps - check with
verify_stepping.py first.

Example:
  python3 capture/extract_clip.py --ttd data/across_the_edge_full.ttd --out data/clip_full
  python3 capture/extract_clip.py --emulator <id> --from 4500 --to 4700 --out data/clip_hiphop
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from common.clip import ClipWriter  # noqa: E402
from common.webapi import Emulator  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--ttd", help="load this .ttd into a new Pentagon 512K instance")
ap.add_argument("--emulator", help="use an existing instance that already holds the session")
ap.add_argument("--model", default="PENTAGON")
ap.add_argument("--ram", type=int, default=512)
ap.add_argument("--from", dest="first", type=int)
ap.add_argument("--to", dest="last", type=int)
ap.add_argument("--out", required=True)
args = ap.parse_args()

if args.emulator:
    emu = Emulator(args.emulator)
else:
    emu = Emulator.create(args.model, args.ram)
    emu.call("POST", "/pause")
    print("load:", emu.call("POST", "/ttd/load", {"path": os.path.abspath(args.ttd)}, timeout=600))

status = emu.call("GET", "/ttd/status")
first = args.first if args.first is not None else status["session_start_frame"]
last = args.last if args.last is not None else status["current_end_frame"]

emu.call("POST", "/ttd/seek", {"frame": first})
writer = ClipWriter(args.out)
t0, frame = time.time(), first
while True:
    scr = emu.call("GET", "/state/screen")
    paging = emu.call("GET", "/state/paging")
    p7ffd = next((l["value"] for l in paging.get("latches", []) if l.get("port") == "0x7FFD"), None)
    writer.add(emu.frame_rgb(), {"frame": frame, "active_screen": scr.get("active_screen"),
                                 "border": scr.get("border_color"), "p7FFD": p7ffd})
    if (frame - first) % 500 == 499:
        print(f"frame {frame}/{last}  {(frame - first + 1) / (time.time() - t0):.0f} fps  palette {len(writer.palette)}",
              flush=True)
    if frame >= last:
        break
    frame = emu.call("POST", "/ttd/step-forward")["frame"]
writer.close()
print("done", first, frame)
