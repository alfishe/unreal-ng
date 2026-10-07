# TODO - ZX-MultiSound (UzixLS) sound card

**Status:** on master since 2026-10-05 (`7605bf104`, pushed): MS-1 to MS-7 first pass, with the ZX-bus slots SL-1 to
SL-7 ([slots TODO](../2026-10-03-zx-bus-slots/TODO.md)); demo to the owner done (MIDI, TSFM, SAA); MS-8 user docs done 2026-10-05. Shipped configs keep
the card off (owner decision): it is fitted through the slots. Left: the open items below (profiling, a
TTD fixture, the MS-7 second pass).

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

- [x] Owner review of the design (by the implementation and the 2026-10-05 demo)
- [x] SAM-0..SAM-2 libsam2695 ([tdd-libsam2695.md](tdd-libsam2695.md) §10 "As built"): `core/src/3rdparty/sam2695/`
  (UART, parser, SF2 loader, voice model, allocation, state; conformance table in its README), FluidSynth harness and
  bank corpus check in `tools/verification/sam2695/`; linked into core (`UNREALNG_HAVE_SAM2695`), used by nothing yet
- [x] SAM-3 / SAM-4 libsam2695 ([tdd-libsam2695.md](tdd-libsam2695.md) §10.5-10.7, 2026-10-04, not committed): every
  chart row done or reasoned in the README conformance table (GS parts and SysEx, GM System On, device ID, NRPN
  01xx / 18-1Exx / 37xx, sostenuto, soft pedal, portamento, voice reserve, controller matrix); effects in `src/fx/`
  (Dattorro-plate reverb with 8 programs, 8-program chorus, spatial, 4- / 2-band EQ, routing, soft / hard clipping,
  codec gain), dry render mode, effects state in the blob (`Fx.ReverbTailAcrossState`); 108 library tests,
  FluidSynth comparison 47 / 47 incl. effect sends, bank corpus rerun
- [x] SAM-5 real-chip check (owner, 2026-10-05): our SAM2695 compared by ear with real-chip recordings (YouTube):
  "very similar, good enough" - accepted. There is no official CleanWave bank to compare against
- [x] SAM-5 verification consolidation: owner compared the emulation with real-chip recordings (YouTube) by ear on
  2026-10-05 - "very similar, good enough"; no official CleanWave bank exists to compare against (mask ROM, not
  distributed). `data/midi/generaluser-gs.sf2` shipped (GeneralUser GS 2.0.3). Recordings of a real board stay welcome
- [x] ML-0..2 MIDI line ([tdd-midi-line.md](tdd-midi-line.md) §2.0, §5 "As built", 2026-10-04, not committed):
  datasheet facts of the I/O port stage (input = `#FF`, push-pull output, reset = inputs), `IAyIoPortListener` on
  `SoundChip_AY8910` (pins, called on change only, one pointer test per register latch without a listener),
  `MidiLine` feeding `sam2695::Synth::WriteLine` with its TTD blob; `AyIoPort_Test` and `MidiLine_Test` in core-tests
- [ ] ML follow-ups with the card: the YM2203 SSG drives the same listener (done in MS-1: `Ym2203Pair::setIoPortListener`),
  `MidiLine` wired to U4 (chip select 0) and "chip 2 does not drive" (done in MS-3:
  `MultiSoundCard_Test.MidiNoteBitBangedOnU4ReachesTheSynthAndU10DoesNotDrive`); the line in the card's blob set and
  the Z80 send routine under TTD done in MS-5 (`TtdMultiSound_Test.MidiByteAcrossCheckpoint`); left: `Describe` on the
  automation surfaces
- [x] SAA-0..3 `Saa1099` ([tdd-saa1099.md](tdd-saa1099.md) §10 "As built"): co-simulation in
  `tools/verification/saa1099/` (SAASound, MAME, MiSTer RTL under Verilator; consensus table in its README), the
  module, golden digests over the corpus, TTD blob `PeripheralId::Saa1099` = 53; not registered in any machine
- [ ] SAA follow-ups: the MultiSound integration plugs it in (TTD done in MS-5: id 53 as a device of the card,
  `<slot>.multisound.saa1099`; mixer row done in MS-4; left: `Describe` on the automation surfaces, its `[SAA1099]` ini section); audio-level check against the real-chip recordings in
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
- [x] MS-4 slot adapter and `SoundManager` rows (2026-10-04, working tree of `zx-bus-slots`, not committed;
  [tdd-integration.md](tdd-integration.md) §3.2, [architecture.md](architecture.md) §1 / §5 / §6 "As built"):
  `MultiSoundSlotCard` (`ICard`), built by `SlotManager` from `[SLOTS] zxbus.N = multisound` (options, `[MIDI] Bank=`);
  its claims resolved by the claim table (CardWins shadowing on the Pentagon, RdWr on the ZX-Evo with the YM2149 out
  of its socket); five rows, HUD source, row state `shadowed by`; `MultiSoundSlotCard_Test` (a Z80 program plays FM,
  SSG, SAA, DAC and a bit-banged MIDI note on a Pentagon and a ZX-Evo)
  - [x] owner decision (slots Q8, 2026-10-05): the AY socket left unconfigured gives up its socketed chip at creation
    (ZX-Evo); an explicit `ay-socket = ay`, a TSFM in the socket or a GS / SounDrive card next to the card refuse the
    machine with the reason ([architecture.md](architecture.md) §6 "As built"); no shipped config fits the card
  - [x] decided 2026-10-05: the card's axis is `CPU_CLOCK_RATE` on every machine, not the machine's T-state rate
    ([tdd-integration.md](tdd-integration.md) §3.2 deviation)
  - [x] the RTL asserts IORQGE on a `#BFFD` read too (no direction term, hardware-reference.md §3.1 / §3.4): the
    claim is `InOut` since 2026-10-05, the card does not drive that read, so on the Pentagon it floats instead of
    reaching the shadowed board AY; the generated card table changed in that one cell
    (`MultiSoundSlotCard_Test.ClaimsAssertIorqgeWhereTheRtlDoes`, `PentagonCardShadowsTheBoardAy`)
- [x] MS-5 TTD (2026-10-05, not committed; [tdd-integration.md](tdd-integration.md) §4.1): four devices named by
  slot (ids 58 `MultiSound`, 53 `Saa1099`, 59 `Sam2695`, 60 `MultiSoundGs`, region 17 `MultiSoundGsRam`), registered
  through `SlotManager`'s cards, recording refused when a slot-built card's device is missing, the bank in the
  fingerprint and the session guard, `TtdMultiSound_Test` (7), the card in the model contract test
  - [ ] a MultiSound fixture in the TTD corpus (Pentagon, ZX-Evo): needs the recorder to fit the card (slot surfaces,
    SL-7 / MS-6) and `TTD_Corpus_Test` to create the fixture's slot set. Since slots SL-6 the core can fit it into a
    running instance (`SlotChange::Run`, a plug applied by a restart, [slots tdd.md](../2026-10-03-zx-bus-slots/tdd.md)
    §14); a model switch carries it (Pentagon -> ZX-Evo keeps it, ZX-Evo -> 128K reports it as not carried)
  - [x] the ids 58-60 / region 17 and input kind 17 (`MidiPanic`) were checked free on master at the landing (2026-10-05)
  - [x] 2026-10-06, found by the corpus fixture `multisound-zxevo`: a seek on a machine without a board AY device (the
    ZX-Evo, whose YM2149 the card takes out of its socket) left the mixer's sample phase (SoundManager::samplePhase,
    the per-frame sample count) at its live pre-seek value - on the Pentagon the AY's blob puts it back. The card
    renders its rows to that count, so the frames after the seek had other lengths and the YM2203 channel decimator's
    phase (TTD state) left the recording at the first replayed frame. The card's blob (`MultiSoundCardTtd` version 2)
    carries the mixer's frame-start phase and a load adopts it, as the TurboSound / TSFM blobs do.
    `TtdMultiSound_Test.SeekRestoresTheMixerSampleCountWithoutABoardAy`
- [x] MS-6 surfaces (2026-10-05, working tree of `zx-bus-slots`, not committed; [tdd-integration.md](tdd-integration.md)
  §5.1): `DeviceState::MultiSound` / `Midi` on every surface, MIDI panic as TTD input `MidiPanic`, Qt slot window card
  options, MIDI activity window, HUD icon; recipe [.recipe/peripherals/multisound.md](../../../.recipe/peripherals/multisound.md)
- [x] MS-7 real software, first pass (2026-10-05, [tdd-integration.md](tdd-integration.md) §6.1): TSFM, SAA, GS,
  SounDrive, MIDI (3.5 / 7 / 14 MHz, line decoded from the TTD journal), VGMPLAY, Ball Quest; TTD seek checks on the TSFM
  and MIDI runs. Found and fixed: the SAA output moved on register writes with its clock stopped (LnxTracker Demo;
  `Saa1099_Test.ClockGateHoldsOutputAcrossRegisterWrites`); the AY socket's mixer rows read "AY 1 / AY 2" for a
  TurboSound FM or TurboSound that replaced the board AY (owner report: "board AY active" with Ball Quest)
- [x] Ball Quest click with `ctrlMask = pro`: **not modeled** (owner decision 2026-10-05). The card's logic does what
  the RTL does (the `#F0-#F7` writes switch the chip and unmute FM), the FM mute output stays silence
- [x] MS-7 owner report 2026-10-05, FM through the card was clicks (`tech_support.sna`; the TSFM in the socket played it
  right): `Ym2203Pair::renderChannels` re-anchored its cursor at the chips, i.e. at the END of the block it was about to
  render, and the 1 : 1 reset left the cursor frame-relative on the card's continuous axis. After any bus reset at a
  time other than 0 (a snapshot load) every block rendered one block ahead of the chips: all queued FM words consumed at
  the first half-tick, the last one held - a frame-rate staircase. Fixed: the re-anchor checks where the block ends and
  places it to end `kRenderLag` behind the chips ([tdd-integration.md](tdd-integration.md) §6.1, [architecture.md](architecture.md)
  §1 module changes). The SSG was not affected (its tones are right; only its write timing moved by a block). Tests
  that assert the tone, not a level: `MultiSoundCard_Test.FmRowCarriesTheNoteOfEachChipAfterAResetAtAnyTime` /
  `SsgRowCarriesTheToneOfEachChipAfterAResetAtAnyTime`, `Ym2203Pair_Test.PerChannelOutputsFollowTheChipsAfterAResetOnAContinuousAxis`.
  The earlier tests passed because they measured peak-to-peak swing, which a staircase of FM words also has
- [x] Owner 2026-10-05, "no synchronization problems": the render cursor / chip time relation is one rule of
  `Ym2203Pair` on every path and axis ([architecture.md](architecture.md) module changes, "Render cursor"), proved by
  the invariant suite in `core/tests/emulator/sound/tsfm/ym2203pair_test.cpp`: `Ym2203PairBoards_Test` (TSFM socket and
  MultiSound card x none / machine reset / snapshot load / core rate 48 kHz / host speed x2 and back / FM mute, each at
  T 0, mid-frame and the frame's last T-state, the FM note and the SSG tone on both chips: frequency within 1 %, no
  period off by more than 5 %, the card's cursor lag within two output samples of `kRenderLag`, word queues bounded, no
  re-anchor in 20 steady frames), `Ym2203PairBoardsLongRun_Test` (1000 frames per board, 10 000 checked by hand, no
  re-anchor, no drift), `Ym2203PairBoardsTtd_Test` (seek at T 0 / mid / end-1 and replay: the CPU position and the
  cursor lag equal to live at every later frame end, the FM capture within 1 % of its peak). The suite found a second
  problem, fixed: the TSFM's sample phase after a host speed multiplier (a click at some frame boundaries back at 1x).
  Not covered: a slot-change restart and a model switch (both build or carry the card on a fresh axis start; the rule
  needs no special case), and a TTD seek across a machine reset (a reset ends the recording by design)
- [x] Owner 2026-10-05: the rows per chip as the TSFM shows them (MS SSG 1 / 2, MS FM 1 / 2, MS SAA, MS PCM, MS MIDI;
  keys `ms_ssg1` .. `ms_midi`), one HUD indicator per row, and the TSFM's FM calibration on the card (`[SOUND]
  TSFM_FmTrimDb`, the audio settings' FM trim drives both boards, `ym.fm_trim_db` in the card report)
  ([architecture.md](architecture.md) §5 with the per-row calibration table). Tests: `MultiSoundMixer_Test` (each chip on
  its own row, `FmTrimFollowsTheTsfmGainLaw`), the card's per-chip tone tests, `MultiSoundSlotCard_Test` (rows and keys
  only with the card, per-chip rows in the five-source program), `AudioActivityIndicators_Test.MultiSoundRowsLightTheirOwnIndicatorPerChip`,
  `Ym2203PairBoardsLevel_Test` (FM rows and master equal to the TSFM's within 0.1 dB at 7.4 and 0 dB)
- [x] 2026-10-05, after master 7bbc2eaaa (AY voicing default back to Classic): the card's SSG rows run through the same
  AY / SSG tone voicing as the socket's chips ([architecture.md](architecture.md) §5); `[AY] Stereo` deliberately not
  applied to the card (board wiring). `Ym2203PairBoardsLevel_Test` over Flat / Classic / Headphones, FM unvoiced on both
- [x] decided 2026-10-05: the SSG rows are the schematic's -7.6 dB below the TSFM's (SSG through 24 k against the
  FM's 10 k on the MultiSound, equal weights on the TSFM); kept as hardware, not matched
- [x] the measured card / TSFM SSG ratio of -7.75 dB, 0.15 dB beyond the schematic's -7.60 dB (2026-10-06): not the
  card's SSG path. The AY room crossfeed (`[SOUND]` AY room, default -9 dB, sound HQ) adds the opposite channel 2 ms late
  at 0.35; the emulator's ABC pan law puts 0.1 of SSG A on the TSFM's right, so the TSFM's left row gets
  0.35 x 0.1 / 0.9 of A back, correlated with the direct A by the square's triangle autocorrelation at 2 ms: +0.14 dB
  at the 427 Hz test tone (punch adds 0.01 dB). The card wires A to the left only and runs its SSG rows through the
  voicing but not the character chain. Room off: -7.595 / -7.608 dB (punch off / on), within 0.01 dB of the schematic.
  `Ym2203PairBoardsLevel_Test` measures with the room off, tolerance 0.03 dB (was 0.2);
  `SsgRatioIsTheSchematicWeightAndTheRoomCrossfeedExplainsTheRest` checks the predicted +0.14 dB on the TSFM and no
  change on the card. Open (owner): should the card's SSG rows run the AY character chain (punch, room) like the
  socket's chips?
- [x] atm3 / atm450 / atm710 carry `TSFM_FmTrimDb=7.4` since 2026-10-05 (`bab7a4e32`), like every shipped config
- [x] side note: the plain AY / TurboSound device (`SoundChip_TurboSound`) has the same sample-phase render loop as the
  TSFM and probably the same click after a host speed multiplier. Confirmed and worse (2026-10-05): its render loop
  stored its frame position unscaled while counting scaled T-states, so 4 frames at x2 left a backlog of tens of
  millions of samples - 8192 time-compressed samples per frame with a jump at every frame boundary for minutes back at
  1x. The sound feature off and on again left every device (AY, TurboSound, TSFM) off the mixer's sample grid (one
  never-rendered or dropped sample at some frame boundaries for good). Fixed with one rule for every device that keeps
  its own sample position: it follows the mixer's frame-start phase (`followSamplePhase`, pushed by
  `SoundManager::handleFrameStart`; the TSFM's x2-only branch is gone into it); the beeper and the Covox / SounDrive
  (blip streams) re-join the mixer's grid after a multiplied frame or a gap, the Covox and the MoonSound drop the
  excess of a multiplied frame instead of keeping it as a growing delay. Tests:
  `SoundChipTurboSoundEvents_Test` (single AY on 48K / 128K / Pentagon, TurboSound, TSFM's SSG x none / machine reset /
  snapshot / core rate / host speed x2 and x4 / hardware turbo / turbo with and without audio / sound off, at T 0, mid,
  end-1), `SoundChipTurboSoundEventsTtd_Test`, `SoundManagerDeviceMarker_Test` (beeper, SounDrive, MoonSound)
- [ ] MS-7 second pass: Z-Player 5, the remaining disks of the test images README, WC MOD / TFC / ETC
- [x] decided 2026-10-05: ZX MIDI Player v3 at 14 MHz on a ZX-Evo sends 23.6 kbaud (the Evo's 14 MHz DRAM waits, as
  the RTL); a limit of the program, not an emulator bug
- [x] side note (not the card): the AY state report named the I/O port direction the wrong way round
  (`devicestate.cpp` reported "input" when R7 bit 6 = 1, which is output): fixed on master in `79c638835` (state
  report, WebAPI register decoding and CLI read R7 bits 6 / 7 as the datasheet does; `DeviceState_Test.AyReportPortDirectionFollowsR7`)
- [x] MS-8 docs (2026-10-05, not committed): the user page [docs/features/multisound.md](../../features/multisound.md)
  (what the card is, machines and conflicts, fitting by INI / Qt / automation with the options, the MIDI bank, mixer
  rows and levels, state reports, TTD, known behavior, software to try); the card in the machine recipes that list sound
  cards and where it fits for real ([pentagon.md](../../../.recipe/machines/pentagon.md),
  [scorpion.md](../../../.recipe/machines/scorpion.md), [atm3-zxevo-baseconf.md](../../../.recipe/machines/atm/atm3-zxevo-baseconf.md),
  [tsconf.md](../../../.recipe/machines/tsconf.md)); links from [slots.md](../../features/slots.md) and the recipe
  [.recipe/peripherals/multisound.md](../../../.recipe/peripherals/multisound.md). Not added to `spectrum.md` / `profi.md`
  (only an `unrealistic` fit there)
- [ ] Profile the card's frame cost (~1 ms per emulated frame on the dev machine with all five paths; the SAM2695
  effects path and the eight Reference-quality YM decimators are the suspects); still open after MS-4 registered it
  (machines without the card pay nothing)
- [x] `data/midi/generaluser-gs.sf2` + license + README tracked (Q4, done 2026-10-05), shipped next to the
  executables by every target that ships `data/rom`; `[MIDI] Bank=NONE`; the test runner's policy keeps the default bank
  out of test machines unless asked (`TestSound::DefaultMidiBank`, `MultiSoundSlotCard_Test.ShippedDefaultBankLoadsWithoutAnOverride`); GS 1.05b ROM as `data/rom/gs105b.rom` done in MS-2 (README-ROMS entry)
- [ ] Later: SAM-6 host MIDI output, SAM-7 Dream-native banks research
