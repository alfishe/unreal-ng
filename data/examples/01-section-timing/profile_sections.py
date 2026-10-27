#!/usr/bin/env python3
"""T-state statistics between entry and exit markers, through the unreal-ng WebAPI.

A section is a piece of code with an entry address and one or more exit addresses. Every entry pushes the
global T-state count (frame * frame length + T in the frame, so it runs on across frames) on a stack, every
exit pops it: count, min, max and sum per section. A maskable interrupt accepted while sections are open is
counted in them with its exact cost: from the instruction boundary where it was accepted to the CPU's return to
the interrupted instruction (acknowledge, a JP in the vector table, the handler, RETI). "Net" is a pass without
them; --expect checks the net time, so constant-time code can also be checked with interrupts on.

    python3 profile_sections.py                                  # demo.sna on a new 48K, 100 frames
    python3 profile_sections.py --model PENTAGON --frames 200
    python3 profile_sections.py --break-if "Sparkles>12000"      # pause when one pass takes longer
    python3 profile_sections.py --stop-at Sparkles.inside        # pause at an ordinary breakpoint
    python3 profile_sections.py --attach --sym my.sym --section render=Render:Render.Exit,Render.Exit2
    python3 profile_sections.py --attach --expect "Dispatch=354" --group-by iy --break-on-mismatch

Finds unreal-qt by itself (ports 8090..8099, UNREAL_WEBAPI_PORT, --url) and waits for it when it is not running
yet; --attach asks which instance when several run. When the emulator goes away in the middle, or on Ctrl+C,
the passes measured so far are still reported.

Writes a Markdown report and the raw data as JSON (every pass, every interrupt inside a section):
section-report.md and section-report.json in the current directory, or --out PREFIX.

How it works: the machine stays paused; POST /steps runs it until the next marker breakpoint, GET
/debug/snapshot reads the time (and the registers) there. Two requests per marker, so a marker hit thousands of
times per frame makes it slow: it measures, it does not run in real time.
"""

import argparse
import datetime
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import unreal_tui as tui  # noqa: E402
from unreal_webapi import (ConnectionLost, ControlHeld, ControlRevoked, InstanceGone, WebApiError, attach,  # noqa: E402
                           connect, load_sym, register_value)

HERE = os.path.dirname(os.path.abspath(__file__))


class Section:
    def __init__(self, name, entry, exits):
        self.name = name
        self.entry = entry
        self.exits = exits
        self.expected = None     # --expect: the T-states every pass must take
        self.passes = []         # {frame, t, elapsed, net, interrupts, key, regs}

    def add(self, frame, t, elapsed, net, interrupts, key, regs):
        self.passes.append({"frame": frame, "t": t, "elapsed": elapsed, "net": net, "interrupts": interrupts,
                            "key": key, "regs": regs})

    def mismatches(self):
        return [p for p in self.passes if self.expected is not None and p["net"] != self.expected]

    def stats(self):
        n = len(self.passes)
        if not n:
            return {"section": self.name, "entry": self.entry, "exits": self.exits, "count": 0}
        elapsed = [p["elapsed"] for p in self.passes]
        net = [p["net"] for p in self.passes]
        worst = max(self.passes, key=lambda p: p["elapsed"])
        st = {
            "section": self.name, "entry": self.entry, "exits": self.exits, "count": n,
            "min": min(elapsed), "max": max(elapsed), "avg": sum(elapsed) / n, "total": sum(elapsed),
            "p50": percentile(elapsed, 50), "p95": percentile(elapsed, 95),
            "net_min": min(net), "net_max": max(net), "net_avg": sum(net) / n,
            "max_at_frame": worst["frame"],
            "interrupted_passes": sum(1 for p in self.passes if p["interrupts"]),
            "interrupts": sum(p["interrupts"] for p in self.passes),
        }
        if self.expected is not None:
            st["expected"] = self.expected
            st["mismatches"] = len(self.mismatches())
        return st

    def by_key(self):
        """Passes grouped by the --group-by register's value at the entry: {key: [passes]}."""
        groups = {}
        for p in self.passes:
            groups.setdefault(p["key"], []).append(p)
        return groups


class Open:
    """A pass that has entered and not left yet."""

    def __init__(self, section, start, frame, t, key, regs):
        self.section = section
        self.start = start
        self.frame = frame
        self.t = t
        self.key = key
        self.regs = regs
        self.interrupts = 0
        self.interrupt_t = 0


ACKNOWLEDGE = {0: 13, 1: 13, 2: 19}   # T-states of the INT acknowledge with the PC push (IM 2: + the vector read)


class Interrupt:
    """An accepted interrupt the CPU has not come back from yet."""

    def __init__(self, ret, sp, start, inside, event):
        self.ret = ret           # the interrupted instruction: the CPU comes back there
        self.sp = sp             # SP once the handler has returned (tells a return from a later call there)
        self.start = start       # global T of the boundary where the interrupt was accepted
        self.inside = inside     # the passes open at that moment
        self.event = event


def interrupt_entry(emu, snap, choice, symbols, by_name):
    """Where an accepted maskable interrupt lands. auto: #0038 in IM 1, in IM 2 the vector-table word at
    I * 256 + #FF (the data bus floats to #FF on a Spectrum); a section name (its entry), a label or an address;
    none: interrupts are not followed."""
    if choice == "none":
        return None
    if choice in by_name:
        return by_name[choice].entry
    if choice != "auto":
        return resolve(choice, symbols)
    im = snap["regs"]["interrupt"]["im"]
    if im == 1:
        return 0x0038
    if im == 2:
        vector = register_value(snap["regs"], "i") * 256 + 0xFF
        low, high = bytearray(emu.read_memory(vector, 2))
        return low | high << 8
    print("IM 0: interrupts are not followed (give --interrupt ADDR)")
    return None


def percentile(values, p):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round(p / 100.0 * (len(ordered) - 1))))]


def resolve(text, symbols):
    """A label from the .sym, or a number (0x8000, #8000, $8000, 32768)."""
    if text in symbols:
        return symbols[text]
    t = text.strip()
    for prefix in ("#", "$"):
        if t.startswith(prefix):
            return int(t[1:], 16)
    try:
        return int(t, 0)
    except ValueError:
        raise SystemExit("unknown label or address: {}".format(text))


def parse_sections(specs, symbols):
    """NAME (labels NAME and NAME.End) or NAME=ENTRY:EXIT[,EXIT...]. None: every NAME with a NAME.End."""
    if not specs:
        specs = sorted((n for n in symbols if n + ".End" in symbols), key=lambda n: symbols[n])
    sections = []
    for spec in specs:
        if "=" in spec:
            name, where = spec.split("=", 1)
            entry, _, exits = where.partition(":")
            if not exits:
                raise SystemExit("section {}: give ENTRY:EXIT".format(spec))
            sections.append(Section(name, resolve(entry, symbols), [resolve(e, symbols) for e in exits.split(",")]))
        else:
            sections.append(Section(spec, resolve(spec, symbols), [resolve(spec + ".End", symbols)]))
    return sections


def parse_limits(specs, sep, what):
    limits = {}
    for spec in specs:
        name, found, value = spec.partition(sep)
        if not found:
            raise SystemExit("{} wants NAME{}TSTATES, got {}".format(what, sep, spec))
        limits[name.strip()] = int(value)
    return limits


# --- reports ---

def hexa(value):
    return "#{:04X}".format(value)


def key_name(key, labels):
    """A --group-by value with the label at that address, when there is one: #8123 (Branch3)."""
    if key is None:
        return "-"
    return "{} ({})".format(hexa(key), labels[key]) if key in labels else hexa(key)


def reg_title(name):
    """iy -> IY, af_ -> AF', a_ -> A'"""
    return name.upper().replace("_", "'")


def reg_text(value, name):
    if value is None:
        return "-"
    return "#{:02X}".format(value) if len(name.rstrip("_")) == 1 else "#{:04X}".format(value)


def bar(fraction, width=20):
    """A text bar: 0.5 -> ##########.........."""
    filled = max(0, min(width, int(round(fraction * width))))
    return "`" + "#" * filled + "." * (width - filled) + "`" + (" over" if fraction > 1 else "")


def histogram(values, buckets=10, width=30):
    lo, hi = min(values), max(values)
    if lo == hi:
        return ["{:>7}            {:>5}  `{}`".format(lo, len(values), "#" * width)]
    step = (hi - lo) / float(buckets)
    counts = [0] * buckets
    for v in values:
        counts[min(buckets - 1, int((v - lo) / step))] += 1
    top = max(counts)
    return ["{:>7} .. {:<7} {:>5}  `{}`".format(int(lo + i * step), int(lo + (i + 1) * step), c,
                                                  "#" * int(round(c * width / float(top))))
            for i, c in enumerate(counts)]


def write_markdown(path, run, sections, interrupt_events, frame_t, group_by, labels, show_regs):
    lines = ["# Section timing report", ""]
    lines += ["| | |", "|:--|:--|"]
    for key, value in run.items():
        if key != "stop" and value is not None:
            lines.append("| {} | {} |".format(key.replace("_", " "), value))
    lines.append("")
    if run.get("stop"):
        lines += ["> **Stopped:** {}".format(run["stop"]), ""]

    lines += ["## Summary", "",
              "T-states per pass. *Net* leaves out the interrupts that came in during the pass, each from the "
              "boundary where it was accepted to the return; "
              "*% frame* is the average against one frame of {} T-states.".format(frame_t), "",
              "| Section | Entry | Exit | Passes | Min | Avg | P95 | Max | Net max | % frame | Load | Interrupted |",
              "|:--|:--|:--|--:|--:|--:|--:|--:|--:|--:|:--|--:|"]
    for s in sections:
        st = s.stats()
        exits = ", ".join(hexa(e) for e in s.exits)
        if not st["count"]:
            lines.append("| {} | {} | {} | 0 | | | | | | | | |".format(s.name, hexa(s.entry), exits))
            continue
        lines.append("| {} | {} | {} | {} | {} | {:.1f} | {} | {} | {} | {:.1f}% | {} | {} |".format(
            s.name, hexa(s.entry), exits, st["count"], st["min"], st["avg"], st["p95"], st["max"], st["net_max"],
            100.0 * st["avg"] / frame_t, bar(st["avg"] / frame_t), st["interrupted_passes"]))
    lines.append("")

    over = [(s, p) for s in sections for p in s.passes if p["elapsed"] > frame_t]
    if over:
        lines += ["Passes longer than a frame: " + ", ".join(
            "{} {} of {}".format(s.name, sum(1 for o in over if o[0] is s), len(s.passes)) for s in sections
            if any(o[0] is s for o in over)) + ".", ""]

    checked = [s for s in sections if s.expected is not None and s.passes]
    if checked:
        lines += ["## Expected time", "",
                  "| Section | Expected | Passes | As expected | Different | Seen |", "|:--|--:|--:|--:|--:|:--|"]
        for s in checked:
            seen = sorted({p["net"] for p in s.passes})
            lines.append("| {} | {} | {} | {} | {} | {} |".format(
                s.name, s.expected, len(s.passes), len(s.passes) - len(s.mismatches()), len(s.mismatches()),
                ", ".join(str(v) for v in seen[:8]) + (" ..." if len(seen) > 8 else "")))
        lines.append("")
        for s in checked:
            wrong = s.mismatches()
            if not wrong:
                lines += ["{}: every pass took exactly {} T-states.".format(s.name, s.expected), ""]
                continue
            offs = {}
            for p in wrong:
                offs[p["net"] - s.expected] = offs.get(p["net"] - s.expected, 0) + 1
            lines += ["{}: {} pass(es) did not take {} T-states ({}){}.".format(
                          s.name, len(wrong), s.expected,
                          ", ".join("{:+d}: {}".format(o, n) for o, n in sorted(offs.items())),
                          ", the first 50 below" if len(wrong) > 50 else ""), "",
                      "| Frame | T at entry | {} | T-states | Net | Off by | Interrupts |{}".format(
                          group_by.upper() if group_by else "Key",
                          "".join(" {} |".format(reg_title(r)) for r in show_regs)),
                      "|--:|--:|:--|--:|--:|--:|--:|" + "--:|" * len(show_regs)]
            for p in wrong[:50]:
                lines.append("| {} | {} | {} | {} | {} | {:+d} | {} |{}".format(
                    p["frame"], p["t"], key_name(p["key"], labels), p["elapsed"], p["net"], p["net"] - s.expected,
                    p["interrupts"], "".join(" {} |".format(reg_text(p["regs"].get(r), r)) for r in show_regs)))
            lines.append("")

    if group_by:
        lines += ["## By {} at the entry".format(group_by.upper()), ""]
        for s in sections:
            groups = s.by_key()
            if not groups:
                continue
            lines += ["### {}".format(s.name), "",
                      "| {} | Passes | Net min | Net max | {}".format(group_by.upper(),
                                                            "Different |" if s.expected is not None else "|"),
                      "|:--|--:|--:|--:|" + ("--:|" if s.expected is not None else "")]
            for key in sorted(groups, key=lambda k: (k is None, k)):
                passes = groups[key]
                elapsed = [p["net"] for p in passes]
                row = "| {} | {} | {} | {} |".format(key_name(key, labels), len(passes), min(elapsed), max(elapsed))
                if s.expected is not None:
                    row += " {} |".format(sum(1 for v in elapsed if v != s.expected))
                lines.append(row)
            lines.append("")

    lines += ["## Sections", ""]
    for s in sections:
        st = s.stats()
        if not st["count"]:
            continue
        lines += ["### {}".format(s.name), "",
                  "{} passes, {} .. {} T-states (median {}), the longest in frame {}. {} of them had an "
                  "interrupt inside ({} interrupts).".format(st["count"], st["min"], st["max"], st["p50"],
                                                             st["max_at_frame"], st["interrupted_passes"],
                                                             st["interrupts"]), "",
                  "```text", "    T-states        passes"] + histogram([p["elapsed"] for p in s.passes]) + ["```", ""]
        worst = sorted(s.passes, key=lambda p: -p["elapsed"])[:5]
        lines += ["| Longest passes | Frame | T at entry | T-states | Net | Interrupts |", "|:--|--:|--:|--:|--:|--:|"]
        for i, p in enumerate(worst, 1):
            lines.append("| {} | {} | {} | {} | {} | {} |".format(i, p["frame"], p["t"], p["elapsed"], p["net"],
                                                                p["interrupts"]))
        lines.append("")

    if interrupt_events:
        shown = interrupt_events[:50]
        lines += ["## Interrupts inside sections", "",
                  "{} interrupt(s) came in while a section was open{}.".format(
                      len(interrupt_events), ", the first {} below".format(len(shown)) if len(shown) < len(
                          interrupt_events) else ""), "",
                  "| Frame | T | Interrupted at | Inside | T-states into it | Cost |", "|--:|--:|:--|:--|--:|--:|"]
        for e in shown:
            lines.append("| {} | {} | {} | {} | {} | {} |".format(e["frame"], e["t"], key_name(e["at"], labels),
                                                                e["section"], e["into"], e.get("cost", "-")))
        lines.append("")

    with open(path, "w") as f:
        f.write("\n".join(lines))


# --- the live panel ---

def dashboard(title, done, frames, hits, started, sections, frame_t, events):
    """The lines of the live panel: progress, one row per section, the latest events."""
    elapsed = max(time.time() - started, 0.001)
    lines = [tui.bold(title),
             "frame {:>5} / {:<5} {} {:>3.0f}%   {:>6} marker hits  {:>4.0f}/s   {:>4.0f} s".format(
                 done, frames, tui.meter(done / float(frames or 1)), 100.0 * done / (frames or 1), hits, hits / elapsed,
                 elapsed),
             "",
             tui.dim("  {:<12} {:>6} {:>7} {:>7} {:>9} {:>7} {:>6}  {}".format(
                 "section", "passes", "last", "min", "avg", "max", "%frame", ""))]
    for s in sections:
        n = len(s.passes)
        if not n:
            lines.append("  {:<12} {:>6}".format(s.name[:12], 0))
            continue
        elapsed_list = [p["elapsed"] for p in s.passes]
        avg = sum(elapsed_list) / float(n)
        note = ""
        if s.expected is not None:
            off = sum(1 for p in s.passes if p["net"] != s.expected)
            note = tui.red("{} off {}".format(off, s.expected)) if off else tui.green("all {}".format(s.expected))
        interrupted = sum(1 for p in s.passes if p["interrupts"])
        if interrupted:
            note += ("  " if note else "") + tui.yellow("{} interrupted".format(interrupted))
        lines.append("  {:<12} {:>6} {:>7} {:>7} {:>9.1f} {:>7} {:>5.1f}%  {}".format(
            s.name[:12], n, elapsed_list[-1], min(elapsed_list), avg, max(elapsed_list), 100.0 * avg / frame_t, note))
    lines.append("")
    for event in events[-3:]:
        lines.append(tui.dim("  " + event))
    return lines


# --- the run ---

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--url", help="WebAPI base, e.g. http://localhost:8090/api/v1 (default: look on 8090..8099)")
    ap.add_argument("--model", default="48K", help="model of the new instance (48K, PENTAGON, ...)")
    ap.add_argument("--attach", nargs="?", const="", metavar="ID",
                    help="use a running instance as it is (no snapshot loaded): this id or id prefix, the only one, "
                         "or pick from a list")
    ap.add_argument("--snapshot", default=os.path.join(HERE, "demo.sna"), help="loaded into a new instance")
    ap.add_argument("--sym", default=os.path.join(HERE, "demo.sym"), help="sjasmplus --sym labels")
    ap.add_argument("--section", action="append", metavar="SPEC",
                    help="NAME (labels NAME / NAME.End) or NAME=ENTRY:EXIT[,EXIT]; repeat. Default: every NAME.End")
    ap.add_argument("--interrupt", default="auto", metavar="auto|none|ADDR",
                    help="where an accepted interrupt lands: auto (IM 1 #0038, IM 2 from I and the vector table), "
                         "a section name, a label or an address; none: not followed")
    ap.add_argument("--recursive", action="store_true",
                    help="a section may be entered again before it is left (recursion); by default a second entry "
                         "means the first one was left without its exit, and that pass is dropped")
    ap.add_argument("--frames", type=int, default=100, help="how long to measure")
    ap.add_argument("--warmup", type=int, default=10, help="frames run before measuring (the demo sets up for ~4)")
    ap.add_argument("--break-if", action="append", default=[], metavar="NAME>T",
                    help="pause when one pass of NAME takes more than T t-states")
    ap.add_argument("--expect", action="append", default=[], metavar="NAME=T",
                    help="every pass of NAME must take exactly T t-states, interrupts left out (net): the report "
                         "lists the ones that do not")
    ap.add_argument("--break-on-mismatch", action="store_true", help="pause at the first pass off its --expect")
    ap.add_argument("--group-by", metavar="REG",
                    help="note a register at every entry (iy, hl, a, ...): the report groups the passes by its value")
    ap.add_argument("--show-regs", metavar="R,R",
                    help="registers noted at every entry and shown next to the passes off their --expect (a_,b: "
                         "A' and B)")
    ap.add_argument("--passes", type=int, help="stop after this many passes (all sections together)")
    ap.add_argument("--stop-at", metavar="LABEL", help="pause at an ordinary execution breakpoint")
    ap.add_argument("--log", action="store_true", help="print every interrupt that lands inside a section")
    ap.add_argument("--out", default="section-report", metavar="PREFIX",
                    help="report files: PREFIX.md and PREFIX.json (default section-report)")
    ap.add_argument("--keep", action="store_true", help="keep the new instance after the run")
    ap.add_argument("--no-take-control", action="store_true",
                    help="do not take control (to see the automation gate: the emulator opens a session by itself "
                         "at the first change and says so)")
    args = ap.parse_args()

    symbols = load_sym(args.sym) if args.sym and os.path.exists(args.sym) else {}
    labels = {}
    for name, address in sorted(symbols.items(), key=lambda kv: kv[0].count(".")):
        labels.setdefault(address, name)                   # an address shows its outermost label
    sections = parse_sections(args.section, symbols)
    if not sections:
        raise SystemExit("no sections: give --section, or a .sym with NAME / NAME.End labels")
    by_name = {s.name: s for s in sections}
    thresholds = parse_limits(args.break_if, ">", "--break-if")
    for name, value in parse_limits(args.expect, "=", "--expect").items():
        if name not in by_name:
            raise SystemExit("--expect: no section {}".format(name))
        by_name[name].expected = value
    for name in thresholds:
        if name not in by_name:
            raise SystemExit("--break-if: no section {}".format(name))
    show_regs = [r.strip().lower() for r in args.show_regs.split(",")] if args.show_regs else []
    stop_at = resolve(args.stop_at, symbols) if args.stop_at else None

    created = args.attach is None
    emu = connect(args.url).create(args.model) if created else attach(args.attach or None, args.url)
    print("{} {} on {}".format("created" if created else "attached to", emu.id[:8], emu.server.describe()))
    # An automation session: the emulator window shows who drives it and locks its controls until we release it;
    # the user can take control back from its toolbar (then we stop)
    try:
        if not args.no_take_control:
            emu.take_control("profile_sections")
    except ControlHeld as e:
        if created:
            emu.remove()
        raise SystemExit("cannot take control of the emulator: {}".format(e))

    paused_for_user = False
    lost = None
    revoked = False
    interrupted = False
    exit_code = 0
    breakpoints = []
    returns = {}             # interrupted address -> [breakpoint id, uses]: where the CPU comes back
    try:
        emu.mute()
        emu.pause()
        if created:
            emu.load_snapshot(args.snapshot)
            if os.path.exists(args.sym):
                emu.import_symbols(args.sym)
            emu.post("/run_frames", {"count": args.warmup})   # let the program set itself up
        emu.enable_breakpoints()

        # address -> what happens there; exits first, so that an address that ends one section and
        # starts the next closes the old pass before it opens the new one
        events = {}
        for s in sections:
            for e in s.exits:
                events.setdefault(e, []).append(("exit", s))
        for s in sections:
            events.setdefault(s.entry, []).append(("entry", s))
        if stop_at is not None:
            events.setdefault(stop_at, []).append(("stop", None))
        landing = interrupt_entry(emu, emu.snapshot(), args.interrupt, symbols, by_name)
        if landing is not None:
            events.setdefault(landing, [])
        for address in events:
            breakpoints.append(emu.add_breakpoint(address, note="profile_sections", group="profile_sections"))
        pending = []             # accepted interrupts not returned from yet, innermost last
        print("interrupts: {}".format("land at " + key_name(landing, labels) if landing is not None
                                      else "not followed"))

        if emu.implicit_session:
            print("the emulator opened a session for us by itself (the automation gate): {}".format(
                emu.implicit_advice))
        frame, t, frame_t = emu.time()
        start_frame = frame
        end_frame = frame + args.frames
        stack = []
        entered = set()
        interrupt_events = []
        orphan_exits = 0
        dropped = 0
        hits = 0
        stop_reason = None

        title = "{} on {}, instance {}: {}".format(emu.model or "?", emu.server.where, emu.id[:8],
                                                   ", ".join(s.name for s in sections))
        panel = tui.Panel()
        recent = []          # the latest events, shown under the table
        started = time.time()

        def event(text):
            recent.append(text)
            if args.log:
                panel.print("  " + text)

        try:
            while stop_reason is None:
                reply = emu.steps(100000)
                snap = emu.snapshot()
                frame, t, frame_t = snap["time"]["frame"], snap["time"]["t"], snap["time"]["frame_t"]
                now = frame * frame_t + t
                if frame >= end_frame:
                    break
                stop = reply.get("stop", {})
                if stop.get("reason") != "breakpoint":
                    continue
                hits += 1
                panel.draw(dashboard(title, frame - start_frame, args.frames, hits, started, sections, frame_t,
                                     recent))
                pc = stop.get("address")

                # back from an interrupt: its whole cost goes to the passes that were open when it came
                while pending and pending[-1].ret == pc and pending[-1].sp == register_value(snap["regs"], "sp"):
                    p = pending.pop()
                    cost = now - p.start
                    for o in p.inside:
                        o.interrupt_t += cost
                    p.event["cost"] = cost
                    returns[pc][1] -= 1
                    if not returns[pc][1]:
                        emu.remove_breakpoint(returns.pop(pc)[0])

                # an accepted interrupt lands here: it was accepted one acknowledge earlier, at the return address
                if pc == landing:
                    sp = register_value(snap["regs"], "sp")
                    low, high = bytearray(emu.read_memory(sp, 2))
                    ret = low | high << 8
                    boundary = now - ACKNOWLEDGE[snap["regs"]["interrupt"]["im"]]
                    ev = {"frame": frame, "t": t, "at": ret,
                          "section": ", ".join(o.section.name for o in stack),
                          "into": boundary - stack[-1].start if stack else 0}
                    for o in stack:
                        o.interrupts += 1
                    if stack:
                        interrupt_events.append(ev)
                        event("frame {:>5} t {:>5}: interrupt at {} inside {} (+{} t-states into it)".format(
                            frame, t, key_name(ret, labels), ev["section"], ev["into"]))
                    pending.append(Interrupt(ret, sp + 2, boundary, list(stack), ev))
                    if ret in returns:
                        returns[ret][1] += 1
                    else:
                        returns[ret] = [emu.add_breakpoint(ret, note="profile_sections", group="profile_sections"), 1]

                for kind, s in events.get(pc, []):
                    if kind == "stop":
                        stop_reason = "stopped at {} (frame {}, t {})".format(args.stop_at, frame, t)
                    elif kind == "entry":
                        # entered again while still open: the open pass left without its exit (a JR elsewhere),
                        # so it and whatever opened inside it are dropped, unless the code really recurses
                        if not args.recursive:
                            depth = next((i for i, o in enumerate(stack) if o.section is s), None)
                            if depth is not None:
                                dropped += len(stack) - depth
                                del stack[depth:]
                        key = register_value(snap["regs"], args.group_by) if args.group_by else None
                        noted = {r: register_value(snap["regs"], r) for r in show_regs}
                        stack.append(Open(s, now, frame, t, key, noted))
                        entered.add(s)
                    else:
                        depth = next((i for i in range(len(stack) - 1, -1, -1) if stack[i].section is s), None)
                        if depth is None:
                            if s in entered:
                                orphan_exits += 1
                            # else: the measurement started inside this section, not an error
                            continue
                        dropped += len(stack) - 1 - depth     # entered and never left: the coder's business
                        o = stack[depth]
                        del stack[depth:]
                        elapsed = now - o.start
                        s.add(o.frame, o.t, elapsed, elapsed - o.interrupt_t, o.interrupts, o.key, o.regs)
                        if args.passes and sum(len(x.passes) for x in sections) >= args.passes:
                            end_frame = frame
                        where = "frame {}{}{}".format(o.frame, ", {} = {}".format(
                            args.group_by.upper(), key_name(o.key, labels)) if args.group_by else "",
                            "".join(", {} = {}".format(reg_title(r), reg_text(v, r)) for r, v in o.regs.items()))
                        net = elapsed - o.interrupt_t
                        if s.expected is not None and net != s.expected:
                            event("frame {:>5}: {} took {} net ({:+d}; {})".format(
                                o.frame, s.name, net, net - s.expected, where))
                        limit = thresholds.get(s.name)
                        if limit is not None and elapsed > limit:
                            stop_reason = "{} took {} t-states > {} ({}, {} interrupt(s) inside, {} net)".format(
                                s.name, elapsed, limit, where, o.interrupts, elapsed - o.interrupt_t)
                        if args.break_on_mismatch and s.expected is not None and net != s.expected:
                            stop_reason = "{} took {} t-states, {} net, expected {} ({:+d}; {})".format(
                                s.name, elapsed, net, s.expected, net - s.expected, where)
        except (ConnectionLost, InstanceGone) as e:
            lost = str(e)
            stop_reason = "lost the emulator at frame {}: {}".format(frame - start_frame, e)
        except ControlRevoked:
            revoked = True
            emu.session = None           # its calls are refused from now on; the clean-up goes without it
            stop_reason = "the user took control back at frame {}".format(frame - start_frame)
        except KeyboardInterrupt:
            interrupted = True
            stop_reason = "interrupted (Ctrl+C) at frame {}".format(frame - start_frame)
        panel.draw(dashboard(title, frame - start_frame, args.frames, hits, started, sections, frame_t, recent),
                   force=True)
        panel.close()

        print()
        print("{:<10} {:>6} {:>7} {:>9} {:>7} {:>6}   {:>7} {:>7}   {}".format(
            "section", "passes", "min", "avg", "max", "%frame", "net min", "net max", "interrupted"))
        for s in sections:
            st = s.stats()
            if not st["count"]:
                print("{:<10} {:>6}".format(s.name, 0))
                continue
            print("{:<10} {:>6} {:>7} {:>9.1f} {:>7} {:>5.1f}%   {:>7} {:>7}   {} of {} ({} interrupts){}".format(
                s.name, st["count"], st["min"], st["avg"], st["max"], 100.0 * st["avg"] / frame_t, st["net_min"],
                st["net_max"], st["interrupted_passes"], st["count"], st["interrupts"],
                "   expected {}: {} off".format(s.expected, st["mismatches"]) if s.expected is not None else ""))
        dropped += len(stack)    # still open at the end
        print("{} marker hits; {} exits without an entry, {} entries never left".format(hits, orphan_exits, dropped))

        run = {
            "date": datetime.datetime.now().strftime("%Y-%m-%d %H:%M"),
            "model": emu.model,
            "instance": emu.id,
            "emulator": emu.server.describe(),
            "program": os.path.basename(args.snapshot) if created else None,
            "frame_t": frame_t,
            "interrupt_entry": key_name(landing, labels) if landing is not None else "not followed",
            "frames": "{} (frames {} .. {})".format(frame - start_frame, start_frame, frame),
            "marker_hits": hits,
            "exits_without_entry": orphan_exits,
            "entries_never_left": dropped,
            "group_by": args.group_by,
            "stop": stop_reason,
        }
        data = dict(run, frames=frame - start_frame, first_frame=start_frame,
                    sections=[dict(s.stats(), passes=s.passes) for s in sections],
                    interrupts_inside=interrupt_events)
        with open(args.out + ".json", "w") as f:
            json.dump(data, f, indent=1)
        write_markdown(args.out + ".md", run, sections, interrupt_events, frame_t, args.group_by, labels, show_regs)
        print("report: {0}.md, data: {0}.json".format(args.out))
        if emu.implicit_session and not emu.session:
            print("the emulator's implicit session ends 15 s after our last request: the window stays locked until "
                  "then (take control explicitly to release it at the end)")

        if lost:
            print()
            print("LOST: " + lost)
            print("  the report holds the passes measured before that")
            exit_code = 2
        elif revoked:
            print()
            print("TAKEN BACK: the user took control back in the emulator window; the report holds the passes so far")
            print("  (not retrying and not taking control again: run the script again when the user is done)")
            exit_code = 3
        elif interrupted:
            print()
            print("INTERRUPTED: the report holds the passes measured until Ctrl+C")
            exit_code = 130
        elif stop_reason:
            paused_for_user = True
            emu.release_control()        # the user looks at the paused machine in the debugger: the window is theirs
            regs = emu.registers()
            print()
            print("PAUSED: " + stop_reason)
            print("  registers: " + json.dumps(regs.get("main", regs)) + " ix={:04X} iy={:04X} pc={:04X}".format(
                regs.get("index", {}).get("ix", 0), regs.get("index", {}).get("iy", 0),
                regs.get("special", {}).get("pc", 0)))
            print("  instance {} stays paused: look at it in the debugger, then resume".format(emu.id[:8]))
    except (ConnectionLost, InstanceGone) as e:
        lost = str(e)
        raise SystemExit("lost the emulator before measuring: {}".format(e))
    except ControlRevoked:
        revoked = True
        emu.session = None
        raise SystemExit("the user took control back in the emulator window before measuring: stopping")
    finally:
        if not lost:
            try:
                for b in breakpoints + [r[0] for r in returns.values()]:
                    emu.remove_breakpoint(b)
                if created and not args.keep and not paused_for_user:
                    emu.remove()
                elif not paused_for_user and not created and not revoked:
                    emu.resume()       # an attached machine runs on; after a take-back it is the user's
            except WebApiError:
                pass
            emu.release_control()
    return exit_code


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        tui.end_progress()
        raise SystemExit("cancelled")
    except WebApiError as e:
        tui.end_progress()
        raise SystemExit(str(e))
