# TODO — Contended opcode fetches without a cost for machines that have no contention

**Status:** phase 1a (baselines) done 2026-09-28 on branch `m1-contention` ([baseline.md](baseline.md)). PLAN.md row #59.
Design: [design.md](design.md). Test programs and the probe suite: [test-programs.md](test-programs.md).

## Done
- **Phase 1a — baselines** (2026-09-28): per-model timing fingerprints on every creatable model in five
  placements (`contentionregression_test.cpp`), `BM_ContentionInstructionMix` and the benchmark numbers
  ([baseline.md](baseline.md)).

## Remaining (value order)
1. **Phase 1b — bus interfaces and M1:** `MemoryReadContended` / `MemoryWriteContended` templates over the
   plain functions, `FastContendedMemIf` / `DbgContendedMemIf`, `Core::SelectMemoryInterface` replacing the
   four selection sites, the branch leaves `Z80::rd` / `wd`, `IoContention` pointer. `UlaContention` gains
   `DelayAt` and the status.
2. **Phase 1c — control and diagnostics:** the `contention` feature (TTD-gated), the status object on every
   surface, the memory maps' `contended` flags from the slot cache, debug-only statistics.
3. **Phase 1d — test suites A-G** (design §8) and the benchmark gate; assert the FUSE `MC`-only checkpoints
   instead of dropping them (test-programs §2.4 step 1).
4. **Phase 1e — emulated-side suite H:** run the in-tree Butler 48K snapshot to completion; vendor Rak Timing
   Test v0.3 and fusetest (GPL); write the `ctprobe` probe suite (test-programs §3) and its `.tap` / `.trd`
   exports; collect the cross-emulator consensus table.
5. **Phase 2:** no-MREQ contention of internal cycles on the 48K / 128K / +2 (`Idle(addr, n)`).
6. **Phase 3:** multi-point I/O contention (C:1 / C:3) in the 48K / 128K port rules.
