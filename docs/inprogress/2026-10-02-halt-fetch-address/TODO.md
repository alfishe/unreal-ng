# TODO: the halted Z80's opcode fetches go to the byte after the HALT

Design: [design.md](design.md); what it touches: [impact.md](impact.md). Bug: [BUGS.md](../BUGS.md) 2026-10-02 #1. Plan: [PLAN.md](../PLAN.md) #81.

| Phase | Item | Status |
|:--|:--|:--|
| 0 | Found and pinned: HALT2INT v3 on the 48K, three values differ from the real early 48K (`Halt2Int_Test`, master `c5471e0b`) | done 2026-10-02 |
| 1 | The tracker counts the idle fetch as an execute access at HALT + 1 (design §4.1); the instruction start stays on the HALT. Tests first, failing on the old code: `Contention48K_Test.HaltedFetchGoesToTheByteAfterTheHalt` (#7FFF: 10 T instead of 4), `Contention128K_Test.HaltedFetchGoesToTheByteAfterTheHalt` | done 2026-10-02 |
| 2 | unreal-ng: `Z80::HaltedM1` (bus at PC + 1), the halted test first; `Halt2Int_Test` with no deviation, `Halt2Int_Test.Early128KIsRecognized` (the 128K program finds "Early" for the HALT too); 6196 tests with the timing, RZX and tape sweep suites; the RZX corpus (14 recordings) plays to the end with the same drift as before, to the T. A/B: pending a quiet machine | done 2026-10-02 (branch `halt-fetch-fix`), A/B open |
| 3 | unreal-z80 (library branch `halt-fetch`, `0b1a8d3`): `Z80HaltT` at PC + 1; z80tests (z80test, the four ZEX), z80ttrace byte-exact, z80inttests 28/28 (a new #7FFF case), z80fusetests, z80diff-z80ex 0 hard mismatches with and without contend. Vendored copies: General Sound's unreal-z80 (the HALT hunks only, noted as a local patch), the Sprinter's Z84C15 (`Z84HaltT`; `Z84Cpu_Test.HaltedCpuFetchesTheByteAfterTheHalt`). `z80bench` A/B: pending a quiet machine | done 2026-10-02, A/B open |
| 4 | TTD fixtures: the TTD tests pass unchanged, nothing to re-record. Docs: memory-contention.md ("The halted CPU"), test-programs.md, contention backlog C9, BUGS.md (Fix Proposed), PLAN.md #81 | done 2026-10-02 |
