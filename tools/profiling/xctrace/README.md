# xctrace: where the host time goes (macOS)

Two tools around Apple's Time Profiler:
- `record.sh` records a trace and exports its samples;
- `xctrace_profile.py` reports from the export.

Each sample is the stack of one thread at one moment, taken about every millisecond. It has inlined frames and
source lines when the binary has debug info: `tools/build/build.sh` builds Release with `-g`. Instruments can open
the same `.trace` for the call tree and the flame graph.

The reports answer different questions:
- `top`: which functions cost the most;
- `lines`: which source line;
- `callees`: what one function spends its time on;
- `threads`: which thread is busy.

All four were used for the Sprinter work of 2026-10-09
([sprinter-cpu-and-peripherals.md §8](../../../docs/emulator/design/core/sprinter-cpu-and-peripherals.md)).

Needs the Xcode command line tools (`xcrun xctrace version`) and Python 3. The recipe for an agent:
[.recipe/testing/host-profiling.md](../../../.recipe/testing/host-profiling.md).

## Recording

```bash
# Launch a command (here a benchmark: a fixed, repeatable load)
UNREAL_SPRINTER_HDD=/path/to/sp_hdd_sys.img \
    tools/profiling/xctrace/record.sh -o scratch/profile/dnt -- \
    cmake-build-agent-release/bin/core-benchmarks --benchmark_filter=BM_SprinterDemo_DontBlink_Shipped

# Attach to a running process for 20 s (the GUI, a long test)
tools/profiling/xctrace/record.sh -o scratch/profile/gui -t 20 -p $(pgrep -n unreal-qt)
```

- **The output.** `<out>.trace` and `<out>.xml`, the time-profile table that the reports read. Without `-o` they
  go to `scratch/profile/<name>-<time>`.
- **The environment.** xctrace does not pass the shell's environment to a launched command. `record.sh` passes
  every `UNREAL_*` variable, and `-e VAR=value` adds others. An attached process keeps its own environment.
- **What to profile.**
  - A benchmark gives the same work every run, so two profiles compare.
  - A test works too, but it profiles its setup and its checks as well. Cut them away with `--under` /
    `--exclude` in the reports.
  - For the GUI, attach while the state of interest runs (a demo playing), with `-t` long enough. Attaching takes
    a few seconds before samples start.
- **The template.** `-T` picks another one (`xcrun xctrace list templates`). The reports read only the Time
  Profiler's `time-profile` table.

## Reports

```bash
P=tools/profiling/xctrace/xctrace_profile.py
python3 $P scratch/profile/dnt.xml threads
python3 $P scratch/profile/dnt.xml --under RunFramePublic top -n 30
python3 $P scratch/profile/dnt.xml --under RunFramePublic lines -n 40
python3 $P scratch/profile/dnt.xml --under RunFramePublic callees SoundChip_NeoGS::handleFrameEnd --depth 2
```

Filters, before any report (except `threads`):

| Option | Effect |
|:--|:--|
| `--thread S` | the threads whose name contains `S`. The default is `Main Thread`; `--thread ""` takes all of them. The emulator thread of the GUI is not the main thread: list them with `threads` first |
| `--under F` | only the samples with a function containing `F` on the stack. This is the measured part: `RunFramePublic` for a benchmark frame, `RunNFrames`, a test body. Repeatable: any of them |
| `--exclude F` | drop the samples with `F` on the stack, for example a check that reads the screen back |
| `--all-states` | count blocked and waiting samples too. Only the running ones count by default |
| `--binary B --load A` | name address-only frames with `atos`. These come from a binary without symbols in the trace, such as a GUI app bundle. `A` is the load address of `B`: the process list in Instruments, or `vmmap <pid>` |

Output of the first example (DNTBLINK with the shipped sound, 500 frames, `--under RunFramePublic`). `top`
lists inclusive time (the function is on the stack), then self time (the function is the leaf):

```
samples: 851
--- inclusive
100.0%    851  MainLoop_CUT::RunFramePublic
 93.4%    795  Core::CPUFrameCycle
...
--- self
 10.2%     87  {anon}::GraphicsSegment
  7.2%     61  SprinterMemory::OnWrite
  3.5%     30  SoundChip_TurboSound::handleStep
```

`lines` gives self time per source line of the leaf, with its caller in brackets. Inlined code is attributed to
its own line, so a line of a small inline function shows up under the function it was inlined into:

```
  8.8%     75  sprintervideorenderer.cpp:256        {anon}::GraphicsSegment              [ScreenSprinter::DrawRange]
  7.1%     60  sprintermemory.cpp:301               SprinterMemory::OnWrite              [SprinterAccelerator::AfterWrite]
```

`callees F` shows where the time under `F` goes: the callee chains below its outermost occurrence, so recursion
counts once, with `--depth` levels.

## Reading the numbers

- **Sample counts are small.** One sample is about 1 ms of one thread. 500 frames of 1.6 ms are about 800
  samples, so one sample is 0.1 %. Below about 1 % a line is noise. Record more frames: more benchmark
  iterations, or a longer `-t`.
- **Self time on one line** is cost of that line: a cache miss on a table, a division, a call that was not
  inlined.
- **Inclusive time** of a function the compiler inlined is still counted, because the frames carry the inlined
  functions.
- **A profile shows where time goes, not whether a change helps.** Measure a change with the A/B procedure
  ([performance-guidelines.md §4](../../../docs/guidelines/performance-guidelines.md)) and check it is exact
  with the comparison tests.

Checked 2026-10-09: `xctrace` 26.0 (Xcode 26), the `BM_SprinterDemo_*` benchmarks, and an attach to a running
`core-benchmarks`.
