# Experiment 05 - port breakpoints with masks

Part of [PoC 022](../README.md).

## Goal

Port breakpoints with a mask (design F7: `(port & mask) == (value & mask)`; a Spectrum decodes ports
partly, #FE answers on every even port). Ports are under 1% of the accesses, so the question is what an
armed port check costs and whether masks change the choice.

## Candidates

| Matcher | How |
|:--|:--|
| `map` | today: `unordered_map` on the exact port; a masked breakpoint expanded into every port it matches |
| `buckets` | one group per distinct mask: a bit set and ids of `(port & mask)`; checks = distinct masks |
| `painted` | a `uint16` id per port per direction (2 x 128 KB), masks expanded at set time |
| `bitspainted` | a bit per port per direction (2 x 8 KB) first, the ids on a set bit |

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

Run `2026-10-03_2356 - full run`: the shared dev machine at load 30-50, benchmarks interleaved in random order, three
repetitions, the minimum shown - relative numbers. All sets: `report.py "scratch/poc-022/data/results/05-ports/2026-10-03_2356 - full run"` after unpacking the [data](../data/README.md).

ns per access above the unarmed path (0.48-0.49 ns).

Unarmed path (ns per access, the floor subtracted below): cputest-z80full 0.48, demo-action 0.49, game-aleste 0.49, game-dizzy 0.48

**port-cold-1** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| map | 0.33 | 0.35 | 0.33 | 0.33 |
| buckets | 0.33 | 0.36 | 0.34 | 0.34 |
| bitspainted | 0.33 | 0.32 | 0.33 | 0.33 |
| painted | 0.33 | 0.32 | 0.33 | 0.34 |

**port-cold-1000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| map | 0.49 | 0.47 | 0.45 | 0.44 |
| buckets | 0.33 | 0.33 | 0.33 | 0.34 |
| bitspainted | 0.33 | 0.32 | 0.33 | 0.33 |
| painted | 0.33 | 0.32 | 0.33 | 0.34 |

**port-warm-10** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| map | 0.33 | 0.34 | 0.33 | 0.34 |
| buckets | 0.34 | 0.33 | 0.34 | 0.34 |
| bitspainted | 0.33 | 0.32 | 0.33 | 0.34 |
| painted | 0.33 | 0.32 | 0.33 | 0.34 |

**port-warm-1000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy |
|:--|--:|--:|--:|--:|
| map | 0.33 | 0.34 | 0.33 | 0.34 |
| buckets | 0.34 | 0.33 | 0.33 | 0.34 |
| bitspainted | 0.33 | 0.31 | 0.33 | 0.33 |
| painted | 0.33 | 0.32 | 0.32 | 0.34 |


## What it means

- All four cost the same, about 0.33 ns above the unarmed path, from 1 to 1 000 breakpoints. Ports are
  under 1% of the accesses (`in` / `out` in the [traces](../recorder/README.md)), so the port check hardly
  shows in a whole trace; the 0.33 ns is the armed memory-side gate the replay passes on every other access.
- `map` moves a little at 1 000 cold breakpoints (0.44-0.49 ns): the hash table grows past the cache with
  the masks expanded into every matching port.
- Masks change nothing at run time once they are expanded at set time.

## Decision

Ports use **a bit per port per direction (8 KB) and a painted id table, masks expanded when the breakpoint
is set** - the same shape as memory (02/03), so one set of painting code serves both.
