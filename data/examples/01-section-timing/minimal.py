#!/usr/bin/env python3
"""The smallest form: one entry / exit pair, min / avg / max, pause when a pass takes longer than LIMIT.

    python3 minimal.py                      # Sparkles .. Sparkles.End of demo.sna on a 48K
    python3 minimal.py 0x8071 0x80C2 15000  # any entry, exit and limit
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from unreal_webapi import Unreal  # noqa: E402

ENTRY, EXIT, LIMIT = (int(a, 0) for a in sys.argv[1:4]) if len(sys.argv) > 3 else (0x8071, 0x80C2, 20000)

emu = Unreal().create("48K")                     # or "PENTAGON"; Unreal().attach() takes the running one
emu.mute()
emu.pause()
emu.load_snapshot(os.path.join(os.path.dirname(os.path.abspath(__file__)), "demo.sna"))
emu.post("/run_frames", {"count": 10})
emu.enable_breakpoints()
entry_bp = emu.add_breakpoint(ENTRY)
exit_bp = emu.add_breakpoint(EXIT)


def now():
    frame, t, frame_t = emu.time()                 # global t-states: they run on across frames
    return frame * frame_t + t


stack, passes = [], []
while len(passes) < 100:
    stop = emu.steps(100000)["stop"]               # run until the next breakpoint
    if stop.get("breakpoint_id") == entry_bp:
        stack.append(now())
    elif stop.get("breakpoint_id") == exit_bp and stack:
        passes.append(now() - stack.pop())
        if passes[-1] > LIMIT:
            print("pass {} took {} t-states > {}: paused at the exit".format(len(passes), passes[-1], LIMIT))
            break

print("passes {}  min {}  avg {:.1f}  max {}".format(len(passes), min(passes), sum(passes) / len(passes), max(passes)))
emu.remove_breakpoint(entry_bp)
emu.remove_breakpoint(exit_bp)
print("instance {} is paused; DELETE /api/v1/emulator/{} when done".format(emu.id, emu.id))
