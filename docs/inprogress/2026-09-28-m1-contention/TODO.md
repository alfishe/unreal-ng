# TODO — Contended opcode fetches without a cost for machines that have no contention

**Status:** phases 1a (baselines), 1b (bus interfaces, M1 contention) 1c (control and diagnostics) and 1d (test suites) done 2026-09-28 on branch `m1-contention` ([baseline.md](baseline.md)). PLAN.md row #61.
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
- **Phase 1d — test suites** (2026-09-28): the FUSE vectors replayed contended on the +3 all-RAM layout
  (1356 opcodes x 9 start T-states against an independent oracle, with negative controls), clones never
  wait, debugger x switch selection, TTD replay of code in contended RAM (checkpoint and mid-frame
  re-execution); FUSE parsing moved to `_helpers/fusevectors.h`. Found on the way: master's TTD replay
  mode picked the plain debug interface (history replayed uncontended) - fixed in `86d414b0`. Status per
  suite: design §8.0.

## Remaining (value order)
1. **Phase 1e — emulated-side suite H** (in progress): done - the Butler 48K suite runs to completion (after
   the `.sna` 48K ROM fix) and the Rak Timing Test matrix against the published screens (test-programs §2.5).
   Fixed on the way: the +2A/+3 gate array window is 129 T (real-hardware photos of the Rak test; every
   emulator surveyed uses 128 - test-programs §2.5). Open: fusetest
   (needs pasmo); the `ctprobe` probe suite (test-programs §3) and its `.tap` / `.trd` exports; the
   cross-emulator consensus table.
2. **Phase 2:** no-MREQ contention of internal cycles on the 48K / 128K / +2 (`Idle(addr, n)`); then
   assert FUSE's `MC`-only checkpoints (test-programs §2.4 step 1) and extend the FUSE contended replay to
   the ULA rule.
3. **Phase 3:** multi-point I/O contention (C:1 / C:3) in the 48K / 128K port rules; drop the 128K rule's
   extra 1 T on even ports (the Rak 128K `IN #00FE` reference has none). Targets: Rak 48K tests 4-5, 128K
   tests 2 and 4.

Ideas backlog (performance of the contended machines): [baseline.md](baseline.md) §3.2.
