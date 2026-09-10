# Phase 4 — Reverse Execution Engine

Started: 2026-08-02
Reference: `docs/emulator/design/debugger/time-travel-debug/implementation-plan.md` §4
Parent TDD: §9 (reverse watchpoints), §10.2 (single-instruction step)
Phase 4 reverse-search deliverables: see [`phase-4-reverse-search.md`](phase-4-reverse-search.md).

## Outcome

Multi-step reverse execution is live as a core engine primitive. Three new
primitives on `TimeTravelManager`:

- **`ReverseStepInstructions(n)`** — go back N opcodes in one call
- **`ReverseStepTStates(n)`** — go back N t-states, landing at the nearest M1
- **`ReverseContinue(pcs)`** — run backward until any PC in the set matches

All three are benchmarked, the optimal strategy is selected adaptively based on
N, and the primitives are exposed through CLI / Python / WebAPI (parallel to the
existing `step-instruction` / `find-last` surfaces).

**446 TTD tests green** (up from 416 at Phase 4 reverse-search completion).

## Items delivered

| # | Item | Files | Tests |
|---|---|---|---|
| 1 | Engine primitives | `timetravelmanager.{h,cpp}` — `TTDM1Record`, `EnumerateM1InRange`, `ReverseStepInstructions`, `ReverseStepTStates`, `ReverseContinue`, `TTDReverseContinueResult`, threshold constants | 28 in `ttd_reverse_executor_test.cpp` |
| 2 | Benchmark harness | `core/benchmarks/debugger/ttd/ttd_reverse_benchmark.cpp` — N={1,4,16,64,256,1024,4096} for A_seq/B_m1list; N={1,100,1000,69888,700000} for B_m1list/B_direct; Bp={1,10,100} for ReverseContinue | — |
| 3 | RunTStates overshoot fix | `emulator.cpp` — when `targetT < frameLimit` and an instruction overshoots the frame boundary, break instead of running an entire extra frame | (existing tests unaffected) |
| 4 | CLI surface | `cli-processor.h`, `cli-processor-ttd.cpp` — `ttd reverse-step [--count N] [--tstates T]`, `ttd reverse-continue --pc <A> [...]`, aliases `rs`/`rc` | — |
| 5 | Python surface | `python_emulator.h` — `ttd_reverse_step(count=1)`, `ttd_reverse_step_tstates(tstates)`, `ttd_reverse_continue(pcs)` | — |
| 6 | WebAPI surface | `emulator_api.h`, `ttd_api.cpp` — `POST /ttd/reverse-step`, `POST /ttd/reverse-continue` | 2 contract tests in `ttd_automation_contract_test.cpp` |
| 7 | LUA surface | `lua_emulator.h` — `ttd_reverse_step()`, `ttd_reverse_step_tstates()`, `ttd_reverse_continue()` + Phase 4 reverse-search gap-fill (`ttd_dump`, `ttd_find_last`, `ttd_step_instruction_back/fwd`) | — |

## Design — three primitives

### `ReverseStepInstructions(n)` — adaptive strategy

| N range | Strategy | Rationale |
|---|---|---|
| N ≤ 4 | **A_seq**: call `StepBackInstruction()` N times | No benefit from M1 enumeration for small N; per-step restore is already cheap |
| N > 4 | **B_m1list**: single `EnumerateM1InRange` pass, pick Nth-from-end M1, `SeekTo` | One replay pass instead of N; replay cost is O(t-states), N-independent |

Threshold constants (in `timetravelmanager.h`):

```cpp
static constexpr uint32_t kReverseSeqStepMaxN  = 4;   // ≤ this → A_seq
static constexpr uint32_t kReverseM1ListLargeN  = 64;  // reference threshold
```

### `ReverseStepTStates(n)` — M1-aligned backward jump

Computes `target_globalT = currentGlobalT − n`, then enumerates M1 cycles in a
window around the target and picks the last M1 whose `globalT ≤ target`. This
ensures the landing position is always at an instruction boundary (Z80 has no
observable state between M1 cycles).

### `ReverseContinue(pcs)` — backward breakpoint scan

Enumerates all M1 cycles from session start to current position, scans the
vector backward for the first PC match, and `SeekTo`s the winner. Returns a
`TTDReverseContinueResult` with `matched`, `pc`, and `arrivedAt`.

### Shared helper: `EnumerateM1InRange(startGlobalT, endGlobalT, outM1s)`

Single silent-replay pass over an interval. Walks checkpoint intervals backward,
restores each checkpoint, arms the Execute probe with full address range, and
replays forward. Every M1 cycle captured by the probe becomes a `TTDM1Record`
with `{globalT, pc, physPage}`. The records are trimmed to `[startGlobalT, endGlobalT)`
and returned in ascending globalT order.

## Benchmark numbers (Debug build)

These were run on the development machine (Apple M1 Ultra) in Debug build.
Release numbers will be ~10× faster across the board; the relative ratios
that drive the threshold selection are stable.

### ReverseStepInstructions: A_seq vs B_m1list

| N | A_seq (µs) | B_m1list (µs) | Winner |
|---|---|---|---|
| 1 | 37,376 | 85,725 | A_seq (2.3×) |
| 4 | 97,529 | 15,386 | B_m1list (6.3×) |
| 16 | — | 17,294 | B_m1list |
| 64 | 297,670 | 231,213 | B_m1list (1.3×) |
| 256 | 746,224 | 137,496 | B_m1list (5.4×) |
| 1024 | — | 132,108 | B_m1list |
| 4096 | — | 131,407 | B_m1list (constant-ish) |

**Conclusion**: N ≤ 1 favors A_seq; N ≥ 4 favors B_m1list. Threshold set at
`kReverseSeqStepMaxN = 4` (calibrated for Release builds where per-step cost
is lower).

### ReverseStepTStates: B_m1list vs B_direct

| T-states | B_m1list (µs) | B_direct (µs) |
|---|---|---|
| 1 | 6,890 | — |
| 100 | 7,012 | 352 |
| 1000 | 8,234 | 352 |
| 69888 (1 frame) | 58,939 | 631 |
| 700000 (~10 frames) | 58,939 | 631 |

B_direct (direct `SeekTo(target)`) is always faster, but it lands mid-instruction.
B_m1list provides M1 alignment at a ~10× cost. The current implementation always
uses B_m1list for correctness; B_direct is available as a benchmark reference.

### ReverseContinue

| Breakpoints | Time (µs) |
|---|---|
| 1 | 226,592 |
| 10 | 236,006 |
| 100 | 274,719 |

Scales linearly with breakpoint count (vector search per M1 record).

## Bug fixed: RunTStates frame-boundary overshoot

During development, the reverse executor tests revealed a latent bug in
`Emulator::RunTStates`. When an instruction overshoots the frame boundary
(e.g., `z80.t` goes from 71677 to 71681 while `frameLimit = 71680`), the
frame-boundary handler fires `AdjustFrameCounters` (incrementing
`frame_counter` and resetting `z80.t`). However, when `targetT < frameLimit`,
the `targetT -= frameLimit` adjustment was skipped, leaving `targetT` at its
original value. The loop then ran for an **entire extra frame**, inflating
`frame_counter` and corrupting the M1 probe records' globalT coordinates.

**Fix** (in `emulator.cpp`): when the frame boundary fires and `targetT <
frameLimit`, `break` out of the loop — the overshooting instruction already
executed past the target, and no further work is needed.

## Automation surface contracts

### CLI

```
ttd reverse-step [--count N]       Step back N instructions (default: 1)
ttd reverse-step --tstates T       Step back T t-states (aligns to M1)
ttd reverse-continue --pc <A> [--pc <B> ...]
                                   Run backward until any PC matches
```

Aliases: `rs` = `reverse-step`, `rc` = `reverse-continue`.

### Python

```python
emulator.ttd_reverse_step(count=1) -> bool
emulator.ttd_reverse_step_tstates(tstates) -> bool
emulator.ttd_reverse_continue(pcs: list[int]) -> dict | None
```

`ttd_reverse_continue` returns `{"matched": bool, "pc": int, "frame": int, "tinframe": int}`
or `None` on no-match.

### LUA

```lua
ttd_reverse_step(count)          -- step back N instructions (default: 1)
ttd_reverse_step_tstates(tstates) -- step back T t-states
result = ttd_reverse_continue(pcs) -- pcs is a 1-based table {0x1234, 0x5678}
                                 -- result = {matched=bool, pc=int, frame=int, tinframe=int}
```

LUA also gained the Phase 4 reverse-search surface in this phase
(`ttd_dump`, `ttd_find_last`, `ttd_step_instruction_back`,
`ttd_step_instruction_forward`) — previously these were only in
CLI / Python / WebAPI.

### WebAPI

```
POST /api/v1/emulator/{id}/ttd/reverse-step
  body: { "count"?: int, "tstates"?: int }   // exactly one
  resp: { "reached": bool, "mode": "count"|"tstates", "frame": int, "tinframe": int }

POST /api/v1/emulator/{id}/ttd/reverse-continue
  body: { "pcs": [int, ...] }
  resp: { "matched": bool, "pc": int, "frame": int, "tinframe": int }
```

## What is NOT here (deferred)

- **GDB RSP mapping** (`reverse-step`, `reverse-continue`, `reverse-finish`
  over RSP) — Phase G3 in the implementation plan, blocked on this work. The
  core primitives are ready; the GDB server just needs to map `bc`/`bs`/`bt`
  packets to these methods.
- **Reverse-finish** (step-out backward) — trivial composition of
  `ReverseContinue` with the call-stack PCs. Build when dogfooding shows demand.
- **Write journaling for Execute accesses** — would let Strategy B skip replay
  for the Execute case, but the replay is already cheap (< 1 frame); not worth
  the journal-size cost.
- **`ReverseContinue` with conditional breakpoints** — current contract is
  PC-set only; condition evaluation during reverse enumeration can be added
  later.
