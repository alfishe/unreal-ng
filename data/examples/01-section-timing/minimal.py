#!/usr/bin/env python3
"""The smallest form: one entry / exit pair, min / avg / max, pause when a pass takes longer than LIMIT.

    python3 minimal.py                      # Sparkles .. Sparkles.End of demo.sna on a 48K
    python3 minimal.py Plasma Plasma.End 54000   # any entry, exit (labels of demo.sym or numbers) and limit
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from unreal_webapi import connect, load_sym  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
labels = load_sym(os.path.join(HERE, "demo.sym"))
entry, exit_, limit = sys.argv[1:4] if len(sys.argv) > 3 else ("Sparkles", "Sparkles.End", "20000")
ENTRY = labels[entry] if entry in labels else int(entry, 0)
EXIT = labels[exit_] if exit_ in labels else int(exit_, 0)
LIMIT = int(limit)

emu = connect().create("48K")    # finds unreal-qt, waits for it; "PENTAGON" too; attach() takes a running one
emu.take_control("minimal.py")   # the emulator window shows who drives it until release_control()
emu.mute()
emu.pause()
emu.load_snapshot(os.path.join(HERE, "demo.sna"))
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
emu.release_control()
print("instance {} is paused; DELETE /api/v1/emulator/{} when done".format(emu.id, emu.id))
