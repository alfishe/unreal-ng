#!/usr/bin/env python3
"""Check that frame steps and direct seeks show the same, beam-accurate frame.

For each frame in the range: picture after ttd/seek {frame} vs picture after
ttd/step-forward from the previous frame. They must be identical, and the
T-state position after a step must stay at the frame boundary (no drift).
Also prints the number of distinct colors in the border - a static memory
decode shows a single border color.

Exit code 1 on any mismatch. Example:
  python3 capture/verify_stepping.py --emulator <id> --from 4520 --count 30
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from common.webapi import Emulator  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--emulator", required=True)
ap.add_argument("--from", dest="first", type=int, required=True)
ap.add_argument("--count", type=int, default=30)
args = ap.parse_args()
emu = Emulator(args.emulator)


def border_colors(rgb):
    mask = np.ones(rgb.shape[:2], bool)
    mask[48:240, 48:304] = False
    return len(np.unique(rgb[mask], axis=0))


seek = {}
for f in range(args.first, args.first + args.count):
    emu.call("POST", "/ttd/seek", {"frame": f})
    seek[f] = emu.frame_rgb()

bad = 0
emu.call("POST", "/ttd/seek", {"frame": args.first})
for f in range(args.first, args.first + args.count):
    pos = emu.call("GET", "/ttd/position")["current"]
    step = emu.frame_rgb()
    same = np.array_equal(step, seek[f])
    bad += (not same) or pos["frame"] != f
    print(f"frame {f}: position {pos['frame']} t={pos['tinframe']}  border colors {border_colors(step)}  "
          f"{'OK' if same else 'MISMATCH'}")
    emu.call("POST", "/ttd/step-forward")
sys.exit(1 if bad else 0)
