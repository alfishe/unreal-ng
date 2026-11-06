# TTD explain frame: what one frame did, in one call

**Status 2026-11-02:** done on all six planes and accepted (one call instead of 59-71, see [plan.md](plan.md#measurements)). Plan row [#114](../PLAN.md). The third item of the agent UX roadmap, after the cause chain
([2026-11-02-ttd-cause-chain](../2026-11-02-ttd-cause-chain/README.md)) and find event
([2026-11-02-ttd-find-event](../2026-11-02-ttd-find-event/README.md)). Accepted only if WebAPI / MCP measurements prove
it (owner 2026-11-01).

Files:

| File | What |
|------|------|
| [design.md](design.md) | the sections of the answer, how each is built from one replay, the reply, costs, the planes |
| [plan.md](plan.md) | steps, tests, benchmarks (the one call against the old loop), acceptance |

## The problem

`find_event` names the frame where something happened ("the lives counter went down in frame 1204"). The next question
is always "what did the game do in that frame?": which routines ran and for how long, what it wrote, which ports it
touched, whether it switched pages, what the player pressed. Today the agent assembles that from several tools, each
answering a slice:

- **Memory:** `memory_diff` lists the byte ranges that differ between two checkpoints, without the writers. Each range
  then needs a `find_last` and a disassembly.
- **Ports:** `port_events in` and `port_events out` with a frame window, each a list of records the agent groups.
- **Input:** the timeline counts key presses per bucket; the keys themselves are not listed for a frame.
- **Code:** nothing lists what ran in a frame. `coverage_probe` answers yes or no for an address range. The agent seeks
  to the frame's start and steps instruction by instruction (a frame is 15,000-70,000 instructions) or guesses
  addresses and probes them.
- **Time:** where the frame's 70,000 T went (the interrupt routine, the main loop, waiting in `HALT`) is not answered
  at all.

The result is many calls, large replies (`memory_diff` and `port_events` return records, not a summary) and a code
picture that is a guess.

## The goal

One call: a frame (the current one by default). The answer is a compact report: the frame's time split (interrupt
routine, main code, `HALT` wait), the routines that ran by time with their call counts, the paging and port activity
summarised per port, the memory changed per region with the instructions that wrote it, the screen and border, the
keys, and the markers inside the frame. As text (MCP, CLI) about 20-40 lines; as structured fields for scripts. The
machine is left where it stood.

## Requirements

| # | Requirement |
|---|-------------|
| EF-1 | One verb, `explain-frame`, on all six planes: CLI (`ttd explain`), WebAPI (`GET /ttd/explain-frame`, also as a socket `call`), Lua, Python, MCP (`time_travel explain_frame`). Docs chain: command-interface.md, then the plane docs, then the recipes |
| EF-2 | Sections: `time` (T in the interrupt routine, in `HALT`, in the rest), `routines` (entry, calls, inclusive and own T, instructions; the top ones by time), `interrupt` (when acknowledged, the routine, its T), `paging` (bank changes with time and PC), `ports` (per direction and port: count, values, first time and PC; keyboard reads with the keys seen), `memory` (changed ranges per region, bytes changed and written, the writers), `screen` (% changed, border colours), `input` (keys held, pressed, released), `markers` |
| EF-3 | Exact: every number comes from one replay of the frame with the instruction and bus hooks; nothing is sampled. The writers of a range are the instructions that wrote it, by count |
| EF-4 | Bounded: `limit` per list (routines, ports, ranges; default 8, max 100), `sections` selects the sections; the text stays short by default and says what it left out ("+12 more routines") |
| EF-5 | Read-only: the machine returns to where it stood; a live recording pauses for browsing as other queries do |
| EF-6 | Generic: routines come from the call structure (CALL, RST, the interrupt acknowledge, RET), regions from the machine's memory map and screen layout, nothing per game |
| EF-7 | The partial last frame (the session end inside it) is explained up to the end and says so; a barrier inside the frame ends the comparison there and is listed |
| EF-8 | Zero recording cost: nothing new captured, no file format change |
| EF-9 | Measured against the old loop through WebAPI and MCP: calls, reply bytes, MCP text characters, wall time, as ratios. Accepted only if it is better |
| EF-10 | Tests about 10 ms each, integration under 1 s; long variants behind `UNREAL_LONG_TESTS=1` |

## Acceptance

- Synthetic programs (plan.md) give the expected routines, times, writes, ports, paging and keys.
- A real game recording (Green Beret in play, a 48K at the ROM's main loop): the frame found by `find_event` is
  explained, each section checked against the old tools (`memory_diff`, `port_events`, a stepped trace).
- The one call against the old loop: fewer calls, bytes and MCP text characters, and not slower. Numbers in plan.md.
- Zero warnings on clang, gcc, MSVC and MinGW; full core-tests; the live MCP scripts.
