# TTD cause chain: plan

Requirements: [README.md](README.md). Design: [design.md](design.md).

## Steps

| # | Step | Done when |
|---|------|-----------|
| 0 | The old loop measured: a script that answers the S-programs' questions the way an agent does today (`find_last`, `disassemble`, `reverse_step` + `registers`, by hand) through WebAPI and MCP | calls, bytes, MCP text chars, ms in this file |
| 1 | `PortClassifier` to a header of its own; the frame trace (`TraceFrame`: records, events, the interrupt split) | trace unit tests green |
| 2 | `Explain`: the source rules of design §4 | one test per row of the table, on code run by the CPU |
| 3 | The walk, `TimeTravelController::QueryCauseChain`, the checks and stops | S1-S10 green |
| 4 | The verb on the six planes, docs, recipe | MCP unit tests, the live MCP script |
| 5 | The one call against the old loop (WebAPI and MCP), on S1-S10 and on a real recording | numbers in this file, judged |
| 6 | All platforms: clang, gcc (docker), MSVC, MinGW; full core-tests | then land |

## Tests

Synthetic programs on Pentagon (and 48K for the ROM interrupt), each recorded a few frames, then asked; every link
checked (kind, PC, value, input):

| # | Program | Expected chain |
|---|---------|----------------|
| S1 | `LD A,(src)` ... `LD (dst),A` | dst: store <- A: load <- (src): initial |
| S2 | `LD HL,src : LD DE,dst : LD BC,n : LDIR` | dst+k: block <- (src+k) |
| S3 | `LD A,(x) : ADD A,B` with B from `LD B,n`, then a store | computed with two inputs: (x) and B: constant n |
| S4 | `PUSH HL` in the main loop, `POP DE` in the ISR, store E | store <- E: load <- stack byte: stack <- L |
| S5 | `EXX` / `EX DE,HL` / `EX AF,AF'` between the load and the store | copies through the primed and swapped names |
| S6 | `IN A,(#FE)` stored | port: #FE, the ULA device, the value read |
| S7 | the value loaded in one frame, stored three frames later | the register search crosses frames |
| S8 | `LD (HL),n` | constant |
| S9 | a byte never written in the history | initial, with the session start's value |
| S10 | an interrupt push read back with `POP` | interrupt leaf with the interrupted PC |

Plus: a register target (`A` at a moment) without a store; `depth` and `max_links` cut the tree with `stop`; the
machine is where it was after the query (position, registers, memory hash); a live recording answers after pausing.

Each test about 10 ms (a few frames recorded, turbo); the frame limit and the real recording behind
`TestTier::Long()`.

## Benchmarks: the one call against the old loop

`tools/verification/mcp/` live script and `scratch/ttd-cause-chain/live_cause.py`: for each S-program and for the
real recording, the old loop (as above) and the one call, through WebAPI and through MCP. Columns: calls, reply
bytes, MCP text characters, wall ms, and the ratios old / new.

## Measurements

(filled as the steps land)
