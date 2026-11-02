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

The scripts `scratch/ttd-find-event/live_event.py` (the T-programs) and `game_event.py` (a real game) answer each
question twice: the old loop of step 0 and one `find_event` call, both through WebAPI and through MCP. Columns: calls,
reply bytes, MCP text characters, wall ms, and the ratios old / new.

## Measurements

(filled as the steps land)
