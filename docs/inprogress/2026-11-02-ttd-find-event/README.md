# TTD find event: the moment something happened

**Status 2026-11-02:** done on all six planes and accepted ([plan.md](plan.md#measurements): one call instead of 2-301,
2.4-387 times less MCP text). Plan row [#113](../PLAN.md). The second item of the agent UX roadmap, after the cause chain
([2026-11-02-ttd-cause-chain](../2026-11-02-ttd-cause-chain/README.md)); explain-frame comes next. Accepted only if
WebAPI / MCP measurements prove it (owner 2026-11-01).

Files:

| File | What |
|------|------|
| [design.md](design.md) | the event kinds, the search per kind, the reply, costs, the planes |
| [plan.md](plan.md) | steps, tests, benchmarks (the one call against the old loop), acceptance |

## The problem

An agent with a recording asks "when did the lives counter go down?", "when did the level screen appear?", "when did
the game first run the death routine at #8A14?", "when did the player press fire?". Today:

- **A value going down:** `find_last` only looks back and only matches a value. The agent calls `memory_at` frame by
  frame and compares, or lists every write with `changes_only` and compares old and new itself.
- **A scene change:** no per-frame screen signal tells a whole new picture from a sprite moving. The timeline's
  `screen` tag only means the screen was written in a quarter of the frames. The agent reads the screen frame by frame
  (`memory_at` 6912 bytes, or screenshots) and diffs.
- **The first run of an address:** `coverage_scan order=oldest` gives the frame. For the instruction and its time the
  agent seeks there and steps forward by hand: `reverse_continue` finds the last run in a window, not the first.
- **A key press:** the input journal holds every key, but no verb returns it. Only the timeline counts presses per
  bucket.
- **The first OUT to a port, a border colour:** `port_events` already answers in one call. It belongs here so that one
  verb answers every "when".

Each step back and forth is a round trip, a reply and the agent's tokens, and the frame-by-frame loops are where a
recording of thousands of frames becomes thousands of calls.

## The goal

One call: an event described by kind and condition, and an order (`first` by default, or `last`) inside a frame window.
The answer is the frame and the time inside it, the instruction that did it (PC, text), the values before and after,
and a one-line "why". `limit` lists more than one. The machine is left where it stood.

## Requirements

| # | Requirement |
|---|-------------|
| FE-1 | One verb, `find-event`, on all six planes: CLI (`ttd event`), WebAPI (`GET /ttd/find-event`, also as a socket `call`), Lua, Python, MCP (`time_travel find_event`). Docs chain: command-interface.md, then the plane docs, then the recipes |
| FE-2 | Kinds: `mem` (a byte or a word: `changed`, `increased`, `decreased`, `equals`, `becomes`, `bit_set`, `bit_clear`), `screen` (a scene change: at least `min_changed` % of the screen bytes differ from the frame before; `cleared`: the pixels filled with one byte), `exec` (an address or range runs), `port` (`in` / `out`, port and mask, value), `border` (the border colour changes, or becomes a colour), `key` (a key or any key is pressed or released) |
| FE-3 | The answer: frame, tinframe, PC and code page, instruction text, `before` / `after`, `why` text; with `limit` the next hits, and `next_from_frame` to continue |
| FE-4 | Exact time: a `mem` hit names the write that made the condition true, an `exec` hit names the instruction's start, `port` / `border` / `key` hits come from their journals with exact time |
| FE-5 | Cheap first: frames are filtered without a replay (piece versions, the coverage index, the journals); at most the hit frames are replayed. The reply carries `frames_scanned`, `frames_replayed`, `ms` |
| FE-6 | `mem` compares frame boundaries by default. `exact=true` follows every CPU write in the window instead, so a value that goes down and back up inside one frame is found too. A change no CPU write explains is reported as a device write |
| FE-7 | Read-only: the machine returns to where it stood; a live recording pauses for browsing as other queries do |
| FE-8 | Generic: the kinds are machine facts (memory, screen layout, ports, input), nothing per game; the screen kind uses the session's screen layout (ULA page 5 / 7, other models through their layout) |
| FE-9 | Bounded: `from_frame` / `to_frame` (the whole history by default), `limit` (default 1, max 100); a search that hits a barrier marker or the end of the window says so |
| FE-10 | Zero recording cost: nothing new captured, no file format change |
| FE-11 | Measured against the old loop through WebAPI and MCP: calls, reply bytes, MCP text characters, wall time, as ratios. Accepted only if it is better |
| FE-12 | Tests about 10 ms each, integration under 1 s; long variants behind `UNREAL_LONG_TESTS=1` |

## Acceptance

- Synthetic programs (plan.md) give the expected frame, time, PC and values for every kind and order.
- A real game recording, the events chosen without knowing the game: the first scene change, the first decrease of a
  byte the game counts down, the first run of an address seen in the coverage, the first key press. Each answer is
  checked against a frame-by-frame scan.
- The one call against the old loop: fewer calls, bytes and MCP text characters, and not slower. Numbers in plan.md.
- Zero warnings on clang, gcc, MSVC and MinGW; full core-tests; the live MCP scripts.
