#!/usr/bin/env python3
"""Check that every pass of Dispatch .. Dispatch.End takes exactly 354 T-states, on a new Pentagon.

    python3 check.py                       # constant.sna (two timing bugs): which passes are off, by handler
    python3 check.py --fixed               # constant-fixed.sna: every pass 354
    python3 check.py --break-on-mismatch   # pause at the first pass that is off
    python3 check.py --attach --sym my.sym # your own program, already running; its labels Dispatch / Dispatch.End

profile_sections.py (../01-section-timing) does the work; any of its options can be added.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "01-section-timing", "profile_sections.py")

args = sys.argv[1:]
program = "constant-fixed" if "--fixed" in args else "constant"
args = [a for a in args if a != "--fixed"]

command = [sys.executable, TOOL, "--model", "PENTAGON",
           "--snapshot", os.path.join(HERE, program + ".sna"), "--sym", os.path.join(HERE, program + ".sym"),
           "--section", "Dispatch", "--expect", "Dispatch=354",
           "--group-by", "iy", "--show-regs", "a_,b",
           "--frames", "400", "--passes", "400", "--out", program + "-report"] + args
sys.exit(subprocess.call(command))
