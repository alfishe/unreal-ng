# Baseline before the contention rework (phase 1a) and the result of phase 1b

**Date:** 2026-09-28 · **Code:** baseline `2b6b7d47` / `98e7c3f1` (the fingerprint memory fix), branch
`m1-contention` · **Belongs to:**
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

Determinism: the first version hashed RAM the mix never writes, which keeps what earlier emulator
instances in the process left there - the state hashes then depended on test order. The hashed regions
are now filled with a fixed pattern first, and the baseline was re-recorded on the pre-rework code
(`98e7c3f1`, passes there in shuffled combined runs). Runtime about 0.35 s (55 emulator instances;
justified in the test's comment).

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

## 3. After phase 1b (bus interfaces, M1 contention)

### 3.1 Fingerprints

Exactly the intended rows changed, and only their timing; every state hash and every other row is
bit-identical:

| Model | codeContended | codePage7 | allRam1 |
|:--|--:|--:|--:|
| 48K | 18675 → 21127 | - | - |
| 128K, +2 | 19002 → 21521 | 19002 → 21521 | - |
| +2A, +3 | 18472 → 21323 | 18472 → 21323 | 21328 → 25133 |

The rows keep their former values in a comment in the test. The M1, operand, prefix and selection cases
are pinned individually in `contention_test.cpp` (`M1_*`, `MemoryInterfaceSelection_Test`).

### 3.2 Performance

The host was under heavy load from other sessions (load average 100-150 on 20 cores), so the numbers
below are **interleaved A/B runs** of the baseline and the new binary, 3 rounds x 3 repetitions each,
median of the round medians. Absolute values are higher than in §2; the ratios are what counts.

| Benchmark | Baseline | Phase 1b | Change |
|:--|--:|--:|--:|
| Pentagon, code at #8000 | 21.8 µs | 20.7 µs | **-5 %** |
| Pentagon, code at #6000 | 21.4 µs | 20.9 µs | **-2 %** |
| 48K, code at #8000 | 38.8 µs | 40.0 µs | +3 % |
| 48K, code at #6000 (now contended fetches) | 38.3 µs | 44.9 µs | +17 % |
| +3, code at #8000 | 20.6 µs | 22.2 µs | +8 % |
| +3, code at #6000 (now contended fetches) | 20.4 µs | 26.2 µs | +28 % |
| `BM_FrameCostNormal` (48K frame, ROM idle loop) | 1146 µs | 1143 µs | 0 % |

Gate met: the uncontended machine is faster (its per-access contention branch is gone from `rd` / `wd`
and its per-port contention call from `in` / `out`).

The contended machines pay for two things. The emulation itself: the fetch waits are now computed
(`UlaContention::DelayAt` for every access to a contended slot, `% tstatesPerLine` and `% 8` each time),
which is the +17 % / +28 % on the contended-code runs. And the wrapper layer: on the contended interfaces
an access is two indirect calls (the interface, then the virtual plain read), the +3 % / +8 % on runs that
touch contended memory rarely.

Ideas backlog (not done; naive first, then measure):

- Call the plain access non-virtually in the contended wrapper (`this->Memory::MemoryReadFast`): the only
  override (`ScorpionMemory`) belongs to a machine without contention; needs a test that every contended
  model's memory object is the base `Memory`.
- A per-line wait table in `UlaContention` (224 / 228 entries) instead of the two modulo operations, or
  a cached "next contended T" window so accesses outside the paper skip the arithmetic entirely.
  SIMD-CANDIDATE: none (scalar lookups).

## 4. How to reproduce

```bash
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DTESTS=ON -DBENCHMARKS=ON
ninja -C build-rel core-tests core-benchmarks
./build-rel/bin/core-tests --gtest_filter='ContentionRegression_Test.*'
UNREAL_CONTENTION_GOLDEN_PRINT=1 ./build-rel/bin/core-tests --gtest_filter='ContentionRegression_Test.*'   # re-record
./build-rel/bin/core-benchmarks --benchmark_filter='BM_ContentionInstructionMix|BM_FrameCostNormal' \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
```
