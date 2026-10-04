# TODO - ZX-MultiSound (UzixLS) sound card

**Status:** design drafted 2026-10-03; owner decisions Q1-Q5 recorded. Card logic CL-0 / CL-1 built 2026-10-04
(branch `multisound`): `MultiSoundLogic` agrees with the card's CPLD Verilog on every scenario and the
full decode sweep. MS-3 `MultiSoundCard` assembled 2026-10-04 (not committed; no dependency on the slots framework). Depends on the
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

The slots core (SL-3 and later) waits for `ttd-engine` to land. Meanwhile: libsam2695 SAM-3 / SAM-4 (done) and the
MIDI line ML-0..2. MS-1 (`Ym2203Pair` from the TSFM) and MS-2 (GS profile) were done before `ttd-engine` lands; the
owner accepted the later rebase conflict in `soundchip_turbosoundfm.h`.

## Remaining

- [ ] Owner review of the design
- [x] SAM-0..SAM-2 libsam2695 ([tdd-libsam2695.md](tdd-libsam2695.md) §10 "As built"): `core/src/3rdparty/sam2695/`
  (UART, parser, SF2 loader, voice model, allocation, state; conformance table in its README), FluidSynth harness and
  bank corpus check in `tools/verification/sam2695/`; linked into core (`UNREALNG_HAVE_SAM2695`), used by nothing yet
- [x] SAM-3 / SAM-4 libsam2695 ([tdd-libsam2695.md](tdd-libsam2695.md) §10.5-10.7, 2026-10-04, not committed): every
  chart row done or reasoned in the README conformance table (GS parts and SysEx, GM System On, device ID, NRPN
  01xx / 18-1Exx / 37xx, sostenuto, soft pedal, portamento, voice reserve, controller matrix); effects in `src/fx/`
  (Dattorro-plate reverb with 8 programs, 8-program chorus, spatial, 4- / 2-band EQ, routing, soft / hard clipping,
  codec gain), dry render mode, effects state in the blob (`Fx.ReverbTailAcrossState`); 108 library tests,
  FluidSynth comparison 47 / 47 incl. effect sends, bank corpus rerun
- [ ] SAM-5 verification consolidation: Dream-board recordings (the curves chosen in tdd §10.6 first),
  `data/midi/` bank pinned, corpus + FluidSynth report as one command; owner review of tdd §10.4 / §10.6
- [x] ML-0..2 MIDI line ([tdd-midi-line.md](tdd-midi-line.md) §2.0, §5 "As built", 2026-10-04, not committed):
  datasheet facts of the I/O port stage (input = `#FF`, push-pull output, reset = inputs), `IAyIoPortListener` on
  `SoundChip_AY8910` (pins, called on change only, one pointer test per register latch without a listener),
  `MidiLine` feeding `sam2695::Synth::WriteLine` with its TTD blob; `AyIoPort_Test` and `MidiLine_Test` in core-tests
- [ ] ML follow-ups with the card: the YM2203 SSG drives the same listener (done in MS-1: `Ym2203Pair::setIoPortListener`),
  `MidiLine` wired to U4 (chip select 0) and "chip 2 does not drive" (done in MS-3:
  `MultiSoundCard_Test.MidiNoteBitBangedOnU4ReachesTheSynthAndU10DoesNotDrive`); left: the line in the card's blob set
  (MS-5), the program-level Z80 send-routine test under TTD, `Describe` on the automation surfaces
- [x] SAA-0..3 `Saa1099` ([tdd-saa1099.md](tdd-saa1099.md) §10 "As built"): co-simulation in
  `tools/verification/saa1099/` (SAASound, MAME, MiSTer RTL under Verilator; consensus table in its README), the
  module, golden digests over the corpus, TTD blob `PeripheralId::Saa1099` = 53; not registered in any machine
- [ ] SAA follow-ups: the MultiSound integration plugs it in (TTD inside the card's blob set, mixer row, `Describe` on
  the automation surfaces, its `[SAA1099]` ini section); audio-level check against the real-chip recordings in
  `rejunity/tt06-psg-saa1099`; captured SAM Coupe / VGM SAA streams in the corpus (tdd §6 item 1)
- [x] CL-0 RTL co-simulation (`tools/verification/multisound/`: pinned `top.v`, Verilator testbench, `.msc` scenarios,
  sweep tables) and CL-1 `MultiSoundLogic` + `core-tests` ([tdd-card-logic.md](tdd-card-logic.md) §2, §4, §5)
- [x] RTL findings F1-F11 folded into [hardware-reference.md](hardware-reference.md) and [architecture.md](architecture.md)
  (GS INT / 321, DAC transfer, IORQGE direction, GS flag rules, GS memory map) - [tdd-card-logic.md](tdd-card-logic.md) §7
- [ ] CL-2 rest (the write-only TFM trace now also plays through the card: `MultiSoundCard_Test.TfmPlayerTracePlaysThroughTheCard`;
  it starts after the player loaded its instruments - TL #7F, AR 0 - so the test adds a stand-in instrument): real-program traces with reads and M1 context (TSFM players with status polling, VGMPLAY.WMF, GS MOD
  player, WC MIDI player, Ball Quest) once the card is on the bus (integration phase); one write-only trace (TFM Music
  Compiler player) is in the corpus
- [x] `MultiSoundDacs` + `MultiSoundMixer` ([architecture.md](architecture.md) §4.4, §5 "as built", 2026-10-04, not
  committed): four DAC channels from strobe events ordered by strobe end (F7) with the measured transfer (F9), blip
  output 0-1 L / 2-3 R, Authentic 16.25 kHz RC, TTD blob (no PeripheralId); the mixer's schematic weights computed
  from the component values (`multisoundanalog.h`), calibration through the TSFM FM measurement and volts, coupling
  high-pass, Authentic SAA ladder; `MultiSoundAnalog_Test`, `MultiSoundDacs_Test`, `MultiSoundMixer_Test`
- [ ] Mixer / DAC follow-ups: done in MS-3 - the card feeds the mixer from `Ym2203Pair::renderChannels` and
  `MultiSoundDacs` from the GS sink and `SoundriveSample` actions with strobe-end times; left: the rows registered with
  SoundManager (MS-4); absolute SAA / SAM2695 levels against a real card (today module conventions,
  hardware-reference §7)
- [x] MS-2 input from the RTL: GS INT 321 clocks of 12 MHz, port reads `#FF`, flag rules on any access, GS memory map;
  the ROM's A15 wiring is moot (the 27C512 image is the 32 KB image twice)
- [x] MS-1 `Ym2203Pair` extraction (TSFM bit-identical) ([architecture.md](architecture.md) §4.1 "As built", 2026-10-04,
  not committed): `core/src/emulator/sound/chips/tsfm/ym2203pair.{h,cpp}` (chips, timers, busy, word / write queues,
  `syncTo` with the `masterClockHz : hostTickRate` accumulator, bus interface, SSG I/O port listener, stereo output
  stage for the TSFM, per-chip / per-channel outputs for the mixer, TTD pieces + own blob); `TsfmGolden_Test` digests
  identical on the pre-extraction binary and after, TSFM suite and TTD corpus green, A/B within noise
  ([tdd-integration.md](tdd-integration.md) MS-1 row); `Ym2203Pair_Test` (ratio, pitch, per-channel outputs, listener on
  chip 1, TTD ratio phase)
- [x] MS-2 GS profile (16 MHz, 1-2 MB, no `#33`, DAC sink) ([architecture.md](architecture.md) §4.2 "As built",
  2026-10-04, not committed): `GSProfile` given to `SoundChip_GeneralSound` (classic default unchanged: GS suites,
  golden digests and TTD tests green, A/B benchmark), MultiSound timing in 48 MHz units with the 33-clock INT pulse,
  the CPLD map through `MultiSoundLogic::GsMemoryMapFor`, CPLD port rules, `IGSDacSink` implemented by
  `MultiSoundDacs`; GS 1.05b boots on 1 MB and 2 MB (`SoundChip_GeneralSound_Profile_Test`)
- [x] MS-3 follow-ups from MS-2 (2026-10-04): the GS owns the mailbox (`MultiSoundLogic` decodes only); SounDrive
  writes reach the GS's shared volume (`sharedVolumeWrite`, `#0B` reads volume 3 bit 5); event times = `Out` time for
  host strobes, instruction start for GS strobes ([architecture.md](architecture.md) §1 "As built")
- [x] MS-3 `MultiSoundCard` (2026-10-04, not committed): self-contained card with port / time / audio API, five rows,
  `Describe`; `MultiSoundCard_Test`; GS host clock + `sharedVolumeWrite` + `resetAtHostNow`; `Ym2203Pair::renderChannels`
  cursor fix ([architecture.md](architecture.md) §1, [tdd-integration.md](tdd-integration.md) MS-3 row)
- [ ] MS-4 slot adapter ([tdd-integration.md](tdd-integration.md) §3.1) and `SoundManager` rows (after slots SL-4)
- [ ] MS-5..MS-8 TTD, surfaces, real software, docs (after slots SL-5); TTD needs the adapter's time base and the
  pair's ratio phase in the card's blob set
- [ ] Profile the card's frame cost (~1 ms per emulated frame on the dev machine with all five paths; the SAM2695
  effects path and the eight Reference-quality YM decimators are the suspects) before MS-4 registers it
- [ ] `data/midi/generaluser-gs.sf2` + license tracked (Q4); GS 1.05b ROM as `data/rom/gs105b.rom` done in MS-2 (README-ROMS entry)
- [ ] Later: SAM-6 host MIDI output, SAM-7 Dream-native banks research
