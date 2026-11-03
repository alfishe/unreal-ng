# TTD diff of two moments: what differs between two points of the history, in one call

**Status 2026-11-03:** done on all six planes, measured and accepted ([plan.md](plan.md)): 1 call instead of 18-74,
6.3-17.4x less MCP text, any T inside a frame. The fourth item of the agent UX roadmap, after the cause chain
([2026-11-02-ttd-cause-chain](../2026-11-02-ttd-cause-chain/README.md)), find event
([2026-11-02-ttd-find-event](../2026-11-02-ttd-find-event/README.md)) and explain frame
([2026-11-02-ttd-explain-frame](../2026-11-02-ttd-explain-frame/README.md)). Accepted only if WebAPI / MCP
measurements prove it (owner 2026-11-01).

Files:

| File | What |
|------|------|
| [design.md](design.md) | the sections of the answer, how each is built, the reply, costs, the planes |
| [plan.md](plan.md) | steps, tests, measurements (the one call against the old loop), acceptance |

## The problem

An agent comparing two moments ("before the player died" and "after", "the title screen" and "the first level",
"before this OUT" and "after it") asks: which registers changed, which pages are mapped now, which bytes of memory
differ and who wrote them, did the screen change, which devices changed. Today it assembles that by hand:

- **Registers:** `seek` to the first moment, read the registers; `seek` to the second, read them again; compare.
- **Memory:** `memory_diff` compares frame starts only (no moment inside a frame) and gives ranges without values
  or writers; each range then needs a `memory_at` or a read at both moments and a `find_last`.
- **Paging:** the latches and the mapping are read at each moment and compared by hand; on the Next the 8K slots
  (NR `#50`-`#57`) need a NextREG read each.
- **Devices:** nothing compares a device's state between moments.
- **In between:** the keys pressed and the markers come from the timeline or the input journal separately.

Many calls, large replies, and two seeks that leave the machine at the second moment unless the agent seeks back.

## The goal

One call: two moments (the second the current position by default), any T inside a frame. The answer: the registers,
latches and slot mapping that differ, the RAM changed in ranges per region (screen pixels, screen attributes, RAM per
page) with the bytes of short ranges and the last CPU write in between, the screen and border, the devices whose state
differs, and the frames, T, keys and markers in between. As text (MCP, CLI) a dozen lines; as structured fields for
scripts. The machine is left where it stood.

## Requirements

| # | Requirement |
|---|-------------|
| DM-1 | One verb, `diff`, on all six planes: CLI (`ttd diff F[+T] [G[+T]]`), WebAPI (`GET /ttd/diff`, also as a socket `call`), Lua (`ttd_diff{}`), Python (`ttd_diff(from_frame, ...)`), MCP (`time_travel diff`). Docs chain: command-interface.md, the plane docs, the recipes |
| DM-2 | Sections: `cpu` (registers), `paging` (latches, 8K slot mapping with the Next's halves), `memory` (ranges, values, last write), `screen` (pixels / attributes changed, page switched, border), `devices` (registered devices whose state differs), `between` (frames, T, keys held and pressed, markers, a barrier) |
| DM-3 | Exact: each moment's state is the machine's own after a replay to it (the same state a seek gives); a moment inside a frame is the first instruction boundary at or after the T asked for, and the answer says which |
| DM-4 | Bounded: `limit` per list (default 16, max 200), `sections`, `writers=false` skips the write search; the text says what it left out |
| DM-5 | Read-only: the machine returns to where it stood; a live recording pauses for browsing as other queries do |
| DM-6 | Generic: regions from the machine's screen layout, the mapping from `Memory::TtdKeyOf` per 8K slot, devices from the TTD registry; nothing per game or per machine |
| DM-7 | Zero recording cost: nothing new captured, no file format change |
| DM-8 | Measured against the old loop through WebAPI and MCP: calls, reply bytes, MCP text characters, wall time, as ratios. Accepted only if it is better |
| DM-9 | Tests about 10 ms each, integration under 1 s |

## Acceptance

- Synthetic programs (plan.md D1-D7) give the expected registers, latches, slots, ranges, values and writers, on a
  Pentagon and on the Next (8K halves).
- A real game recording (Green Beret in play): two moments compared, each section checked against the old tools.
- The one call against the old loop: fewer calls, bytes and MCP text characters, and not slower. Numbers in plan.md.
- Zero warnings; full core-tests; the live MCP scripts.
