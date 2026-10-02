# TODO — Z84C15 CPU library for the Sprinter

**Status marker:** steps 0-4 done on branch `sprinter-cpu` (2026-10-01), not merged into master.
PLAN row **#59** (Sprinter). Design: [design.md](design.md).

## Steps

- [x] 0. Design ([design.md](design.md)); the research's §8 updated with the owner decision
  ([research-cpu-z84c15.md](../2026-09-28-sprinter/research-cpu-z84c15.md) §8.0)
- [x] 1. Engine seam in `Z80`, behavior-neutral: `ICpuEngine`, `kStepWorkEngine`, `EngineStep`, the
  INT / NMI acknowledge hooks, 20 CPU-LIBRARY-MIGRATION ids (rule 7 of
  [performance-guidelines.md](../../guidelines/performance-guidelines.md)). `core/src/emulator/cpu`
  gained 140 lines, removed none. Full suite green, TTD corpus and CI gate unchanged; Pentagon host
  frame within noise (Fast mean of paired diffs -0.0 %, Debug -0.2 %, load 150-225)
- [x] 2. `core/src/3rdparty/z84c15/`: fork of unreal-z80 0.5.0 (`a0433ec`), CMOS deltas, the
  on-chip block. FUSE vectors 1356/1356 on the library (CMOS `ED 71`; MEMPTR after repeating block
  I/O = PC + 1, unreal-z80's silicon-verified value, where the vectors and the native core use BC);
  z80test (z80doc, z80docflags, z80flags, z80full, z80ccf, z80memptr) and zexdoc / zexall pass
  through the library (a scratch harness on the unreal-z80 tree's data, 6.4 min for the two ZEX
  tapes; not in the suite)
- [x] 3. The Sprinter on the library: `Z84C15Engine` (`core/src/emulator/io/z84c15/`), the old
  package removed. Pentagon host frame A/B within noise (Fast +0.6 %, Debug -0.1 %, load 84-102)
- [x] 4. Docs: Sprinter TODO / roadmap / technical design / tdd-accel-sound-input point here; PLAN #59

## Timing changes on the Sprinter (evidence: research §4.1, §5; design §6)

| What | Before (native NMOS core, = MAME) | Now (Z84C15 library) |
|:--|:--|:--|
| PLD loader, one bitstream byte (WCR = `#04`) | 113 T | 142 T (`SprinterPldConfig_Test.FullStart_LoaderRunsWithTheChipWaits`) |
| Last loader stream write (full start, T from reset) | 6 691 671 (MAME 6 691 665) | 8 409 025 (2.40 s at 3.5 MHz) |
| BIOS 3.04 port accesses after `InitCpuPorts`' WCR = 0 write, relative to the first BIOS access | MAME's to the T-state | MAME's + 22 T (22 memory cycles with one wait before the write; `SprinterReference_Test.Bios304_PortTraceMatchesMame`) |
| INT positions per FN_SYNC mode, the acknowledge (6-11 T), the logo palette, page `#40`, "DCP opened" at `#0CD8` | equal to MAME | unchanged |

`SprinterReference_Test.CallFnSync` now leaves a HALT before it injects its call (as an INT
would): on the library a halted CPU keeps running HALT M1 cycles at PC, so the injected stub only
started at the next INT, one byte in. The native core had the opposite defect (stub run at once,
then the INT acknowledge skipped one byte of it); the test passed there by luck of timing.

## Open questions

- **O1** Power-on wait window: 15 or 16 M1 cycles at WCR = `#FF` (PS0182 p. 318 says both
  "fifteen /M1 cycles" and "the trailing edge of the 16th /M1"). Modeled: 15.
- **O2** `#F4` values 6 and 7: undefined in the data sheets; modeled as MAME does (`& 3`).
- **O3** Watchdog `/WDTOUT` on the Sprinter board: not found (research Q3). The chip runs the
  watchdog; the Sprinter leaves the event unconnected.
- **O4** SIO transmit and external-status interrupts, PIO handshake (modes 0-2) interrupts: not
  modeled; nothing on the Sprinter wires them yet (keyboard / mouse come in S4).
- **O5** The engine path's cost per Sprinter instruction (callbacks, T mapping) is not measured yet;
  the Sprinter has no frame benchmark.
