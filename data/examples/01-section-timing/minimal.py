#!/usr/bin/env python3
"""The smallest form: one entry / exit pair, min / avg / max, and a pause on either of two things: a pass whose
T-states match a condition, or an ordinary breakpoint. IY (or --reg) at the entry is kept for every pass.

    python3 minimal.py                                    # Sparkles .. Sparkles.End of demo.sna, pause above 20000 T
    python3 minimal.py Plasma Plasma.End 54000            # a number alone means "longer than"
    python3 minimal.py Plasma Plasma.End "!=53361"        # any condition: > < >= <= == !=
    python3 minimal.py Sparkles Sparkles.End ">99999" --stop-at Sparkles.inside    # pause at a breakpoint
    python3 minimal.py Dispatch Dispatch.End "!=354" --model PENTAGON --snapshot ../02-constant-time/constant.sna

A pass runs from the start of the instruction at ENTRY to the start of the instruction at EXIT, in global T-states
(frame * frame length + T in the frame: they run on across frames). Everything in between counts, an interrupt
too: for code that runs with interrupts on, profile_sections.py takes them out (net).
"""

import argparse
import operator
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from unreal_webapi import connect, load_sym, register_value  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = (("!=", operator.ne), (">=", operator.ge), ("<=", operator.le), ("==", operator.eq), (">", operator.gt),
         ("<", operator.lt), ("=", operator.eq), ("", operator.gt))

ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
ap.add_argument("entry", nargs="?", default="Sparkles", help="entry marker: a label or an address")
ap.add_argument("exit", nargs="?", default="Sparkles.End", help="exit marker: a label or an address")
ap.add_argument("pause_if", nargs="?", default=">20000", help="pause at the exit of a pass whose T-states match")
ap.add_argument("--stop-at", help="also pause at this ordinary breakpoint (a label or an address)")
ap.add_argument("--reg", default="iy", help="register kept from the entry of every pass (default iy)")
ap.add_argument("--passes", type=int, default=100)
ap.add_argument("--model", default="48K", help="48K, PENTAGON, ...")
ap.add_argument("--snapshot", default=os.path.join(HERE, "demo.sna"), help="its labels: the same name with .sym")
args = ap.parse_args()

sym = os.path.splitext(args.snapshot)[0] + ".sym"
labels = load_sym(sym) if os.path.exists(sym) else {}
names = {address: name for name, address in sorted(labels.items(), key=lambda kv: -kv[0].count("."))}


def address(text):
    return labels[text] if text in labels else int(text.replace("#", "0x").replace("$", "0x"), 0)


def where(value):
    return "#{:04X}{}".format(value, " ({})".format(names[value]) if value in names else "")


ENTRY, EXIT = address(args.entry), address(args.exit)
STOP = address(args.stop_at) if args.stop_at else None
op, test = next((o, f) for o, f in TESTS if args.pause_if.startswith(o))
LIMIT = int(args.pause_if[len(op):], 0)

emu = connect().create(args.model)   # finds unreal-qt, waits for it; attach() takes a running one instead
emu.take_control("minimal.py")       # the emulator window shows who drives it until release_control()
emu.mute()
emu.pause()
emu.load_snapshot(args.snapshot)
emu.post("/run_frames", {"count": 10})
emu.enable_breakpoints()
breakpoints = [emu.add_breakpoint(a) for a in {ENTRY, EXIT, STOP} if a is not None]

opened, passes, paused = None, [], None          # opened: (global T, register) of the entry not left yet
while len(passes) < args.passes and not paused:
    stop = emu.steps(100000)["stop"]             # run to the next breakpoint; the machine stays paused there
    if stop.get("reason") != "breakpoint":
        continue
    pc = stop.get("address")
    snap = emu.snapshot()                        # the time and the registers there, in one reading
    now = snap["time"]["frame"] * snap["time"]["frame_t"] + snap["time"]["t"]
    if pc == EXIT and opened:                    # the exit first: one address may end a pass and start the next
        passes.append(now - opened[0])
        if test(passes[-1], LIMIT):
            paused = "pass {} took {} T-states ({}), {} = {} at its entry".format(
                len(passes), passes[-1], args.pause_if, args.reg.upper(), where(opened[1]))
        opened = None
    if pc == ENTRY:                              # an entry that never reached the exit is simply replaced
        opened = (now, register_value(snap["regs"], args.reg))
    if pc == STOP:
        paused = "at the breakpoint {}".format(where(STOP))

if passes:
    print("passes {}  min {}  avg {:.1f}  max {}".format(len(passes), min(passes), sum(passes) / len(passes),
                                                          max(passes)))
for b in breakpoints:
    emu.remove_breakpoint(b)
emu.release_control()
if paused:
    print("PAUSED " + paused)
    print("instance {} stays paused for the debugger; DELETE /api/v1/emulator/{} when done".format(emu.id, emu.id))
else:
    emu.remove()
