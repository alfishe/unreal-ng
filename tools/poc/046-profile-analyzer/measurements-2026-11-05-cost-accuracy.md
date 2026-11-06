# POC 046 measurements, 2026-11-05: cost and accuracy

Raw data: `scratch/2026-11-05-poc-046/bench/*.txt` (cost), `scratch/2026-11-05-poc-046/replay/` (accuracy).

## Setup

- macOS 15 (Darwin 24.6), Apple Silicon, release build with LTO (`cmake-build-agent-release`).
- `poc046-profile bench`: 1500 turbo frames on the calling thread, thread CPU time, 12 rounds per variant, the
  variants interleaved A B C ... then reversed; the machine was shared (load 61 before, 243 after: the medians carry
  the noise, the order of the variants holds in every workload).
- Workloads: Zynaps `r1/start.szx` (48K, play, 11.9 M instructions), Nether Earth `play` and `deep` start snapshots
  (48K, 10.7 M instructions), 7th Reality (`testdata/loaders/sna/7threality.sna`, Pentagon, HALT-paced, 1.6 M
  instructions in 1500 frames).

## Variants

| Variant | What runs on each step |
|:--|:--|
| A-off | no subscriber |
| B-counts | execution count per physical byte |
| C-time16 | + T-states per byte, 16 B cells (count 64 bit, T 64 bit) |
| C8-time8 | + T-states, 8 B cells (32 + 32 bit, spill on overflow) |
| D-stack | C16 + the shadow call stack (self / inclusive, entries, anomalies) |
| E-beam | D + self T-states per 1/64 of the frame |
| F-frames | D + the per-frame series (flush at the frame end) |
| G-all | D + beam + frames |
| Di / Gi | D / G with the subscriber idle-tolerant (a halted stretch run in one go) |

## Cost: median ms per 1500 frames, % over A-off, ns per instruction

| Variant | Zynaps | Nether play | Nether deep | 7th Reality |
|:--|--:|--:|--:|--:|
| A-off | 549.9 | 437.6 | 434.0 | 696.2 |
| B-counts | +3.4 % (1.6) | +7.8 % (3.2) | +7.7 % (3.1) | +94.7 % |
| C-time16 | +15.2 % (7.0) | +15.1 % (6.2) | +18.6 % (7.5) | +104.9 % |
| C8-time8 | +15.7 % (7.3) | +16.4 % (6.7) | +18.7 % (7.6) | +107.5 % |
| D-stack | +23.3 % (10.8) | +20.8 % (8.5) | +24.5 % (9.9) | +109.0 % |
| E-beam | +27.9 % (12.9) | +26.3 % (10.7) | +27.3 % (11.1) | +114.4 % |
| F-frames | +25.6 % (11.9) | +22.5 % (9.2) | +21.8 % (8.8) | +107.0 % |
| G-all | +30.1 % (13.9) | +25.0 % (10.2) | +26.3 % (10.7) | +114.5 % |
| Di-stack-idle | +22.5 % (10.4) | +19.5 % (8.0) | +24.5 % (9.9) | +3.3 % |
| Gi-all-idle | +28.4 % (13.1) | +25.4 % (10.4) | +30.0 % (12.2) | +3.2 % |

7th Reality's ns per instruction is not comparable (400+ ns): a halted CPU is stepped one 4 T fetch at a time when
any step subscriber is not idle-tolerant, so the cost is the stepping, not the profile. Idle-tolerant, it is +3 %.

## Off against master

Raw data: `scratch/2026-11-05-poc-046/ab-off-2/ab.csv` (`ab-off/run-ab.sh 20`): `core-benchmarks` of master
`8801b5e64` (base worktree) and of this tree (the flow hook in the CALL / RST / RET handlers, the profile off), 20
rounds A B / B A, thread CPU time (real time is scheduling noise at load 90-130). An earlier 8-round run
(`ab-off/`) gave -1.4 % .. +1.1 %.

| Benchmark | master, median | this tree, median | diff of medians | paired median | this tree slower in |
|:--|--:|--:|--:|--:|--:|
| BM_HostFrame_48K_Fast | 481.3 µs | 488.5 µs | +1.50 % | +0.15 % | 10 / 20 |
| BM_HostFrame_Pentagon_Fast | 459.2 µs | 452.9 µs | -1.38 % | -1.87 % | 8 / 20 |
| BM_HostFrame_Sprinter_Fast | 1949.3 µs | 1966.6 µs | +0.89 % | +0.04 % | 11 / 20 |
| BM_NextRuntime/cpu3_busy | 15429 µs | 15319 µs | -0.72 % | -0.39 % | 9 / 20 |

The sign does not hold across rounds (8-11 of 20 either way) and the paired medians are within ±2 %, both
directions: off costs nothing measurable.

## Cost of the core analyzer and its flow source

Raw data: `scratch/2026-11-05-poc-046/bench-flow/*.txt` (`run-bench-flow.sh`). The core `ProfileAnalyzer` itself
(not the POC's step functions) after the CPU flow events and the RET-chain fix, against off and the POC's per-step
stack (D); 1500 turbo frames, 10 rounds interleaved, thread CPU time, load 117-124 (the machine shared with other
builds: differences under ~3 % are noise, as the Nether column shows).

| Variant | What | Zynaps | Nether play | 7th Reality |
|:--|:--|--:|--:|--:|
| A-off | no subscriber | 548.3 ms | 432.7 ms | 689.6 ms |
| D-stack | the POC: cells + stack decoded per step | +24.5 % (11.3) | +22.7 % (9.2) | +112 % (stepped idle) |
| K0-core-cells | cells and T only (`stack=false`) | +18.6 % (8.6) | +17.1 % (6.9) | +4.7 % |
| K1-core-stack | + the stack from the CPU flow events (`flowSource` 0, the default) | +18.6 % (8.6) | +16.8 % (6.8) | +4.0 % |
| K1o | + the stack decoded per step from `opcode` / `prefix` (`flowSource` 1) | +21.1 % (9.7) | +14.9 % (6.0) | +1.6 % |
| K1m | + the stack decoded per step from the byte in memory (`flowSource` 2) | +25.2 % (11.6) | +24.1 % (9.7) | +1.7 % |
| K2-core-default | K1 + the per-frame series (the `start` default) | +23.2 % (10.7) | +13.4 % (5.4) | +0.9 % |
| K3-core-all | K2 + beam bins | +25.2 % (11.6) | +19.0 % (7.7) | +1.3 % |

(ns per instruction in brackets.) With the flow events the shadow stack costs nothing measurable over the cells
(Zynaps 8.57 vs 8.58 ns an instruction): the per-instruction work is the cell update, the stack moves only in the
CALL / RST / RET handlers. Decoding per step costs +1.1 ns (opcode) to +3 ns (memory) on Zynaps. The core's cells
(8.6 ns) and the POC's C-time16 of the first run (7.0 ns) were measured under different loads and are not compared;
within this run D (the POC's stack) is 2.7 ns above K1. 7th Reality, HALT-paced, is idle-tolerant in
every core variant: +1-5 %, within the noise.

## Sizes and the per-frame series

| | Zynaps | Nether | 7th Reality |
|:--|--:|--:|--:|
| pages with executed code | 2 | 1 | 2 |
| routines found | 120 | 7 | 61 |
| active routines a frame (mean / max) | 30.5 / 52 | 3.7 / 8 | 8.4 / 13 |
| frame flush, mean µs (max) | 1.1-1.3 (102, one page fault) | 0.2-0.3 (105) | 0.5-0.6 (102) |
| a mark's baseline copy, µs | 9-16 | 2-6 | 8-22 |

The ~100 µs maximum is the first touch of the ring (a page fault), once a run.

## Accuracy

Replays of the recordings the reverse-cases L1 files were built from (`games/nether-earth/work/play`, 16 513 frames,
153.5 M instructions; `games/zynaps/work/r1`, 75 002 frames, 634.7 M instructions), slots `edge.1=kempston,
ay-socket=tsfm` as recorded. Compared with `python/compare_l1.py` (the POC profile) and `python/compare_kit.py` (the
core analyzer's kit export, `poc046-profile replay --core 1`). Files: `replay/*-vs-*.json`; the core runs before the
RET-chain fix in `replay/pre-retchain/`.

### Per instruction

| | Nether Earth play | Zynaps r1 |
|:--|:--|:--|
| instructions L1 lists / the profile has | 4918 / 4918 (none only on one side) | 5265 / 5265 |
| POC (acknowledge folded as L1) vs L1: counts equal | 4918 | 5265 |
| ... T-states equal | 4917; the one left is the window's last instruction, 7 T | 5264; the last one, 4 T |
| core vs POC (acknowledge apart, as the core books it): counts / T equal | 4918 / 4917 (the POC misses the last instruction's 7 T) | 5265 / 5264 (4 T) |
| core vs L1: window totals | 16 514 frames, 1 154 130 431 T: equal | 75 003 frames, 5 241 809 042 T: equal |
| core vs L1: T equal per instruction | 4683; the others differ by the acknowledges moved to the handler's first instruction, sum exactly 150 860 T = 2 x 75 430 (`acknowledge_tstates`) | 5101; exactly 1 461 746 T = 2 x 730 873 |

Counts and T-states are exact against L1: the only difference is where the acknowledge is booked (documented, folded
back for the kit by the POC). The core closes the window at the stop (`ProfileAnalyzer::Settle`: the last
instruction's T and the frame that ended after it); before that fix the kit export was one frame and the last
instruction (4-7 T) short, like the POC still is.

`fig_budget.py` (reverse-cases kit, unchanged) on the core's Zynaps kit and on the L1 with the same `game.ann`
(`fig/core.txt`, `fig/l1.txt`, `fig/*.png`): the same 24 routines in the same order, shares equal to 0.01 %
(EffectPlay 22.49 vs 22.50 %, HALT 1.61 vs 1.62 %: the acknowledges).

### Per routine (top 24 by L1 self, ‰ of the window)

| | Nether Earth play | Zynaps r1 |
|:--|--:|--:|
| self by address (nearest entry at or before, L1's entries as the map) vs L1 self | 0.000 | 0.000 |
| self by the stack vs L1 self | 371 (`FD69` / `FD6D`: a fall-through pair L1's CFG splits) | 96, the core 84 (`B360`..`B363`, `85E7` / `8603`: fall-through entries and jump-entered code) |
| inclusive, routines both have frames for, vs L1 | 0.10 | 4871 (L1 stacks a frame per dispatch of the scenery script's RET chain, so its inclusive is up to 5.7x the window; the core keeps a chain one frame deep) |
| core vs POC: self, worst | 0.000 | 1.8 |
| core vs POC: inclusive, worst | 233 (the root dispatch frame) | 873 (`B1E9`, `8797`: the POC stacked RET-chain frames, the core replaces them) |

Shadow-stack self and L1's CFG self answer different questions (the routine whose frame runs vs the code a routine
owns); with a map, attribution by address gives L1's numbers exactly, which is what `fig_budget.py --ann` does.

### Discovery against L1's routines

| L1 kind | Nether: L1 / found | Zynaps: L1 / found |
|:--|--:|--:|
| call | 194 / 194 | 112 / 112 |
| rst | 5 / 5 | - |
| interrupt | 2 / 2 | 1 / 1 |
| dispatch | 46 / 27 | 90 / 90 |
| jump (JP-entered, no CALL) | 64 / 17 | 39 / 13 |
| indirect | 18 / 2 | 50 / 4 |
| shared (fall-through) | 22 / 0 | 20 / 0 |
| entries the profile found that L1 lacks | 0 | 4 |

Call / RST / interrupt entries: 100 % (criterion ≥ 90 %). JP-entered, indirect and fall-through entries are found
only when some CALL or dispatch also reaches them: the profile never sees a JP as an entry (by design, §5.3).

### Stack health (core, after the RET-chain fix)

| | Nether Earth play | Zynaps r1 |
|:--|--:|--:|
| max depth | 19 | 14 |
| stack overflows (1024 frames) | 0 | 0 (83 821 in 3000 frames before the fix) |
| abandoned frames | 330 | 86 660 (the stack blitters move SP over return addresses) |
| patched returns | 462 | 10 |
| routines | 226 | 208 |
| replay time with the core profile on (thread CPU) | 14.3 s | 47.2 s |
