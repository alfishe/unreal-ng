# P0 baseline: MemoryAccessTracker cost per turbo frame

This is the hot-path baseline that phase P0 needs before L0 adds the tracker level `facts`
([plan.md](../plan.md) P0, [design.md](../design.md) "L0 Access facts", the budget table). It also answers
the P0 question whether the Z80 predecoder already has an instruction class for stack and block accesses.

## Benchmark

`BM_MemoryTrackerFrame/machine:<m>/level:<l>` in
[`core/benchmarks/emulator/memory/memorytracker_frame_benchmark.cpp`](../../../../core/benchmarks/emulator/memory/memorytracker_frame_benchmark.cpp).

Each run does this:

1. It creates a fresh machine and loads the game snapshot.
2. It enables turbo and runs 50 warm-up frames with the tracker off.
3. It switches to the level and measures 500 turbo frames (`MainLoop::RunFramePublic`).

Every level runs the same emulated frames. `facts` will be level 4: one more enum value in the file.

| Level | Arg | What is on |
|-------|----:|-----------|
| off | 0 | debug mode off: the fast memory interface, what a user runs |
| debug | 1 | debug mode on, memory tracking off: the debug memory interface alone (the counters need it) |
| counters-z80 | 2 | tracking on, `TrackingMode::Z80AddressSpace`: the Z80 64K counters plus the physical ones |
| counters-phys | 3 | tracking on, `TrackingMode::PhysicalMemory`: only the physical counters |

The two counter levels are today's `counters`. The tracker has no separate physical-only switch apart from
the mode. Z80 mode updates both counter sets (`memoryaccesstracker.cpp` `TrackMemoryRead/Write/Execute`).
Physical mode skips the Z80 set. Breakpoints, the call trace and the opcode profiler are off at every level.

| Machine | Arg | Snapshot (testdata/) | Game | Accesses per frame (R / W / X) |
|---------|----:|----------------------|------|------------------------------:|
| 48K | 0 | `loaders/rzx/oracle/garfield-300.z80` | Garfield: SkoolKit's state after 300 frames of the Zero sample recording | 3 352 / 2 699 / 9 236 |
| 128K | 1 | `loaders/rzx/oracle/greenberet-300.z80` | Green Beret (128K, hardware mode 4), the same | 1 205 / 729 / 13 161 |
| PENTAGON | 2 | `loaders/z80/dizzyx.z80` | Dizzy X (Pentagon .z80, hardware mode 9) | 2 863 / 2 733 / 13 887 |

- X counts instruction fetches: opcodes and operands.
- `ram_exec_share` = 1.00 on all three: every executed byte is at #4000 or above, so the game runs, not the ROM.
- The Manic Miner snapshot of run B (`scratch/reaper-mm/`) is not in `testdata/`, so it is not used.

## Method

- Release build (`cmake-build-agent-release`, `-DBENCHMARKS=ON`), built with `tools/build/build.sh core-benchmarks`.
- Three processes, 15 repetitions each, in Google Benchmark's random interleaving. That gives 45 runs per case,
  with the case order shuffled in every round.
- The machine was not quiet; more rounds were run instead of waiting.
- CPU time per frame. Medians with p10-p90.
- The overhead is the median of the paired per-repetition ratios (case vs `off`, or vs `debug`, within the same
  repetition of the same process). The ± is the 95 % half-width of that median (1.2533 σ/√n × 1.96).

```bash
cd cmake-build-agent-release/bin
for r in 1 2 3; do
  ./core-benchmarks --benchmark_filter=BM_MemoryTrackerFrame --benchmark_repetitions=15 \
      --benchmark_enable_random_interleaving=true --benchmark_out=round$r.json --benchmark_out_format=json
done
```

| | |
|---|---|
| Host | Apple M1 Ultra, 20 cores, macOS 15.8.1, Apple clang 17.0.0 (clang-1700.6.4.2) |
| Date | 2026-10-25 23:41-23:44 |
| Tree | `5738a019a` (branch `analysis-layers-proposal`) plus the new benchmark file |
| Load (1 / 5 / 15 min) | start 47.2 / 62.2 / 63.1, end 41.6 / 55.4 / 60.3 |

## Results

Frame = one emulated frame in turbo. The emulated frame is about 20 ms, so the host cost is well under 1 ms.

| Machine | Level | median ms/frame | p10-p90 ms | vs off (paired) | vs debug (paired) |
|---|---|---:|---:|---:|---:|
| 48K garfield | off | 0.160 | 0.145-0.187 | - | - |
| 48K garfield | debug | 0.233 | 0.218-0.268 | +45.9 ± 6.1 % | - |
| 48K garfield | counters-z80 | 0.296 | 0.274-0.333 | +83.6 ± 6.9 % | +26.8 ± 3.9 % |
| 48K garfield | counters-phys | 0.288 | 0.263-0.315 | +77.5 ± 5.9 % | +21.2 ± 4.1 % |
| 128K greenberet | off | 0.148 | 0.137-0.167 | - | - |
| 128K greenberet | debug | 0.274 | 0.254-0.309 | +79.0 ± 7.5 % | - |
| 128K greenberet | counters-z80 | 0.336 | 0.309-0.373 | +123.4 ± 8.4 % | +22.7 ± 3.9 % |
| 128K greenberet | counters-phys | 0.323 | 0.303-0.367 | +120.1 ± 9.8 % | +19.2 ± 4.5 % |
| PENTAGON dizzyx | off | 0.153 | 0.138-0.169 | - | - |
| PENTAGON dizzyx | debug | 0.225 | 0.209-0.256 | +47.6 ± 6.3 % | - |
| PENTAGON dizzyx | counters-z80 | 0.302 | 0.281-0.337 | +100.2 ± 7.4 % | +34.5 ± 4.9 % |
| PENTAGON dizzyx | counters-phys | 0.305 | 0.275-0.323 | +94.5 ± 6.7 % | +29.4 ± 4.3 % |

The rounds agree with each other. Per process, `counters-z80` vs `off` was:

| Machine | Round 1 | Round 2 | Round 3 |
|---------|--------:|--------:|--------:|
| 48K | +86 % | +82 % | +84 % |
| 128K | +118 % | +131 % | +140 % |
| Pentagon | +106 % | +98 % | +101 % |

The spread between rounds is within each round's ± of 9-18 %.

### What this says

- **Counters cost about 2× a turbo frame against `off`**: +78 to +123 %.
  - The debug memory interface is the larger part: +46 to +79 %.
  - The counters add +19 to +35 % on top of `debug`.
- **The 128K debug path is the dearest**: +79 % against +46 to +48 % on the 48K and the Pentagon. Green Beret has
  the fewest data accesses of the three, so this cost lies in the 128K's debug interface itself, not in the
  counters. It is outside the analysis layers, but worth a look.
- **Physical mode is 3-5 % cheaper than Z80 mode** (it skips the Z80 set). The difference is about the size of
  the ±.
- **Budget for `facts`** (design.md: at most 25 % slower than `counters`): measured on this benchmark at level 4
  against level 2, paired within the same rounds. At these frame costs 25 % is about 0.07-0.08 ms per frame.

## Does the predecoder hold a stack / block instruction class?

**No.** The core has no predecoder at all, and nothing on the execution path classifies an instruction as a
stack or block access. The facts, with references at tree `5738a019a`:

- **No predecode in the CPU.**
  - `Z80Step` fetches the opcode and dispatches straight through the opcode table:
    `cpu.opcode = FetchM1()` at `core/src/emulator/cpu/z80.cpp:769`, `(normal_opcode[opcode])(&cpu)` at
    `z80.cpp:788`.
  - The only "decoded" state is `Z80DecodedOperation` (`core/src/emulator/cpu/z80.h:270`): the raw prefix,
    opcode and operand bytes, with no class.
  - The core performance review rules a predecoder out on purpose ("No predecode or recompilation",
    `docs/inprogress/2026-09-24-core-performance/unreal-ng-core-perf-and-gating-review.md:28`).
- **The predecoder that exists is a benchmark POC.**
  - `PredecodedOp` / `PredecodeCache` in `core/benchmarks/emulator/z80_predecode_benchmark.cpp:28` and `:52`.
    It is not linked into the core.
  - Its flags (`:40-46`) are `HAS_IMM8/16`, `READS_MEM`, `WRITES_MEM`, `BRANCHES` and `DDCB_FDCB`. It has no
    stack or block flag.
- **The memory path does not know the instruction.**
  - `Z80::rd(addr, isExecution)` (`z80.cpp:1494`) passes one bit: fetch or not.
  - `Memory::ReadDebugEffects` (`core/src/emulator/memory/memory.cpp:326-339`) passes `addr`, `value` and
    `m1_pc` to `TrackMemoryExecute` / `TrackMemoryRead`.
  - PUSH / POP / EX (SP) / LDIR use the same `rd` / `wd` as any data access:
    - `op_C1` pop bc and `op_C5` push bc: `core/src/emulator/cpu/op_noprefix.cpp:1021`, `:1069`;
    - `op_E3` ex (sp),hl: `op_noprefix.cpp:1405`;
    - `ope_A0` ldi and `ope_B0` ldir: `core/src/emulator/cpu/op_ed.cpp:690`, `:910`.
  - The interrupt and NMI pushes bypass `wd` and write through `MemIf->MemoryWrite` directly (`z80.cpp:1918-1919`,
    `:2144-2145`). An access context set by the instruction decoder would miss them. The INT / NMI accept code
    must set it itself.
- **The closest existing classes are debugger-side and only partial.**
  - The disassembler tables carry `OF_BLOCK`, `OF_CALL`, `OF_RET`, `OF_RST`
    (`core/src/debugger/disassembler/z80disasm.h:121-144`).
    - LDIR is tagged: `{ OF_BLOCK | OF_VAR_T, ... "ldir" }` at `core/src/debugger/disassembler/z80disasm.cpp:1081`.
    - PUSH and POP are `OF_NONE` (`z80disasm.cpp:263`, `:267`).
    - EX (SP),HL is only `OF_INDIRECT` (`:298`).
    - So "stack" cannot be derived from these flags: PUSH and POP would need a new flag.
  - The call trace's `Z80ControlFlowDecoder` (`core/src/debugger/disassembler/z80cfdecoder.h:145`,
    `Z80CFType` in `core/src/emulator/memory/calltrace.h:20`) knows CALL / RST / RET / RETI. It decodes from
    memory on every step while a call trace session runs (`calltrace.cpp:457-469`). It knows no PUSH / POP or
    block instructions.
- **Consequence for L0.** The access context has to be new. The cheapest form:
  - a 256-entry class table per prefix (none / CB / ED / DD / FD), read once after `FetchM1` in `Z80Step`, only
    at level `facts`;
  - or a store of the class byte in the few handlers (PUSH / POP / CALL / RET / RST / EX (SP) / the ED block
    group);
  - in either form, an explicit set in the INT / NMI accept paths.

  The disassembler's tables are a source for building the table offline, not something to read at run time.

Side finding (not fixed here): the `OpFlags` descriptors in `z80disasm.h:183-208` are off by one bit against
the `OF_*` constants from bit 9 up. `OpFlags` has no `Call`, so `Ret` is 1<<9 (= `OF_CALL`), `Rst` 1<<10
(= `OF_RET`), `Block` 1<<15 (= `OF_REG_EXCHANGE`), and so on. A flag description built from `OpFlags` names a
CALL "Ret" and LDIR "IO".
