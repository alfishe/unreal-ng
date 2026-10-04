# Experiment 03 - address ranges

Part of [PoC 022](../README.md).

## Goal

Range breakpoints over CPU addresses (design F2): 16, 256 and 4096 bytes, 1 to 10 000 of them, placed by the
generator (they overlap at high counts). Which structure keeps the miss and the hit cheap as ranges grow in
number and size.

## Candidates

| Matcher | How |
|:--|:--|
| `linear` | every range of the access's kind compared (a plain interval list) |
| `segments` | the ranges flattened at set time into disjoint covered segments, binary search |
| `bitsseg` | a bit per address per kind (8 KB, painted at set time) first, `segments` only on a set bit |
| `painted` | a `uint16` id per address per kind (128 KB, painted): one load |
| `bitspainted` | the bit filter first, the id table only on a set bit |

An interval tree was not built: for "any range covering this address" the disjoint segments answer the same
query with one binary search over a flat array, and the painted forms answer it with one load.

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

Run `2026-10-03_2356 - full run` (in the [data archive](../data/README.md)): the shared dev machine at load 45-120, so the
benchmarks ran interleaved in random order, three repetitions each, and the minimum is shown - relative
numbers, good for ranking the candidates, not as absolute timings. All sets: `report.py "scratch/poc-022/data/results/03-ranges/2026-10-03_2356 - full run"` after unpacking the [data](../data/README.md).

ns per access above the unarmed path (0.49-0.51 ns).

Unarmed path (ns per access, the floor subtracted below): cputest-z80full 0.50, demo-action 0.50, game-aleste 0.49, game-dizzy 0.51

**range16-cold-10** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| linear | 0.89 | 1.00 | 1.30 | 1.12 |
| painted | 0.56 | 0.54 | 0.53 | 0.56 |
| bitspainted | 0.50 | 0.47 | 0.49 | 0.40 |
| segments | 0.74 | 1.05 | 1.11 | 1.22 |
| bitsseg | 0.42 | 0.45 | 0.49 | 0.41 |

**range16-warm-100** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| linear | 14.15 | 15.30 | 17.37 | 15.56 |
| painted | 0.60 | 0.56 | 0.54 | 0.54 |
| bitspainted | 0.52 | 0.47 | 0.48 | 0.42 |
| segments | 3.82 | 4.40 | 4.58 | 3.84 |
| bitsseg | 0.71 | 0.56 | 0.65~ | 0.84 |

**range256-cold-100** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| linear | 14.28 | 15.10 | 15.78 | 10.32 |
| painted | 0.62 | 0.53 | 0.54 | 0.54 |
| bitspainted | 0.51 | 0.47 | 0.49 | 0.43 |
| segments | 3.60 | 2.95 | 4.69 | 4.49 |
| bitsseg | 0.46 | 0.48 | 1.66 | 2.57 |

**range256-cold-10000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| linear | 783.41 | 1035.63 | 221.72 | 40.14 |
| painted | 0.59 | 0.54 | 0.57 | 0.52 |
| bitspainted | 0.52 | 0.45 | 0.49 | 0.43 |
| segments | 1.31 | 1.01 | 2.32 | 1.57 |
| bitsseg | 0.74 | 0.48 | 2.43 | 1.85 |

**range256-warm-1000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| linear | 79.20 | 45.73 | 84.80 | 52.73 |
| painted | 0.60 | 0.51 | 0.55 | 0.58 |
| bitspainted | 0.50 | 0.45 | 0.49 | 0.46 |
| segments | 1.97 | 5.28 | 3.11 | 2.44 |
| bitsseg | 0.75 | 4.83 | 2.18 | 1.83 |

**range4096-warm-100** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| linear | 2.29 | 6.43 | 10.79 | 9.43 |
| painted | 0.58 | 0.51 | 0.57 | 0.57 |
| bitspainted | 0.51 | 0.49 | 0.48 | 0.42 |
| segments | 0.93 | 1.33 | 1.48 | 1.44 |
| bitsseg | 1.03 | 1.52 | 0.91 | 1.66 |

**range4096-warm-10000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| linear | 4.06 | 7.80 | 126.91 | 14.93 |
| painted | 0.60 | 0.51 | 0.57 | 0.54 |
| bitspainted | 0.52 | 0.45 | 0.49 | 0.42 |
| segments | 1.37 | 0.99 | 1.58 | 1.43 |
| bitsseg | 1.46 | 1.08 | 1.81 | 1.71 |


## What it means

- **`bitspainted` is flat at 0.42-0.52 ns** for every length (16 B to 4 KB) and count (1 to 10 000), cold
  or hitting. It is the same structure as 02's winner: a painted range costs the hot path nothing more than
  a single address.
- **`painted`** (no bit filter) is flat too, 0.05-0.1 ns slower (the L2 load of 02).
- **`segments`** (binary search over disjoint segments) costs 1-5 ns once ranges are many or hit.
  **`bitsseg`** helps only while the filter is mostly empty (few, short ranges).
- **`linear`** is out of the question: 14-17 ns at 100 ranges, up to 1 000 ns at 10 000.
- Painting is set-time work: at most 64K entries per kind, however many ranges there are.

## Decision

Ranges are **painted into the same bit filter and id table as single addresses** (02). No interval
structure on the hot path; overlapping ranges share an address through the candidate list (design §3).
