# Experiment 06 - whole matchers

Part of [PoC 022](../README.md).

## Goal

Put the winners of 02-05 together and measure whole matchers on mixed sets the way a debugging session
builds them (55% code points, 20% watchpoints, 13% CPU-address ranges, 7% physical ranges, 5% masked ports;
ranges of 64 bytes), N = 0 to 10 000, on all traces including `remapheavy-dizzy`. Every candidate keeps the
"any breakpoint of this kind?" gate first, so an empty set costs what the unarmed path costs.

## Candidates

| Matcher | Miss path | Hit path | Remap |
|:--|:--|:--|:--|
| `threeloads` | CPU-address id table, the slot's page slice, the slot's slot-bound slice (up to 3 loads) | the first non-zero id | move pointers |
| `globalbits` | one bit in an 8 KB CPU-address filter that over-approximates (a physical offset marked in all four slots) | the three loads | move pointers |
| `slotbits` | one bit in a per-slot filter = CPU bits of the slot OR the page's bits OR the slot-bound bits (exact) | the three loads | recombine the slot's 2 KB per kind (copy when the page has no slices) |
| `threebits` | three bits ORed before one branch (CPU address, the slot's page, slot-bound), all L1 | the three loads | move pointers |

Ports: the bit filter and painted ids of 05 in all four.

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

Run `2026-10-03_2356 - full run`: the shared dev machine at load 15-50, benchmarks interleaved in random order, three
repetitions, the minimum shown - relative numbers. All sets: `report.py "scratch/poc-022/data/results/06-combined/2026-10-03_2356 - full run"` after unpacking the [data](../data/README.md).

ns per access above the unarmed path (0.49 ns; 0.59 ns on `remapheavy-dizzy`).

Unarmed path (ns per access, the floor subtracted below): cputest-z80full 0.49, demo-action 0.49, game-aleste 0.49, game-dizzy 0.49, remapheavy-dizzy 0.59

**mixed-cold-0** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| threeloads | 0.89 | 0.88 | 0.87 | 0.80 | 0.83 |
| globalbits | 0.90 | 0.86 | 0.87 | 0.80 | 0.83 |
| threebits | 2.00 | 1.93 | 1.96 | 1.77 | 1.81 |
| slotbits | 0.90 | 0.87 | 0.87 | 0.80 | 0.86 |

**mixed-cold-1** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| threeloads | 1.60 | 1.51 | 1.54 | 1.45 | 1.47 |
| globalbits | 1.41 | 1.29 | 1.30 | 1.22 | 1.27 |
| threebits | 2.23 | 2.14 | 2.17 | 2.00 | 2.04 |
| slotbits | 1.42 | 1.28 | 1.30 | 1.23 | 1.80 |

**mixed-cold-10** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| threeloads | 1.76 | 1.64 | 1.67 | 1.56 | 1.58 |
| globalbits | 1.41 | 1.26 | 1.30 | 1.22 | 1.25 |
| threebits | 2.38 | 2.27 | 2.31 | 2.12 | 2.15 |
| slotbits | 1.41 | 1.30 | 1.31 | 1.25 | 2.72 |

**mixed-cold-100** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| threeloads | 1.77 | 1.65 | 1.65 | 1.72 | 1.76 |
| globalbits | 1.39 | 1.31 | 1.29 | 1.46 | 1.50 |
| threebits | 2.37 | 2.27 | 2.31 | 2.29 | 2.29 |
| slotbits | 1.40 | 1.30 | 1.32 | 1.47 | 3.32 |

**mixed-cold-1000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| threeloads | 1.77 | 1.66 | 1.67 | 1.71 | 1.71 |
| globalbits | 1.42 | 1.42 | 1.52 | 1.52 | 1.55 |
| threebits | 2.39 | 2.28 | 2.30 | 2.39 | 2.35 |
| slotbits | 1.42 | 1.29 | 1.31 | 1.53 | 3.43 |

**mixed-cold-10000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| threeloads | 1.80 | 1.75 | 1.70 | 1.72 | 1.77 |
| globalbits | 1.87 | 1.98 | 1.73 | 1.59 | 1.66 |
| threebits | 2.48 | 2.38 | 2.45 | 2.43 | 2.41 |
| slotbits | 1.44 | 1.44 | 1.52 | 1.58 | 3.57 |

**mixed-warm-1** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| threeloads | 1.60 | 1.51 | 1.53 | 1.44 | 1.46 |
| globalbits | 1.42 | 1.28 | 1.27 | 1.24 | 1.26 |
| threebits | 2.25 | 2.15 | 2.17 | 2.01 | 2.01 |
| slotbits | 1.42 | 1.28 | 1.30 | 1.24 | 1.79 |

**mixed-warm-10** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| threeloads | 1.81 | 1.63 | 1.69 | 1.55 | 1.58 |
| globalbits | 1.43 | 1.29 | 1.29 | 1.20 | 1.26 |
| threebits | 2.42 | 2.31 | 2.31 | 2.13 | 2.14 |
| slotbits | 1.42 | 1.29 | 1.30 | 1.25 | 2.73 |

**mixed-warm-100** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| threeloads | 1.76 | 1.67 | 1.69 | 1.56 | 1.58 |
| globalbits | 1.50 | 1.33 | 1.30 | 1.37 | 1.37 |
| threebits | 2.42 | 2.29 | 2.31 | 2.18 | 2.22 |
| slotbits | 1.39 | 1.30 | 1.30 | 1.27 | 3.14 |

**mixed-warm-1000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| threeloads | 1.73 | 1.77 | 1.71 | 1.63 | 1.67 |
| globalbits | 1.90 | 1.72 | 1.63 | 1.48 | 1.51 |
| threebits | 2.64 | 2.48 | 2.48 | 2.21 | 2.29 |
| slotbits | 1.78 | 1.48 | 1.44 | 1.45 | 3.39 |

**mixed-warm-10000** (ns per access above unarmed)

| matcher | cputest-z80full | demo-action | game-aleste | game-dizzy | remapheavy-dizzy |
|:--|--:|--:|--:|--:|--:|
| threeloads | 1.62 | 2.78 | 1.71 | 1.70 | 1.66 |
| globalbits | 1.87 | 3.01 | 1.77 | 2.01 | 1.92 |
| threebits | 2.51 | 4.08 | 2.52 | 2.34 | 2.28 |
| slotbits | 1.80 | 2.66 | 1.62 | 1.67 | 3.51 |


## What it means

- **`globalbits` and `slotbits` tie on the recorded traces up to 1 000 breakpoints** (1.2-1.5 ns).
- **On the remap-heavy trace `slotbits` loses** 0.5-1.9 ns: recombining the slot's filter on every remap
  costs. **`globalbits` does not care** how often the program pages (1.25-1.9 ns).
- **At 10 000 mixed breakpoints `globalbits` falls behind `slotbits`** (1.6-2.0 against 1.4-1.6 ns cold). Its
  shared filter marks every physical offset in all four slots, and some 700 physical ranges make the filter
  say "maybe" often. A debugging session with hundreds of physical ranges is unusual.
- **`threebits` is the slowest throughout** (2.0-2.6 ns): three dependent loads on every armed access cost
  more than one bit test and a rare resolve.
- **`threeloads`** (no filter, up to three table loads) sits between them (1.45-1.8 ns) and does not grow.
- **Even an empty set costs 0.8-0.9 ns above unarmed here** (0.45 ns in 02/03). That is the shape of a
  whole matcher: one function for every kind, the kind dispatched inside, the gate behind it. In the core
  each call site knows its kind, and the gate and the filter bit go inline at the call site (02's
  decision), so this overhead is not part of the design.

## Decision

**`globalbits`**, by the rule of
[hotpath-matching-design.md](../../../../docs/inprogress/2026-08-17-conditional-breakpoints/hotpath-matching-design.md) §6:
`slotbits` wins nowhere on the recorded traces and costs up to 1.9 ns more when code pages often - and ATM,
TS-Conf and Sprinter software pages far more often than a 128K game.

Recorded for later: if sessions with hundreds of physical ranges turn out to matter, resolve a filter "maybe"
through the page's 2 KB bit slice before the id slices, which keeps the over-approximation cheap.
