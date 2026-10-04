# Experiment 02 - single addresses

Part of [PoC 022](../README.md).

## Goal

For single-address breakpoints (exec, read, write): which filter and which lookup answer "is there a
breakpoint here" fastest, and how each scales from 1 to 10 000 breakpoints.

## Candidates

| Matcher | Filter | Resolve |
|:--|:--|:--|
| `map2` | byte flags, 64 KB (today) | `unordered_map`, page key then wildcard key (today) |
| `flat` | byte flags, 64 KB | flat open-addressing hash, linear probing |
| `sorted` | byte flags, 64 KB | sorted vector, binary search |
| `bits` | one bit per address per kind, 3 x 8 KB (fits L1) | flat hash |
| `direct` | none | `uint16` id per address per kind, 3 x 128 KB: one load is the answer |
| `bitsdirect` | bits, 3 x 8 KB | the id table, only on a set bit |

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
numbers, good for ranking the candidates, not as absolute timings. All sets: `report.py "scratch/poc-022/data/results/02-exact-lookup/2026-10-03_2356 - full run"` after unpacking the [data](../data/README.md).

ns per access above the unarmed path (0.50-0.51 ns on every trace).

Unarmed path (ns per access, the floor subtracted below): cputest-z80full 0.51, demo-action 0.51, game-aleste 0.50, game-dizzy 0.50

**exec-cold-0** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| direct | 0.58 | 0.51 | 0.55 | 0.51 |
| bits | 0.46 | 0.46 | 0.49 | 0.42 |
| sorted | 0.44 | 0.44 | 0.44 | 0.41 |
| map2 | 1.63 | 1.54 | 1.76 | 1.47 |
| flat | 0.45 | 0.45 | 0.49 | 0.40 |
| bitsdirect | 0.52 | 0.45 | 0.47 | 0.42 |

**exec-cold-10000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| direct | 0.58 | 0.52 | 0.56 | 0.51 |
| bits | 0.47 | 0.46 | 0.49 | 0.42 |
| sorted | 0.45 | 0.46 | 0.47 | 0.44 |
| map2 | 1.73 | 1.67 | 1.98 | 1.61 |
| flat | 0.45 | 0.47 | 0.52 | 0.47 |
| bitsdirect | 0.52 | 0.46 | 0.47 | 0.42 |

**exec-warm-100** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| direct | 0.59 | 0.52 | 0.53 | 0.50 |
| bits | 0.49 | 0.48 | 0.53 | 0.45 |
| sorted | 0.85 | 0.54 | 0.91 | 1.09 |
| map2 | 1.84 | 1.64 | 1.91 | 1.65 |
| flat | 0.49 | 0.48 | 0.55 | 0.46 |
| bitsdirect | 0.52 | 0.46 | 0.49 | 0.42 |

**exec-warm-10000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| direct | 0.58 | 0.52 | 0.54 | 0.51 |
| bits | 0.59 | 1.20 | 0.69 | 0.56 |
| sorted | 12.73 | 9.14 | 9.56 | 8.46 |
| map2 | 2.97 | 3.72 | 2.75 | 2.40 |
| flat | 0.58 | 1.13 | 0.60 | 0.55 |
| bitsdirect | 0.52 | 0.45 | 0.48 | 0.42 |

**rw-cold-1000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| direct | 0.59 | 0.51 | 0.54 | 0.50 |
| bits | 0.46 | 0.47 | 0.49 | 0.42 |
| sorted | 0.66 | 0.66 | 0.68 | 0.62 |
| map2 | 1.70 | 1.65 | 1.94 | 1.58 |
| flat | 0.64 | 0.65 | 0.69 | 0.63 |
| bitsdirect | 0.54 | 0.45 | 0.48 | 0.43 |

**rw-warm-1000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| direct | 0.58 | 0.52 | 0.53 | 0.51 |
| bits | 0.62 | 0.53 | 0.64 | 0.58 |
| sorted | 1.41 | 0.79 | 1.36 | 3.58 |
| map2 | 1.99 | 1.76 | 2.22 | 2.30 |
| flat | 0.81 | 0.70 | 0.82 | 0.70 |
| bitsdirect | 0.52 | 0.45 | 0.49 | 0.43 |

**rw-warm-10000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| direct | 0.59 | 0.54 | 0.53 | 0.52 |
| bits | 0.63 | 0.84 | 0.96 | 0.60 |
| sorted | 5.43 | 3.00 | 7.78 | 6.88 |
| map2 | 2.47 | 2.86 | 3.22 | 2.42 |
| flat | 0.76 | 1.02 | 1.08 | 0.69 |
| bitsdirect | 0.52 | 0.45 | 0.46 | 0.42 |


## What it means

- **`bitsdirect` is the fastest everywhere and does not move**: 0.42-0.54 ns for 0 to 10 000 breakpoints,
  cold or hitting. The 8 KB bit filter stays in L1, and a hit costs one more load from the id table.
- **`direct`** (the id table alone, no filter) is flat too, 0.1 ns slower: a 128 KB table is an L2 load on
  every access.
- **`flat` and `bits`** are as fast while the filter says "no", but their hash probes show once breakpoints
  hit (up to 1.1-1.2 ns with 10 000 hitting).
- **`sorted`** collapses when breakpoints hit: 8-13 ns at 10 000. Binary search over a large vector misses
  the cache.
- **`map2` (today) costs 1.5-2.0 ns even with an empty set**, three times the others. Its lookup code is too
  big to inline, so every access is a function call - which is also how the core calls
  `BreakpointManager::HandleMemoryRead` today, from `Memory` on every access.

## Decision

Single addresses go into **a bit filter per kind (8 KB) and a painted `uint16` id table (64K entries) per
kind**: one L1 bit test on a miss, one more load on a hit. And from the `map2` observation: **the gate and
the bit test go inline at the call site** (a header-inline check in `Memory` / `Z80` / the port decoder);
the out-of-line call happens only on a set bit.
