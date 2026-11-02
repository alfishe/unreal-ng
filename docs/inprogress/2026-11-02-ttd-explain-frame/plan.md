# TTD explain frame: plan

Requirements: [README.md](README.md). Design: [design.md](design.md).

## Steps

| # | Step | Done when |
|---|------|-----------|
| 1 | The query types and `QueryExplainFrame`: the trace, time, routines, interrupt | T1-T4 green |
| 2 | paging, ports, memory, screen, input, markers; partial frame, barrier, limits | T5-T11 green |
| 3 | The verb on the six planes, docs, recipe | MCP unit tests, the live MCP script |
| 4 | The one call against the old loop (WebAPI and MCP), on a 48K and Green Beret | numbers in this file, judged |
| 5 | All platforms: clang, gcc (docker), MSVC, MinGW; full core-tests | then land |

## Tests

T-programs on a Pentagon (IM 2, the find event fixture), recorded a few frames in turbo, then one frame explained.

| # | Program | Expected |
|---|---------|----------|
| T1 | main: `CALL #9000` (a loop of known length), `CALL #9100` twice, then `HALT` | routines #9000 1x and #9100 2x with their exact T and instruction counts; `HALT` time = the frame end minus the HALT start; time split adds up to the frame |
| T2 | a routine that calls another (nested), and one that returns through `POP` + `JP` | inclusive T of the outer covers the inner; own T excludes it; the `POP`+`JP` routine stays open until a return below it |
| T3 | the interrupt routine (IM 2) calling a sub-routine | the routine is marked `isr`, its T from the acknowledge to `RETI`, the vector read, back to the interrupted PC |
| T4 | `DI` at the start, no interrupt | "no interrupt (DI)" |
| T5 | `OUT (#7FFD)` switching bank 3 to page 7 and back | two paging rows with time, PC, old and new page |
| T6 | `OUT (#FE)` colours 1, 1, 2; reads of two half-rows with SPACE held | ports: out ULA x3 with the values, in ULA x2 with SPACE down; border `1 x2 -> 2` |
| T7 | `LDIR` of 256 bytes into the screen, `LD (#A000),A` of one byte, a byte written with the same value | memory: screen pixels 256 changed by the LDIR; `#A000` before / after by its LD; the unchanged write counted as written, not changed |
| T8 | the stack: CALLs and PUSHes | one stack range, not one per push |
| T9 | a key pressed inside the frame, another held from before | input: held and pressed with time |
| T10 | the last frame of the session (partial) and a frame with a snapshot load inside (barrier) | `partial`, the barrier listed and the comparison ended there |
| T11 | `limit` and `sections` | lists cut at `limit` with `more`, only the asked sections; an out-of-window frame is an error |

The machine must be where it was after every query: position, registers, memory hash. Each test about 10 ms.

## Benchmarks: the one call against the old loop

`scratch/ttd-explain-frame/live_explain.py` answers "what did frame F do?" twice for a few frames (a 48K at the ROM
main loop, Green Beret in play, the frame `find_event` names for a counter): the old loop and one `explain_frame`
call, through WebAPI and through MCP.

The old loop is what an agent can do today: `seek` F; `memory_diff` ram F -> F+1; for each changed range up to 8
`find_last` and the writer's disassembly; `port_events in` and `out` in the frame; `timeline` around F for the keys;
`step_instruction` in steps over the frame to see the code (bounded at 50 calls; the code picture stays partial).
Columns: calls, reply bytes, MCP text characters, wall ms, ratios, and what each answer covers (routines named,
writers named).

## Measurements

(filled as the steps land)
