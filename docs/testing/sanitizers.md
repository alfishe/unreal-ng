# Finding memory, thread and lifetime bugs: sanitizers, stress, hangs, leaks

How to find the bugs a normal test run does not show: use-after-free, buffer overflows, heap corruption that crashes
somewhere else later, data races, deadlocks, undefined behavior and leaks. Every command here was run on this tree on
2026-10-10 (macOS arm64, Apple clang 17; Linux arm64 in the CI image, gcc 14); what each one found is listed so the
next run can tell old findings from new ones.

Where this sits: [`bug-tracking-guide.md`](bug-tracking-guide.md) is the process (triage, root cause, fix ladder);
this document is the instruments. Tests themselves follow [`core/tests/README.md`](../../core/tests/README.md).

## Contents

1. [Which tool for which symptom](#1-which-tool-for-which-symptom)
2. [AddressSanitizer (+ UndefinedBehaviorSanitizer)](#2-addresssanitizer--undefinedbehaviorsanitizer)
3. [Reading an ASan report](#3-reading-an-asan-report)
4. [From a finding to a regression test](#4-from-a-finding-to-a-regression-test)
5. [Whole-application stress through the WebAPI](#5-whole-application-stress-through-the-webapi)
6. [Hangs and deadlocks](#6-hangs-and-deadlocks)
7. [When the stacks do not add up: address diagnostics](#7-when-the-stacks-do-not-add-up-address-diagnostics)
8. [ThreadSanitizer](#8-threadsanitizer)
9. [Leaks](#9-leaks)
10. [Crash reports from the field](#10-crash-reports-from-the-field)
11. [Linux (CI toolchain)](#11-linux-ci-toolchain)
12. [Pitfalls](#12-pitfalls)
13. [Findings log](#13-findings-log)

## 1. Which tool for which symptom

| Symptom | First tool | Then |
|:--|:--|:--|
| Crash in `free` / `malloc` / a random place, different each time | ASan on `core-tests` (§2), then the stress (§5) | §3 reading, §4 test |
| A crash report from a user or an agent | `tools/sanitize/ips_stacks.py` (§10) | ASan: the faulting frame of heap damage is rarely the culprit |
| Instances created / deleted / switched from automation misbehave | the WebAPI stress on an ASan `unreal-qt` (§5) | §6 / §7 |
| The app stops answering, nothing crashes | `sample <pid>` (§6) | a deadlock test with abort-on-timeout (§6) |
| Something works alone and fails with several instances or threads | TSan on the narrowest test (§8), ASan stress (§5) | |
| Arithmetic / shift / index oddities, compiler-dependent results | UBSan (part of the `asan` build, §2) | |
| Memory grows over a session | `leaks --atExit` (§9) on macOS, LSan in Docker (§11) | `HeapCounter` in a test (§9) |
| A value differs run to run, an ini key or a loader behaves oddly once in a while | valgrind memcheck (§11): uninitialized reads | |

## 2. AddressSanitizer (+ UndefinedBehaviorSanitizer)

ASan finds use-after-free, heap / stack / global overflows, use-after-scope, container overflow (reading a
`std::vector` past `size()` but inside its capacity). UBSan rides along in the same build (out-of-bounds index of a
known-size array, signed overflow, invalid shifts, misaligned access). Cost: ~2x CPU, ~3x memory; the tests still run
in minutes.

One-time configure, then build with the usual wrapper (the build directory is separate; nothing else changes). The
script is a shortcut for the CMake option `-DSANITIZE=<list>` (`cmake/Sanitize.cmake`: compile and link flags for every
target; `address,undefined`, `thread`, ...; MSVC takes `address` only), which any configure of the tree or of a
standalone `core/tests` accepts:

```bash
tools/sanitize/configure.sh asan                                   # -> cmake-build-asan (address,undefined)
BUILD_DIR=$PWD/cmake-build-asan tools/build/build.sh core-tests    # or unreal-qt, unreal-asm-tests
```

Run one suite while working, everything before calling a run clean (shards in parallel; each shard stops at its first
finding, so a finding can hide the next one in the same shard - rerun after each fix):

```bash
cd cmake-build-asan/bin
ASAN_OPTIONS=halt_on_error=1:detect_leaks=0 ./core-tests --gtest_filter='EmulatorManager_Test.*'

mkdir -p ../../scratch/asan
for k in $(seq 0 7); do
  (GTEST_TOTAL_SHARDS=8 GTEST_SHARD_INDEX=$k ASAN_OPTIONS=halt_on_error=1:detect_leaks=0 nice -n 10 \
     ./core-tests > ../../scratch/asan/s$k.log 2>&1; echo $? > ../../scratch/asan/s$k.rc) &
done; wait
grep -l "ERROR: AddressSanitizer" ../../scratch/asan/*.log
ASAN_OPTIONS=halt_on_error=1:detect_leaks=0 ./unreal-asm-tests        # the assembler library has its own binary
```

- `detect_leaks=0` on macOS: LeakSanitizer is not supported there ("detect_leaks is not supported on this platform");
  leaks are §9.
- `halt_on_error=1`: stop at the first error; the report is complete and nothing runs on damaged memory.
- Expected failures under the sanitizers, not bugs: `MediaMemory_Test.ResidentGrowthPerMode` (measures resident
  memory, which ASan's shadow and quarantine inflate) and the `BreakpointHotpathBench.*` timing budgets. Everything
  else passes on a clean tree (2026-10-10: ~8600 tests, 0 ASan errors).
- UBSan prints `runtime error:` lines and lets the test continue: collect them with
  `grep -h "runtime error" ... | sort | uniq -c`. `UBSAN_OPTIONS=print_stacktrace=1` adds the stack;
  `halt_on_error=1` makes the first one fatal. Outside `core/tests/` the tree is UB-clean (2026-10-23, a full
  run on 16 shards; the 2026-10-10 sites - TurboSound sample clamps, `Core` without an `Emulator`, `loader_scl.cpp`,
  `modulelogger.cpp`, `bithelper.h` - are fixed). What is left:

  | Count | Site | What |
  |--:|:--|:--|
  | ~45 | `core/tests/...` | test-only: `*_CUT` downcasts of objects that are not CUTs, misaligned loads from test data |

  New `runtime error` lines outside `core/tests/` are yours.
- A sanitizer report aborts with SIGABRT: macOS writes a crash report for it to `~/Library/Logs/DiagnosticReports`.
  It is the sanitizer stopping the process on purpose, not a second bug; delete your own when done (§12).

## 3. Reading an ASan report

```text
ERROR: AddressSanitizer: heap-use-after-free on address 0x617000b92008 ...
WRITE of size 1 at 0x617000b92008 thread T471          <- what touched the memory, on which thread
    #0 op_FD(Z80*) op_ddcb.cpp:338
    ...
0x617000b92008 is located 392 bytes inside of 656-byte region [0x617000b91e80,0x617000b92110)
freed by thread T24 here:                               <- who released it
    #1 Core::Release() core.cpp:694
    #6 EmulatorManager::RemoveEmulatorInstance ...
previously allocated by thread T25 here:                <- who created it
    #1 Core::Init() core.cpp:396
Thread T471 created by T24 here:                        <- where the accessing thread came from
```

Read all four parts, not only the first: the useful question is "why does this thread still hold that object".
- **The region start** (`[0x617000b91e80`) identifies the object; the offset tells the member.
- **Thread creation stacks** say which request / instance a thread belongs to. When the accessing thread and the
  allocating thread belong to *different* instances, look for state shared between instances (globals, statics,
  caches) - that is how the DDCB bug was found (§13).
- `heap-buffer-overflow ... 0 bytes after N-byte region`: a read one past the end (the Laser Genius `SkipRaw` case).
- `stack-buffer-overflow ... overflows this variable 'comps'`: an index from uninitialized data (the GIF palette).
- `container-overflow`: `front()` / `[i]` on an empty or shorter vector (the TTD timeline).

## 4. From a finding to a regression test

The fix is not done until a test fails without it. Prefer a test that fails *without* ASan too:

| Bug | The observable the test asserts |
|:--|:--|
| TS-Conf / ATM710 teardown wrote into freed parts | `EmulatorManager_Test.EveryModelCanBeRemovedWhileRunning`: every model created, run, removed while running (ASan-only signal; fast enough to keep) |
| GIF palette left garbage nodes | `Gif_Test.APaletteOfAFewColorsHasEveryNodeFilled`: prefill the palette with `0xFF` (the "garbage"), assert every node is valid |
| DDCB result went to the last created CPU | `Z80_Test.DdcbResultRegisterBelongsToTheCpuRunningIt`: two CPUs, assert the *right* CPU's register changed and the other's did not |
| UUID generator shared between threads | `UUID_Test.ThreadsGeneratingAtOnceGetDistinctIds`: 8 threads, assert no duplicates (~30 per run before) |
| TTD erased its timeline | the checkpoint count after N frames (0 before, 51 after) |
| Removal deadlock | §6: the test aborts the process if the threads do not finish |

Always run the new test against the unfixed code (revert the source, keep the test) and record the failure in the
commit message. Keep it under the 50 ms budget or justify it (turbo, fewer iterations).

## 5. Whole-application stress through the WebAPI

`core-tests` exercises one instance at a time. The bugs automation hits - removal while another thread reads, the GUI
following a selection, a model switch under a running request - need the real application with many threads.

```bash
tools/sanitize/configure.sh asan                 # once
tools/sanitize/stress_round.sh 900 r1            # 15 minutes; builds unreal-qt into cmake-build-asan first
```

`stress_round.sh` builds the ASan `unreal-qt`, starts it headless on its own ports (WebAPI 8190, MCP 8192, ... - a
normal emulator on 8090 is left alone), and runs `tools/sanitize/webapi_stress.py`: 8 workers each create an
instance of a random model, run it free or by `run_frames`, read the state endpoints automation uses (video changes,
screen, memory search, disassembly, port trace, debug wait ...), sometimes switch its model, then delete it; 2
long-lived neighbours run and are read the whole time. The round ends with one of:

- `app died` + `scratch/sanitize/<tag>/asan.<pid>`: a sanitizer finding (§3).
- `APP HUNG` + `hang.txt`: the server stopped answering; every thread was sampled (§6).
- `app survived`: no finding in this round. Rounds per minute matter: before the 2026-10-10 fixes a round lasted 3-15
  minutes and ~300 instance cycles; after them ~14 minutes and ~3000 cycles.

The per-minute progress line in `stress.log` (`ok`, `http_err`, `cycles`, `instances`) shows the run is alive; a jump
of `conn_err` with `instances None` is the server gone.

Iterate: fix the finding (its own commit and test), rebuild, run the next round. A finding that keeps coming back in
new places (the GUI reading a removed instance's context: about 144 call sites) is a design question - file it
(BUGS.md 2026-10-10 #11, fixed structurally) instead of patching sites one per round.

Optional, not part of the routine: `stress_round.sh --busy-gui 900 <tag>` opens the debugger, TTD, media, network,
slots, traffic, MIDI, tape and log windows (`UNREAL_QT_STRESS_WINDOWS=all`) and selects half of the instances through
the CLI before deleting them, so the GUI is bound to what is removed. Without the selection the GUI stays on the
first neighbour and never races a removal. Use it after a change to GUI / instance lifetime code.

## 6. Hangs and deadlocks

```bash
sample <pid> 3 -file scratch/hang.txt          # macOS: every thread's stack, 3 seconds
grep -n "EmulatorManager::\|MessageCenter\|condition_variable::wait\|mutex::lock" scratch/hang.txt
```

Look for the pair: thread A holds lock L and waits for something thread B does; thread B waits for L. The 2026-10-10
case: the WebAPI thread removed an instance under `_emulatorsMutex`, and `Release()` -> `~RecordingManager` ->
`EventQueue::RemoveObserver` waited for the message dispatch in flight; that dispatch
(`MainWindow::handleEmulatorSelectionChanged` -> `EmulatorManager::GetEmulator`) waited for `_emulatorsMutex`.
The rule that fell out: never stop / release an instance (or remove an observer) while holding a lock a message
handler may take.

A deadlock test cannot fail politely - both threads are stuck. Make it deterministic with gates (a context lease to
hold the removal at a known point, an atomic "entered" flag in the handler), wait with `TestWait::For` and a budget,
and on timeout print the cause and `std::abort()` (see `EmulatorManager_Test.RemovalDoesNotHoldTheLockAHandlerWaitsFor`).
Then run it against the unfixed code: it must abort every time.

## 7. When the stacks do not add up: address diagnostics

If the report says a thread used an object that thread could not have reached, add temporary `fprintf(stderr, ...)`
lines (unbuffered, survive the abort; never commit them) at the lifetime points, with addresses:

- the object's creation and deletion (`DIAG Z80 new %p core=%p ctx=%p`, `DIAG Z80 delete ...`),
- the thread's start and end with the object it runs (`DIAG Run emu=<id> core=%p z80=%p`),
- the lifecycle calls (`StartAsync`, `Stop` with what it joins, `Release`).

Then find the report's region start in the log: who created it, which thread ran it, and whether any running loop
should have reached it. On 2026-10-10 the log showed the freed Z80 belonged to an instance whose loop had exited
cleanly before `Release` - so the writer reached it through shared state, which was the global `direct_registers[]`
table. Python over the log (`alive` loops per object, releases with a loop alive) settles such questions faster than
reading by eye. Remove the diagnostics with `git checkout` of those files before the fix commit.

## 8. ThreadSanitizer

```bash
tools/sanitize/configure.sh tsan
BUILD_DIR=$PWD/cmake-build-tsan tools/build/build.sh core-tests
cd cmake-build-tsan/bin
TSAN_OPTIONS="halt_on_error=0:suppressions=$PWD/../../tools/sanitize/tsan.supp" ./core-tests --gtest_filter='UUID_Test.*:EmulatorManager_Test.*'
```

`tools/sanitize/tsan.supp` holds the races that are there by design: a tool reading guest memory live
(`DirectReadFromZ80Memory`), a test reading the machine state live (the access sits in a `*_test.cpp` or
`testwaithelper.h`), the machine time stamped on a coordinator log entry. The worst case of such a read is a value one
step older or newer. These entries are `race_top:` (the frame that touches the memory); a `race:` entry matches any
frame of either stack and would hide real races under `Core::CPUFrameCycle`. The only `race:` entries are anchored on
the global `std::cout` / `std::cerr`: concurrent output to them is not a race by the standard, TSan sees libc++
resetting the field width. Containers, pointers to storage that can be freed and read-modify-write are never
suppressed: they are bugs.

TSan and ASan cannot share a build. TSan works here, but the tree is not race-free yet: on 2026-10-10
`EmulatorManager_Test.*:UUID_Test.*:MessageCenter*:ExecutionArbiter*` gave 55 warnings, mostly the emulation
thread against API threads (`core.cpp:1093` frame cycle, `timetravelcontroller.cpp:660`, `portdecoder.cpp:263`,
`emulator.cpp:1524`, zstd in TTD). So use it differentially: run the narrowest test that exercises your change before
and after, and compare the `SUMMARY:` lines (`grep "^SUMMARY: ThreadSanitizer" | sort | uniq -c`). It exits non-zero
whenever it reported anything (`ThreadSanitizer: reported N warnings`); `MessageCenterVariants_Test.FastCenter_ThroughputSanity`
misses its time budget under TSan (expected).

A full run on 16 shards (as in §3, `TSAN_OPTIONS` instead of `ASAN_OPTIONS`) needs `UNREAL_TEST_TIME_SCALE=5`: the
Dezog and ZEsarUX tests wait at most 5 s for their free runs, and under TSan with 16 shards at once they miss it
(they pass alone and with the scale). Since `c9e59f6ca` (2026-10-23, was 25 warnings) such a run reports none; under
TSan only time and memory budgets fail (`BreakpointHotpathBench.*` ns per call, `MediaMemory_Test.ResidentGrowthPerMode`
with the shadow memory), as expected. New `SUMMARY:` sites are yours.

On Linux, `docker/linux/build.sh --tsan [--test]` builds with gcc 14 in the CI image (`scratch/linux-amd64-<type>-san-thread`).
gcc's TSan keeps a fixed shadow layout that a kernel with high `mmap_rnd_bits` (Ubuntu 24.04 on 6.6+) breaks
(`FATAL: ThreadSanitizer: unexpected memory mapping`): the script runs the build (gtest discovery starts the binary) and
the tests under `setarch -R` in a container with `seccomp=unconfined`. Running the binary by hand needs the same:
`docker run --security-opt seccomp=unconfined ... setarch -R ./core-tests`. gcc also warns that TSan does not model
`atomic_thread_fence` (`-Wtsan`); `cmake/Sanitize.cmake` keeps that a warning.

It does find what it should: a shared `std::mt19937` drawn from two threads is reported as a data race in
`mersenne_twister_engine::operator()` - the UUID duplicate bug in its pure form.

## 9. Leaks

macOS (LeakSanitizer unavailable) - the system `leaks` tool at process exit, with allocation stacks:

```bash
cd cmake-build-agent-release/bin
MallocStackLogging=1 leaks --atExit -- ./core-tests --gtest_filter='EmulatorManager_Test.*'
#   Process 48075: 9 leaks for 264192 total leaked bytes.
#   STACK OF 1 INSTANCE OF 'ROOT LEAK: <Config>': ... EmulatorManager::CreateEmulatorWithModel ... Emulator::Init()
```

- `ROOT LEAK` is unreachable memory; `ROOT CYCLE` is a group of objects referencing each other with no outside owner
  (as much a leak). The stack is the allocation site; read it from the bottom to the first project frame.
- Run a narrow filter: process-lifetime singletons and caches show up too; judge each by whether it grows with use.
- Exit code 1 means leaks were found.

Inside a test - the heap a piece of code leaves behind, without any tool (`core/tests/_helpers/heapcounter.h`):
`HeapCounter::Start()` ... `Stop()`, then `Net()` (bytes left allocated), `Peak()`, `Allocations()`. Count one
thread's work; `Available()` is false where the platform has no block size query. In use:
`TimeTravelEngineHeap_Test` (the engine's reported memory equals what the allocator really holds),
`ExtentReader_Test` (`Allocations() == 0`: reads allocate nothing), `ttdcontrol_test.cpp` (peak heap of an operation).
A leak check is the same shape: run the operation, undo it, assert `Net()` is back near 0.

Linux - LeakSanitizer comes with ASan: `docker/linux/build.sh --sanitize address,undefined` with
`ASAN_OPTIONS=detect_leaks=1` (§11). It reports at exit, grouped as `Direct leak` (nothing points to it) and
`Indirect leak` (reachable only from a leaked block); fix the direct ones, the indirect follow. Group by the first
project frame to see the sites:

```bash
ASAN_OPTIONS=detect_leaks=1 docker/linux/build.sh --sanitize address,undefined --type RelWithDebInfo \
    --test --filter 'EmulatorManager_Test.CreateEmulatorWith*' > scratch/lsan.log 2>&1
grep -A12 "^Direct leak\|^Indirect leak" scratch/lsan.log | grep "/src/core/src/" | head -20
```

What both tools found on 2026-10-10 (`EmulatorManager_Test.CreateEmulatorWith*`, `Z80_Test.*` ...):

| Size | Site | Meaning |
|--:|:--|:--|
| ~700 KB | `Emulator::Emulator` / `Emulator::Init` from `CreateEmulatorWithModel` | a create that fails (unknown model, unsupported RAM) leaks the half-built instance: `Config`, `EmulatorContext`, loggers (BUGS.md) |
| 16 KB | `WD1793::WD1793` creating the four `FDD`s | test-only: the drives are freed by `Emulator::ReleaseNoGuard`, so tests that build a bare `Core` (`Z80_Test`) leak them |

## 10. Crash reports from the field

macOS writes one report per crash to `~/Library/Logs/DiagnosticReports/<app>-<date>.ips` (JSON).

```bash
tools/sanitize/ips_stacks.py --list                      # unreal-* reports, pid, signal, sanitizer runtime or not
tools/sanitize/ips_stacks.py ~/Library/Logs/DiagnosticReports/unreal-qt-2026-10-10-001426.ips
```

It prints the exception, whether the binary carried a sanitizer runtime (a SIGABRT from an ASan build is the
sanitizer's own stop: read its `asan.*` log instead), the crashed thread and every thread passing through project
code. Release builds without symbols: [`docs/crash-symbolication.md`](../crash-symbolication.md),
`tools/symbolicate.py`.

A crash in `free` / `malloc` (`tiny_free_list_remove_ptr`, `free_tiny`) means the heap was damaged *earlier*, by
someone else: the frame on top only found the damage. Do not fix the frame on top. Reproduce under ASan (§2, §5),
which stops at the bad write itself. The 2026-10-10 report (`VideoWriteLog::BeginFrame` -> `free_tiny`) led, through
ASan, to the teardown use-after-free and the DDCB write into a removed CPU.

## 11. Linux (CI toolchain)

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 docker/linux/build.sh --sanitize address,undefined \
    --type RelWithDebInfo --test --filter 'EmulatorManager_Test.*:Z80_Test.*'
docker/linux/build.sh --tsan --type RelWithDebInfo --test --filter 'ExecutionArbiter*'   # --asan: address,undefined
```

`--sanitize` (or `--asan` / `--tsan`) configures with `-DSANITIZE=<list>` and builds into
`scratch/linux-<arch>-<type>-san-<list>/`, passes `ASAN_/LSAN_/UBSAN_/TSAN_OPTIONS` from
the host, and grants `SYS_PTRACE` (LeakSanitizer stops the threads through ptrace). gcc reports false
`maybe-uninitialized` inside libstdc++ `<regex>` once instrumented; the script keeps that one warning from failing the
build. A full build takes ~30 minutes at 6 jobs. Remove the build directories when done (`rm -rf scratch/linux-*`).

The test binary replaces the global `operator new` / `delete` (`core/tests/_helpers/heapcounter.cpp`). Every form
the standard library uses must be replaced, or ASan pairs the runtime's `new` with the replacement's `free` and reports
`alloc-dealloc-mismatch` - ~2100 of them on 2026-10-10 from `std::stable_sort`'s nothrow temporary buffer, until the
nothrow forms were added. A flood of identical reports from a library frame is a harness problem, not a product bug.

Slower here than on macOS: `EmulatorManager_Test.EveryModelCanBeRemovedWhileRunning` takes ~90 s under ASan in the
container (every model boots) - give it its own run or skip it in quick checks.

### valgrind memcheck

```bash
tools/sanitize/valgrind.sh                       # RelWithDebInfo build + the profile, results in scratch/valgrind/<date>/
tools/sanitize/valgrind.sh --no-build --filter 'Config_Test.*' --origins   # one suite, with the origin of a value
```

What ASan does not see: reads of uninitialized memory (a missing ini key compared an uninitialized buffer),
`new[]` freed with `delete`, a test that never frees its machine. The script builds RelWithDebInfo through
`docker/linux/build.sh`, adds valgrind to the CI image for the run (the image is removed at the end), and runs
`core-tests` in gtest shards at once, one valgrind process per core. The profile keeps it under an hour on 16 cores
(the full suite took 6 hours on 2026-10-10):

- [`valgrind-exclude.txt`](../../tools/sanitize/valgrind-exclude.txt): tests that measure time or memory (they fail
  under valgrind by design) and the boot-bound machine runs over 60 s each; the long threaded and lifetime tests stay.
- `UNREAL_TEST_TIME_SCALE=20`: every `TestWait` timeout is 20 times longer. A native timeout under valgrind reported
  hangs that were not there, and the machine fuzz's hang exit then stalled its process for hours.
- `UNREAL_EXEC_FUZZ_ITERATIONS=2`, a hard limit of 90 minutes per shard.
- [`valgrind.supp`](../../tools/sanitize/valgrind.supp): gtest printing a parameter struct's padding bytes.

`summary.txt` lists the distinct errors by their first frames in our code, the leaks and, for information, the tests
that failed under valgrind (timing). Exit 0 = no memcheck error. Read an `Invalid read` like an ASan report (§3);
`Conditional jump ... uninitialised` means a branch on a value nobody wrote: `--origins` names the allocation.
Known false positives at `-O2`: a branch on a disengaged `std::optional` whose payload gcc reads before the flag
(`CompositeMediumFactory::Build`); likely the same kind, not confirmed: `TTDDeviceTable::SameSet` (its origin is the
padding of a `TTDDeviceEntry` copy). The first run (2026-10-11, 40 minutes on 14 cores) found the MCP self-holding
chains, the DSD bit byte, the duplicate-breakpoint leak and the test fixtures' leaks (BUGS.md 2026-10-11 #7-#11, all fixed the same day).

## 12. Pitfalls

- **One sanitizer per build directory**: `cmake-build-asan` (address + undefined), `cmake-build-tsan`. Build only
  through `BUILD_DIR=... tools/build/build.sh` (job limits, queueing).
- **A green normal run says nothing about memory**: the teardown use-after-free and the GIF overflow passed every
  normal test for weeks.
- **Timing changes under sanitizers**: tests with time budgets or "work happens while X runs" can fail or pass by
  luck (`ExecutionArbiterMachine_Test.ReadingTheVideoLog...`, `TimeTravelManager_JournalSegments_Test.Switching...`).
  Rerun alone before blaming a change.
- **Shards stop at the first finding**: after a fix, rerun everything - the next bug may have been behind it.
- **zsh**: an unmatched glob is an error (`rm scratch/x/asan.*` with no file aborts the command); quote patterns
  passed to tools (`-U 'OPENSSL*'`).
- **Your own artifacts**: sanitizer build directories are several GB each; crash reports of your sanitizer runs pile
  up in `~/Library/Logs/DiagnosticReports`. Delete yours (by pid, `ips_stacks.py --list`), keep reports you did not
  produce.
- **Temporary diagnostics** (§7) never go into a commit: `git diff` before `git add`.

## 13. Findings log

What these instruments found (the bug entries are in the git history of
[`docs/inprogress/BUGS.md`](../inprogress/BUGS.md): fixed bugs are removed from it), so a rerun can tell old from new:

| Instrument | Finding | Commit |
|:--|:--|:--|
| ASan, `core-tests` | TS-Conf / ATM Turbo 2+ decoder destructors touched freed memory / keyboard | `fa8b68fbe` |
| ASan, `core-tests` | Laser Genius detection read past its buffer | `7d40cd730` |
| ASan, `core-tests` | GIF palette garbage nodes indexed past `comps[3]` | `dc0b47c9f` |
| ASan, `core-tests` | TTD with a shadow engine read an empty timeline | `53325eb5b` |
| stress + `sample` | removal deadlock (lock held while releasing) | `fc9d535e5` |
| ASan stress | GUI menu / FDC widget read removed contexts | `39770502f`, `ea5696507` |
| test written while chasing a stress report | UUID duplicates from concurrent creates | `b9291e6a7` |
| ASan stress + address diagnostics | DDCB results written into the last created CPU | `c68ea8a24` |
| macOS `leaks`, LSan | a failed `CreateEmulatorWithModel` leaks the half-built instance | `d5e197532` |
| LSan | `FDD`s of a bare `Core` (tests) never freed | test-only |
| UBSan | logger array index, `1 << 255`, float->short in TurboSound, misaligned SCL loads, null `Emulator&` | `d5e197532` |
| Linux ASan | test `operator new` replacement missed the nothrow forms (false `alloc-dealloc-mismatch`) | this change |
| TSan | 55 races between the emulation thread and API threads (baseline); the last 24 of a sharded run | `c9e59f6ca` |
| ASan stress | ~144 GUI context reads without a lease | `f38e72b8e` |
| valgrind (2026-10-11) | the `.z80` loader freed `new[]` staging pages with `delete` | `9643b9623` |
| valgrind (2026-10-11) | a missing `RESET=` compared an uninitialized buffer (reset ROM undefined) | `202db83c2` |
| valgrind (2026-10-11) | test fixtures leaked their `Core`, drives and context | `befe0e730` |
| TSan on Linux (2026-10-11) | ZX-Poly status read the members' registers while they ran | `a5aa1b910` |
| TSan on Linux (2026-10-11) | two overscan switches at once both saw the old video mode | `c13e28bb0` |
| TSan, `core-tests` (2026-10-24) | a snapshot load zeroed the memory-tracking counters while another thread's feature change freed them (SIGSEGV in `MemoryAccessTracker::ResetCounters`) | `MemoryAccessTracker_Test.ResetWhileAnotherThreadTogglesTracking` |
