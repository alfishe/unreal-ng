# TODO - ZX-MultiSound (UzixLS) sound card

**Status:** design drafted 2026-10-03; owner decisions Q1-Q5 recorded. Card logic CL-0 / CL-1 built 2026-10-04
(branch `multisound`, not committed): `MultiSoundLogic` agrees with the card's CPLD Verilog on every scenario and the
full decode sweep. Depends on the
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

## Order while the slots core is paused (owner decision 2026-10-04)

The slots core (SL-3 and later) waits for `ttd-engine` to land. Meanwhile: libsam2695 SAM-3 / SAM-4 and the MIDI line
ML-0..2. MS-1 (`Ym2203Pair` from the TSFM) and MS-2 (GS profile) also wait: `ttd-engine` changes the same modules.

## Remaining

- [ ] Owner review of the design
- [x] SAM-0..SAM-2 libsam2695 ([tdd-libsam2695.md](tdd-libsam2695.md) §10 "As built"): `core/src/3rdparty/sam2695/`
  (UART, parser, SF2 loader, voice model, allocation, state; conformance table in its README), FluidSynth harness and
  bank corpus check in `tools/verification/sam2695/`; linked into core (`UNREALNG_HAVE_SAM2695`), used by nothing yet
- [ ] SAM-3 the rest of the MIDI chart (GS SysEx, NRPN 01xx / 18-1Exx / 37xx, sostenuto, soft pedal, portamento,
  part assignment, voice reserve, assignable controllers), SAM-4 effects (reverb, chorus, spatial, EQ, clipping,
  codec gain) and render modes, SAM-5 verification consolidation (Dream-board recordings, `data/midi/` bank pinned)
- [x] ML-0..2 MIDI line ([tdd-midi-line.md](tdd-midi-line.md) §2.0, §5 "As built", 2026-10-04, not committed):
  datasheet facts of the I/O port stage (input = `#FF`, push-pull output, reset = inputs), `IAyIoPortListener` on
  `SoundChip_AY8910` (pins, called on change only, one pointer test per register latch without a listener),
  `MidiLine` feeding `sam2695::Synth::WriteLine` with its TTD blob; `AyIoPort_Test` and `MidiLine_Test` in core-tests
- [ ] ML follow-ups with the card: the YM2203 SSG drives the same listener (MS-1, `Ym2203Pair`), `MidiLine` wired to
  YM chip 1 and carried in the card's blob set, `MidiLine_Test.Chip2DoesNotDrive`, the program-level Z80 send-routine
  test under TTD, `Describe` on the automation surfaces
- [x] SAA-0..3 `Saa1099` ([tdd-saa1099.md](tdd-saa1099.md) §10 "As built"): co-simulation in
  `tools/verification/saa1099/` (SAASound, MAME, MiSTer RTL under Verilator; consensus table in its README), the
  module, golden digests over the corpus, TTD blob `PeripheralId::Saa1099` = 48; not registered in any machine
- [ ] SAA follow-ups: the MultiSound integration plugs it in (TTD inside the card's blob set, mixer row, `Describe` on
  the automation surfaces, its `[SAA1099]` ini section); audio-level check against the real-chip recordings in
  `rejunity/tt06-psg-saa1099`; captured SAM Coupe / VGM SAA streams in the corpus (tdd §6 item 1)
- [x] CL-0 RTL co-simulation (`tools/verification/multisound/`: pinned `top.v`, Verilator testbench, `.msc` scenarios,
  sweep tables) and CL-1 `MultiSoundLogic` + `core-tests` ([tdd-card-logic.md](tdd-card-logic.md) §2, §4, §5)
- [x] RTL findings F1-F11 folded into [hardware-reference.md](hardware-reference.md) and [architecture.md](architecture.md)
  (GS INT / 321, DAC transfer, IORQGE direction, GS flag rules, GS memory map) - [tdd-card-logic.md](tdd-card-logic.md) §7
- [ ] CL-2 rest: real-program traces with reads and M1 context (TSFM players with status polling, VGMPLAY.WMF, GS MOD
  player, WC MIDI player, Ball Quest) once the card is on the bus (integration phase); one write-only trace (TFM Music
  Compiler player) is in the corpus
- [ ] MS-2 input from the RTL: GS INT 321 clocks of 12 MHz, port reads `#FF`, flag rules on any access, GS memory map
  incl. the ROM's A15 wiring (check `gma[15]` -> 27C512 A15 in the rev.A2 schematic)
- [ ] MS-1 `Ym2203Pair` extraction (TSFM bit-identical)
- [ ] MS-2 GS profile (16 MHz, 1-2 MB, no `#33`, DAC sink)
- [ ] MS-3..MS-8 card, mixer, TTD, surfaces, real software, docs (after slots SL-4 / SL-5)
- [ ] `data/midi/generaluser-gs.sf2` + license tracked (Q4); GS 1.05b ROM as `data/rom/gs105b.rom`
- [ ] Later: SAM-6 host MIDI output, SAM-7 Dream-native banks research
