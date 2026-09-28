# TODO — Contended opcode fetches without a cost for machines that have no contention

**Status:** phases 1a (baselines), 1b (bus interfaces, M1 contention) and 1c (control and diagnostics) done 2026-09-28 on branch `m1-contention` ([baseline.md](baseline.md)). PLAN.md row #59.
Design: [design.md](design.md). Test programs and the probe suite: [test-programs.md](test-programs.md).

## Done
- **Phase 1a — baselines** (2026-09-28): per-model timing fingerprints on every creatable model in five
  placements (`contentionregression_test.cpp`), `BM_ContentionInstructionMix` and the benchmark numbers
  ([baseline.md](baseline.md)); fingerprint memory from a fixed pattern (the state hash depended on test
  order).
- **Phase 1b — bus interfaces and M1** (2026-09-28): `Memory::MemoryReadContended` / `MemoryWriteContended`
  templates over the plain functions (`memorycontended.cpp`), `FastContendedMemIf` / `DbgContendedMemIf`,
  `Core::SelectMemoryInterface` replacing the four selection sites (Core, FeatureManager, Emulator, CLI) and
  called from `Screen::InitRaster`; the contention branch left `Z80::rd` / `wd`; `Z80::ioContention` for
  `in` / `out`. Opcode fetches and every byte read at PC now wait. Tests `M1_*`,
  `MemoryInterfaceSelection_Test`; fingerprints changed in the 11 intended rows only; Pentagon 2-5 % faster.
- **Phase 1c — control and diagnostics** (2026-09-28): the `contention` feature (`cont`, fixed for a TTD
  timeline) and Qt **Machine > Memory Contention**; `DeviceState::Contention` on every surface (CLI
  `state contention`, WebAPI `/state/contention`, Lua / Python `contention_state()`, MCP aspect
  `contention`); debug-only statistics per kind; every memory map's `contended` flag from
  `Core::IsSlotContended`; interface docs and the spectrum recipe updated.

## Remaining (value order)
1. **Phase 1d — test suites A-G** (design §8) and the benchmark gate; assert the FUSE `MC`-only checkpoints
   instead of dropping them (test-programs §2.4 step 1).
2. **Phase 1e — emulated-side suite H:** run the in-tree Butler 48K snapshot to completion; vendor Rak Timing
   Test v0.3 and fusetest (GPL); write the `ctprobe` probe suite (test-programs §3) and its `.tap` / `.trd`
   exports; collect the cross-emulator consensus table.
3. **Phase 2:** no-MREQ contention of internal cycles on the 48K / 128K / +2 (`Idle(addr, n)`).
4. **Phase 3:** multi-point I/O contention (C:1 / C:3) in the 48K / 128K port rules.

Ideas backlog (performance of the contended machines): [baseline.md](baseline.md) §3.2.
