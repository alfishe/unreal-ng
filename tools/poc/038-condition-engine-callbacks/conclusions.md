# Conclusions

| Question | Expected | Measured | Met |
|:--|:--|:--|:--|
| fast predicate | ≤ 5 ns | 1.1-1.8 ns | yes |
| RPN VM, 3-15 ops | ≤ 30 ns | 8.6-12.7 ns (4-10 ops), 15-16 ns at 22 ops | yes |
| Lua, own state, trivial body | ≤ 0.5 µs | 19 ns (31 ns realistic; sol2 +12 ns) | yes, 20× under |
| Lua count hook overhead | ≤ 10 % | +26-34 % (+8-10 ns) | no in relative terms, yes in absolute terms |
| count hook stops `while true` within 1 ms | ≤ 1 ms | exact at the budget: 100 k instr = 0.23 ms, ~430 k instr = 1 ms | yes |
| Lua GC max, a small table per call | ≤ 50 µs | 21-43 µs | yes |
| round trip to the Lua worker | 5-50 µs | p50 3-4 µs, p99 16-18 µs, max to 0.3 ms | yes: too slow per hit |
| async hand-off | ≤ 50 ns | 1.6-4 ns q1 | yes |
| Python, GIL free | 1-5 µs | 3 µs with Ensure/Release, 125 ns with a kept thread state | better than expected |
| Python with a busy Python thread | up to 5 ms | p99 6.3 ms (0.7 ms at a 0.5 ms switch interval) | yes: never synchronous by default |
| two clock reads | ≤ 50 ns | 34 ns | yes |

## Answers

- **G1.** Conditions cost nothing measurable on taps and stay within the R02 P-2 budgets everywhere. The full-range
  exec worst case is 1.4 % of a core at real time. Fast shapes plus the RPN VM are enough, and no JIT is needed.
- **G2.** A synchronous Lua callback is affordable per hit (~40 ns with the budget hook) only in a Lua state owned by
  the emulation thread. A round trip to today's worker state costs 100-500× more. The count hook is a deterministic
  per-call budget that stops a runaway exactly.
- **G3.** Python is deferred by default. Synchronous Python is fast only while no other Python thread runs, which
  the emulator cannot guarantee; one GIL wait can exceed a frame.
- **G4.** A per-call clock pair is affordable (34 ns). The cheaper default is the instruction count per call plus one
  clock pair per frame's batch.
