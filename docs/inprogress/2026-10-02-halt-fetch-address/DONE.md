# DONE: the halted Z80's opcode fetches go to the byte after the HALT

Design: [design.md](design.md); what it touches: [impact.md](impact.md). Bug: [BUGS.md](../BUGS.md) 2026-10-02 #1. Plan: [PLAN.md](../PLAN.md) #81.

| Phase | Item | Status |
|:--|:--|:--|
| 0 | Found and pinned: HALT2INT v3 on the 48K, three values differ from the real early 48K (`Halt2Int_Test`, master `c5471e0b`) | done 2026-10-02 |
| 1 | The tracker counts the idle fetch as an execute access at HALT + 1 (design §4.1); the instruction start stays on the HALT. Tests first, failing on the old code: `Contention48K_Test.HaltedFetchGoesToTheByteAfterTheHalt` (#7FFF: 10 T instead of 4), `Contention128K_Test.HaltedFetchGoesToTheByteAfterTheHalt` | done 2026-10-02 |
| 2 | unreal-ng: `Z80::HaltedM1` (bus at PC + 1), the halted test first; `Halt2Int_Test` with no deviation, `Halt2Int_Test.Early128KIsRecognized` (the 128K program finds "Early" for the HALT too); 6196 tests with the timing, RZX and tape sweep suites; the RZX corpus (14 recordings) plays to the end with the same drift as before, to the T. A/B: pending a quiet machine | done 2026-10-02 (branch `halt-fetch-fix`), A/B open |
| 3 | unreal-z80 (library branch `halt-fetch`, `0b1a8d3`): `Z80HaltT` at PC + 1; z80tests (z80test, the four ZEX), z80ttrace byte-exact, z80inttests 28/28 (a new #7FFF case), z80fusetests, z80diff-z80ex 0 hard mismatches with and without contend. Vendored copies: General Sound's unreal-z80 (the HALT hunks only, noted as a local patch), the Sprinter's Z84C15 (`Z84HaltT`; `Z84Cpu_Test.HaltedCpuFetchesTheByteAfterTheHalt`). `z80bench` A/B: pending a quiet machine | done 2026-10-02, A/B open |
| 4 | TTD fixtures: the TTD tests pass unchanged, nothing to re-record. Docs: memory-contention.md ("The halted CPU"), test-programs.md, contention backlog C9, BUGS.md (Fix Proposed), PLAN.md #81 | done 2026-10-02 |

## Closed 2026-10-03

What landed: `cf5b28ac1` (merged `1e5782f2f`): `Z80::HaltedM1`, the Sprinter's Z84C15 core and General Sound's
vendored unreal-z80; permanent documentation: [memory-contention.md](../../emulator/design/core/memory-contention.md)
("The halted CPU"). Evidence: `Halt2Int_Test` without a known deviation, `Contention48K_Test` /
`Contention128K_Test.HaltedFetchGoesToTheByteAfterTheHalt` (they fail on the old code), the whole suite green on master.

The one item left open when it landed was the A/B of the non-halted path (performance guidelines section 4). Done
2026-10-03 on a quiet machine: two Release builds with `-DBENCHMARKS=ON` from the same checkout, A = master with the Z80
part of `cf5b28ac1` reverted (`z80.cpp`, `z80.h`, `op_noprefix.cpp`), B = master; `BM_HostFrame_{48K,Pentagon,Scorpion}_Fast`
(1000 frames), run from each build's `bin`, five runs a side in the order A B A B A B B A B A, 1-minute load 9.5-11.9.
CPU time per 1000 frames (us):

| Benchmark | A (5 runs) | B (5 runs) | best of 5, B vs A |
|:--|:--|:--|:--|
| 48K Fast | 1161 1153 1163 1151 1168 | 1160 1153 1179 1185 1163 | +0.12 % |
| Pentagon Fast | 1551 1565 1580 1572 1573 | 1550 1552 1606 1714 1572 | -0.03 % |
| Scorpion Fast | 1896 1890 1960 1927 1927 | 1883 1924 1942 2067 1916 | -0.37 % |

The means differ by +0.7 .. +2 % only because of the pair of runs 7 and 8 (load 11.3 and 11.9, just under the gate: B
1714 against A 1572 on the Pentagon); the sign of the other pairs is mixed in both orders, so there is no cost within
the +-2 % noise of this procedure.
