# TODO — Contended opcode fetches without a cost for machines that have no contention

**Status:** phases 1a (baselines), 1b (bus interfaces, M1 contention) 1c (control and diagnostics), 1d (test suites), phase 2 (internal cycles) and phase 3 (multi-point I/O) done 2026-09-28 on branch `m1-contention` ([baseline.md](baseline.md)). PLAN.md row #61.
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

- **Phase 2 — internal (no-MREQ) cycles** (2026-09-28): `Z80::Idle(addr, n)` replaces the address-less cycle
  count at every internal cycle (IR, displacement, HL, SP, DE, BC per instruction); the Ferranti ULA contends
  each T-state, the gate array none. FUSE's no-MREQ checkpoints are now asserted (address and T-state, all
  1356 vectors) and a ULA replay of every vector passes; MAME and xpeccy-plus use the same per-cycle table
  (MAME differs only on the stepped DE/HL of three repeat cycles; we follow FUSE and xpeccy-plus). Butler
  48K: 68 of 70 pass with contention (was 34); the rest was fixed with the floating bus below. The DDCB operation byte is now an
  ordinary read, not an M1 (R +2 as on the hardware). Pentagon cost within noise (~0.5 %).

- **Phase 3 — multi-point I/O contention** (2026-09-28): the four "Contended I/O" patterns (N:4, N:1 C:3,
  C:1 C:3, C:1 x4) on the Ferranti ULA, the high byte read against the current mapping (an odd page at
  #C000 on the 128K counts); the 128K rule's extra T on every even port is gone. `UlaContention::
  IoWaitBeforeIorq` / `IoWaitAfterIorq` from `Z80::in` / `out`. Rak's Timing Test: all 15 reference screens
  match (48K / 128K / +3); the FUSE ULA replay now covers every vector, port cycles included.

- **Floating bus — Butler 48K 72 of 72** (2026-09-28): the suite has 37 tests (1-35 from both RAM kinds,
  36-37 contended with a screen full of text: 72 results, the harness stopped at 70). Test 35 ("IN A,(n);
  OUT (n),A; IN r,(C); OUT (C),r") feeds each port read into the next port's high byte, so the byte read
  decides the contention. A frame-exact I/O trace against SkoolKit's contention simulator (which passes all
  72) matched to the T for 158 iterations and then diverged on #xx1F: three causes, fixed:
  - the standard 48K fits a **Kempston mouse** (#xx1F with A9 set answers the Y counter): the Butler runner
    now runs a bare 48K (`kempstonmouse` off), as the suite was measured;
  - the **Beta 128 ports** (#1F/#3F/#5F/#7F/#FF) answered outside TR-DOS on the 48K, 128K and +3 decoders
    (Pentagon already gated them on `CF_TRDOS`): #FF read the FDC instead of the floating bus;
  - `FloatBus=0` in the 48K / 128K / +2 / +2A / +3 configs (an Unreal Speccy knob): now 1 - the ULA and the
    gate array always drive the bus.
  The floating-bus **phase** was 2 T late on the Ferranti ULA: an I/O cycle starting on the contention onset
  (delay 6) reads the bitmap byte (FUSE, Zero, ZXMAK2, MAME, pico-spec; the "14338" in the articles is FUSE's
  end-of-cycle count of the same T). `UlaContention::FetchLead` (6 on the Ferranti ULA, 4 on the discrete
  clones, unchanged). Tests 36 and 37 (real hardware values) pass only with it.

## Remaining (value order)
1. **Phase 1e — emulated-side suite H** (in progress): done - the Butler 48K suite runs to completion (after
   the `.sna` 48K ROM fix) and the Rak Timing Test matrix against the published screens (test-programs §2.5).
   Fixed on the way: the +2A/+3 gate array window is 129 T (real-hardware photos of the Rak test; every
   emulator surveyed uses 128 - test-programs §2.5). The `ctprobe` probe suite v2 runs (test-programs
   §3.7): 53 cases match an independent oracle on the 48K, 128K, +3, Pentagon and Scorpion; standalone
   reference files `ctprobe.tap` / `ctprobe.trd` print a report and pass on all five when loaded as a user
   does. Open: running them on real hardware and other emulators (§3.6), X-04; fusetest (needs pasmo); the
   cross-emulator consensus table.

Ideas backlog (performance of the contended machines): [baseline.md](baseline.md) §3.2.
