# PoC 022: breakpoint matching on the debug hot path

**Goal:** find the structure that answers "does this access hit a breakpoint?" fastest for every
breakpoint kind the conditional-breakpoints design adds - exact addresses, address ranges, physical pages
(any slot), slot-bound pages, masked ports - and keeps that answer cheap as the set grows from 1 to 10 000
breakpoints and as programs switch pages.

**Design this feeds:**
[hotpath-matching-design.md](../../../docs/inprogress/2026-08-17-conditional-breakpoints/hotpath-matching-design.md)
(the algorithm, with diagrams), part of the
[conditional-breakpoints](../../../docs/inprogress/2026-08-17-conditional-breakpoints/design.md) track.

**Status:** done (2026-10-04). Every experiment ran on the shared dev machine under load (15-120), so all
numbers are relative: benchmarks interleaved in random order, three repetitions, the minimum taken. The
winning combination is below and in the design; integration into `BreakpointManager` and the A/B on the
emulator are next.

## Layout

| Folder | What |
|:--|:--|
| [recorder/](recorder/README.md) | access traces of real programs (a 128K Spectrum on unreal-z80) |
| `common/` | the trace format, the breakpoint model and set generator, the reference matcher, the harness |
| [01-baseline/](01-baseline/README.md) | today's hot path, replicated: the number to beat |
| [02-exact-lookup/](02-exact-lookup/README.md) | single addresses: filters and lookups |
| [03-ranges/](03-ranges/README.md) | address ranges: lists, segments with binary search, painting |
| [04-physical/](04-physical/README.md) | physical and slot-bound pages, remap cost |
| [05-ports/](05-ports/README.md) | port breakpoints with masks |
| [06-combined/](06-combined/README.md) | whole matchers on mixed sets, N = 0 to 10 000 |
| [data/](data/README.md) | **the traces and raw results of the published run, in one 2.3 MB 7z**, and how to reproduce everything |
| `run-all.sh` | runs all six experiments the way the published results were made |
| `report.py` | markdown tables from a results folder (`pass*.json`) |

## Reproduce

Short version (details, file list, checksums: [data/README.md](data/README.md)):

```bash
# the stored traces and results
mkdir -p scratch/poc-022/data
7zz x -oscratch/poc-022/data tools/poc/022-breakpoint-matching/data/poc022-data-2026-10-04.7z
# any table of the READMEs, from the stored results
python3 tools/poc/022-breakpoint-matching/report.py "scratch/poc-022/data/results/06-combined/2026-10-03_2356 - full run"
# build and rerun everything on the stored traces
cmake -S tools/poc/022-breakpoint-matching -B scratch/poc-022/build -G Ninja -DCMAKE_BUILD_TYPE=Release
BUILD_DIR=$PWD/scratch/poc-022/build tools/build/build.sh
tools/poc/022-breakpoint-matching/run-all.sh scratch/poc-022/build scratch/poc-022/data/traces scratch/poc-022/results
```

Every benchmark checks its matcher against the brute-force reference before timing it; a disagreement
stops the run.

## Final report

All numbers: ns per access **above the unarmed path** (the replay loop plus the one "any breakpoint of this
kind?" flag: 0.49-0.51 ns on every trace), the minimum of three interleaved repetitions, on four real
programs (two games, a demo, a CPU tester) and a synthetic trace that pages every 64 accesses.

### What each experiment decided

| Experiment | Question | Winner | Cost | Today / worst loser |
|:--|:--|:--|:--|:--|
| [01](01-baseline/README.md) | today's hot path | - | 1.6-1.7 empty, 2.6-3.7 with 10 000 hitting | - |
| [02](02-exact-lookup/README.md) | single addresses | bit filter 8 KB + painted id table | **0.42-0.54**, flat from 0 to 10 000, cold or hitting | today 1.5-3.7; sorted vector 8-13 when hitting |
| [03](03-ranges/README.md) | ranges, 16 B-4 KB, 1-10 000 | the same bit filter + painted ids | **0.42-0.52**, flat in count and length | binary search 1-5; linear list up to 1 000 |
| [04](04-physical/README.md) | physical / slot-bound pages | per-page bit + id slices, slot pointers moved on remap | **0.93-1.21**; 1.2-1.4 when paging every 64 accesses | hash keys 1.4-5.6; rebuilding per-slot tables 21-335 |
| [05](05-ports/README.md) | ports with masks | bit filter + painted ids, masks expanded at set time | **0.33**, flat | all equal (ports are under 1% of accesses) |
| [06](06-combined/README.md) | whole matchers, mixed sets | `globalbits` | **1.2-1.5** up to 1 000; 1.6-2.0 at 10 000; 1.25-1.9 paging hard | `slotbits` up to 3.6 paging hard; `threebits` 2.0-2.6 |

### The chosen combination

1. **The gate and one filter bit inline at the call site.** Each call site (`Z80` instruction start,
   `Memory` read / write, the port decoder) knows its kind. It tests `has[k]`, then one bit of the kind's
   8 KB filter; only a set bit calls out of line. Experiment 02 shows why: today's 1.5-1.7 ns "empty" cost
   is the call itself.
2. **CPU-address breakpoints and ranges painted** into the filter and a 64K `uint16` head table per kind.
   One load answers the hit, however many breakpoints there are (02, 03).
3. **Physical and slot-bound breakpoints in per-page slices** (a 2 KB bit slice and a 16K id slice per page,
   or per page and slot). Per-slot pointers are moved on a remap; nothing is rebuilt (04).
4. **One shared, over-approximating filter (`globalbits`)**: a physical offset is marked in all four slots,
   and the slices answer the rare false "maybe" (06). It was chosen over the exact per-slot filter, because
   that one costs up to 1.9 ns more when code pages often.
5. **Ports** use the same shape, with masks expanded at set time (05).

The set grows without the hot path noticing: from 1 to 10 000 breakpoints, single addresses and ranges stay
at 0.42-0.54 ns above unarmed, against today's 1.6 to 3.7 ns. The one place the cost grows is hundreds of
physical ranges with the shared filter (06); a cheap fix is noted there.

### Into the core

Algorithm, data structures and flows:
[hotpath-matching-design.md](../../../docs/inprogress/2026-08-17-conditional-breakpoints/hotpath-matching-design.md).

Next:
- integrate into `BreakpointManager`, with the call-site inline check;
- unit tests per rule;
- A/B on the emulator against master: debug mode off unchanged, debug mode on and unarmed, and 10, 1 000
  and 10 000 mixed breakpoints, interleaved.
