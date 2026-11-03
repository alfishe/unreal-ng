# TTD find event: plan

Requirements: [README.md](README.md). Design: [design.md](design.md).

## Steps

| # | Step | Done when |
|---|------|-----------|
| 0 | Measure the old loop: a script answers the T-program questions and the game questions the way an agent does today, through WebAPI and MCP. For `mem` that is `memory_at` frame by frame, then `find_last`. For `screen`, `memory_at` of the screen frame by frame plus a diff. For `exec`, `coverage_scan`, then `seek` and `step_instruction`. For `key`, the timeline buckets, then `port_events key` | calls, bytes, MCP text characters and ms recorded in this file |
| 1 | The query types and `QueryFindEvent` for `mem` (boundaries and exact) and `exec` | T1-T4 and T7 green |
| 2 | `screen`, `port`, `border`, `key`; barriers, `last`, `limit`, `next_*` | T5, T6 and T8-T12 green |
| 3 | The verb on the six planes, docs, recipe | MCP unit tests, the live MCP script |
| 4 | The one call against the old loop (WebAPI and MCP), on the T-programs and a real recording | numbers in this file, judged |
| 5 | All platforms: clang, gcc (docker), MSVC, MinGW; full core-tests | then land |

## Tests

The T-programs run on a Pentagon (a 48K for the key test). Each is recorded for a few frames, in turbo, then asked.

| # | Program | Expected |
|---|---------|----------|
| T1 | a counter at #A000 counts down by one every other frame, from 5 | `mem decreased` first: the second frame's `DEC (HL)`, its PC, #05 -> #04; `last`: the final step; `limit 3`: three hits in order |
| T2 | the same counter, `equals 2` / `becomes 2` / `bit_clear 0` | the frame and the write where it first holds |
| T3 | a byte set to #FF and back to #00 within one frame | boundaries: no hit; `exact`: the write that set #FF |
| T4 | a word at #A000 (`LD (#A000),HL`) going #00FF -> #0100 | `size 2 increased`: one hit, the write of the high byte |
| T5 | a program that fills the screen (pixels and attributes) in frame 3, and moves one 8-byte sprite every frame | `screen 50` first: frame 3 only, about 100 %; `screen 1`: every sprite frame; `cleared`: the frame after a pixel clear |
| T6 | page 5 drawn, then the 7FFD bit switching the shown screen to page 7, which holds another picture | `screen` hits at the switch |
| T7 | a routine at #9000 called first in frame 4 | `exec #9000` first: frame 4, the CALL's target start, tinframe; `last`: its last run |
| T8 | OUT (#FE) with colours 1, 1, 2 | `border` first: colour 1 at the first OUT, then 2 (the repeat of 1 is not a hit); `border 2`: the third OUT |
| T9 | a port 7FFD write of value #17 | `port out #7FFD mask #8002 value #17`: its time and PC |
| T10 | key presses injected in frames 3 and 6 (SPACE, then Q) | `key` first: SPACE at frame 3; `key Q`; `pressed=false`: the release |
| T11 | a snapshot load in the middle of the recording (a barrier) | a `mem changed` across it is not a hit; the barrier is listed |
| T12 | the window and the limits | `from_frame` / `to_frame`, `next_from_frame` continues, `limit` 100 maximum, an unknown kind or cond is an error |

The machine must be where it was after every query: position, registers, memory hash. Each test should take about
10 ms, and the long variants run behind `TestTier::Long()`.

## Benchmarks: the one call against the old loop

The script `scratch/ttd-find-event/live_event.py` (a 48K recording and Green Beret in play, 300 frames each) answers each
question twice: the old loop of step 0 and one `find_event` call, both through WebAPI and through MCP. Columns: calls,
reply bytes, MCP text characters, wall ms, and the ratios old / new.

## Measurements

2026-11-02, Apple M-series, `live_event.py` against a launched unreal-qt (release). Per question: the MCP old loop
against the one MCP call (calls, MCP text characters, ms) and the REST reply bytes; frames found by each.

| Question | Old loop (MCP) | `find_event` (MCP) | REST bytes old / new | Frame old / new |
|----------|----------------|--------------------|----------------------|-----------------|
| 48K FRAMES wraps to 0 (`mem becomes 0`) | 141 calls, 7241 chars, 1264 ms | 1 call, 144 chars, 8 ms | 14920 / 381 | 338 / 338 |
| 48K FRAMES last increase (`order last`) | 4 calls, 393 chars, 105 ms | 1 call, 167 chars, 13 ms | 537 / 405 | 529 / 530 |
| 48K first IM 1 entry (`exec #0038`) | 3 calls, 584 chars, 19 ms | 1 call, 174 chars, 8 ms | 667 / 385 | 201 / 201 |
| 48K SPACE pressed (`key`) | 2 calls, 486 chars, 66 ms | 1 call, 135 chars, 8 ms | 1534 / 278 | 501 / 501 |
| GB scene change 30 % (`screen`) | 301 calls, 60391 chars, 2815 ms | 1 call, 156 chars, 35 ms | 4192710 / 330 | none / 59 |
| GB #A130 decreased (`mem`) | 63 calls, 3257 chars, 385 ms | 1 call, 133 chars, 10 ms | 6584 / 370 | 59 / 59 |
| GB first run #A46B (`exec`) | 3 calls, 566 chars, 55 ms | 1 call, 175 chars, 9 ms | 653 / 391 | 59 / 59 |

The two disagreements are the old loop's misses:

- **Last increase.** `memory_at` reads frame starts, so the loop cannot see the write inside the session's last frame
  (530, `#BF -> #C0` at t 1882, before the session end); it names the one before.
- **Scene change.** Green Beret redraws over frames 59..61 (39 % in all), and no frame alone reaches 30 %: the
  frame-by-frame diff finds nothing, the run rule (design §3) finds it. The old loop also moves 4 MB.

The run found one bug, now fixed: through the verb an absent `pressed` read as false (released). T10 now asks through
`TTDControl` too.

**Judged: accepted.** Where the old loop is a scan (mem from boundaries, screen) the one call takes 63-301 times fewer
calls, 24-387 times less MCP text and 37-155 times less time. Where it is already short (exec, key, `last`) still
2-4 calls fewer, 2.4-3.6 times less text and 2-8 times less time, plus the instruction, the values and the why in one
line, and two answers the old loop gets wrong.
