#!/usr/bin/env python3
"""T-state statistics between entry and exit markers, through the unreal-ng WebAPI.

A section is a piece of code with an entry address and one or more exit addresses. Every entry pushes the
global T-state count (frame * frame length + T in the frame, so it runs on across frames) on a stack, every
exit pops it: count, min, max and sum per section. An interrupt section (the frame interrupt's handler)
taken while other sections are open is counted in them: how many interrupts came in and how many T-states
they took ("net" = without them).

    python3 profile_sections.py                                  # demo.sna on a new 48K, 100 frames
    python3 profile_sections.py --model PENTAGON --frames 200
    python3 profile_sections.py --break-if "Sparkles>12000"      # pause when one pass takes longer
    python3 profile_sections.py --stop-at Sparkles.inside        # pause at an ordinary breakpoint
    python3 profile_sections.py --attach --sym my.sym --section render=Render:Render.Exit,Render.Exit2

Writes a Markdown report and the raw data as JSON (every pass, every interrupt inside a section):
section-report.md and section-report.json in the current directory, or --out PREFIX.

How it works: the machine stays paused; POST /steps runs it until the next marker breakpoint, GET
/debug/snapshot reads the time there. Two requests per marker, so a marker hit thousands of times per frame
makes it slow: it measures, it does not run in real time.
"""

import argparse
import datetime
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from unreal_webapi import Unreal, WebApiError, load_sym  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


class Section:
    def __init__(self, name, entry, exits):
        self.name = name
        self.entry = entry
        self.exits = exits
        self.passes = []         # {frame, t, elapsed, net, interrupts}

    def add(self, frame, t, elapsed, net, interrupts):
        self.passes.append({"frame": frame, "t": t, "elapsed": elapsed, "net": net, "interrupts": interrupts})

    def stats(self):
        n = len(self.passes)
        if not n:
            return {"section": self.name, "entry": self.entry, "exits": self.exits, "count": 0}
        elapsed = [p["elapsed"] for p in self.passes]
        net = [p["net"] for p in self.passes]
        worst = max(self.passes, key=lambda p: p["elapsed"])
        return {
            "section": self.name, "entry": self.entry, "exits": self.exits, "count": n,
            "min": min(elapsed), "max": max(elapsed), "avg": sum(elapsed) / n, "total": sum(elapsed),
            "p50": percentile(elapsed, 50), "p95": percentile(elapsed, 95),
            "net_min": min(net), "net_max": max(net), "net_avg": sum(net) / n,
            "max_at_frame": worst["frame"],
            "interrupted_passes": sum(1 for p in self.passes if p["interrupts"]),
            "interrupts": sum(p["interrupts"] for p in self.passes),
        }


class Open:
    """A pass that has entered and not left yet."""

    def __init__(self, section, start, frame, t):
        self.section = section
        self.start = start
        self.frame = frame
        self.t = t
        self.interrupts = 0
        self.interrupt_t = 0


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


def parse_thresholds(specs):
    thresholds = {}
    for spec in specs:
        name, sep, value = spec.partition(">")
        if not sep:
            raise SystemExit("--break-if wants NAME>TSTATES, got {}".format(spec))
        thresholds[name.strip()] = int(value)
    return thresholds


# --- reports ---

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


def write_markdown(path, run, sections, interrupt_events, frame_t):
    hexa = lambda a: "#{:04X}".format(a)   # noqa: E731
    lines = ["# Section timing report", ""]
    lines += ["| | |", "|:--|:--|"]
    for key, value in run.items():
        if key != "stop" and value is not None:
            lines.append("| {} | {} |".format(key.replace("_", " "), value))
    lines.append("")
    if run.get("stop"):
        lines += ["> **Paused:** {}".format(run["stop"]), ""]

    lines += ["## Summary", "",
              "T-states per pass. *Net* leaves out the interrupts that came in during the pass; "
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
                  "| Frame | T | Inside | T-states into it |", "|--:|--:|:--|--:|"]
        for e in shown:
            lines.append("| {} | {} | {} | {} |".format(e["frame"], e["t"], e["section"], e["into"]))
        lines.append("")

    with open(path, "w") as f:
        f.write("\n".join(lines))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--url", help="WebAPI base (default http://localhost:8090/api/v1, or UNREAL_WEBAPI_URL / _PORT)")
    ap.add_argument("--model", default="48K", help="model of the new instance (48K, PENTAGON, ...)")
    ap.add_argument("--attach", nargs="?", const="", metavar="ID",
                    help="use a running instance (the only one, or this id) as it is: no snapshot loaded")
    ap.add_argument("--snapshot", default=os.path.join(HERE, "demo.sna"), help="loaded into a new instance")
    ap.add_argument("--sym", default=os.path.join(HERE, "demo.sym"), help="sjasmplus --sym labels")
    ap.add_argument("--section", action="append", metavar="SPEC",
                    help="NAME (labels NAME / NAME.End) or NAME=ENTRY:EXIT[,EXIT]; repeat. Default: every NAME.End")
    ap.add_argument("--interrupt", default="Isr", help="the section that is the frame interrupt (default Isr)")
    ap.add_argument("--frames", type=int, default=100, help="how long to measure")
    ap.add_argument("--warmup", type=int, default=10, help="frames run before measuring (the demo sets up for ~4)")
    ap.add_argument("--break-if", action="append", default=[], metavar="NAME>T",
                    help="pause when one pass of NAME takes more than T t-states")
    ap.add_argument("--stop-at", metavar="LABEL", help="pause at an ordinary execution breakpoint")
    ap.add_argument("--log", action="store_true", help="print every interrupt that lands inside a section")
    ap.add_argument("--out", default="section-report", metavar="PREFIX",
                    help="report files: PREFIX.md and PREFIX.json (default section-report)")
    ap.add_argument("--keep", action="store_true", help="keep the new instance after the run")
    args = ap.parse_args()

    symbols = load_sym(args.sym) if args.sym and os.path.exists(args.sym) else {}
    sections = parse_sections(args.section, symbols)
    if not sections:
        raise SystemExit("no sections: give --section, or a .sym with NAME / NAME.End labels")
    by_name = {s.name: s for s in sections}
    thresholds = parse_thresholds(args.break_if)
    for name in thresholds:
        if name not in by_name:
            raise SystemExit("--break-if: no section {}".format(name))
    interrupt = by_name.get(args.interrupt)
    stop_at = resolve(args.stop_at, symbols) if args.stop_at else None

    server = Unreal(args.url) if args.url else Unreal()
    created = args.attach is None
    emu = server.create(args.model) if created else server.attach(args.attach or None)
    paused_for_user = False
    breakpoints = []
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
        for address in events:
            breakpoints.append(emu.add_breakpoint(address, note="profile_sections", group="profile_sections"))

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

        print("{} frames of {} t-states on {}, sections: {}".format(
            args.frames, frame_t, args.model if created else "instance " + emu.id,
            ", ".join(s.name for s in sections)))

        while stop_reason is None:
            reply = emu.steps(100000)
            frame, t, frame_t = emu.time()
            now = frame * frame_t + t
            if frame >= end_frame:
                break
            stop = reply.get("stop", {})
            if stop.get("reason") != "breakpoint":
                continue
            hits += 1
            for kind, s in events.get(stop.get("address"), []):
                if kind == "stop":
                    stop_reason = "stopped at {} (frame {}, t {})".format(args.stop_at, frame, t)
                elif kind == "entry":
                    if s is interrupt:
                        for o in stack:
                            o.interrupts += 1
                            interrupt_events.append({"frame": frame, "t": t, "section": o.section.name,
                                                     "into": now - o.start})
                            if args.log:
                                print("  frame {:>5} t {:>5}: interrupt inside {} (+{} t-states into it)".format(
                                    frame, t, o.section.name, now - o.start))
                    stack.append(Open(s, now, frame, t))
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
                    s.add(o.frame, o.t, elapsed, elapsed - o.interrupt_t, o.interrupts)
                    if s is interrupt:
                        for outer in stack:
                            outer.interrupt_t += elapsed
                    limit = thresholds.get(s.name)
                    if limit is not None and elapsed > limit:
                        stop_reason = "{} took {} t-states > {} (frame {}, {} interrupt(s) inside, {} net)".format(
                            s.name, elapsed, limit, o.frame, o.interrupts, elapsed - o.interrupt_t)

        print()
        print("{:<10} {:>6} {:>7} {:>9} {:>7} {:>6}   {:>7} {:>7}   {}".format(
            "section", "passes", "min", "avg", "max", "%frame", "net min", "net max", "interrupted"))
        for s in sections:
            st = s.stats()
            if not st["count"]:
                print("{:<10} {:>6}".format(s.name, 0))
                continue
            print("{:<10} {:>6} {:>7} {:>9.1f} {:>7} {:>5.1f}%   {:>7} {:>7}   {} of {} ({} interrupts)".format(
                s.name, st["count"], st["min"], st["avg"], st["max"], 100.0 * st["avg"] / frame_t, st["net_min"],
                st["net_max"], st["interrupted_passes"], st["count"], st["interrupts"]))
        print("{} marker hits; {} exits without an entry, {} entries never left".format(hits, orphan_exits, dropped))

        run = {
            "date": datetime.datetime.now().strftime("%Y-%m-%d %H:%M"),
            "model": args.model if created else None,
            "instance": None if created else emu.id,
            "program": os.path.basename(args.snapshot) if created else None,
            "frame_t": frame_t,
            "frames": "{} (frames {} .. {})".format(frame - start_frame, start_frame, frame),
            "marker_hits": hits,
            "exits_without_entry": orphan_exits,
            "entries_never_left": dropped,
            "stop": stop_reason,
        }
        data = dict(run, frames=frame - start_frame, first_frame=start_frame,
                    sections=[dict(s.stats(), passes=s.passes) for s in sections],
                    interrupts_inside=interrupt_events)
        with open(args.out + ".json", "w") as f:
            json.dump(data, f, indent=1)
        write_markdown(args.out + ".md", run, sections, interrupt_events, frame_t)
        print("report: {0}.md, data: {0}.json".format(args.out))

        if stop_reason:
            paused_for_user = True
            regs = emu.registers()
            print()
            print("PAUSED: " + stop_reason)
            print("  registers: " + json.dumps(regs.get("main", regs)) + " pc={:04X}".format(
                regs.get("special", {}).get("pc", 0)))
            print("  instance {} stays paused: look at it in the debugger, then POST /resume".format(emu.id))
    finally:
        for b in breakpoints:
            try:
                emu.remove_breakpoint(b)
            except WebApiError:
                pass
        if created and not args.keep and not paused_for_user:
            emu.remove()
        elif not paused_for_user and not created:
            emu.resume()


if __name__ == "__main__":
    try:
        main()
    except WebApiError as e:
        raise SystemExit(str(e))
