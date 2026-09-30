# TODO — Contended opcode fetches without a cost for machines that have no contention

**Status (2026-09-29):** phases 1a-1d, 2 and 3 on master; the Scorpion's Even M1 in the core, the ctprobe
probe suite, the co-emulation harness (eight runners incl. SkoolKit) and the probe's Even M1 mode, all on
master (see Done). **Where we stopped:** the six follow-ups in "Remaining": items 1, 2, 3 and 6 done,
item 4 (not modeled yet) done on branch `not-modeled-waits`, item 5 (harness) next. PLAN.md row #61.
Design: [design.md](design.md). Test programs and the probe suite: [test-programs.md](test-programs.md).
How the work went: [walkthrough.md](walkthrough.md). For AI agents writing such tests: [test-writing-guide.md](test-writing-guide.md).

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

- **Machines research, Scorpion Even M1** (2026-09-29): which machines contend, by circuit
  ([contention-by-machine.md](contention-by-machine.md)); the stable summary is
  [docs/emulator/design/core/memory-contention.md](../../emulator/design/core/memory-contention.md). The
  Scorpion has no contention but Even M1 (an opcode fetch from RAM that would start on an odd T-state waits
  one): in the core since `f2323325` (`EvenM1_Test`).
- **ctprobe v2 and the co-emulation harness** (2026-09-29, master): 53 cases, standalone `.tap` / `.trd`
  printing through the ROM; `tools/verification/coemu` runs it on unreal-ng, FUSE, MAME, ZEsarUX, ZXMAK2,
  xpeccy-plus, Xpeccy and SkoolKit (`db0efe06`, PLAN #74); results table in the probe's README.
- **ctprobe measures Even M1 machines** (2026-09-29, `3952bdc8`): 2 T-state delays
  (`DELAYE`) and two worked-out engine corrections; classes 5 (Scorpion) and 6 (Even M1 without the
  attribute bus). unreal-ng, MAME and ZXMAK2 all as expected on the Scorpion; xpeccy-plus differs on P-02
  only (2 T: it adds the tick before sampling the interrupt).

## Remaining (value order, the six follow-ups agreed 2026-09-29)
1. ~~**Probe engine for Even M1 machines**~~ - done (`3952bdc8`, see Done).
2. ~~**Snow**~~ - done 2026-09-29 on branch `ula-snow`: snow and double in the core, anchored on Snow Hold's
   photos from three real 48K machines; the visual test program snowtest; the floating-bus check dropped as not
   observable ([2026-09-29-ula-snow](../2026-09-29-ula-snow/TODO.md)). Next: follow-up 3.
3. ~~**Real-hardware kit**~~ - done 2026-09-29: [tools/verification/contention/README.md](../../../tools/verification/contention/README.md)
   lists what only hardware settles (P-05 on the 128K, Even M1 per Scorpion board, the Even M1 start offset,
   snow on the 128K / +2 and on the +2A / +3 / clones), what to run with ctprobe and snowtest, what to send
   back. Not covered by a program: the Scorpion's turbo waits. Waiting for results from real machines.
4. ~~**Not modeled yet**~~ - done 2026-09-29 on branch `not-modeled-waits`: the ZX-Evo's 14 MHz waits (its
   DRAM's code and data cache words, from the RTL and a Verilator run) and the Scorpion Turbo+ slot waits (the
   SC15.1 firmware, decoded from its fuse map) as host bus overlays installed only in turbo
   ([2026-09-29-machine-waits](../2026-09-29-machine-waits/TODO.md)). Still open there: the ZX-Evo's 48K /
   128K raster contention (the rasters are not modeled, PLAN #55), the Scorpion's 3.5 MHz while /INT is
   active, SC15.3. TS-Conf cache misses belong to the TSConf machine (PLAN #41). Next: follow-up 5.
5. **Harness follow-ups**: why MAME's `scorpio` crashed earlier (it now runs), further runners (Kozynax,
   ZX-M8XXX, spec_chum), fusetest (needs pasmo), X-04.
6. ~~**This TODO brought up to date**~~ - done 2026-09-29.

Ideas backlog (performance of the contended machines): [baseline.md](baseline.md) §3.2.
