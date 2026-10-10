# Recipe: Find where the emulator spends its host CPU time (macOS)

Goal: a function / source-line profile of the emulator's own code for a fixed workload, to find what to optimize
or why a build got slower. It profiles the host process, so no MCP / WebAPI call is involved. To profile the
guest program (Z80 code), use [analysis/calltrace-and-opcode-profiler.md](../analysis/calltrace-and-opcode-profiler.md).

Tools and every option: [tools/profiling/xctrace/README.md](../../tools/profiling/xctrace/README.md). Measuring
a change (A/B): [performance-guidelines.md §4](../../docs/guidelines/performance-guidelines.md).

## 1. Pick a repeatable load

| Load | Command | Measured part (`--under`) |
|:--|:--|:--|
| a classic machine idle in BASIC | `core-benchmarks --benchmark_filter=BM_HostFrame_Pentagon_Fast` | `RunFramePublic` |
| the Sprinter in a demo (needs the system disk) | `UNREAL_SPRINTER_HDD=... core-benchmarks --benchmark_filter=BM_SprinterDemo_DontBlink_Shipped` | `RunFramePublic` |
| a test scenario | `core-tests --gtest_filter='Suite.Name'` | the test body (`Suite_Name_Test::TestBody`) |
| the GUI in a given state | attach to `unreal-qt` (step 2) | the emulator thread (`threads` report) |

Build first: `tools/build/build.sh core-benchmarks` (Release with `-g`, so the frames have source lines). The
benchmarks need one configure with `-DBENCHMARKS=ON`. The Sprinter benchmarks and their disk:
[core/benchmarks/emulator/machines/README.md](../../core/benchmarks/emulator/machines/README.md).

## 2. Record

```bash
UNREAL_SPRINTER_HDD=/path/to/sp_hdd_sys.img \
    tools/profiling/xctrace/record.sh -o scratch/profile/dnt -- \
    cmake-build-agent-release/bin/core-benchmarks --benchmark_filter=BM_SprinterDemo_DontBlink_Shipped
#  -> scratch/profile/dnt.trace (Instruments) and scratch/profile/dnt.xml (the reports' input)

tools/profiling/xctrace/record.sh -o scratch/profile/gui -t 20 -p $(pgrep -n unreal-qt)   # the GUI: attach, 20 s
```

`UNREAL_*` variables reach the launched command; add others with `-e VAR=value`.

## 3. Read

```bash
P=tools/profiling/xctrace/xctrace_profile.py
python3 $P scratch/profile/dnt.xml threads                                   # which thread works
python3 $P scratch/profile/dnt.xml --under RunFramePublic top -n 30          # functions: inclusive, self
python3 $P scratch/profile/dnt.xml --under RunFramePublic lines -n 40        # self per source line [caller]
python3 $P scratch/profile/dnt.xml --under RunFramePublic callees Z80::Z80FrameCycle --depth 2
python3 $P scratch/profile/gui.xml --thread "" threads                       # the GUI: find the emulator thread,
python3 $P scratch/profile/gui.xml --thread "<its name>" top                 # then profile it alone
```

## Assert on / pitfalls

- **Keep the setup out.** Without `--under`, a benchmark profile is mostly its setup: boot and load in turbo.
  The Sprinter demo case is about 3 300 samples, of which 850 are the measured frames.
- **Enough samples.** One sample is about 1 ms. Below about 1 % of the samples a line is noise; record longer
  for detail.
- **The main thread.** The reports look at `Main Thread` unless told otherwise. A benchmark or a test runs the
  emulator there; the GUI does not.
- **Unnamed frames.** Address-only frames come from a binary without symbols in the trace, such as the app
  bundle. Name them with `--binary <executable> --load <load address>`, taken from `vmmap <pid>` or Instruments.
- **A profile does not prove a speed-up.** Confirm with the interleaved A/B runs, and confirm the change is
  exact with the two-machine comparison tests.
