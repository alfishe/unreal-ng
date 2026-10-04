# TODO — TUI debugger

Spec done; FTXUI POC committed (`815d1797`, `tools/poc/018-tui-debuggers`). Remaining: a real TUI front-end on the core. It is one skin of the [debugger model](../2026-09-28-debugger-model/) (2026-09-28): its widget and field set is the model's content source, and the front-end runs on the model's protocol.

Plan: [PLAN.md](../PLAN.md) #49 (T4).

- [x] Can the TS-Conf TUI run on the WebAPI (2026-10-03): [webapi-gap-analysis.md](webapi-gap-analysis.md) - feasible with about 8 server additions; the step that parked the HTTP server on a breakpoint is fixed and WebSocket debugger events exist ([.recipe/analysis/breakpoints-and-events.md](../../../.recipe/analysis/breakpoints-and-events.md)); D1 (raw TS-Conf registers, programmed DMA, the line's tile pages) and D2 (MEMPTR, Q, T, HALT, boundary, IM / IFF writes, R bit 7) are on every surface, named as in the debugger protocol. D4's page condition (`--page ram32`, JSON `{kind, page}`) too. The breakpoint work of conditional-breakpoints §5.2 is built (2026-10-04): ranges, physical pages, port masks, hit counts. Next: D5 / D6 / D8, D7 + E3, E1, E6 / E4 / E5 through the protocol's routes; conditions with the expression evaluator
- [ ] The rest of the additions in that analysis (raw TS-Conf registers, one-call snapshot and binary memory, port write, PC history, smaller fields, conditional breakpoints), after what the TTD v2 work covers is known
