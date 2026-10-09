# TODO - ZX-MultiSound (UzixLS) sound card

**Status:** on master since 2026-10-05 (`7605bf104`, pushed), with the ZX-bus slots SL-1 to SL-7
([slots TODO](../2026-10-03-zx-bus-slots/TODO.md)); demo to the owner done (MIDI, TSFM, SAA). MS-1 to MS-8 done; on
2026-10-06 (branch `multisound-remainder`) the 0.15 dB SSG residue explained, the frame cost profiled (silent FM skips
its FIR), the TTD corpus fixtures (they found a seek bug, fixed) and the MS-7 second pass; on 2026-10-07 (branch
`ms-ssg-chain-fixtures`) the SSG rows got the AY character chain and the corpus sessions are recorded by the tests. Shipped configs keep the card
off (owner decision): it is fitted through the slots. Left: the open items below - follow-ups that need a real board
or real-program traces, the frame-cost backlog, SAM-6 / SAM-7.

**Global plan:** the card is off the global plan (owner, 2026-10-08; its former row #88 in
[PLAN.md](../PLAN.md#retired-rows) stays reserved); the remaining items are tracked only here.

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
- [x] ML follow-ups with the card: the YM2203 SSG drives the same listener (done in MS-1: `Ym2203Pair::setIoPortListener`),
  `MidiLine` wired to U4 (chip select 0) and "chip 2 does not drive" (done in MS-3:
  `MultiSoundCard_Test.MidiNoteBitBangedOnU4ReachesTheSynthAndU10DoesNotDrive`); the line in the card's blob set and
  the Z80 send routine under TTD done in MS-5 (`TtdMultiSound_Test.MidiByteAcrossCheckpoint`); the line's report (pin,
  level, edges, last change) on every surface since MS-6 (`DeviceState::Midi`, the `line` object of
  `/state/audio/midi`)
- [x] SAA-0..3 `Saa1099` ([tdd-saa1099.md](tdd-saa1099.md) §10 "As built"): co-simulation in
  `tools/verification/saa1099/` (SAASound, MAME, MiSTer RTL under Verilator; consensus table in its README), the
  module, golden digests over the corpus, TTD blob `PeripheralId::Saa1099` = 53; not registered in any machine
- [ ] SAA follow-ups: the MultiSound integration plugs it in (TTD done in MS-5: id 53 as a device of the card,
  `<slot>.multisound.saa1099`; mixer row done in MS-4; its report - registers, voices, envelopes, noise - in the
  card's report on every surface since MS-6). Left: an audio-level check against the real-chip recordings in
  `rejunity/tt06-psg-saa1099`; captured SAM Coupe / VGM SAA streams in the corpus (tdd §6 item 1). The shipped
  configs' `[SAA1099] FQ=` is a legacy section nothing parses (the card's SAA runs at its board's 8 MHz)
- [x] CL-0 RTL co-simulation (`tools/verification/multisound/`: pinned `top.v`, Verilator testbench, `.msc` scenarios,
  sweep tables) and CL-1 `MultiSoundLogic` + `core-tests` ([tdd-card-logic.md](tdd-card-logic.md) §2, §4, §5)
- [x] RTL findings F1-F11 folded into [hardware-reference.md](hardware-reference.md) and [architecture.md](architecture.md)
  (GS INT / 321, DAC transfer, IORQGE direction, GS flag rules, GS memory map) - [tdd-card-logic.md](tdd-card-logic.md) §7
- [x] CL-2 real-program traces with reads and M1 context (2026-10-08, branch `ms-cl2-traces`;
  walkthrough and results: [cl2-real-program-traces.md](cl2-real-program-traces.md)): the card's bus trace
  (`MultiSoundCard::SetBusTrace`, host and GS side, M1 address, times, read values), captured from five programs on
  the bus - TFM Music Compiler player with busy polling ("uzhos"), Mod Player v2.5 (GS), Ball Quest (ZX-Evo, `pro` and
  `classic`), VGMPLAY.WMF (YM2203 + SAA) and GSPLAYER.WMF MIDI (TS-Conf 14 MHz); stored in
  `testdata/sound/multisound/traces/` (zstd, 67 KB, with their capture scripts) because the programs cannot be
  committed. Logic = RTL = the emulator's reads on all 208 176 cycles; a fresh card replays each program read for
  read with the same GS cycles; audio digests and content checks (`MultiSoundTrace_Test`). No model bug found. The
  earlier write-only TFM trace stays (`MultiSoundCard_Test.TfmPlayerTracePlaysThroughTheCard`).
  Left, with reasons: no trace from a real card (needs a logic analyzer on the board's bus); SounDrive players not
  traced (no reads; the shared DACs have their card tests)
- [x] `MultiSoundDacs` + `MultiSoundMixer` ([architecture.md](architecture.md) §4.4, §5 "as built", 2026-10-04, not
  committed): four DAC channels from strobe events ordered by strobe end (F7) with the measured transfer (F9), blip
  output 0-1 L / 2-3 R, Authentic 16.25 kHz RC, TTD blob (no PeripheralId); the mixer's schematic weights computed
  from the component values (`multisoundanalog.h`), calibration through the TSFM FM measurement and volts, coupling
  high-pass, Authentic SAA ladder; `MultiSoundAnalog_Test`, `MultiSoundDacs_Test`, `MultiSoundMixer_Test`
- [ ] Mixer / DAC follow-ups: done in MS-3 - the card feeds the mixer from `Ym2203Pair::renderChannels` and
  `MultiSoundDacs` from the GS sink and `SoundriveSample` actions with strobe-end times; the rows registered with
  SoundManager in MS-4. Left: absolute SAA / SAM2695 levels against a real card (today module conventions,
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
  - [x] a MultiSound fixture in the TTD corpus (2026-10-06): `multisound-pentagon` and `multisound-zxevo`, in v1's
    corpus (`testdata/ttd/`) and the engine's (`testdata/ttd/engine/`), 70 frames each (the SAM2695's state changes
    in every checkpoint: about 30 KB a frame in the engine file, 100 KB in v1's; 2.1 / 2.4 MB and 6.9 / 7.4 MB).
    The recorder created the machine with `"slots": {"zxbus.1": "multisound"}` and recorded
    [testdata/sound/multisound/ttd/allsources.sna](../../../testdata/sound/multisound/ttd/README.md), a program that
    plays every source of the card (written by `tools/verification/multisound/ttd-fixture/make-program.py`).
    `TTD_Corpus_Test` and `TimeTravelControllerCorpus_Test` build their machine with the card and the shipped default
    bank (`core/tests/_helpers/ttdslotcards.h`). The ZX-Evo fixture found the sample-phase bug below
  - [x] 2026-10-07 (owner decision, branch `ms-ssg-chain-fixtures`): the four stored sessions (~19 MB) are gone; the
    corpus tests record the same program in their own process instead
    (`core/tests/_helpers/ttdmultisoundsessions.h`: a fresh machine with the card in `zxbus.1`, 44.1 kHz, Sound HQ and
    Screen HQ on, the snapshot, 10 settle frames, 70 recorded frames with the write journal, saved to the process's
    scratch folder, deleted when the process ends), with v1 for `TTD_Corpus_Test`, `TTDSessionFile_Test` and
    `TTDV1Feeder_Test` and with the engine for `TimeTravelControllerCorpus_Test`; once per recorder and test process,
    about 0.35 s per session (the bank load included). `allsources.sna` stays committed; a change of the card or the
    program needs no re-recording. `record_fixtures.py` no longer lists them; the corpus README lists them as
    recorded sessions
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
  `SsgRatioIsTheSchematicWeightAndTheRoomCrossfeedExplainsTheRest` checked the predicted +0.14 dB on the TSFM and no
  change on the card. Owner decision 2026-10-07: the card's SSG rows run the AY character chain too (next item)
- [x] 2026-10-07 (owner decision, branch `ms-ssg-chain-fixtures`): the card's SSG rows run the AY character chain
  (punch, room crossfeed) exactly as the socket's chips' SSG, after the voicing ([architecture.md](architecture.md)
  §5): one `AudioCharacterChain` per row next to its `VoicingStage` (`SoundManager::CardSsgRow`;
  `CardMixerRow::ssgVoicing` became `ssgRow`, `ICard::VoicedMixerBuffer` became `SsgMixerBuffer`), driven by the same
  settings (`ay_punch`, `ay_room`, Sound HQ) on every surface, live, with the socket chains' attach configuration,
  rate setup, HQ bypass and reset, and a reset with the card's render epoch on a TTD restore. FM, SAA, PCM and MIDI
  rows untouched. `Ym2203PairBoardsLevel_Test.SsgRatioIsTheSchematicWeightAndTheChainActsAlikeOnBothBoards` (replaces
  `...RoomCrossfeedExplainsTheRest`): punch and room off, card / TSFM within 0.03 dB of the schematic's -7.60 dB;
  with punch and / or room on, each board's row equals a standalone chain at the same settings run over that board's
  chain-off row (within 2 LSB: the chain's int16 round trip taken twice; effect RMS 13-1227 against an error RMS of
  0.7-1.3); the room's content on each board's wiring: +0.14 dB on the TSFM's left, the card's left unchanged and 0.35
  of it on the card's right. `TtdMultiSound_Test.RoundTripMidTune` stays bit-exact. Golden digests: none of the
  committed ones changes - `MultiSoundCard_Test.YmRowsMatchTheirGoldenDigests` hashes the card's rows before
  `SoundManager` (no voicing, no chain), `TtdMultiSound_Test` compares a run with its own replay. What changes by
  design is the mixed card SSG with Sound HQ on: punch (on by default) and room (-9 dB by default) now shape it; with
  both off only the chain's int16 round trip (x 32767 / 32768, truncated, at most 1 LSB) is added, as for the socket
- [x] 2026-10-07 (owner decision, branch `sound-chain-bypass`): `AudioCharacterChain` is zero cost and bit-exact when
  not used - with punch and room off, or Sound HQ off, the row passes untouched (the int16 round trip above is gone, for
  the card's SSG rows and every socket chain alike). Switching punch / room / the room level / Sound HQ ramps over one
  frame; an effect switched on starts from the current input and one that ramped out is cleared; a gap (sound off,
  turbo without audio, a TTD restore of the card or of the socket device, a machine reset, a rate change) resets the
  chains. The board test above now matches its reference chain exactly (0 LSB). Design:
  [ay-tone-voicing.md](../../emulator/design/audio/ay-tone-voicing.md#switching-and-bypass)
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
- [x] MS-7 second pass (2026-10-06, [tdd-integration.md](tdd-integration.md) §6.2): every check against a reference
  rendering (the same program on the TSFM / classic GS / SounDrive it replaces, the SMF on the disk for MIDI, the SAA
  register file for the SAA). Pass: Z-Player 5 (MIDI note for note, MODs = classic GS), Z-Player 4, WC GSPLAYER MOD,
  WPLAYER TFC (= TSFM) and ETC (the E-Tracker disk's tune), the E-Tracker disk, TFM tunes, deNextPlayer (TurboSound
  PT3), X Ball's SounDrive part (Space starts it), the 128K MIDI game Sinty Snoki, MIDIPIANO. ZXM-SoundCard programs
  never start the SAA clock (silent SAA, as on the card); the S98 player is for other ports. No emulator bug found in
  the programs. Not driven: the menus of Titanic, GS Music Player, GS-Player, Wild Player, The Link, the ZXAAA MFX pack
  and a few others (list in §6.2)
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
- [x] Profile the card's frame cost (2026-10-06). Benchmark `BM_MultiSoundFrame/<sources>`
  (`core/benchmarks/emulator/sound/multisoundframe_benchmark.cpp`): one Pentagon frame of the card alone, the
  argument a mask of the sources that play (1 YM2203 pair, 2 SAA, 4 SounDrive, 8 SAM2695, 16 GS module, 31 all;
  0 = idle, the GS firmware in its command loop). Minimum CPU time per frame, dev Mac at load 5-11: idle 0.95 ms,
  YM 1.06 ms, all five 1.31 ms. Where an all-five frame goes (`sample` profile): the YM2203 pair 53 % - its eight
  Reference-quality channel decimators (six SSG FIRs of 97 taps, two FM FIRs of 193 taps, all evaluated for every
  output sample) about 36 %, the SSG generators with their unused stereo mix 11 %, ymfm 5 %; the GS Z80 at 16 MHz
  27 %; the SAM2695 10 % (voices 2.4 %, output resampler 2.3 %, reverb 2.4 %, equalizer 1 %, chorus 0.9 %); the
  board mixer 5 %. The SAM2695 effects path is not the cost; the YM decimators are.
  Done (clearly wasteful, output unchanged): a muted or silent FM part feeds its decimator zeros, and once the
  whole FIR window is zero the output is exactly +0.0 - the FIR is skipped (`Ym2203Pair::renderChannels`,
  `FilterDecimator::window`). A/B (interleaved A B A B A B B A B A, load 5-11, CPU time per frame): FM silent
  -13 % to -16 % (idle 0.95 -> 0.82 ms, SAA / SounDrive / MIDI / GS alone the same), FM playing within noise
  (-0.4 %, -0.3 % all five). Golden digests of the YM rows unchanged
  (`MultiSoundCard_Test.YmRowsMatchTheirGoldenDigests`, taken before the change); the TSFM path is untouched.
  Done 2026-10-07 (branch `ym-decimators-a-c0`; prototype, quality analysis and A/B in
  [ym-decimator-prototype.md](ym-decimator-prototype.md)): one multi-stream FIR pass with NEON / SSE2 kernels
  (`FilterDecimator::getOutputs`, `DecimatorDot`; also used by the TSFM board and the AY device) and the silent-SSG
  FIR skip - both bit-identical; card frame -11 to -27 %, TSFM frame -5 %.
  Done 2026-10-08 (branch `sam-fxidle`): the SAM2695 effects with no input skip their blocks once their state is
  exact zeros (`Effects::Process`; the reverb, chorus, spatial effect and equalizer each derive an idle flag from
  their state, never serialized; a skipped block only moves the write positions and LFO phases). Bit-identical:
  `Fx.IdleEffects*` render every case with the skip on and off (`SynthConfig::skipIdleEffects`) and compare every
  output bit and the state blob. It engages while the chip has played nothing since power-up or a reset (the card
  at rest); a tail does not reliably end in exact zeros - the tank reverbs (0-5), chorus 5 / 7 and the equalizer
  settle into a limit cycle at the denormal guard's quantum (about 2e-27 at the output) and keep running; making
  them stop needs an output change (owner decision: done below, the tail floor). A/B (interleaved base land base land base land
  land base land base, load 6-12, CPU time per frame, minimum, paired mean in brackets): idle 597 -> 589 us
  -1.3 % (-3.8 %), YM -6.6 % (-4.6 %), SAA -4.5 % (-2.4 %), SounDrive -4.4 % (-4.3 %), GS -9.2 % (-6.6 %), MIDI
  -1.0 % (+0.2 %) and all five +1.1 % (+0.2 %) within noise (the effects are busy there); a first run without the
  MIDI rows: idle -5.4 % (-6.6 %), SAA -5.5 %, SounDrive -5.2 %, GS -8.8 %.
  Done 2026-10-08 (branch `sam-fxtail`): the tail floor (library README "Tail floor"). A block without input that
  leaves the reverb's lines and filters, the chorus line or the equalizer's memory below 2^-24 of full scale
  (-144 dBFS; half the LSB of a 24-bit word, the finest word length documented in the SAM2695's family: 16-bit
  samples and RAM, up to 20-bit audio data in the SAM2634 / SAM2635, 24-bit SAM5000 DSP) sets them to +0.0, so
  every tail ends (hall2 5.8 s after the note-off, pan delay 8.0 s, chorus 7 1.0 s) and the skip engages after
  playback. Part of the model (skip on or off), a function of the state (per-line runs of below-floor writes,
  derived again after a load, confirmed by a scan), blob format unchanged. Output change at most 1.0e-7 (0.003 of
  a 16-bit LSB); the card's int16 MIDI row identical on 17 played-and-stopped scenarios (2^-19 and 2^-15 change
  3 samples by 1 LSB; no MIDI golden digest exists). Tests `Fx.TailsEndAtTheFloor`, `Fx.TailOutAcrossState`.
  A/B (`BM_MultiSoundFrame`, new row 32 = the 16 MIDI notes played 2 s, released, 30 s of silence; interleaved,
  load 5-11, CPU time per frame, minimum, paired mean in brackets): MIDI stopped 619 -> 590 us -4.6 % (-4.9 %),
  all five -2.6 % (-1.6 %), idle / YM / SAA / SounDrive / MIDI / GS within noise (-0.7 to +1.3 %).
  Backlog (measure before and after, owner rule "naive first"):
  - an SSG "channel levels only" mode for the card's two generators (prototype variant b, kept as a
    patch, [ym-decimator-prototype.md](ym-decimator-prototype.md) §7: bit-identical, -2 to -4 % of a card frame; it duplicates the level computation of
    `SoundChip_AY8910::updateMixer`, and folding both onto one helper touches the AY hot path: its own A/B)
  - SSG channels that hold one non-zero level still run their FIR (prototype variant c, kept as a
    patch, [ym-decimator-prototype.md](ym-decimator-prototype.md) §7): identical at the card's float output for every YM2149 level, but not the same
    arithmetic (1.2e-15 in double); owner decision 2026-10-07: not landed
- [x] `data/midi/generaluser-gs.sf2` + license + README tracked (Q4, done 2026-10-05), shipped next to the
  executables by every target that ships `data/rom`; `[MIDI] Bank=NONE`; the test runner's policy keeps the default bank
  out of test machines unless asked (`TestSound::DefaultMidiBank`, `MultiSoundSlotCard_Test.ShippedDefaultBankLoadsWithoutAnOverride`); GS 1.05b ROM as `data/rom/gs105b.rom` done in MS-2 (README-ROMS entry)
- [x] Owner question (2026-10-06): should the card's SSG rows run the AY character chain (punch, room crossfeed) the
  socket's chips run? Decided 2026-10-07: yes, done (above)
- [ ] Later: SAM-6 host MIDI output, SAM-7 Dream-native banks research (format reverse-engineered, POC reader /
  converter, integration proposal and owner questions: [sam7-dream-banks.md](sam7-dream-banks.md))
