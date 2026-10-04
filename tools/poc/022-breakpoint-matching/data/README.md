# PoC 022 data: traces and raw results

Everything the published numbers were made from, in one archive:
[`poc022-data-2026-10-04.7z`](poc022-data-2026-10-04.7z) - **2.3 MB** (about 173 MB unpacked: the recorded
programs repeat their loops, so LZMA2 packs a 49 MB trace into 40 KB).

SHA-256 of the archive: `9f640ed4c033bfd894649f790b0a8c8833fccda8d6e23c5babc111ae5599f38a`

## What is inside

| Path in the archive | Size | What it is | Made by |
|:--|--:|:--|:--|
| `traces/game-dizzy.trace` | 49.2 MB | Dizzy Y 2 (128K game, menu), 2 000 000 instructions, 8 206 640 events | `recorder` ([recorder/README.md](../recorder/README.md)) |
| `traces/game-aleste.trace` | 43.4 MB | Aleste (128K game), 7 228 910 events | `recorder` |
| `traces/demo-action.trace` | 35.9 MB | Action (128K demo), 5 988 193 events | `recorder` |
| `traces/cputest-z80full.trace` | 30.8 MB | z80full (48K instruction tester), 5 138 187 events | `recorder` |
| `traces/SHA256SUMS` | - | the checksums of the four traces (`shasum -a 256 -c`) | - |
| `results/01-baseline/2026-10-04_0140 - full run/` | 0.9 MB | today's hot path | `e01-baseline` |
| `results/02-exact-lookup/2026-10-03_2356 - full run/` | 2.7 MB | single addresses | `e02-exact-lookup` |
| `results/03-ranges/2026-10-03_2356 - full run/` | 3.7 MB | address ranges | `e03-ranges` |
| `results/04-physical/2026-10-03_2356 - full run/` | 4.9 MB | physical and slot-bound pages | `e04-physical` |
| `results/05-ports/2026-10-03_2356 - full run/` | 0.8 MB | ports with masks | `e05-ports` |
| `results/06-combined/2026-10-03_2356 - full run/` | 1.2 MB | whole matchers on mixed sets | `e06-combined` |

Each results folder holds `pass1.json` (Google Benchmark JSON: every repetition of every benchmark, with the
`ns_per_access`, `hits` and `events` counters) and `pass1.txt` (the console output of the same run). The
synthetic `remapheavy-dizzy` trace of experiments 04 and 06 is not stored: the experiments build it from
`game-dizzy` in memory.

Trace format (`common/trace.h`): magic `PB22`, the four initial slot pages, the event count, then 6 bytes
per event - address, value, kind (exec, fetch, read, write, in, out, remap).

## Reproduce

All commands from the repository root.

**1. Unpack**

```bash
mkdir -p scratch/poc-022/data
7zz x -oscratch/poc-022/data tools/poc/022-breakpoint-matching/data/poc022-data-2026-10-04.7z
(cd scratch/poc-022/data && shasum -a 256 -c traces/SHA256SUMS)
```

**2. Rebuild any table of the READMEs from the stored results** (no build needed):

```bash
python3 tools/poc/022-breakpoint-matching/report.py \
    "scratch/poc-022/data/results/06-combined/2026-10-03_2356 - full run"
python3 tools/poc/022-breakpoint-matching/report.py \
    "scratch/poc-022/data/results/03-ranges/2026-10-03_2356 - full run" --sets=range256-cold-10000,range4096-warm-100
```

`--sets=` picks set recipes (`<shape>-<cold|warm>-<count>`), `--traces=` picks traces by name part.

**3. Build the experiments**

```bash
cmake -S tools/poc/022-breakpoint-matching -B scratch/poc-022/build -G Ninja -DCMAKE_BUILD_TYPE=Release
BUILD_DIR=$PWD/scratch/poc-022/build tools/build/build.sh
```

**4. Rerun the benchmarks on the stored traces**

```bash
tools/poc/022-breakpoint-matching/run-all.sh scratch/poc-022/build scratch/poc-022/data/traces scratch/poc-022/results
```

About 1 h 40 min for all six on the loaded dev machine (03 and 04 take most of it). One experiment, or a
subset, directly:

```bash
POC022_TRACES=scratch/poc-022/data/traces scratch/poc-022/build/e06-combined \
    --benchmark_filter='(globalbits|slotbits)/mixed-cold-1000/' --benchmark_repetitions=3
```

Every benchmark first checks its matcher against the brute-force reference and stops on any disagreement.
Absolute timings depend on the machine and its load; the rankings are what the READMEs rely on.

**5. Re-record the traces** (optional: the recorder reproduces them bit for bit - checked 2026-10-04 for
`game-dizzy`, same SHA-256):

```bash
B=scratch/poc-022/build; T=scratch/poc-022/traces; mkdir -p $T
$B/recorder data/rom/128.rom "testdata/loaders/sna/Dizzy Y 2.sna" $T/game-dizzy.trace      2000000
$B/recorder data/rom/128.rom testdata/loaders/sna/aleste1.sna     $T/game-aleste.trace     2000000
$B/recorder data/rom/128.rom testdata/loaders/sna/action.sna      $T/demo-action.trace     2000000
$B/recorder data/rom/128.rom testdata/loaders/sna/z80full.sna     $T/cputest-z80full.trace 2000000
```

**6. Pack a new run** the same way (traces and results side by side, maximum LZMA2):

```bash
cd <folder holding traces/ and results/>
7zz a -t7z -mx=9 -m0=lzma2 -md=256m poc022-data-<date>.7z traces results
```
