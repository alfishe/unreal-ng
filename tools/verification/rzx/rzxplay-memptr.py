#!/usr/bin/env python3
"""SkoolKit rzxplay.py with the MEMPTR-exact Z80 simulator.

rzxplay.py runs SkoolKit's fast C simulator, which updates MEMPTR only in its
contention build: after a taken JR / JP / DJNZ the undocumented flags 3 and 5
of BIT n,(HL) come from a stale MEMPTR. The contention simulator (CCMIOSimulator)
tracks MEMPTR on every instruction; contention itself does not change the CPU
path of an RZX playback (every IN comes from the recording). Worked example:
Dargon's Crypt (+2) ends with F = #74 on a real Z80 and here, #5C with the
fast simulator.

Usage: rzxplay-memptr.py [rzxplay.py options] FILE [OUTFILE]
"""
import sys

from skoolkit import CCMIOSimulator, rzxplay

if CCMIOSimulator is None:
    sys.exit("SkoolKit was installed without its C extensions (pip install skoolkit builds them)")
rzxplay.CSimulator = CCMIOSimulator
sys.exit(rzxplay.main(sys.argv[1:]))
