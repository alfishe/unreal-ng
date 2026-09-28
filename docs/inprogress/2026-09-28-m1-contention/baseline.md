# Baseline before the contention rework (phase 1a)

**Date:** 2026-09-28 · **Code:** `e521eb03` (master), branch `m1-contention` · **Belongs to:**
[design.md](design.md) §8 suite F and §9.

Recorded on the code before any change, so the rework can be checked against it: what must stay
identical, what may change, and by how much.

## 1. Timing fingerprints (suite F)

`core/tests/emulator/video/contentionregression_test.cpp` runs one instruction mix (data reads and writes,
`PUSH` / `POP`, `EX (SP),HL`, `INC (HL)`, indexed read and write, `LDI`, `IN` / `OUT` on port `#40FE`, `JR`)
for 1500 instructions from INT + 14000 with interrupts off, on every creatable model, in five placements.
Each run records the total T-states, a hash of the per-instruction T-states, and a hash of the registers
and data memory afterwards. The full table (45 rows, with hashes) is in the test.

Total T-states:

| Model | free | dataContended | codeContended | codePage7 | allRam1 |
|:--|--:|--:|--:|--:|--:|
| Pentagon, ATM710, ATM3, Scorpion, ProfScorp, Profi | 18472 | 18472 | 18472 | 18472 | - |
| 48K | 18675 | 20962 | 18675 | - | - |
| 128K, +2 | 19002 | 21332 | 19002 | 19002 | - |
| +2A, +3 | 18472 | 21328 | 18472 | 18472 | 21328 |

| Placement | Code | Data (HL, DE, IX, IY) | Stack |
|:--|:--|:--|:--|
| free | #8000 | #9000-#A2FF | #B000 |
| dataContended | #8000 | #4000-#52FF | #5F00 |
| codeContended | #6000 | #9000-#A2FF | #B000 |
| codePage7 | #C000, page 7 (paged via #7FFD) | #9000-#A2FF | #B000 |
| allRam1 | #0000, +2A / +3 layout 1 (pages 4-7, via #1FFD) | #9000-#A2FF (page 6) | #B000 |

Reading the table:

- 48K / 128K / +2 "free" is longer than the Pentagon's only by the I/O contention of the two port
  accesses per loop (`#40FE`: high byte in contended memory); the gate array has no I/O contention, so
  the +2A / +3 match the Pentagon.
- "codeContended" and "codePage7" equal "free" on the ULA and gate array models: opcode fetches are not
  contended yet. These rows, and only these, are what the M1 rework must change, and the expected
  increase is the oracle's sum of fetch waits. Every other row must stay bit-identical.
- "allRam1" on the +2A / +3 equals "dataContended" because layout 1 puts the data (page 6) in contended
  memory; the code at #0000 (page 4) will add fetch waits after the rework.
- The state hashes differ between some models with the same timing (ATM710, ATM3 vs the Pentagon): the
  `IN A,(C)` from `#40FE` returns a machine-specific value, which the loop stores. Not a contention matter.

Found while recording: the raw `Memory::SetRAMPageToBank3` does not change what the CPU fetches on the
ATM710 (the CPU kept running the previous mapping and left the program). The test pages through the
machine's ports, as software does, and asserts that the PC stays inside the program so a wrong placement
cannot produce a silently meaningless fingerprint.

Determinism: the suite passes under `--gtest_repeat=3 --gtest_shuffle` together with the existing
contention suites. Runtime about 0.35 s (55 emulator instances; justified in the test's comment).

## 2. Performance (§9 gate)

Apple M1 Ultra, Release build (`-DTESTS=ON -DBENCHMARKS=ON`), 5 repetitions, CPU-time median.

`BM_ContentionInstructionMix` (`core/benchmarks/emulator/contention_benchmark.cpp`, new): 1000
instructions of the same mix through `Z80Step`, 0.2 s warm-up.

| Model | code at #8000 | code at #6000 (contended on 48K / +3) | cv |
|:--|--:|--:|--:|
| Pentagon | 19.7 µs | 20.1 µs | ≤ 1.4 % |
| 48K | 36.2 µs | 36.3 µs | ≤ 1.0 % |
| +3 | 19.2 µs | 19.0 µs | ≤ 0.8 % |

`BM_FrameCostNormal` (48K, full frame through the main loop): 1081 µs median, cv 0.54 %.

Gate for the rework: the Pentagon rows within noise of these values or faster; the 48K / +3 rows
reported.

Observation for phase 1b: the same mix costs the 48K about 1.85 times what it costs the Pentagon and the
+3, with no contended memory access involved in the #8000 run. The difference is on the port path
(`IN` / `OUT` on `#40FE`: the 48K port decoder and `GetIOContentionDelay`), which §6.4 moves into the
selector; it is measured again there.

## 3. How to reproduce

```bash
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DTESTS=ON -DBENCHMARKS=ON
ninja -C build-rel core-tests core-benchmarks
./build-rel/bin/core-tests --gtest_filter='ContentionRegression_Test.*'
UNREAL_CONTENTION_GOLDEN_PRINT=1 ./build-rel/bin/core-tests --gtest_filter='ContentionRegression_Test.*'   # re-record
./build-rel/bin/core-benchmarks --benchmark_filter='BM_ContentionInstructionMix|BM_FrameCostNormal' \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
```
