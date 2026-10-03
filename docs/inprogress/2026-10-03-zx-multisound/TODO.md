# TODO - ZX-MultiSound (UzixLS) sound card

**Status:** design drafted 2026-10-03; owner decisions Q1-Q5 recorded. No code yet. Depends on the
[ZX-bus slots](../2026-10-03-zx-bus-slots/TODO.md) (SL-1 to SL-5).

## Documents

- [hardware-reference.md](hardware-reference.md): board, bus, port map, control byte, reset state, sound blocks,
  mixer weights from the schematic, DIP, revisions; comparison with the TSFM
- [requirements.md](requirements.md)
- [architecture.md](architecture.md): composition from shared modules, time, ports, module changes, mixer rows, TTD
- New modules: [tdd-saa1099.md](tdd-saa1099.md), [tdd-libsam2695.md](tdd-libsam2695.md),
  [tdd-midi-line.md](tdd-midi-line.md), [tdd-card-logic.md](tdd-card-logic.md)
- [tdd-integration.md](tdd-integration.md): steps MS-1 to MS-8, TTD, surfaces, real-software checks
- [open-questions.md](open-questions.md): owner decisions Q1-Q5

## Materials (not in the repo)

- Card sources: [UzixLS/zx-multisound](https://github.com/UzixLS/zx-multisound) at commit `d7f3ac2` (schematics,
  CPLD Verilog, errata, GS ROM)
- Schematic analysis (netlister, netlists of rev.A / A1 / A2, findings) is kept in the owner's local materials
  collection, outside the repository; its results are in [hardware-reference.md](hardware-reference.md)
- GM banks, Dream banks and the SAM2695 / Dream datasheets: `testdata/midi/` (git-ignored except its index
  [README](../../../testdata/midi/README.md), which lists every source with its link and license)

## Remaining

- [ ] Owner review of the design
- [ ] Independent modules (can start before the slots work): SAA-0..3, SAM-0..5, ML-0..2, CL-0..2
- [ ] MS-1 `Ym2203Pair` extraction (TSFM bit-identical)
- [ ] MS-2 GS profile (16 MHz, 1-2 MB, no `#33`, DAC sink)
- [ ] MS-3..MS-8 card, mixer, TTD, surfaces, real software, docs (after slots SL-4 / SL-5)
- [ ] `data/midi/generaluser-gs.sf2` + license tracked (Q4); GS 1.05b ROM as `data/rom/gs105b.rom`
- [ ] Later: SAM-6 host MIDI output, SAM-7 Dream-native banks research
