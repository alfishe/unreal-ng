# Experiment 01 - today's hot path

Part of [PoC 022](../README.md).

## Goal

Measure the breakpoint check exactly as `BreakpointManager` does it on master (7eca03436), on real traces,
as the set grows - the number every candidate has to beat.

## What is measured

`baseline.cpp` replicates the hot path outside the core:

| Access | Today's code | Replicated as |
|:--|:--|:--|
| exec / read / write | `hasX` flag -> `addressFlags[64K]` bit -> `MapZ80AddressToPhysicalPage` -> `unordered_map::find` (page key) -> `find` (wildcard key) -> `active` && type bit | the same, the page from the replay's slot table |
| port in / out | `hasX` flag -> `unordered_map::find(port)` (no filter) | the same |

Today there are no ranges, physical (any-slot) breakpoints or port masks. A range is what a user can do
now - one breakpoint per address - so range sets are expanded into points; sets the manager cannot hold
(two breakpoints on one key, a physical one, a masked port) are left out of this experiment.

Sets: exec points and read/write points (N = 0, 1, 10, 100, 1000, 10000, cold and warm), ranges of 16 and
256 bytes as points (1, 10, 100), exact ports (1, 10, 100).

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

Run `2026-10-04_0140 - full run`: the shared dev machine under load, benchmarks interleaved in random order, three
repetitions, the minimum shown - relative numbers. All sets: `report.py "scratch/poc-022/data/results/01-baseline/2026-10-04_0140 - full run"` after unpacking the [data](../data/README.md).

ns per access above the unarmed path (0.49-0.50 ns).

Unarmed path (ns per access, the floor subtracted below): cputest-z80full 0.49, demo-action 0.50, game-aleste 0.50, game-dizzy 0.49

**exec-cold-0** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| baseline | 1.71 | 1.67 | 1.71 | 1.57 |

**exec-cold-10000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| baseline | 1.99 | 1.91 | 1.99 | 1.73 |

**exec-warm-1000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| baseline | 2.82 | 2.18 | 2.70 | 2.58 |

**exec-warm-10000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| baseline | 3.29 | 3.71 | 2.88 | 2.61 |

**port-warm-100** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| baseline | 1.71 | 1.67 | 1.71 | 1.53 |

**range256-cold-100** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| baseline | 2.07 | 1.89 | 2.58 | 2.65 |

**range256-warm-100** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| baseline | 2.17 | 2.56 | 2.17 | 2.86 |

**rw-cold-10000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| baseline | 2.01 | 1.87 | 1.92 | 1.74 |

**rw-warm-10000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| baseline | 2.56 | 2.93 | 3.07 | 2.19 |


## What it means

- **Today's check costs 1.6-1.7 ns per access as soon as one breakpoint of the kind exists**, before any
  lookup. The matcher is a function call on every access: its hash-map code does not inline. The core is
  built the same way, with `Memory` calling `BreakpointManager::HandleMemoryRead` out of line on every
  access.
- **It grows with breakpoints that hit or sit near hot code**: 2.2-2.9 ns at 1 000, 2.6-3.7 ns at 10 000.
  The filter says "maybe" more often, and each "maybe" is up to two `unordered_map` lookups.
- **Ranges are only possible as points**, and a 256-byte range then costs 1.9-2.9 ns.
- **Physical (any-slot) breakpoints and port masks do not exist** today.

## Decision

This is the reference for the [final report](../README.md#final-report): every chosen structure has to beat
it while adding ranges, physical pages and port masks.
