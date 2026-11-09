# TTD backtrace: the call stack at a moment

**Status 2026-11-07:** implemented on all six planes (CLI `ttd bt`, `GET /ttd/backtrace` and its WebSocket call, MCP
`time_travel backtrace`, Lua / Python `ttd_backtrace`); unit tests `TTDBacktrace_Test`, live MCP check in
`tools/verification/mcp/test_time_travel.py`.

## The question

"How did the program get here?" An agent at a moment of the history (a `find_event` hit, a `cause_chain` link, a
crash) wants the chain of callers. Before this verb it walked back by hand: `reverse_continue` to the routine's
entry, `reverse_step` one instruction to the CALL, read SP, repeat per caller - two calls and a replay per frame, and
no way to tell a live return address from a stale word.

## The method

The Z80 has no frame pointers, so a heuristic walk ("every word that points just after a CALL is a return
address") mistakes data for frames and misses RST and interrupts. The history knows better: every stack word was
written by an instruction, and that instruction says what the word is.

For each word from SP upward:

1. Its last write before the moment: `FindAccesses` with `beforeGlobalT` (the write journal when it is on, else the
   pruned replay), on the physical page the address maps to at the moment.
2. The instruction of that write: a replay of its frame (`TraceCauseFrame`, eight frames cached), the instruction
   holding the W event at that address, page, flip and T.
3. Its class from the cause decoder (`TTDExplainInstruction`): output kind `return` (CALL / RST) or `interrupt` with
   the pushed value equal to the word now - a frame; anything else (PUSH, a store) - a data word, counted under the
   frame above it.
4. The routine entered: the next instruction in the trace (unless it is an interrupt record), else the CALL operand
   or the RST target.

Why it is exact: with the usual stack discipline every word between SP and the stack's base was written since the
stack last grew past it, so its last writer produced what it holds now. A call that returned is gone (its word
written over by a later push, or below SP). A stack moved by `LD SP` shows the frames written where SP now points.

The walk stops and says why: `max_depth` (frames, default 16, at most 64), `max_slots` (words, default 256, at most
4096), `top_of_memory`, `rom` (a word in ROM), `before_session` (a word never written in the history: the stack's
base, or frames older than the session) or `barrier` (a replay barrier before the write).

Nothing per program: the classes come from the decoder table that `cause_chain` uses.

## Session rules

Like `cause-chain`: refused while a recording runs unless browsing pauses it; the live state is saved and restored
around the replays, so the machine stays where it was.

## Answer

`#0` the PC, the routine it is in, SP and the moment; `#1..` one line per frame: the CALL / RST PC and its text, the
routine it entered, the return address, when it was pushed, the slot, the data words below it; then the stop line.
The JSON form carries the same fields (`webapi-interface.md`).

## Files

- `core/src/debugger/ttd/ttdbacktrace.h`, `ttdcontrollerbacktrace.cpp` - the query and its text.
- `core/src/debugger/ttd/ttdcontrol.cpp` - the `backtrace` verb (options, reply).
- Planes: `ttd_api.cpp` + `openapi_ttd.inc`, `mcp-tools.cpp`, `cli-processor-ttd.cpp`, `lua_emulator.h`,
  `python_emulator.h`.
- Tests: `core/tests/debugger/ttd/ttdbacktrace_test.cpp` (nested calls with a PUSH, a returned call, an IM 2
  interrupt, `max_depth`).
