# Performance investigation: closing the gap vs the original interpreter

## Context

After the zero-copy register file work ([register-zero-copy-tdd.md](register-zero-copy-tdd.md)),
the real, integrated emulator was still measurably slower than the original
hand-written interpreter it replaced. Synthetic microbenchmarks
(`core/benchmarks/emulator/cpu/z80_engine_bus_benchmark.cpp`) don't capture
this gap well on their own — they exercise a tiny 6-opcode loop, not a real
ROM's wide instruction mix — so this investigation measured the *real*
integrated cost directly (`BM_Z80RealFrameCost`, a live 48K emulator instance
in turbo mode) and, separately, the same benchmark run against `master` at
`5f965a07` (the last commit before any of this vendoring work), to get a
true apples-to-apples baseline:

| Stage | `BM_FrameCostTurbo` / `BM_Z80RealFrameCost` | vs. original interpreter |
|---|---|---|
| Original interpreter (`master` @ `5f965a07`) | 239 µs/frame | 1.00x |
| Non-zero-copy engine integration | 453 µs/frame | 1.90x slower |
| + zero-copy register file | 387 µs/frame | 1.63x slower |

This document covers finding and fixing the next chunk of that gap.

## Method

Profiled `BM_Z80RealFrameCost` with macOS's built-in `sample` tool
(`sample <pid> <duration>`, 1 ms interval) rather than `xctrace`/Instruments:
an `xctrace record --launch` attempt in this (headless, non-interactive)
environment hung indefinitely at 0% CPU — almost certainly blocked on a
one-time developer-tools permission dialog that has no way to be answered
here. `sample`'s plain-text call tree and its "Sort by top of stack, same
collapsed" section (a genuine per-function self-time ranking, not just
total-including-children) turned out to be sufficient for precise numbers
without needing the GUI tool.

## Finding 1: no LTO — real function calls for what should be inlined field reads

`Z80CpuTstates`, `Z80CpuGetReg`, `Z80CpuSetTstates`, `Z80CpuIntPossible` etc.
are plain `extern "C"` one-line accessors (`z80cpu.h`/`z80cpu.cpp`, e.g.
`uint32_t Z80CpuTstates(const Z80CPU* cpu) { return cpu->t; }`), called from
a *different* translation unit (`core/src/emulator/cpu/z80.cpp`).
**`CMAKE_INTERPROCEDURAL_OPTIMIZATION` was not set anywhere in this project's
CMake.** Without it, the compiler cannot inline these across TUs, so every
call pays full call overhead for what should compile to a single memory
load. The profile confirmed it: `Z80CpuTstates` alone showed up with real,
non-trivial self-time in the ranked list — for a one-line accessor, that
self-time should be ~0.

Self-time breakdown at the time of this finding (5693 samples, main thread,
real 48K ROM execution, turbo mode):

| Category | Self samples | % of frame time |
|---|---|---|
| `Z80EngineBridge::MemRead` trampoline itself | 962 | ~17% |
| Genuine opcode-body execution (19 different `op_XX` handlers) | 1071 | ~19% |
| `Z80::Z80Step` orchestration (breakpoint/ROM-paging/analyzer checks) | 416 | ~7% |
| `Z80EngineBridge::Contend` + `Z80ContendSlow` | 396 | ~7% |
| `Z80::ExecuteEngineInstruction` (bridge) | 332 | ~6% |
| `Z80::Z80FrameCycle` | 188 | ~3% |
| `Z80Cb::Step` (engine's callback-bus dispatcher) | 167 | ~3% |
| `Z80::OnInstructionFetch` | 147 | ~3% |
| `Z80::ProcessInterrupts` | 107 | ~2% |
| Other CPU-related (`MemWrite`, `Tstates` get/set, `IntPossible`, `FinishFlags`, `ddfd_prefixes`, analyzer-manager lookup) | ~333 | ~6% |
| Sound/screen/tape/disk per-frame work (not CPU at all) | ~1645 | ~29% |

The bridge/dispatch layer as a whole (`MemRead` + `Contend`/`ContendSlow` +
`Z80Step` + `ExecuteEngineInstruction` + `Z80Cb::Step` + `Tstates` get/set,
≈2065 samples, ~36% of total frame time) cost roughly *twice* as much as
actual opcode execution (~19%) — and a meaningful chunk of that bridge cost
was exactly the kind of trivial cross-TU accessor call LTO exists to
eliminate.

### Fix

Added an opt-in `ENABLE_LTO` CMake option (root `CMakeLists.txt`), scoped to
the `core` static library, `core-tests` and `core-benchmarks` targets only —
not the whole unified build (Qt apps, automation modules and their
third-party dependencies like drogon/jsoncpp/lua aren't verified LTO-clean,
and this option exists to help exactly the vendored-engine/bridge boundary,
not to LTO everything). Uses `check_ipo_supported()` (must run after
`project()` — CMake needs a language already enabled to check IPO support)
and warns rather than hard-errors if the toolchain doesn't support it.

```
cmake -S . -B <build-dir> -G Ninja -DTESTS=ON -DBENCHMARKS=ON -DENABLE_LTO=ON
```

### Results

Zero regressions: full `core-tests` still green (3315/3317, same 2
pre-existing skips), `FusePhase_Test.AllOpcodes` still 1356/1356, zero build
warnings.

| Benchmark | Before LTO | After LTO | Δ |
|---|---|---|---|
| `BM_Z80EngineFlatBus` (ceiling, unaffected) | 4.42 ns | 4.36 ns | -1% (noise) |
| `BM_Z80EngineCallbackBus` (no register API touched) | 5.92 ns | 4.99 ns | **-16%** |
| `BM_Z80EngineCallbackBusAttached` (what `z80.cpp` uses) | 5.93 ns | 5.02-5.07 ns | **-15%**, now close to the flat-bus ceiling |
| **`BM_Z80RealFrameCost`** (real ROM, turbo mode) | 387 µs | **342 µs** | **-11.6%** |

Updated cumulative picture:

| Stage | Frame cost | vs. original interpreter |
|---|---|---|
| Original interpreter (`master` @ `5f965a07`) | 239 µs | 1.00x |
| Non-zero-copy engine integration | 453 µs | 1.90x slower |
| + zero-copy register file | 387 µs | 1.63x slower |
| **+ LTO** | **342 µs** | **1.43x slower** |

## Finding 2: `MemIf` dispatch pays two indirections, not one (not yet fixed)

`Z80::MemoryReadNoContention` calls `(_memory->*MemIf->MemoryRead)(addr, isExecution)`
— a member-function-pointer call. But `Memory::MemoryReadFast`/
`MemoryReadDebug` are `virtual` (genuinely needed: `ScorpionMemory` overrides
`MemoryReadFast` for plane-notification timing —
`core/src/emulator/memory/scorpion/scorpionmemory.{h,cpp}`). A
pointer-to-member-function targeting a virtual method costs **the
member-pointer dispatch *and* a vtable lookup inside it** — two
indirections per memory access, for every model, even the ones (Pentagon,
48K, 128K — everything this investigation's benchmarks actually exercise)
that never need the virtual override.

Not fixed yet: this needs a devirtualized fast path for the common case
(cache "is this a plain `Memory`, not a `ScorpionMemory`" once, call a
non-virtual method directly when true) without breaking Scorpion's override
or the existing `FastMemIf`/`DbgMemIf` selection (`Core::UseFastMemoryInterface`/
`UseDebugMemoryInterface`) — a real (if scoped) correctness-risk change,
unlike the LTO fix above. Left as a follow-up; see [TODO.md](TODO.md).

## Other minor observations, not yet acted on

- `busTraceHook` (a `std::function`, null in production) is checked
  unconditionally on every single memory read/write inside
  `MemoryReadNoContention`/`MemoryWriteNoContention`. Cheap per-call, but
  100% wasted in production; could be gated behind a cached bool the way
  `_feature_calltrace_enabled`/`_feature_opcodeprofiler_enabled` already
  gate their own hot-path checks elsewhere in `z80.cpp`.
- `DebugManager::GetAnalyzerManager()` showed up with real self-time (27
  samples) even with no analyzers active — a per-instruction lookup cost
  that's paid regardless of whether anything is listening.
