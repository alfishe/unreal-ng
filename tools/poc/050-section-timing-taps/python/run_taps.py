#!/usr/bin/env python3
"""POC 050: section timing on exec taps against the reference script.

One run = a new instance of MODEL, the demo loaded with its labels, WARMUP frames run paused, then one variant:

  ref       data/examples/01-section-timing/profile_sections.py --attach (POST /steps + GET /debug/snapshot)
  sync      exec taps + sections.lua chunks in sync mode (hook state, emulation thread)
  frame     the same chunks in frame mode (the frame's hits at the frame end)
  deferred  the same code as a function in the script interpreter (the hits through the ring)
  count     the same taps as count-only breakpoints (stop: false): the tap matching alone
  native    the native analyzer (section-timer): the same markers, the CPU's int_accept / int_return for the
            interrupts (no handler exits), started through Lua section_timer_start
  none      no taps: the run's speed without them

The tap variants run FRAMES frames with POST /run_frames (full host speed) and read the passes back through
shared_set. Every run writes OUT/<variant>-<model>.json: the passes (section, frame, t, elapsed, net, interrupts),
the statistics, the wall time of the measured run.
"""

import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
EXAMPLE = os.path.join(ROOT, "data", "examples", "01-section-timing")
sys.path.insert(0, os.path.join(ROOT, "data", "examples", "lib"))

import unreal_webapi as uw  # noqa: E402

SECTIONS = ["Frame", "Plasma", "Sparkles", "Music", "Isr"]
ACK = {0: 13, 1: 13, 2: 19}
RETURN_LEN = {0xED: 14, 0xC9: 10}   # RETI / RETN (ED 4D / ED 45), RET


def lua(server, code):
    reply = server.request("POST", "/lua/exec", {"code": code})
    if not reply.get("success", True) or reply.get("error"):
        raise RuntimeError("lua: {}".format(reply))
    return reply.get("output", "")


def prepare(server, model, warmup):
    emu = server.create(model)
    emu.mute()
    emu.pause()
    emu.load_snapshot(os.path.join(EXAMPLE, "demo.sna"))
    emu.import_symbols(os.path.join(EXAMPLE, "demo.sym"))
    emu.post("/run_frames", {"count": warmup})
    return emu


def markers(symbols, extra):
    """address -> list of (kind, name): exits first, so an address that ends one section and opens the next
    closes the old pass first"""
    events = {}
    for s in SECTIONS:
        events.setdefault(symbols[s + ".End"], []).append(("exit", s))
    for s in SECTIONS:
        events.setdefault(symbols[s], []).append(("entry", s))
    for name in extra:
        events.setdefault(symbols[name], [])
    return events


def interrupt_points(emu, symbols):
    """The landing of an IM 2 interrupt (the vector-table word at I * 256 + #FF) and the handler's exits"""
    snap = emu.snapshot()
    im = snap["regs"]["interrupt"]["im"]
    i = uw.register_value(snap["regs"], "i")
    low, high = bytearray(emu.read_memory(i * 256 + 0xFF, 2))
    landing = low | high << 8
    exit_at = symbols["Isr.End"]
    opcode = bytearray(emu.read_memory(exit_at, 1))[0]
    return im, landing, {exit_at: RETURN_LEN[opcode]}


def chunk(template, ft, ack, events, ordered):
    body = []
    for kind, arg in events:
        if kind == "entry":
            body.append('entry("{}", hit)'.format(arg))
        elif kind == "exit":
            body.append('exit("{}", hit)'.format(arg))
        elif kind == "landing":
            body.append("landing(hit)")
        elif kind == "handler_exit":
            body.append("handler_exit(hit, {})".format(arg))
    return template.replace("@FT@", str(ft)).replace("@ACK@", str(ack)).replace("@EVENTS@", "\n    ".join(body) or "return").replace(
        "@ORDERED@", "true" if ordered else "false")


DUMP = r"""
if ST.flush then ST.flush() end
local p, out = ST.passes, {}
for i = 1, #p, 6 do
  out[#out + 1] = string.format('["%s",%d,%d,%d,%d,%d]', p[i], p[i+1], p[i+2], p[i+3], p[i+4], p[i+5])
end
shared_set("poc050", '{"passes":[' .. table.concat(out, ",") .. '],"hits":' .. ST.hits ..
           ',"orphan":' .. ST.orphan .. ',"dropped":' .. ST.dropped .. '}')
"""


def run_taps(server, emu, variant, frames, sym, extra):
    symbols = sym
    ft = emu.time()[2]
    im, landing, exits = interrupt_points(emu, symbols)
    events = markers(symbols, extra)
    events.setdefault(landing, []).append(("landing", None))
    for address, length in exits.items():
        events.setdefault(address, []).append(("handler_exit", length))
    template = open(os.path.join(HERE, "sections.lua")).read()

    lua(server, "for _, c in ipairs(callbacks()) do callback_remove(c.id) end")
    ids = []
    if variant == "count":
        emu.enable_breakpoints()
        for address in events:
            ids.append(emu.add_breakpoint(address, stop=False, note="poc050", group="poc050"))
    elif variant in ("sync-empty", "sync-grow", "deferred-empty"):
        for address in events:
            if variant == "sync-empty":
                code = "local id, err = on_tap({{exec = {}}}, 'return function(hit) end', {{mode = 'sync', " \
                       "policy = 'ignore', label = 'poc050'}})\nprint(id, err)".format(address)
            elif variant == "sync-grow":
                # the hook keeps a growing state, as a timer keeping its passes does: one small table per call
                code = "local id, err = on_tap({{exec = {}}}, 'G = G or {{}}; return function(hit) " \
                       "G[#G + 1] = {{hit.t, hit.frame}} end', {{mode = 'sync', policy = 'ignore', " \
                       "label = 'poc050'}})\nprint(id, err)".format(address)
            else:
                code = "local id, err = on_tap({{exec = {}}}, function(hit) end, {{ring = 4096, label = 'poc050'}})" \
                       "\nprint(id, err)".format(address)
            out = lua(server, code).split()
            if not out or out[0] == "-1":
                raise RuntimeError("on_tap {:#06x}: {}".format(address, out))
            ids.append(int(out[0]))
    elif variant in ("sync", "frame", "deferred"):
        for address, evs in events.items():
            source = chunk(template, ft, ACK[im], evs, variant == "sync")
            if variant == "deferred":
                code = "ST = nil\n" if not ids else ""
                code += "local id, err = on_tap({{exec = {}}}, load([==[{}]==])(), {{ring = 4096, label = 'poc050'}})\n" \
                        "print(id, err)".format(address, source)
            else:
                code = "local id, err = on_tap({{exec = {}}}, [==[{}]==], {{mode = '{}', instructions = 1000000, " \
                       "budget_us = 100000, policy = 'ignore', label = 'poc050'}})\nprint(id, err)".format(
                           address, source, variant)
            out = lua(server, code).split()
            if not out or out[0] == "-1":
                raise RuntimeError("on_tap {:#06x}: {}".format(address, out))
            ids.append(int(out[0]))

    if variant == "native":
        spec = ", ".join("'{}={}:{}'".format(s, symbols[s], symbols[s + ".End"]) for s in SECTIONS)
        extra_list = ", ".join(str(symbols[n]) for n in extra)
        reply = lua(server, "local r, err = section_timer_start{{frame_t = {}, sections = {{{}}}, extra = {{{}}}}}\n"
                            "print(r ~= nil, err)".format(ft, spec, extra_list)).split()
        if not reply or reply[0] != "true":
            raise RuntimeError("section_timer_start refused: {}".format(reply))

    started = time.perf_counter()
    emu.post("/run_frames", {"count": frames})
    if variant.startswith("deferred"):
        lua(server, "callbacks_run()")
    wall = time.perf_counter() - started

    result = {"variant": variant, "frames": frames, "wall_s": wall, "taps": len(events)}
    if variant in ("sync", "frame", "deferred"):
        if variant == "deferred":
            lua(server, DUMP)
        else:
            # The dump runs in the hook state: one more chunk, at the next marker hit
            lua(server, "local id = on_tap({{exec = {}}}, [==[{}]==], {{mode = 'sync', instructions = 100000000, "
                        "budget_us = 1000000, policy = 'ignore'}})\nprint(id)".format(
                            symbols["MainLoop"], "return function(hit)\n" + DUMP + "\nend"))
            emu.post("/run_frames", {"count": 2})
        data = json.loads(lua(server, 'print(shared_get("poc050"))'))
        result.update(data)
        result["callbacks"] = json.loads(json.dumps(server.request("GET", "/emulator/{}/callbacks".format(emu.id))))
        lua(server, "for _, c in ipairs(callbacks()) do callback_remove(c.id) end")
    if variant == "native":
        result.update(json.loads(lua(server, "print(section_timer_json())")))
        lua(server, "section_timer_stop()")
    if variant.endswith("-empty"):
        result["callbacks"] = server.request("GET", "/emulator/{}/callbacks".format(emu.id))
        result["hits"] = sum(c.get("hits", 0) for c in result["callbacks"].get("callbacks", []))
        lua(server, "for _, c in ipairs(callbacks()) do callback_remove(c.id) end")
    if variant == "count":
        result["breakpoints"] = emu.get("/breakpoints")
        for b in ids:
            emu.remove_breakpoint(b)
    return result


def run_ref(server, emu, frames, out):
    prefix = os.path.join(out, "ref-raw")
    cmd = [sys.executable, os.path.join(EXAMPLE, "profile_sections.py"), "--url", server.base,
           "--attach", emu.id, "--frames", str(frames), "--out", prefix]
    for s in SECTIONS:
        cmd += ["--section", s]
    started = time.perf_counter()
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
    wall = time.perf_counter() - started
    raw = json.load(open(prefix + ".json"))
    passes = []
    for sec in raw["sections"]:
        for p in sec.get("passes", []):
            passes.append([sec["section"], p["frame"], p["t"], p["elapsed"], p["net"], p["interrupts"]])
    passes.sort(key=lambda p: (p[1], p[2]))
    return {"variant": "ref", "frames": frames, "wall_s": wall, "passes": passes, "hits": raw.get("marker_hits"), "orphan": raw.get("exits_without_entry"),
            "dropped": raw.get("entries_never_left")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:28700/api/v1")
    ap.add_argument("--model", default="48K")
    ap.add_argument("--frames", type=int, default=200)
    ap.add_argument("--warmup", type=int, default=50)
    ap.add_argument("--variant", action="append", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--extra", action="append", default=[], help="a label tapped with no event (a dense marker)")
    ap.add_argument("--tag", default="", help="added to the result file's name")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    server = uw.Unreal(args.url, timeout=600)
    sym = {k: v for k, v in uw.load_sym(os.path.join(EXAMPLE, "demo.sym")).items()}
    for variant in args.variant:
        emu = prepare(server, args.model, args.warmup)
        try:
            if variant == "ref":
                result = run_ref(server, emu, args.frames, args.out)
            else:
                result = run_taps(server, emu, variant, args.frames, sym, args.extra)
        finally:
            emu.remove()
        result["model"] = args.model
        path = os.path.join(args.out, "{}-{}{}.json".format(variant, args.model, args.tag))
        json.dump(result, open(path, "w"), indent=1)
        print("{:9} {:8} {:7.3f} s  passes {}".format(variant, args.model, result["wall_s"],
                                                       len(result.get("passes", []))))


if __name__ == "__main__":
    main()
