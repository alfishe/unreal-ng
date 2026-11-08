# 046 — analysis

Numbers: [measurements-2026-11-05-cost-accuracy.md](measurements-2026-11-05-cost-accuracy.md). Variant names as
there (A-off, B-counts, C-time16, C8-time8, D-stack, E-beam, F-frames, G-all, Di / Gi, K0-K3 for the core analyzer).

## Cost against the expectations

| Part | Expected (design §9) | Measured | Comment |
|:--|:--|:--|:--|
| counts only | (part of +5-10 %) | +3.4 % Zynaps, +7.8 % Nether | the dispatch through `AnalyzerManager` and the physical offset are most of it |
| + T-states | +5-10 % with counts | +15-19 % (7-8.6 ns an instruction) | twice the estimate: the delta and the second cell's store miss the estimate's "< 0.5 ns" each; the step callback is a real call per instruction, not inlined |
| 8 B cells with a spill vs 16 B | 8 B cheaper (half the memory) | 8 B +0.5 % slower | the cells of a game fit the cache either way (1-2 pages); the spill branch costs more than the bytes save |
| + the shadow stack, decoded per step | +10-15 % total | +21-25 % total (D), +3-5 ns | the per-step store of PC / SP / class and the class table lookup |
| + the shadow stack from CPU flow events | | equal to the cells alone (K0 8.57, K1 8.58 ns on Zynaps) | only the 26 CALL / RST / RET handlers test the hook; no per-step stack work remains |
| + beam bins | < 0.5 ns | +2-5 % over the stack | the multiply and a per-routine bin array touched every step |
| + the per-frame series | 2-5 µs a frame | 1.1-1.3 µs a frame, +1-5 % (noise level) | 30 active routines a frame on Zynaps, 4 on Nether |
| a mark | | 9-22 µs | a copy of 1-2 pages of cells |
| the halted idle, stepped | "to measure" | +95-115 % on a HALT-paced demo | every idle fetch became a callback; idle-tolerant subscription: +1-5 % |

The measurements ran on a shared machine (load 60-240); each table compares its variants within one interleaved run,
so the order holds; differences under ~3 % are not significant (Nether's K2 below K1 shows the noise floor).

## Variants compared

- **Cell width.** 16 B cells win: no gain from 8 B, and the 16 B cell has room for the routine id of an entry in the
  count's high bits, so the entry test reads nothing more.
- **Flow source.** The CPU flow events (K1) remove the stack's cost; the opcode decoder (K1o) is +1 ns, the
  memory re-read (K1m, the POC's original way) +3 ns. The three give the same profile (unit test
  `FlowSourcesAgree`), so the events are the default and the decoders stay only for the A/B and that test.
- **Idle.** The idle-tolerant subscription is what makes the profile usable on HALT-paced programs: the delta model
  needs no callback per idle fetch, the stretch lands on the HALT at the next instruction start.

## Accuracy against the expectations

- **Per instruction: exact.** Counts equal L1's for every instruction of both recordings (4918 and 5265 keys); T-states
  equal with the acknowledge folded back as L1 does it. The only remaining difference is the window's last
  instruction (4-7 T): L1 closes it at the window end, the profile at the next instruction start. The core analyzer
  equals the POC on every cell.
- **Per routine, self.** By address (nearest entry, L1's entries as the map) the profile gives L1's self exactly
  (0.000 ‰ for the top 24). By the stack it differs where L1's CFG splits fall-through entries (Nether `FD69` /
  `FD6D`: 371 ‰ moves between the two) or the code is entered by JP (Zynaps `B360`-`B363`). Not an error: the stack
  answers "whose frame is running", L1 "which routine owns this code". The kit export plus `fig_budget.py --ann`
  gives the second.
- **Per routine, inclusive.** Nether within 0.1 ‰. Zynaps' scenery script runs as RET chains on its own stack: L1
  stacks a frame per dispatch, so its inclusive of the chain's routines exceeds the window (5.7x); the core keeps a
  chain one frame deep (design §5.2 rule 4), which also stopped the 1024-frame overflow (83 821 overflows in 3000
  frames before, 0 after, max depth 14).
- **Discovery.** Every CALL, RST and interrupt entry of L1 is found. Entries reached only by JP, by `JP (HL)` or by
  falling through are found only when a CALL or a dispatch also reaches them, as designed: the profile counts the
  JP arrivals at known entries (`jump_ins`) but does not make a JP target an entry.

## Surprises

1. The halted idle doubled the cost of a HALT-paced program: the profile must not force stepping, hence
   `subscribeCPUStep(..., idleTolerant)`.
2. Stacking dispatch frames (the first core build) overflowed on Zynaps; a RET chain must replace its dispatch frame.
3. `ProfileAnalyzer::Reset` before the first activation crashed (no routine chunk yet); fixed and covered.
4. The core's `start` keeps the settings it does not name, so a `stack=false` start followed by a `beam=true` start
   is refused ("beam and frames need the call stack"); the POC's bench variants now name every option.
5. The window was a frame short: the last instruction's T-states and the frame end after it are booked at the next
   instruction start, which a stop never sees. `ProfileAnalyzer::Settle` books them at the stop; the core's kit now
   has L1's frames and T-states exactly (the POC, which has no settle, keeps the 4-7 T gap).
6. A refused `start` (a bad `ring_kib`) switched a running profile off: the checks ran after the deactivation. They
   run first now (`ProfileAnalyzer::Validate`); found by the live MCP script.
