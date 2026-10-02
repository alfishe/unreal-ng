# Performance guidelines: hot paths, zero-cost features, A/B measurement

How to add a feature to the emulator core without slowing down the machines
that do not use it, and how to prove it with a measurement. Companion to
[coding-guidelines.md](coding-guidelines.md) ("Performance & Memory
Management").

## Contents

- [1. Where the time goes](#1-where-the-time-goes)
- [2. Rules](#2-rules)
- [3. Patterns in this code base](#3-patterns-in-this-code-base)
- [4. Measuring: the A/B procedure](#4-measuring-the-ab-procedure)
- [5. Worked example: the per-step work gate](#5-worked-example-the-per-step-work-gate)
- [6. Known candidates](#6-known-candidates)
- [7. References](#7-references)

## 1. Where the time goes

A hot path is code that runs so often that a few CPU cycles there show up in
the frame time. For a 3.5 MHz machine in one 20 ms frame:

| Path | How often per frame | Where |
|:--|:--|:--|
| Instruction step | ~15 000-20 000 | `Z80::StepInstruction`, `Z80::Z80FrameCycle` |
| M1 (opcode fetch) | about one per instruction, more with prefixes | `Z80::m1_cycle` |
| Memory access | 3-5 per instruction | `Z80::rd` / `wd` → `Memory::*MemIf` |
| Port access | a few hundred | `Z80::in` / `out` → the port decoder |
| Frame | 1 | `Core::AdjustFrameCounters`, `MainLoop::OnFrameEnd` |

A host frame of a classic machine costs about 1.1-1.9 ms of CPU
(`BM_HostFrame_*`, Apple M-series, 2026-09-29). One host cycle per
instruction is about 0.3 %, so a few extra tests per instruction are at the
edge of what a careful A/B can see (section 5: 0 to +1.8 % depending on the
run).

## 2. Rules

1. **A machine pays nothing for a feature it does not use.** Prefer a
   decision made once (an interface selected at a switch, a template
   instance, a function pointer) to a test repeated on every call.
2. **Several rare checks on one hot path become one combined gate:** one word
   of bits, one load and one branch; the rare work lives in an out-of-line
   function that re-checks each bit. A new feature adds a bit, not a test.
3. **Keep the hot function small.** Put rare code out of line and mark the
   branch `[[unlikely]]`. A regression often comes from code layout (the
   compiler spills registers or grows the loop) more than from the branch
   itself.
4. **Use a template parameter instead of a runtime flag inside a hot
   function** when the flag is fixed for the whole run of that function.
5. **Naive first, then measure.** Build the simple version, measure it with
   the procedure in section 4, and put further optimization ideas in the
   feature's backlog rather than into the first version. Mark scalar loops
   that would benefit from SIMD with `// SIMD-CANDIDATE(<id>): ...`.
6. **Every change to a hot path gets an A/B measurement** (section 4) in the
   commit or the design document, including when the result is "no change".

## 3. Patterns in this code base

| Pattern | Example | What it saves |
|:--|:--|:--|
| Interface selected at a switch | `Core::SelectMemoryInterface` picks Fast / Debug / Contended / Overlay memory interfaces (`core/src/emulator/cpu/core.cpp`) | no debug, contention or overlay test on a machine that has none |
| Device on a bus overlay | `HostBusOverlay` (NeoGS ZX-DMA, TSConf FM window), `MemoryWaitOverlay` (turbo waits) — `core/src/emulator/memory/` | the overlay interfaces are used only while an overlay is installed |
| Combined gate | `EmulatorContext::stepWork` + `Z80::StepInstructionWithWork` | one load per instruction for TTD input, a machine's interrupt source, a machine engine (and RZX later) |
| Template instead of a flag | `Z80::ProcessInterruptsImpl<bool UseSource>`, `Memory::MemoryReadContended<Plain, Stats>` | the classic path compiles without the test; statistics exist only in the debug instance |
| Nullable model hook | `Z80::machineM1Hook`, `Z80::portInterceptor` | one pointer test; acceptable where the path is not per instruction, or as a first version |
| Lazy allocation | memory tracker, trace buffers (coding-guidelines) | memory and cache footprint only when the feature is on |

A combined gate in code (`core/src/emulator/cpu/z80.cpp`):

```cpp
Z80::StepResult Z80::StepInstruction(bool skipBreakpoints)
{
    if (const uint32_t work = _context->stepWork.load(std::memory_order_relaxed)) [[unlikely]]
        return StepInstructionWithWork(work, skipBreakpoints);  // out of line: all rare jobs

    // the classic step, with nothing added
    ...
}
```

A job owns its bit and keeps it true while it needs the slow path:

```cpp
void Z80::SetMachineStepHook(IMachineStepHook* hook)
{
    _machineStepHook = hook;
    _context->SetStepWork(EmulatorContext::kStepWorkMachineStep, hook != nullptr);
}
```

## 4. Measuring: the A/B procedure

Wall-clock numbers on a busy machine are meaningless: the development Mac
often runs many sessions at once (load average 100+), which moves frame times
by 3x.

1. **Two builds, same toolchain.** A = the commit before the change, B = the
   change. Use detached worktrees so nothing else differs:
   ```bash
   git worktree add --detach scratch/wt-bench-A <before>
   git worktree add --detach scratch/wt-bench-B <after>
   # in each: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBENCHMARKS=ON
   #          ninja -C build core-benchmarks
   ```
   For a merge commit, A is its first parent, so the difference is exactly
   the merged change.
2. **Wait for a quiet machine.** `sysctl -n vm.loadavg`; run only when the
   1-minute value is below ~12. A script can wait:
   ```bash
   while [ $(sysctl -n vm.loadavg | awk '{print int($2)}') -ge 12 ]; do sleep 60; done
   ```
3. **Run from the build's `bin` directory** (the benchmarks find `unreal.ini`
   and ROMs there), with CSV output:
   ```bash
   cd scratch/wt-bench-A/build/bin
   ./core-benchmarks --benchmark_filter='BM_HostFrame_' --benchmark_format=csv > ../../../bench-A-1.csv
   ```
   Names carry a suffix (`BM_HostFrame_48K_Fast/iterations:1000`), so do not
   anchor the filter with `$`.
4. **Interleave and reverse.** Rounds A, B, A, B, A, B, then B, A, B, A.
   Drift (load, heat) then hits both sides; a real difference keeps its sign
   in both orders.
5. **Compare the CPU time** (`cpu_time` column): the minimum per side and the
   mean of the paired differences per round. Round-to-round noise here is
   about ±2 %; a shift of 1 % whose sign holds in most pairs, in both orders,
   is real.
6. **Record the result** in the design document or the commit message: the
   commits compared, the load, the table of rounds.

Useful benchmarks:

| Benchmark | File | Measures |
|:--|:--|:--|
| `BM_HostFrame_{48K,Pentagon,Scorpion}_{Fast,Debug}` | `core/benchmarks/emulator/memory/hostbusoverlay_benchmark.cpp` | one host frame of a classic machine: the per-instruction and per-access paths |
| `BM_HostFrame_Pentagon_Overlay_*`, `BM_HostFrame_NeoGS_*` | same | the overlay path |
| `z80_overhead_attribution.cpp` | `core/benchmarks/emulator/` | isolated costs: member-pointer calls, `std::function` tests, dereferences |
| `turbo_frame_benchmark.cpp`, `contention_benchmark.cpp` | `core/benchmarks/emulator/` | turbo mode, ULA contention |
| `TTDBench_Test` (TTD CI gate) | `core/tests/debugger/ttd/bench/ttdbench_test.cpp` | TTD bytes per stream and counted capture work against `testdata/ttd/bench/v1-ci-gate.txt`; no clock, so host load cannot fail it ([`tools/verification/ttd-bench/`](../../tools/verification/ttd-bench/README.md)) |

## 5. Worked example: the per-step work gate

PLAN #60(a) added a machine interrupt source and a machine engine hook for
TSConf and the Sprinter. The first version tested two nullable pointers on
every instruction (`interruptSource` in `ProcessInterrupts`, `machineStepHook`
in `OnCPUStep`), on machines that use neither.

**First run.** A = `55f6dea3` (before), B = `69a3aee1` (the change), load 8-10
for rounds 1-3 (A first), 17-30 for rounds 4-5 (B first); CPU time per frame,
B vs A:

| Frame | Rounds 1-3 | Rounds 4-5 | Mean |
|:--|:--|:--|--:|
| 48K fast | +0.7 −0.6 +3.5 | +2.9 −0.5 | +1.2 % |
| 48K debug | +2.9 +0.2 +2.2 | +2.0 +0.3 | +1.5 % |
| Pentagon fast | +1.3 −0.1 +2.4 | +1.4 −0.6 | +0.9 % |
| Pentagon debug | +1.2 +1.4 +2.5 | +1.2 −1.1 | +1.0 % |
| Scorpion fast | +2.7 +2.5 +1.8 | +0.4 −0.3 | +1.4 % |
| Scorpion debug | +3.0 +1.1 +2.1 | +2.3 +0.4 | +1.8 % |

The fix applied rules 2-4: both tests moved behind the existing TTD input
test, which became the bit set `stepWork`; the rare step moved out of line
(`StepInstructionWithWork`); `ProcessInterrupts` became a template so the
classic instance has no source test.

**Second run, with the fix.** C = the fix (branch `step-work-gate` on
`d9e7e9f7`); A, B, C interleaved, rounds 1-3 in the order A, B, C and 4-5 in
C, B, A; load 9-11 throughout (steadier than the first run). Mean of the five
paired differences:

| Frame | B vs A | C vs A | C rounds 1-5 |
|:--|--:|--:|:--|
| 48K fast | −0.2 % | −0.1 % | +0.5 +0.5 +0.5 −0.1 −1.7 |
| 48K debug | +0.2 % | +0.2 % | +0.2 −0.1 +1.7 +0.2 −1.2 |
| Pentagon fast | +0.3 % | −0.1 % | −0.3 +0.6 −0.1 −0.3 −0.2 |
| Pentagon debug | −0.2 % | −0.7 % | −0.4 −1.2 −1.9 +0.3 −0.1 |
| Scorpion fast | −0.0 % | −0.8 % | +0.5 −0.3 −0.4 −0.0 −4.0 |
| Scorpion debug | +0.6 % | +0.2 % | −0.0 −0.2 +0.5 +0.6 +0.1 |

What this says, honestly:
- The two tests cost **0 to about 1 %** depending on the run: the steady
  second run shows B within noise except Scorpion debug (+0.6 %), the first
  run showed +0.9 to +1.8 %. Such a small cost needs repeated runs to see.
- The gate (C) is **within noise of the code without the feature** in both
  its rounds and its minimums (Pentagon fast 1560 vs 1563 µs, Scorpion debug
  1878 vs 1878 µs).
- The reason to prefer the gate is not only the number: it keeps the classic
  step identical to what it was, and the next feature (RZX) adds a bit instead
  of a third test.

## 6. Known candidates

Hot-path tests that could join a combined gate (not changed yet; measure
before and after, section 4):

| Where | Tests | Frequency |
|:--|:--|:--|
| `Z80::InstructionStartObserved` | `m1TraceHook`, `ttdCoverageActive`, `ttdProbe.IsArmed()` | every instruction start |
| `Z80::m1_cycle` | `machineM1Hook` (before and after the fetch) | every M1 |
| `Z80::rd` / `wd` | `busTraceHook` (`std::function`) | every memory access |
| `Z80::Z80FrameCycle` | `emulator->IsPaused()` | every instruction |

## 7. References

- Agner Fog, *Optimizing software in C++* — branch prediction, code layout,
  the cost of indirect calls: <https://www.agner.org/optimize/>
- Denis Bakhvalov, *Performance Analysis and Tuning on Modern CPUs* —
  measurement methodology, noise, code layout: <https://easyperf.net/>
- Google Benchmark user guide, "Reducing variance":
  <https://github.com/google/benchmark/blob/main/docs/reducing_variance.md>
- Chandler Carruth, "Tuning C++: Benchmarks, and CPUs, and Compilers! Oh My!"
  (CppCon 2015).
- In this repository: [core/benchmarks/README.md](../../core/benchmarks/README.md),
  [core/tests/README.md](../../core/tests/README.md) (test time budget).
