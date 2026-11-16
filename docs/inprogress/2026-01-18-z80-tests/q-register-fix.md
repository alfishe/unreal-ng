# Q latch per instruction class (2026-11-15)

z80test's z80ccf (SCF / CCF behind every instruction) failed 4 of 160 tests on every machine: DEC r (056, 058),
POP AF (125) and EX AF,AF' (129). Landed in 84adde805 (merged as 87718293a).

## Cause

Q came in with 3ac5bf6a3 (2026-01-18) as "Q = F & 0x28 if F changed value, else 0". The chip sets Q when the
instruction writes F, whatever the value:

| Instruction | Old rule | Chip |
|:--|:--|:--|
| DEC r leaving F as it was | Q = 0 | Q = F & 0x28 |
| POP AF, EX AF,AF' (F moved, not written by the flag logic) | Q = F & 0x28 | Q = 0 |

Nothing caught it: z80full passes either way, and `Z80TestVerification` drives z80full's vectors only, each with Q = 0.

## Fix

`core/src/emulator/cpu/qregister.h`: a Q mask per instruction for each map (base / DD / FD, CB and DDCB / FDCB, ED),
the classes z80ccf checks. `Z80::Z80Step` sets Q = F & mask from the first opcode byte; the CB, ED and DD / FD handlers
set it from their own map. The old code saved F before the instruction and compared it after.

The same change brings the INIR / INDR MEMPTR fix (repeating: MEMPTR = PC + 1, z80memptr), also on the
pentagon-2666-le branch.

## Tests

- `QRegister_Test`: every instruction of every map (7 x 256) from 4 register states against the unreal-z80 library:
  F, Q and SCF after it.
- `Z80TestSuite_Test` (long tier): all six z80test 1.2a programs on the 48K to "all tests passed"
  (`testdata/cpu/z80test/`).

## A/B (performance-guidelines.md §4)

A = github/master e7d5a5fdb, B = the change, `BM_HostFrame_*_Fast`, `cpu_time` in µs, 30 rounds each
(15 ABAB, then 15 BABA). Columns: minimum per side, difference of the minima, median of the paired differences, the
same median per order, rounds where B was faster.

First version, branchless `Q = (F & m) | (Q & keep)` with a 16-bit table (reads Q back on every instruction), load
183 → 169:

| Benchmark | min A | min B | min diff | paired median | AB / BA | B faster |
|:--|--:|--:|--:|--:|--:|--:|
| 48K | 453.6 | 465.2 | +2.55 % | +0.02 % | -0.57 % / +0.41 % | 15/30 |
| Pentagon | 430.3 | 438.7 | +1.95 % | +2.08 % | +2.90 % / +2.02 % | 5/30 |
| Scorpion | 672.0 | 676.6 | +0.68 % | +1.19 % | +1.31 % / +1.08 % | 10/30 |

Pentagon about 2 % slower in both orders: replaced.

Landed version, 8-bit table, `if (mask != kPrefixed) Q = F & mask` (no read of Q; the branch is taken on prefixed
instructions only). Two runs, load 140 → 132 and 125 → 118:

| Benchmark | min A | min B | min diff | paired median | AB / BA | B faster |
|:--|--:|--:|--:|--:|--:|--:|
| 48K | 459.4 | 468.6 | +1.99 % | -0.17 % | -0.86 % / +0.19 % | 15/30 |
| Pentagon | 433.5 | 428.3 | -1.20 % | +2.46 % | +3.72 % / -0.56 % | 13/30 |
| Scorpion | 698.9 | 693.1 | -0.83 % | +1.94 % | +2.47 % / +1.37 % | 10/30 |
| 48K | 469.6 | 476.7 | +1.50 % | +1.02 % | -0.08 % / +1.96 % | 13/30 |
| Pentagon | 436.9 | 435.3 | -0.36 % | +1.11 % | +1.32 % / -0.20 % | 13/30 |
| Scorpion | 687.7 | 697.0 | +1.35 % | +0.48 % | +0.29 % / +0.67 % | 14/30 |

48K and Pentagon: the sign flips with the order and between runs, no shift. Scorpion: +0.3 % to +2.5 % in both orders
of both runs while B is faster in 10 and 14 of 30 rounds: at most about 1 %, inside the noise and the known A/A bias
between two build trees (about 1.8 %, step perf backlog). Per instruction the epilogue is 8 instructions with a
well-predicted branch, against the old save of F, a compare and a data-dependent branch plus 3-5 instructions.

Candidate if a later run shows a real cost: keep the mask next to the handler pointer in `normal_opcode` (one load
from the line already fetched for the call, no table address to build).
