# TODO — Z84C15 CPU library for the Sprinter

**Status marker:** design written (2026-10-01); implementation on branch `sprinter-cpu`. PLAN row
**#59** (Sprinter). Design: [design.md](design.md).

## Steps

- [x] 0. Design ([design.md](design.md)); the research's §8 updated with the owner decision
- [ ] 1. Engine seam in `Z80`, behavior-neutral: `ICpuEngine`, `kStepWorkEngine`, `EngineStep`,
  the INT / NMI acknowledge hooks, the CPU-LIBRARY-MIGRATION tags; gate: full suite, TTD corpus and
  CI gate unchanged, Pentagon frame benchmark within noise
- [ ] 2. The library `core/src/3rdparty/z84c15/`: fork of unreal-z80 0.5.0, CMOS deltas, the
  on-chip block (wait generator, chip selects, MCR, watchdog, CTC / SIO / PIO, daisy chain, fixed
  ports); library tests; exercisers run once
- [ ] 3. The Sprinter on the library: `Z84C15Engine`, the old package removed, loader timing from
  the wait generator, the S1 reference checks re-verified
- [ ] 4. Docs: Sprinter roadmap / TODO / technical design point here; PLAN #59 note

## Open questions

- **O1** Power-on wait window: 15 or 16 M1 cycles at WCR = `#FF` (PS0182 p. 318 says both
  "fifteen /M1 cycles" and "the trailing edge of the 16th /M1"). Modeled: 15.
- **O2** `#F4` values 6 and 7: undefined in the data sheets; modeled as MAME does (`& 3`).
- **O3** Watchdog `/WDTOUT` on the Sprinter board: not found (research Q3). The chip runs the
  watchdog; the Sprinter leaves the event unconnected.
- **O4** SIO transmit and external-status interrupts, PIO handshake (modes 0-2) interrupts: not
  modeled; nothing on the Sprinter wires them yet (keyboard / mouse come in S4).
