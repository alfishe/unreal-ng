# Frame profiles: what a frame costs per machine and per run profile

`frame_profile_benchmark.cpp` registers `BM_Prof_*`: the host CPU time of one emulated frame of a whole machine,
built from its shipped `configs/<model>/unreal.ini`, in several run profiles (1x, turbo, debugging, TTD, cards
removed one by one). It answers "what does the user pay for this machine in this mode", which the narrower
benchmarks (`BM_HostFrame_*` with screen and sound HQ off, the per-device frame benchmarks) do not.

This guide covers running it, comparing two commits with it, and profiling a row to see where the time goes.
The general rules for hot-path changes and the A/B procedure are in
[performance-guidelines.md](../../../../docs/guidelines/performance-guidelines.md); this file does not repeat them.

## Contents

- [1. The rows](#1-the-rows)
- [2. Building](#2-building)
- [3. Running](#3-running)
- [4. Comparing two commits](#4-comparing-two-commits)
- [5. Profiling a row](#5-profiling-a-row)
- [6. Reading the ladder](#6-reading-the-ladder)
- [7. Results](#7-results)

## 1. The rows

Every row creates the machine, boots 300 frames (past the ROM and the card POSTs: classic GS about 26 frames,
the NeoGS loader, the TR-DOS menu), then times `MainLoop::RunFrame()` through `MainLoopCUT::RunFramePublic()`.
That is the frame the paced loop runs, with every frame hook and with turbo render decimation as configured.
Pacing, the GUI and MessageCenter consumers are not included.

| Row suffix | Configuration | Turbo | Question it answers |
|:--|:--|:-:|:--|
| `ShipNormal` | shipped INI, features at their defaults | no | what a 1x frame costs |
| `ShipTurbo` | shipped INI | yes | the app's max speed (Turbo Mode) |
| `BareTurbo` | shipped INI; `screenhq`, `soundhq`, `sound`, `hud`, `debugmode` off | yes | what the host-side switches buy |
| `ShipTurboDebugHud` | shipped INI; `debugmode`, `breakpoints`, `hud` on | yes | a debugging session at max speed |
| `WorstNormal` | shipped INI; `debugmode`, `memorytracking`, `calltrace`, `timetravel` on, TTD recording | no | the worst case (turbo is refused while TTD records) |
| `NoCardsTurbo` | AY in the AY socket, no bus cards | yes | the machine without the shipped cards |
| `Only<Card>_Turbo` | `NoCardsTurbo` + one card: `NeoGS`, `GS`, `MoonSound`, `SounDrive` in `zxbus.1`, or `TSFM` in the AY socket | yes | what one fitted, silent card costs |
| `MaxTurbo` | `NoCardsTurbo` + everything `BareTurbo` switches off | yes | the floor: the CPU, memory, ports and the screen |

Machines: Pentagon (all rows), 48K, 128K, Scorpion, TS-Conf (`TSL`), ATM3 (BaseConf), ATM710, Profi, Sprinter, ZX
Next.

A row named `<Machine>_<Program>_<Profile>` loads a program after the boot frames and runs a few more before the timed
loop (`FRAME_PROFILE_WORKLOAD`): `TSConf_Sprites_*` is the TS-Conf SDK sprite example (`testdata/machines/tsconf/spg/
sprites.spg`). The snapshot path is a testdata path or an absolute one, so a program that is not in the repository
can be measured by a local row (not committed).

The counter `fps` is frames per second of host CPU time on one thread: `1e6 / cpu_time_us`.

## 2. Building

```bash
cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DBENCHMARKS=ON
ninja -C build-bench core-benchmarks          # or tools/build/build.sh core-benchmarks once configured
```

Notes:
- **Release, with symbols for profiling.** `RELEASE_WITH_DEBUG_SYMBOLS=ON` (the default) adds `-g` without
  changing the code; profilers then show inlined frames and source lines.
- **A headless build is enough.** `-DBUILD_QT_APPS=OFF -DENABLE_AUTOMATION=OFF` halves the build on a small
  machine. `-DENABLE_RECORDING=OFF` works too: `core-benchmarks` links `recording` only when the target exists.
- **A fresh configure can fail in unreal-asm** (`core/src/3rdparty/unreal-asm/CMakeLists.txt`, a deferred
  `add_subdirectory` from `unrealasm_add_benchmarks`, seen with CMake 3.28). Configure with
  `-DUNREAL_ASM_BENCHMARKS=OFF` until that is fixed.
- **Submodules.** `lib/benchmark` must be checked out (`git submodule update --init lib/benchmark`).

## 3. Running

Run from the build's `bin` directory: the benchmarks find `configs/` and the ROMs there.

```bash
cd build-bench/bin
./core-benchmarks --benchmark_filter='BM_Prof_'                       # everything, ~2 minutes
./core-benchmarks --benchmark_filter='BM_Prof_Pentagon_'              # one machine
./core-benchmarks --benchmark_filter='BM_Prof_Pentagon_Only'          # the cards one by one
./core-benchmarks --benchmark_filter='BM_Prof_' --benchmark_format=csv > ../../scratch/prof-1.csv
```

- Names carry a suffix (`BM_Prof_Pentagon_ShipTurbo/min_time:1.000`): do not anchor a filter with `$`; end it
  with `/` to match one row exactly (`'BM_Prof_Pentagon_ShipTurbo/'`).
- Every row runs at least one second (`MinTime(1.0)`). Compare `cpu_time`, not `real_time`.
- Startup logs go to stderr; `2>/dev/null` keeps the table readable.

## 4. Comparing two commits

The source compiles in trees from before the ZX-bus slots (2026-10-03): `__has_include` drops the slot rows there.
So one copy of this file measures an old and a new commit with exactly the same rows.

```bash
git worktree add --detach scratch/wt-old <old-commit>
mkdir -p scratch/wt-old/core/benchmarks/emulator/profiles
cp core/benchmarks/emulator/profiles/frame_profile_benchmark.cpp scratch/wt-old/core/benchmarks/emulator/profiles/
# keep ENABLE_RECORDING=ON for old trees: before this change core-benchmarks always linked `recording`,
# and some core sources include its headers
cmake -S scratch/wt-old -B scratch/b-old -G Ninja -DCMAKE_BUILD_TYPE=Release -DBENCHMARKS=ON -DENABLE_RECORDING=ON
ninja -C scratch/b-old core-benchmarks
```

Then interleave rounds and keep the minimum per row (performance-guidelines §4: A, B, A, B, then B, A …):

```bash
F='BM_Prof_Pentagon_'
for r in 1 2 3; do
  for side in old new; do
    (cd scratch/b-$side/bin && ./core-benchmarks --benchmark_filter="$F" --benchmark_format=csv 2>/dev/null \
       | grep '^"BM' | sed "s/^/$side,$r,/") >> scratch/ab.csv
  done
done
```

```python
import csv, collections
best = collections.defaultdict(lambda: float('inf'))
for side, r, name, *rest in csv.reader(open('scratch/ab.csv')):
    key = (name.split('/')[0], side)
    best[key] = min(best[key], float(rest[2]))     # cpu_time column
for n in sorted({k[0] for k in best}):
    o, h = best.get((n, 'old')), best.get((n, 'new'))
    print(f"{n:40s} {o or 0:8.0f} {h or 0:8.0f}  x{(h / o) if o and h else float('nan'):.2f}")
```

A missing old value is a slot row (it does not exist there).

## 5. Profiling a row

A benchmark says how long; a profile says where. Profile one row at a time, the one whose number moved.

### Linux: callgrind (instruction counts, deterministic)

The benchmark switches collection on for the timed loop only, so startup and the boot frames stay out:

```bash
cd build-bench/bin
valgrind --tool=callgrind --collect-atstart=no \
    --callgrind-out-file=../../scratch/cg-pentagon-turbo.out \
    ./core-benchmarks --benchmark_filter='BM_Prof_Pentagon_ShipTurbo/'

callgrind_annotate --inclusive=yes ../../scratch/cg-pentagon-turbo.out | head -60   # where the time goes, by subtree
callgrind_annotate ../../scratch/cg-pentagon-turbo.out | head -40                   # self cost, by function
callgrind_annotate --auto=yes ../../scratch/cg-pentagon-turbo.out <file.cpp>        # per source line (needs -g)
```

- Valgrind is ~50x slower; one row takes 1-3 minutes. Run two rows in parallel at most on a 2-core machine.
- `TOTALS / iterations` is the instructions per frame: a clock-free number that compares across machines and
  commits (`Ir` per frame, see section 7).
- `kcachegrind` / `qcachegrind` open the `.out` file for the call graph.
- **Turbo rows draw every frame under valgrind.** The adaptive turbo render cadence counts frames per wall-clock
  second; at valgrind speed it decimates by about 1, so the profile shows `ScreenZX::DrawRange` and friends that
  the real run skips. Read the screen share of a turbo row from a `MaxTurbo`-style row or from `perf`, not from
  callgrind.
- The `valgrind/callgrind.h` header comes with the valgrind package (`valgrind` or `valgrind-dev`); without it the
  toggles compile to nothing and you profile with `--collect-atstart=yes` instead.

### Linux: perf (sampled time, real speed)

```bash
perf record -g --call-graph=dwarf -o scratch/perf.data \
    ./core-benchmarks --benchmark_filter='BM_Prof_Pentagon_ShipTurbo/'
perf report -i scratch/perf.data --no-children --percent-limit 1
```

Needs `kernel.perf_event_paranoid <= 1` (often not available in containers and cloud VMs; use callgrind there).

### macOS: xctrace

[tools/profiling/xctrace/](../../../../tools/profiling/xctrace/README.md) records the Time Profiler and reports
by function, line and callee:

```bash
tools/profiling/xctrace/record.sh -o scratch/profile/pent-turbo -- \
    cmake-build-agent-release/bin/core-benchmarks --benchmark_filter='BM_Prof_Pentagon_ShipTurbo/'
python3 tools/profiling/xctrace/xctrace_profile.py scratch/profile/pent-turbo.xml --under RunFramePublic top -n 30
```

### Turbo rows: the boot frames are in the profile

Every run of a row boots its machine for 300 frames at 1x, and Google Benchmark runs a row several times (1, 10,
100 ... iterations) before the timed one. In a turbo row of a slow machine those 1x frames can be most of the
samples, and they show work the turbo frame does not do (synthesis, rendering of every frame). Filtering by function
does not help on macOS: xctrace attributes the frames of a small non-inlined helper to its neighbour. Profile the
timed loop alone by attaching to the process during its last run instead: raise the row's `MinTime` locally (say
`MinTime(40.0)`), start the benchmark, wait until the last run has started (print `state.max_iterations` before the
loop and wait until no new line comes for ~10 s), then `record.sh -p <pid> -t 15`. Found on 2026-10-13
([heavy-machines analysis](../../../../docs/inprogress/2026-10-13-heavy-machines-perf/analysis.md), section 2).

### The app itself

The rows measure the core frame only. When the app is slower than its row predicts, profile the app at max
speed on the emulation thread (xctrace `-p $(pgrep -n unreal-qt)`, `threads` report first) and compare with the
row: the difference is pacing, GUI work, MessageCenter consumers and the persisted `features.ini`.

## 6. Reading the ladder

Subtract neighbours; each difference is one cause.

| Difference | Is the cost of |
|:--|:--|
| `ShipTurbo − NoCardsTurbo` | the shipped cards together |
| `Only<Card>_Turbo − NoCardsTurbo` | one fitted, silent card (should be close to 0) |
| `ShipTurbo − BareTurbo` | screen/sound HQ, sound generation, HUD in turbo |
| `ShipTurboDebugHud − ShipTurbo` | the debug memory interface, breakpoint dispatch, HUD posts |
| `WorstNormal − ShipNormal` | debug interface, tracking, call trace, TTD capture at 1x |
| `ShipNormal − ShipTurbo` | sound rendering and per-frame screen drawing (turbo skips both) |
| `NoCardsTurbo − MaxTurbo` | host-side switches with no cards in the way |

The card rows are close to additive: `NoCardsTurbo + Σ(Only<Card> − NoCardsTurbo) ≈ ShipTurbo`. When they are
not, two cards interact (a shared port, the mixer, a bus overlay).

## 7. Results

### 2026-10-12: 435ebe2b (2026-09-07) against 1589e5c2

Linux VM, Xeon 2.1 GHz, gcc 13, Release `-O3`, `ENABLE_RECORDING=OFF`, `ENABLE_AUTOMATION=OFF`. Three interleaved
rounds, minimum `cpu_time` in µs per frame. 435ebe2b is the turbo work of 2026-09-07 (adaptive render cadence,
synthesis skipped in turbo); its commit message quotes 5008 fps.

| Row | 435ebe2b | 1589e5c2 | fps (new) | x |
|:--|--:|--:|--:|--:|
| Pentagon_ShipTurbo | 178 | **735** | 1,361 | 4.13 |
| Pentagon_BareTurbo | 177 | 709 | 1,410 | 4.02 |
| Pentagon_ShipTurboDebugHud | 283 | 834 | 1,199 | 2.95 |
| Pentagon_NoCardsTurbo | — | 198 | 5,063 | |
| Pentagon_MaxTurbo | — | **185** | 5,399 | |
| Pentagon_OnlyNeoGS_Turbo | — | 237 | 4,217 | |
| Pentagon_OnlyGS_Turbo | — | 556 | 1,797 | |
| Pentagon_OnlyMoonSound_Turbo | — | 652 | 1,534 | |
| Pentagon_OnlyTSFM_Turbo | — | 286 | 3,493 | |
| Pentagon_OnlySounDrive_Turbo | — | 190 | 5,265 | |
| Pentagon_ShipNormal | 1,068 | 2,323 | 430 | 2.18 |
| Pentagon_WorstNormal | 1,224 | 2,619 | 382 | 2.14 |
| 48K_ShipTurbo | 200 | 369 | 2,708 | 1.85 |
| 48K_MaxTurbo | — | 281 | 3,562 | |
| 128K_ShipTurbo | 182 | 340 | 2,939 | 1.87 |
| TSConf_ShipTurbo / MaxTurbo | — | 6,109 / 3,925 | 164 / 255 | |
| ATM3_ShipTurbo / MaxTurbo | — | 1,324 / 813 | 755 / 1,230 | |
| ATM710_ShipTurbo | — | 1,476 | 678 | |
| Profi_ShipTurbo | — | 591 | 1,692 | |

Callgrind, instructions per frame (turbo rows render every frame under valgrind, section 5):

| Row | Ir / frame | Largest parts (inclusive) |
|:--|--:|:--|
| Pentagon_ShipTurbo, 435ebe2b | 4.7 M | Z80 step 38 %, per-step screen 51 % |
| Pentagon_ShipTurbo, 1589e5c2 | 23.2 M | MoonSound `Opl4::SyncTo` 41.5 % (FM `Advance` 31 %), TSFM per-step `syncTo` 17 %, Z80 ~20 %, NeoGS ~4 % |
| Pentagon_ShipNormal, 1589e5c2 | 24.9 M | TSFM per-step render 39 % (`FilterDecimator::getOutputs` 15 %), MoonSound 30 % (resampler 10 %), screen 13 % |
| TSConf_MaxTurbo, 1589e5c2 | 66 M | `StepInstructionWithWork` 95 %: `HaltedM1` 29 %, `TsConfEngine::OnMachineStep` 22 %, `CatchUp` 12 % |

What it says:
- **The core did not regress much**: Pentagon without cards 178 → 198 µs, `MaxTurbo` 185 µs.
- **The shipped cards are the regression.** Since the ZX-bus slots, `pentagon128k` fits TSFM, NeoGS, MoonSound
  and SounDrive, and in turbo every card keeps its core running (machine state must not depend on the audio
  mode). Idle and fitted: MoonSound +454 µs (no idle path in the OPL4 core or the resampler), classic GS +358 µs
  (`SoundChip_GeneralSound` does not implement the `GSCardRunner` sleep that NeoGS has), TSFM +88 µs in turbo
  and the largest item at 1x (`Ym2203Pair::syncTo` after every instruction), NeoGS +39 µs (sleeps).
- **TS-Conf is slow per instruction, not because of cards**: the per-step machine engine and a halted CPU that
  re-executes `HaltedM1` every 4 T with the engine around it (the Sprinter's `39417bff` fast path is not there).
- **TTD and traces are not the problem**: `WorstNormal − ShipNormal` is ~300 µs on a 2.3 ms frame.

The rows to watch after a fix: `Only<Card>_Turbo` should approach `NoCardsTurbo`, and `ShipTurbo` should approach
`MaxTurbo`.

### 2026-10-12: peripheral standby

The idle cards now advance in closed form, exactly as the stepped card: Pentagon_ShipTurbo 548 -> 170 µs,
Pentagon_ShipNormal 1555 -> 1038 µs, OnlyGS 447 -> 164, OnlyMoonSound 470 -> 158 (dev Mac, interleaved A/B).
What changed per card, the exactness tests and the full table:
[docs/inprogress/2026-10-12-peripheral-standby/results.md](../../../../docs/inprogress/2026-10-12-peripheral-standby/results.md).

### 2026-10-13: Sprinter, ZX Next, TS-Conf

New rows for the Sprinter, the Next and TS-Conf at 1x and with a program, where the time goes on each, and the
proposals: [docs/inprogress/2026-10-13-heavy-machines-perf/analysis.md](../../../../docs/inprogress/2026-10-13-heavy-machines-perf/analysis.md).
