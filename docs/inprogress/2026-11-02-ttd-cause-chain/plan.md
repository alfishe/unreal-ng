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
| S11 | `LDIR` and a question about HL after it | one link `repeated N times`, then the `LD HL,nn`; L from L alone |

Plus: a register target (`A` at a moment) without a store; `depth` and `max_links` cut the tree with `stop`; the
machine is where it was after the query (position, registers, memory hash); a live recording answers after pausing.

Each test about 10 ms (a few frames recorded, turbo); the frame limit and the real recording behind
`TestTier::Long()`.

## Benchmarks: the one call against the old loop

`tools/verification/mcp/` live script and `scratch/ttd-cause-chain/live_cause.py`: for each S-program and for the
real recording, the old loop (as above) and the one call, through WebAPI and through MCP. Columns: calls, reply
bytes, MCP text characters, wall ms, and the ratios old / new.

## Measurements

2026-11-02, Apple M-series, unreal-qt release, `scratch/ttd-cause-chain/live_cause.py` (S-programs, Pentagon) and
`game_cause.py` (Green Beret in play, `testdata/loaders/rzx/oracle/greenberet-300.z80`, 50 recorded frames). The
old loop is the agent's loop of step 0 (`find_last` -> `disassemble` -> `seek` -> `reverse_step` + `registers` +
`disassemble` until the register's writer -> `find_last` for the address read ...), depth 4, at most 3000 reverse
steps per register. Calls, reply bytes (REST: the bodies; MCP: `structuredContent`), MCP text characters, wall ms:

| Question | Surface | Old loop: calls / bytes / text / ms | One call: calls / bytes / text / ms | Links old / new |
|----------|---------|-------------------------------------|-------------------------------------|-----------------|
| S1 load / store | REST | 14 / 2,444 / - / 249 | 1 / 632 / - / 10.8 | 3 / 3 |
| S1 | MCP | 14 / 2,847 / 1,450 / 277 | 1 / 703 / 236 / 14.6 | 3 / 3 |
| S3 `ADD A,B` | REST | 25 / 4,420 / - / 334 | 1 / 955 / - / 26.5 | 6 / 5 |
| S3 | MCP | 25 / 5,175 / 2,634 / 477 | 1 / 1,075 / 360 / 18.9 | 6 / 5 |
| S4 PUSH / POP via the ISR | REST | 29 / 5,263 / - / 286 | 1 / 987 / - / 56.0 | 5 / 5 |
| S4 | MCP | 29 / 6,157 / 3,132 / 444 | 1 / 1,115 / 345 / 9.9 | 5 / 5 |
| S7 across frames (HALTs) | REST | 9,007 / 1,672,695 / - / 58,076, **stuck** (3000 steps) | 1 / 627 / - / 13.9 | 2 / 3 |
| S7 | MCP | 9,007 / 1,990,877 / 1,131,630 / 37,999, **stuck** | 1 / 707 / 239 / 9.0 | 2 / 3 |
| 48K ROM FRAMES `#5C78` | REST | 29 / 5,208 / - / 45.9 | 1 / 1,050 / - / 4.4 | 5 / 5 |
| 48K ROM FRAMES | MCP | 29 / 6,102 / 3,217 / 89.1 | 1 / 1,181 / 538 / 9.5 | 5 / 5 |
| Green Beret `#A161` | REST | 26 / 4,754 / - / 43.1 | 1 / 633 / - / 3.0 | 3 / 3 |
| Green Beret `#A161` | MCP | 26 / 5,581 / 2,943 / 58.0 | 1 / 704 / 235 / 3.9 | 3 / 3 |
| Green Beret `#A130` | REST | 18,067 / 3,479,142 / - / 113,496, **stuck** | 1 / 493 / - / 10.0 | 8 / 2 |
| Green Beret `#A130` | MCP | 18,067 / 4,121,326 / 2,250,852 / 168,256, **stuck** | 1 / 550 / 154 / 3.5 | 8 / 2 |

Ratios: 14-29x fewer calls where the old loop finishes, 4-8x fewer bytes, 6-13x less MCP text, 4-30x faster; where
the producer is far back in instructions (a HALT loop, a long frame) the old loop does not finish in 3000 reverse
steps per register while the one call answers in 4-14 ms. The old loop's links count leaves it cannot classify.

Green Beret acceptance (the bytes chosen without knowing the game: the 32 RAM bytes outside the screen that changed
in the 50 frames, 12 spread over them, plus A / HL / SP / IX): every answer names the value at the moment first;
the chains end at a constant (`xor a`, `ld hl,#63E0`), a return address (`call`), or initial data tables
(`#5BF5..`, `#5C01`), never unknown; 12 memory questions in 143 ms over MCP. HL after the screen copy is one link,
`ldir` repeated 1709 times, from `ld hl,#4000`; IX never written: `initial` for both bytes, 51 frames traced once
(113 ms; 488 ms before the per-frame register set).

Judged: accepted (CC-11).
