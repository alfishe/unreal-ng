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
| T3 | (as run) on a Pentagon the INT at the frame end: the frame starts in the routine's tail | the tail is the interrupt acknowledged before the frame (`before_frame`, carried from the frame before), back to the loop; the frame's own one still running at the end |
| T3b | the frame (the session's first) starts at the IM 2 entry with IFF1 off | that routine is the interrupt, not a starting context; its RETI returns to the stacked PC |
| T4 | `DI` at the start, no interrupt | "no interrupt (DI)" (checked in T1) |
| T5 | `OUT (#7FFD)` switching bank 3 to page 7 and back | two paging rows with time, PC, old and new page |
| T6 | `OUT (#FE)` colours 1, 1, 2; reads of two half-rows with SPACE held | ports: out ULA x3 with the values, in ULA x2 with SPACE down; border `1 x2 -> 2` |
| T7 | `LDIR` of 256 bytes into the screen, `LD (#A000),A` of one byte, a byte written with the same value | memory: screen pixels 256 changed by the LDIR; `#A000` before / after by its LD; the unchanged write counted as written, not changed |
| T8 | the stack: CALLs and PUSHes | one stack range, not one per push |
| T9 | a key pressed inside the frame, another held from before | input: held and pressed with time |
| T10 | the last frame of the session (partial) and a frame with a snapshot load inside (barrier) | `partial`, the barrier listed and the comparison ended there |
| T11 | `limit` and `sections` | lists cut at `limit` with `more`, only the asked sections; an out-of-window frame is an error |
| T12 | a Next: 8K page `#0F` (page 7's upper half) in slot 6, a write to `#C005` | the range is page 7 offset `#2005`, written and changed 1, no device range |

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

2026-11-02, `scratch/ttd-explain-frame/live_explain.py` on unreal-qt release (macOS, M-series, the machine shared
with other builds: wall times vary 2-3x between runs). A 48K at the ROM's main loop (frame 350), Green Beret in play
(frame 149) and the frame `find_event` names for `#A130` decreased (frame 59). The old loop: `seek`, `memory_diff`, up
to 8 `find_last` + `disassemble`, `port_events` in / out, `timeline`, 50 `step_instruction`.

| Frame | Surface | Calls old / one | Bytes old / one | MCP text old / one | ms old / one |
|-------|---------|-----------------|-----------------|--------------------|--------------|
| 48K main loop | REST | 59 / 1 | 7977 / 2325 (3.4x) | - | 341 / 11 |
| 48K main loop | MCP | 59 / 1 | 8936 / 2635 (3.4x) | 3591 / 1282 (2.8x) | 1060 / 9 |
| GB in play | REST | 71 / 1 | 10469 / 2206 (4.7x) | - | 826 / 4 |
| GB in play | MCP | 71 / 1 | 11715 / 2516 (4.7x) | 5378 / 1133 (4.7x) | 1615 / 6 |
| GB `#A130` decreased | REST | 71 / 1 | 11271 / 3975 (2.8x) | - | 982 / 6 |
| GB `#A130` decreased | MCP | 71 / 1 | 12631 / 4529 (2.8x) | 5461 / 2034 (2.7x) | 1263 / 109 |

What each answer covers:

| Frame | Old loop | One call |
|-------|----------|----------|
| 48K main loop | 2 changed runs, 2 writers, no routines (50 steps of 6965 instructions) | 2 ranges, 9 writers, 8 routines with times, the IM 1 interrupt (882 T, back to its PC), 8 keyboard reads |
| GB in play | 19 changed runs, 4 writers in the frame | 3 merged ranges, 14 writers, 6 routines, the IM 2 interrupt via `#CCFF` |
| GB `#A130` decreased | 42 changed runs, 8 writers (8 of 42 ranges asked) | 10 ranges (screen, stack, RAM pages) with 27 writers, 11 routines, the border OUT |

The old loop never gives the routine structure or the time split: stepping 50 instructions sees under 1 % of a frame,
and stepping all of it would be thousands of calls. **Accepted**: one call instead of 59-71, 2.7-4.7x less MCP text,
an answer the old loop cannot reach.

The live run found one defect the unit tests missed: on a Pentagon the INT is at the frame end (+71655), so the next
frame starts inside the 128K ROM's IM 1 handler (a ROM paging trampoline through `PUSH` + `RET`), shown as code at
the frame start. Fixed generically: IFF1 off at the frame start replays the frame before and carries its open routines
(T3, T3b, the live MCP test on the frame after).

### Every configuration

2026-11-03, `scratch/ttd-explain-frame/models_explain.py`: every creatable model of `GET /api/v1/emulator/models` at
each RAM size and every variant (a game or program where one fits: 7th Reality on the 48K, Green Beret on the 128K,
`.spg` on TS-Conf and TS-Conf + VDAC2), 300 frames warm, 12 recorded, 6 explained. Each answer checked against
independent facts: the time split adds up to `end_t`, the changed bytes equal `memory_diff ram F -> F+1`, the
position does not move.

- Pass: Pentagon 128 / 512 / 1024, 48K, 128K, +2, +2A, +3, TS-Conf, TS-Conf + VDAC2, ATM 3, ATM 7.10 128-1024,
  ATM 4.50 512 / 1024, Profi 512 / 1024, Profi 3 512 / 1024, Profi+, Scorpion 256 / 1024, Prof Scorpion 256 / 1024,
  GMX, Sprinter, Next 1024 / 2048, Next KS1, Next KS3.
- Not creatable (no ROM set): Kay, Quorum, LSY256, Phoenix. ZX-Poly 48K / 128K / Pentagon: time travel refused by
  design.

It found two defects on the Next, both fixed:
- The Next's port decoder never said its TTD clock units (3.5 / 7 / 14 / 28 MHz: 8), so a frame was 70908 units
  while the firmware ran it at 28 MHz: every replay to a frame's end stopped after its first eighth (all of TTD on
  the Next, not only explain). Now 8 units: a frame is 567264 (`TimeTravelController_TurboClock_Test.NextCountsItsTopClock`).
- The traces named a byte by its 16K window (`TtdPageOfBank`, address & `#3FFF`); the Next's 8K slots can show either
  half of a page. `Memory::TtdLocate` answers per slot (T12).

Follow-up (branch `ttd-next-pages`), the rest of TTD on the Next:
- The write journal, the coverage index, the access probe, the cause chain and find-event keyed a byte by page and
  address & `#3FFF` too. A record now carries a flip bit (`Memory::TtdKeyOf`, `RecordOffset`), a write block adds a
  flip column only when it holds such a record (`TTDWriteJournal_Compression_Test.EightKHalves...`), a query with an
  unknown half checks both (`CoverageMayContain`). Cause chain S12: `#C005` written through 8K page `#0F`, then
  through `#0E`, asks for the byte mapped now and gets the first store, not the later one to the other half.
- Every seek on the Next re-derived slots 0, 1, 6 and 7 from `#7FFD` after the image had restored the MMU
  (`UpdateZ80Banks` after the device restore): a program paging through NR `#50`-`#57` saw other pages after a seek.
  The restore now ends with `Memory::RestorePaging()` (the Next: `Remap()`), checked by S12's seek to the end.
