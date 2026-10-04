# Experiment 04 - physical pages and slot-bound breakpoints

Part of [PoC 022](../README.md).

## Goal

Breakpoints on physical memory: a page and offsets that match through whatever slot shows the page (design
F4), and slot-bound ones that match only while the page is in one slot (F3, today's page breakpoints). The
page of an access comes from the slot table, and paging moves pages at run time - so a candidate may do
work per access, per remap, or both. Which balance wins, including when code pages very often.

## Candidates

| Matcher | Per access | Per remap |
|:--|:--|:--|
| `hashkey` | bit filter over CPU addresses (a physical offset marked in all four slots), then a flat hash on (page, offset) | nothing |
| `pagemap` | the same filter, then `unordered_map` page -> 16K id slice | nothing |
| `slices` | per slot a pointer to the id slice of the page it shows (or an empty slice): one dependent load; slot-bound ones in a second slice per (page, slot) | move 2 pointers per kind |
| `merged` | one 16K id table per slot (page slice + slot-bound slice): one load | rebuild the slot's tables (32 KB per kind) |
| `bitslices` | a 2 KB bit slice per page next to the id slice; the bit first | move 4 pointers per kind |

Sets: physical and slot-bound, single addresses and 256-byte ranges, N = 1 to 1000, cold and warm. Traces:
the four recorded ones and `remapheavy-dizzy` (slot 3 switched every 64 events).

## How it is measured

- Google Benchmark, one benchmark per *matcher / set / trace*: the whole trace replayed through the
  matcher, `ns_per_access` = CPU time / events (remap events included). The matcher is built outside the
  timed region (set time is not access time).
- Before its first timed run every benchmark is checked against `ReferenceMatcher` (brute force over the
  set): hit or miss must agree on every access (the whole trace for sets up to 100 breakpoints, a prefix
  for larger ones). A disagreement stops the run - a wrong matcher is not reported as a fast one.
- Sets come from `MakeSet` (`common/breakpoints.h`), seed 22: *cold* sets sit on addresses the trace never
  touches (the pure filtering cost, the normal state of a debugging session), *warm* ones on touched
  addresses (they hit; a hit is counted and the replay goes on).
- One run on the shared dev machine under load (15-120): within the experiment the benchmarks run in
  random interleaved order, three repetitions each, and the tables show the minimum minus the trace's
  *unarmed* floor (the replay loop plus the one "any breakpoint of this kind?" flag that every design
  keeps); `~` marks a cell whose repetitions spread by more than 15%. Relative numbers: good for ranking
  the candidates, not as absolute timings.
- Raw output: `results/<experiment>/<stamp> - full run/pass1.{json,txt}` inside
  [`../data/poc022-data-2026-10-04.7z`](../data/README.md); how to unpack, rerun and rebuild the tables:
  [`../data/README.md`](../data/README.md).

## Results

Run `2026-10-03_2356 - full run`: the shared dev machine at load 30-120, benchmarks interleaved in random order, three
repetitions, the minimum shown - relative numbers. All sets: `report.py "scratch/poc-022/data/results/04-physical/2026-10-03_2356 - full run"` after unpacking the [data](../data/README.md).

ns per access above the unarmed path (0.49 ns; 0.58 ns on `remapheavy-dizzy`, whose extra remap events
are part of the replay).

Unarmed path (ns per access, the floor subtracted below): cputest-z80full 0.49, demo-action 0.49, game-aleste 0.49, game-dizzy 0.49, remapheavy-dizzy 0.58

**phys1-cold-10** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| pagemap | 1.68 | 1.58 | 1.56 | 1.49 | 1.46 |
| bitslices | 0.95 | 0.97 | 0.98 | 0.93 | 1.32 |
| hashkey | 1.69 | 1.58 | 1.58 | 1.44 | 1.50 |
| slices | 1.17 | 1.15 | 1.15 | 1.06 | 1.34 |
| merged | 1.64 | 1.71 | 1.65 | 1.79 | 21.53~ |

**phys256-cold-100** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| pagemap | 1.70 | 2.24 | 2.49 | 2.49 | 2.41 |
| bitslices | 0.95 | 1.00 | 1.05 | 1.09 | 1.20 |
| hashkey | 1.73 | 2.75 | 2.26 | 2.16 | 2.28 |
| slices | 1.17 | 1.13 | 1.18 | 1.09 | 1.21 |
| merged | 1.62 | 1.74 | 1.63 | 1.83 | 23.63~ |

**phys256-warm-1000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| pagemap | 2.72 | 3.08 | 2.68 | 2.64 | 2.69 |
| bitslices | 1.05 | 1.21 | 1.19 | 1.04 | 1.27 |
| hashkey | 1.98 | 5.57 | 2.11 | 1.81 | 1.87 |
| slices | 0.92 | 1.17 | 1.14 | 0.89 | 1.03 |
| merged | 1.64 | 1.74 | 1.68 | 1.79 | 23.18~ |

**slot1-warm-10** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| pagemap | 1.67 | 1.55 | 1.55 | 1.45 | 1.46 |
| bitslices | 0.95 | 0.97 | 0.98 | 0.93 | 1.32 |
| hashkey | 1.70 | 1.59 | 1.54~ | 1.44 | 1.49 |
| slices | 1.15 | 1.11 | 1.18 | 1.05 | 1.33 |
| merged | 1.65 | 2.13 | 1.71 | 2.40 | 117.33 |

**slot256-cold-1000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| pagemap | 2.37 | 2.52 | 2.73 | 2.55 | 3.00 |
| bitslices | 1.01 | 1.07 | 1.08 | 0.98 | 1.37 |
| hashkey | 2.35 | 4.95 | 3.83 | 3.40 | 2.43 |
| slices | 1.17 | 1.10 | 1.15 | 1.03 | 1.18 |
| merged | 1.64 | 2.99 | 1.64 | 3.70 | 335.50 |

**slot256-warm-100** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| pagemap | 2.04 | 1.81 | 1.76 | 1.59 | 1.64 |
| bitslices | 0.95 | 0.98 | 0.99 | 0.98 | 1.28 |
| hashkey | 2.18 | 1.89 | 1.76 | 1.64 | 1.77 |
| slices | 1.15 | 1.09 | 1.15 | 1.04 | 1.31 |
| merged | 1.65 | 2.65 | 1.63 | 3.70 | 266.43 |


## What it means

- **`bitslices` wins or ties everywhere**: 0.93-1.21 ns on the recorded traces, 1.20-1.37 ns on the
  remap-heavy one. The page's 2 KB bit slice answers the miss; a remap moves four pointers per kind.
- **`slices`** (no bit slice) is close (0.9-1.3 ns): its 32 KB id slices cost a little more cache than
  the 2 KB bits on a miss.
- **`hashkey` and `pagemap`** pay a hash per "maybe" and over-mark the filter (a physical offset is marked in
  all four slots): 1.4-5.6 ns.
- **`merged` must not be used**: rebuilding per-slot tables on every remap is 21-335 ns per access once a
  program pages every 64 accesses, and 1.6-3.7 ns even on the 128K game traces.
- A physical or slot-bound check costs about twice a CPU-address one (about 1.0 against 0.45 ns, 02/03):
  the slot pointer is one more dependent load. The whole matcher (06) has to keep that load off the path of
  plain CPU-address breakpoints.

## Decision

Physical and slot-bound breakpoints live in **per-page (and per page-and-slot) slices: a 2 KB bit slice and
a 16K-entry id slice, reached through per-slot pointers that a remap moves** (design §3, §5.2). Work done
per remap stays a handful of pointer stores; no per-slot table is ever rebuilt. How these bits combine with
the CPU-address filter is experiment 06's question.
