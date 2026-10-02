# TODO: the halted Z80's opcode fetches go to the byte after the HALT

Design: [design.md](design.md). Bug: [BUGS.md](../BUGS.md) 2026-10-02 #1. Plan: [PLAN.md](../PLAN.md) #81.

| Phase | Item | Status |
|:--|:--|:--|
| 0 | Found and pinned: HALT2INT v3 on the 48K, three values differ from the real early 48K (`Halt2Int_Test`, master `c5471e0b`) | done 2026-10-02 |
| 1 | What the tracker (`isExecution`) and the TTD instruction-start observers see on a halted step; `Z80Halt_Test` pinning today's address | open |
| 2 | unreal-ng: `HaltedM1` (bus at PC + 1), the reordered halted test; `Halt2Int_Test` deviations dropped; full suite, timing suites, RZX full; A/B | open |
| 3 | unreal-z80: `Z80HaltT` at PC + 1; every suite; `z80bench` A/B | open |
| 4 | TTD fixtures re-recorded; docs (memory-contention.md, test-programs.md, contention backlog C9, BUGS.md, PLAN.md) | open |
