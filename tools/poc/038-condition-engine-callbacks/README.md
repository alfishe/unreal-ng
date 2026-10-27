# 038 — condition-engine-callbacks

Feature: [breakpoint taps](../../../docs/inprogress/2026-10-26-breakpoint-taps/README.md) phase 5 and the condition
engine F1 of [conditional breakpoints](../../../docs/inprogress/2026-08-17-conditional-breakpoints/design.md).

Taps (non-stopping breakpoints) now serve haptics, analyzers and game events; the next step puts conditions on them
(`when: "A==3"`) and lets scripts react to a hit (a Lua or Python callback). Both run on the emulation thread's hit
path, so their cost decides what is allowed there and what must be deferred.

## Goals

1. **G1 — conditions.** What does one evaluation cost: the fast predicates (`reg == k`, a masked flag, `MWV == k`, a
   byte compare) and the RPN VM of [performance.md](../../../docs/inprogress/2026-08-17-conditional-breakpoints/performance.md)
   §2 S2 / S4 over the canonical conditions of design.md §4.2, mostly false (the real case) and at random.
2. **G2 — Lua callbacks.** What does one call cost from the emulation thread:
   - a state owned by the calling thread (raw C API and sol2), a trivial and a realistic body, a body that allocates
     (GC: p99 and max);
   - the instruction-count hook as a deterministic budget (its overhead; how fast it stops a runaway `while true`);
   - a synchronous round trip to a worker thread that owns the state (today's AutomationLua model);
   - an async hand-off (a queue the worker drains later).
3. **G3 — Python callbacks.** One call with the GIL held, with `PyGILState_Ensure` from a foreign thread while the GIL
   is free, and while another thread runs Python (the GIL hand-off latency).
4. **G4 — measuring a callback.** What the per-call timing costs (two clock reads), so a budget check can stay on.

## Requirements

- macOS arm64 (this host, M-series); the numbers that decide are ratios and orders of magnitude, so one host is enough
  for the POC; x86-64 Linux (.238) if a result is close to a limit.
- The vendored Lua 5.4.7 and sol2 3.5.0 (`core/automation/lua/lib`), the CPython the embedding uses (3.12 here).
- Per-call distributions (p50, p99, max), not only means, for everything that can stall (GC, GIL, thread hand-off).

## Constraints

- No change to `core/`; POC code is standalone (`cpp/`), built into `scratch/2026-10-26-poc-038/`.
- The evaluator here is a prototype of the shapes and the VM, hand-compiled programs (the parser runs at set time and
  is not on the hit path).
- The machine is shared and loaded: interleaved rounds, the lower quartile and the medians reported, not one run.

## Acceptance criteria / expected results

| Question | Expected | Decides |
|:--|:--|:--|
| fast predicate | ≤ 5 ns | conditions on taps cost nothing measurable per hit |
| RPN VM, 3-15 ops | ≤ 30 ns | the VM is the general evaluator, no JIT |
| Lua, own state, trivial body | ≤ 0.5 µs | a synchronous Lua callback per hit is affordable at human-scale and event rates (≤ 1000 hits / frame ≈ 0.5 ms) |
| Lua count hook | ≤ 10 % on a realistic body; stops `while true` within 1 ms | the instruction budget is the deterministic limit (not wall clock) |
| Lua GC | max ≤ 50 µs with a small table per call | whether the hook state needs a GC step per frame instead |
| Round trip to the Lua worker | 5-50 µs | synchronous calls on the worker thread are too slow per hit; only async or the own state |
| Async hand-off | ≤ 50 ns | deferred callbacks for every language cost nothing on the hit path |
| Python, GIL free | 1-5 µs | Python per-hit synchronous only with care |
| Python, GIL held by a busy thread | up to the switch interval (5 ms) | Python callbacks never synchronous on the emulation thread: deferred only |
| Two clock reads | ≤ 50 ns | the per-call budget check stays always on |
