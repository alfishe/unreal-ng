# ZX-MultiSound integration: technical design

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | Draft for owner review |
| **Prerequisite** | [ZX-bus slots](../2026-10-03-zx-bus-slots/tdd.md) phases SL-1 to SL-4 (slot core, claim table, migrated TSFM, GS and SounDrive cards) |
| **Modules** | [SAA1099](tdd-saa1099.md), [libsam2695](tdd-libsam2695.md), [MIDI line](tdd-midi-line.md), [card logic](tdd-card-logic.md) |
| **Architecture** | [architecture.md](architecture.md) |
| **Effort scale** | S < 1 week, M 1-2 weeks, L 2-4 weeks |

## 1. Goal

The card exists in the slot catalog, plugs into a ZX-bus slot on every machine that has one, sounds like the real
board through five mixer rows, is fully recorded by TTD, and is configurable and inspectable on every surface, with
real software verified as a user runs it.

## 2. Steps

| Step | Content | Depends on |
|---|---|---|
| MS-1 | `Ym2203Pair` extracted from `SoundChip_TurboSoundFM` with `masterClockHz` + ratio accumulator and the I/O port listener; TSFM bit-identical (golden digests before / after, A/B benchmark). **Done 2026-10-04** (not committed): [architecture.md](architecture.md) §4.1 "As built"; `TsfmGolden_Test` digests identical before / after, TSFM suite + TTD corpus (`tsfm_tech_support` included) green, `Ym2203Pair_Test`. A/B `BM_TurboSoundFrame_*` (4 interleaved rounds x 3 repetitions, CPU-time medians, load average 85-125): Idle 2595 / 2621 us, PlayerLoad 2460 / 2473 us, PlayerLoad_Turbo 1195 / 1201 us before / after (+0.5-1.0 %, inside the 5 % round-to-round spread) | - |
| MS-2 | GS profile (clock, RAM up to 2 MB, host port set, DAC sink); classic GS bit-identical. **Done 2026-10-04** (not committed): `GSProfile` ([architecture.md](architecture.md) §4.2 "As built"), `SoundChip_GeneralSound_Profile_Test`, GS 1.05b in `data/rom/` | - |
| MS-3 | `MultiSoundCard`, `MultiSoundLogic` wired to `Ym2203Pair`, `Saa1099`, GS, `MultiSoundDacs`, `MidiLine`, `sam2695::Synth`, `MultiSoundMixer`. **Done 2026-10-04** (not committed), without the slots framework: a self-contained class with an explicit port / time / audio API ([architecture.md](architecture.md) §1 "As built"); the `ICard` adapter and the `CardType` entry move to MS-4 (§3.1). `MultiSoundCard_Test` (13 tests: the requirements §1 worked example, FM muted after reset until bit 2 clears, `#FF` stops the SAA, `#DFFD`, ROM lock, DIP / `ctrlMask` options, the board weights per row, hard-panned SounDrive, GS / SounDrive on one DAC with the GS `#0B` bit, GS 1.05b boot + sample upload and playback, a bit-banged MIDI note on U4 and none from U10, "no bank", the TFM player trace); `Ym2203Pair_Test.PerChannelOutputsRenderAWholeSyncedFrameAtOnce` (the render cursor fix) | card logic CL-1, SAA-1, SAM-1, ML-2 |
| MS-4 | Slot adapter (`ICard` + `CardType` entry `multisound`, §3.1) once slots SL-4 lands; the five `SoundManager` rows from `MultiSoundCard::Row`, HUD sources (`MultiSoundMixer` is in the card since MS-3). **Done 2026-10-04** (not committed): §3.2 "As built"; `MultiSoundSlotCard_Test` (10 tests: the card built for its slot with its options, the ZX-Evo YM2149 out of its socket, CardWins shadowing on the Pentagon, RdWr detection of the ZX-Evo's board ports, the rows, a Z80 program playing all five sources on a Pentagon and a ZX-Evo, the matrix refusals at creation) | MS-3, slots SL-4 |
| MS-5 | TTD: card blob + SAA + SAM ids, registry through `SlotManager`, round-trip and session-match tests. **Done 2026-10-05** (not committed): §4 "As built"; ids 58 `MultiSound`, 59 `Sam2695`, 60 `MultiSoundGs`, region 17 `MultiSoundGsRam`; `TtdMultiSound_Test` (7 tests: devices registered by slot, `RoundTripMidTune`, `MidiByteAcrossCheckpoint`, `SessionRefusesOtherBank`, card / no-card mismatch, a missing card device refuses recording, two instances of one module refused by the planner); the card in `TTDModelStateContract_Test.EveryDeviceMatchesItsDescriptorOnEveryModel` | MS-3, slots SL-5 |
| MS-6 | Automation (card options through the slot surfaces; card state report `multisound` on every surface), OpenAPI, Qt card panel, recipe | MS-3, slots SL-6 |
| MS-7 | Real-software verification (§6) with TTD recording on | MS-4, MS-5 |
| MS-8 | Docs: `docs/features/` user page, `.recipe/sound/multisound.md`, machine recipes updated where the card is listed. **Done 2026-10-05** (not committed): [docs/features/multisound.md](../../features/multisound.md); the recipe is [.recipe/peripherals/multisound.md](../../../.recipe/peripherals/multisound.md) (MS-6); the card in the Pentagon, Scorpion, ZX-Evo Baseconf and TS-Conf machine recipes | MS-6 |

## 3. Card catalog entry

| Field | Value |
|---|---|
| id | `multisound` |
| display name | ZX-MultiSound (UzixLS) |
| native bus | `zxbus` |
| required signals | IORQGE, +12 V |
| options | `dip` (set of `ym`, `saa`, `gs`, `sd`; default all), `gsRam` (`1M` default, `2M`), `ctrlMask` (`pro` default, `classic` marked unofficial) |
| functions | `ym` -> `ay-socket` (shadowing role) + `midi`; `saa`; `gs`; `sd` -> `soundrive` |
| ports | architecture §3 |
| media | none (the GS has no SD slot on this card) |
| mixer rows | `MS SSG 1`, `MS SSG 2`, `MS FM 1`, `MS FM 2`, `MS SAA`, `MS PCM`, `MS MIDI` (per chip since 2026-10-05, [architecture.md](architecture.md) §5) |
| ROM | `data/rom/gs105b.rom` (next to `gs104.rom` / `gs105a.rom`) (GS 1.05b, from the card repository's `rom/`; checksum in the ROM README) |
| bank | `[MIDI] Bank=` (default `data/midi/generaluser-gs.sf2`, Q4) |

### 3.1 Slot adapter shape (MS-4)

The slots `ICard` ([slots architecture](../2026-10-03-zx-bus-slots/architecture.md) §3.2) wraps `MultiSoundCard`
one call to one call; the adapter owns the only state the card does not: the absolute time base.

```cpp
class MultiSoundSlotCard : public ICard
{
public:
    MultiSoundSlotCard(CardContext& ctx, const CardOptions& o)      // options -> MultiSoundCardConfig (dip, gsRam,
        : _card(ctx.emulator, ConfigFrom(ctx, o)) {}                //   ctrlMask, [MIDI] Bank, the host's AudioTstate
                                                                    //   rate, the SoundManager's output rate)
    const CardType& Type() const override;                          // the refdata entry "multisound"
    uint8_t In(uint16_t port, uint64_t t, bool& drives) override  { return _card.In(port, Abs(t), drives); }
    void Out(uint16_t port, uint8_t v, uint64_t t) override       { _card.Out(port, v, Abs(t)); }
    uint8_t Peek(uint16_t port) const override                     { return _card.Peek(port); }
    void BusReset(uint64_t t) override                             { _card.BusReset(Abs(t)); }
    void FrameStart() override  { _card.FrameStart(_base + Now(), FrameTicks()); }
    void FrameEnd() override    { _card.FrameEnd(_base + FrameTicks(), _soundManagerSamples);
                                  _base += FrameTicks(); }          // the machine rebases its t by the frame
    void RegisterMixerRows(SoundManager&) override;                 // five rows reading _card.Row(...)
    void CollectTtdSerializers(std::vector<ttd::TTDSerializable*>&) override;   // MS-5
    void Describe(CardReport& out) const override;                  // MultiSoundCardReport -> CardReport
    // M1: the claim table calls _card.M1(pc) for lockedOnRomFetch claims (slots §4.3 "ROM-fetch lock")

private:
    uint64_t Abs(uint64_t frameRelativeT) const { return _base + frameRelativeT; }  // t = AudioTstate(z80->t)
    MultiSoundCard _card;
    uint64_t _base = 0;         // absolute card time of the current frame's t = 0
};
```

- **Time:** the machine's `t` is frame-relative (`AudioTstate(z80->t)`, rebased by the frame length at every frame
  end); the card wants absolute, monotonic ticks. The adapter keeps `_base` and adds it; `_base` is TTD state of the
  adapter (MS-5: it goes into the card's blob set with the pair's ratio phase).
- **Rates:** `hostTickRate` = the machine's audio T-state rate (frame / frame duration: 3.5 MHz Pentagon, 3.5469 MHz
  128K); turbo is already removed by `AudioTstate`. A configuration change that alters it restarts the machine (slots
  Q6), so the card never sees a rate change.
- **Ports:** the claims of architecture §3 come from the refdata entry; the adapter forwards every claimed cycle, and
  `Iorqge(port)` answers the claim table where the arbitration needs the card's view (the claims already carry it).
- **Rows:** `RowFrames()` frames per `FrameEnd`, the count `SoundManager` asked for.

### 3.2 As built (MS-4, 2026-10-04)

The slots framework gained what a card written for slots needs ([slots tdd.md](../2026-10-03-zx-bus-slots/tdd.md) §9):
`ICard` / `CardType` (`core/src/emulator/slots/card.{h,cpp}`), `SlotManager::BuildCards` / `ReleaseCards`, and the
claim table's cycle resolution in production for slot-built cards. The adapter is
`core/src/emulator/slots/cards/multisound/multisoundslotcard.{h,cpp}` (`MultiSoundSlotCard`).

| Item | As built |
|---|---|
| Shape | `ICard` derives from `PortDevice`: the claim table hands the card its bus cycles through `portDeviceOutMethod` / `portDeviceReadCycle(port, drives)` (new on `PortDevice`; the default drives what `portDeviceInMethod` returns) at the machine's now, so the access path passes no time; `Peek`, `BusReset`, `FrameStart`, `FrameEnd(samples)`, `SetOutputRate`, `MixerRows`, `MixerBuffer`, `WantsWideMix`. TTD (`CollectTtdSerializers`), media and `Describe` on the interface come with MS-5 / MS-6 |
| Time | the axis `SoundChip_Moonsound` uses: origin + `AudioTstate(z80->t)` x the host speed multiplier at `CPU_CLOCK_RATE` ticks per second (the axis of every sound device and of the mixer's sample count; turbo descaled), clamped monotonic; the origin moves by `config.frame` x the multiplier at each `FrameEnd`. **Deviation from §3.1:** `hostTickRate` is `CPU_CLOCK_RATE` (3.5 MHz) on every machine, not the machine's own T-state rate (3.5469 MHz on a 128K): the mixer turns `config.frame` T-states into `frame x rate / CPU_CLOCK_RATE` output samples on every model, so one T-state is 1 / 3.5 MHz of output on every model and the card must count on the same axis to fill the same samples; the YM pair's ratio is then 1 : 1 everywhere (architecture.md §2's 128K ratio assumed the other axis) |
| Options | `dip` (`ym`, `saa`, `gs`, `sd`), `gsRam` (`1m` / `2m`), `ctrlMask` (`pro` / `classic`) from the slot (`OptionsFrom`); `[MIDI] Bank=` (`Config::GetMidiBank`, since 2026-10-05 outside `CONFIG`: slots tdd.md §12; resolved like a ROM path; empty = the card's default `midi/generaluser-gs.sf2`); the row rate = `SoundManager`'s core rate, followed on a rate change |
| ROM lock | before each cycle the adapter gives the card `M1(z80->m1_pc)` (the IN / OUT instruction's opcode fetch); the claim table's `lockedOnRomFetch` reads the same address through `IClaimSignals`, so the SAA and SounDrive claims are skipped by both |
| Rows | `MS SSG 1`, `MS SSG 2`, `MS FM 1`, `MS FM 2`, `MS SAA`, `MS PCM`, `MS MIDI` (`AudioSourceType::MultiSound*`, mixer keys `ms_ssg1`, `ms_ssg2`, `ms_fm1`, `ms_fm2`, `ms_saa`, `ms_pcm`, `ms_midi`; per chip since 2026-10-05, before that five rows `ms_fm` / `ms_ssg` / `ms_saa` / `ms_dac` / `ms_midi`); volume / mute / solo / analyzer capture like any row; the wide float bus with the master limiter while the card is fitted; one HUD indicator per row (`AudioSource::MultiSoundSsg1` .. `MultiSoundMidi`: "MS AY 1/2", "MS FM 1/2", "MS PCM", "MS SAA", "MS MIDI", category `audio-multisound`); multitrack recording names. Rows exist only with the card |
| Frames | `SoundManager` calls `FrameStart` at every frame start (all modes) and `FrameEnd(samplesThisFrame)` before mixing; turbo without audio `FrameEnd(0)` (the card still runs to the frame end) |
| Bus reset | `SoundManager::reset` (machine reset) calls `BusReset`: the card's CPLD, YM pair, SAA, GS, DACs, MIDI line and SAM2695 share the ZX /RESET |

Not in MS-4: the card is not in the TTD device set (MS-5, done 2026-10-05: §4.1), no automation state report or Qt
panel (MS-6), the frame cost (~1 ms per frame, TODO) is not profiled.

## 4. TTD

- Blobs: `MultiSoundCard` (logic latches, DACs, YM pair, MIDI line), `Saa1099`, `Sam2695`, the board's GS. New ids are
  taken in landing order from the next free ones (58+ on master today, 2026-10-04); the time-travel engine binds
  devices by `PeripheralId` and regions by `TTDRegionId`, so the board's GS gets its own peripheral id and RAM region id
  (17+) next to a classic GS card (architecture §4.2).
- Engine contract (TTD v2): every device's `TTDDescribe` matches its blob (`CheckDeviceTable`) and the card joins
  `TTDModelStateContract_Test.EveryDeviceMatchesItsDescriptorOnEveryModel` once registered. Ready on the branch: the
  YM pair's time fields (`Ym2203Pair::TTDTimeFields`) and sync check (`Ym2203Pair::TTDSyncedTime`), the GS profile's
  descriptor and RAM region (architecture §4.1, §4.2). The card's blob places the pair's chips at a known offset and
  passes it to `TTDTimeFields`; `runsBehindCpu` because the pair and the GS run behind the CPU.
- The card's options and the bank SHA-256 are part of the session's configuration fingerprint; a session recorded with
  another bank or other options is refused with the difference (slots §8).
- Tests (`core/tests/debugger/ttd/ttdmultisound_test.cpp`):
  - `TtdMultiSound_Test.RoundTripMidTune`: record a scripted tune using all five sources, seek back, replay, compare
    the card state hash and the five rows' audio digests with the original run.
  - `TtdMultiSound_Test.MidiByteAcrossCheckpoint`: a checkpoint between two bits of a MIDI byte, restore, byte arrives.
  - `TtdMultiSound_Test.SessionRefusesOtherBank`.
- The TTD fixture corpus gets one MultiSound fixture per machine family where it fits (Pentagon, ZX-Evo); recorded
  once the card lands (memory: re-record after any device-state change).

### 4.1 As built (MS-5, 2026-10-05)

| Item | As built |
|---|---|
| Devices | Four engine devices, each named by the card's slot: `MultiSound` (id 58, `zxbus.N.multisound`: the adapter's time base, then `MultiSoundCard::TtdSave` - the card axis times, the FM mute changes not yet rendered, the CPLD latches and DAC registers, the YM2203 pair's synced time and blob, the MIDI line, the shared DACs; layout in `multisoundcard.h`), `Saa1099` (53, `.saa1099`, the chip's own blob), `Sam2695` (59, `.sam2695`, `sam2695::Synth::SaveState`, which names the bank by SHA-256), `MultiSoundGs` (60, `.gs`, the GS blob; its RAM is the engine region `MultiSoundGsRam` (17), `multisound.gs.ram`). `multisoundttd.{h,cpp}` holds the two wrappers (`MultiSoundCardTtd`, `Sam2695Ttd`); the GS takes its ids from its profile (`GSProfile::ttdPeripheralId` / `ttdRegionId` / names; the classic card keeps 5 / 1) |
| Registration | `ICard::CollectTtdDevices` (id, device, instance, region source); `RegisterMachinePeripherals` registers every slot-built card's devices before the plan check; `CardType::ttdIds` lists the ids a card registers, and `SlotManager::TtdDevicesMatchPlan` refuses recording, naming the slot, when one is missing or the card declares none (a slot-built card can no longer be recorded without its state) |
| Engine descriptor | `MultiSound`: `runsBehindCpu`, time fields (the adapter's origin and last time, the card's now / frame base / rendered-to, the pair's synced time and the ymfm counters through `Ym2203Pair::TTDTimeFields`, the DACs' time); `TTDSyncedTime` = the pair synced to the CPU's position on the card axis. `Sam2695`: `firmwareFingerprint` = the bank's SHA-256 folded to 64 bits. `MultiSoundGs`: the GS descriptor (ROM fingerprint, runs behind the CPU) |
| Render layers | Not state (filters, output buffers, resamplers, the last MIDI level): every module's load drops them and `MultiSoundCard::TtdLoad` resets the mixer, so the audio after a restore does not depend on what played before it |
| Fingerprint | `slots.<slot>.bank` = the bank's SHA-256 folded (0 = no bank), `affectsRestore`, from `ICard::TtdFingerprint` once the cards are built; the card's options were already in `slots.<slot>` (SL-5) |
| Session guard | the slot-set guard knows a position per slot-built card type (its first id): `zxbus.1: recorded none, this machine multisound` / `multisound card: recorded multisound, this machine none`; then `ICard::TtdSessionMatches`: the MultiSound reads the bank digest out of the session's `Sam2695` blob (`sam2695::Synth::StateBank`) and refuses another bank (`zxbus.1: MIDI bank differs from the recording (recorded SHA-256 ...)`) |
| Two instances | the planner refuses them up front (a second MultiSound shares `saa`, a GS card shares `gs`: a conflict, slots Q8); with the card's GS switched off by its DIP a GS card may stay, and the two GS record under their own ids (5 and 60) |
| Corpus | no device format changed (the classic GS blob and every other id are unchanged), so the corpus is not re-recorded. No MultiSound fixture: the corpus recorder creates machines from the shipped configs through the WebAPI, which cannot fit a card yet (slot surfaces: SL-7 / MS-6), no shipped config fits the card (slots Q8), and `TTD_Corpus_Test` would need the slot set of the fixture. Left in the TODO |
| Cost | the GS blob carries its 1-2 MB RAM in every v1 checkpoint, as the classic GS does with its 128-512 KB (compressed; the engine keeps the RAM as a region of 4 KB pieces and stores only written pieces) |

Tests (`core/tests/debugger/ttd/ttdmultisound_test.cpp`, `TtdMultiSound_Test`): `DevicesRegisteredBySlot`;
`RoundTripMidTune` (all five sources, the CPU writing a SounDrive square wave: recorded for eight frames, replayed from
the session start and from frame 3; every frame's five row digests and the four devices' state equal the original
run's - the original run restarts its render layers at the same two points, since a restore does so);
`MidiByteAcrossCheckpoint` (the frame boundary inside the second byte of a Note On: restored there, the byte still
arrives, no framing error, the synthesizer state equal); `SessionRefusesOtherBank`; `SessionGuardRefusesCardMismatch`
(both ways); `RecordingRefusedWhenACardDeviceIsMissing`; `TwoInstancesOfOneModuleRefusedByThePlanner`. Also
`TTDModelStateContract_Test.EveryDeviceMatchesItsDescriptorOnEveryModel` with the card on Pentagon, Profi Scorpion,
ZX-Evo, TS-Conf, ATM Turbo 2+ (7.10, 4.50, behind the CPU-socket adapter) - the Scorpion's system port lacks +12 V, so
the card is left out there - and `SoundChip_GeneralSound_Profile_Test.MultiSound_MatchesTheEngineDescriptorWithItsRamAsRegion`
(the board GS's own ids). A mutant that skips the YM pair's restore fails `RoundTripMidTune` and
`MidiByteAcrossCheckpoint`.

## 5. Automation and Qt

| Surface | Content |
|---|---|
| Slots surfaces | `slots plug zxbus.1 multisound dip=ym,gs gsRam=2M` etc. (slots §9); nothing card-specific needed for fitting |
| State report | `DeviceState::MultiSound`: logic latches, DIP, fit, shadowed devices, YM pair state (shared with TSFM's report), SAA registers, GS (shared GS report), DAC channels, MIDI line + synthesizer (channels, programs, active voices, UART counters, bank) |
| WebAPI | `GET /state/audio/multisound`; MIDI control `POST /control/audio/midi` (`panic` = all notes off); the bank is configuration (`[MIDI] Bank=`), changed like a slot option: plan, then a machine restart (slots Q6) |
| CLI | `multisound` (state), `midi panic`; bank through the configuration |
| MCP | `inspect_state` aspects `audio_multisound`, `audio_midi`; control through `invoke_api` |
| Lua / Python | `multisound_state()`, `midi_state()`, `midi_panic()` |
| Qt | slot window card panel (DIP checkboxes, gsRam, ctrlMask with the "unofficial" note), mixer rows, a MIDI activity view (channels, programs, note activity) |
| Recipe | `.recipe/sound/multisound.md`: fit the card, play a TSFM tune, a SAA tune, a MIDI file through WC, record each source |

### 5.1 As built (MS-6, 2026-10-05; with slots SL-7)

| Item | As built |
|---|---|
| Fitting | the slots surfaces (`slots plug zxbus.next multisound gsRam=2m`, WebAPI `/slots/{slot}/plug`, MCP `slots_plug`, Lua / Python `slots_plug`, create with `"slots"`, Qt Machine > Slots with check boxes for the DIP set) |
| State | `DeviceState::MultiSound` / `DeviceState::Midi` (`slots/cards/multisound/multisounddevicestate.cpp`): slot, options with `ctrl_mask_note`, fit, `shadowed_devices`, `logic` (CPLD latches + the four DAC registers as latched), `ym.chips[2]` with `ssg` = `DeviceState::AyChipReport` and `fm` = `DeviceState::Ym2203ChipReport` (the TSFM report's builder, now shared), `saa`, `gs` = `DeviceState::GeneralSoundReport` (the GS slot's builder) plus firmware state, `dac` (output stage), `midi`; `Midi()`: line, bank, 16 parts (program 1-128, preset name from the bank, volume, pan, expression, pitch bend, voices, `keys` / `notes` sounding), polyphony, effects, counters |
| MIDI panic | `MidiControl::Execute(context, "panic")` -> TTD live input `TTDInputKind::MidiPanic` (17) -> `ICard::MidiPanic` -> `sam2695::Synth::Panic(t)`: a queued event that kills every voice (All Sound Off on all parts) and leaves controllers, programs and the parser's running status; journaled while recording, replayed by a seek |
| Library | `sam2695::Synth::Panic`, `SynthReport::ChannelView::keys` (the keys sounding, 2 x 64 bits) |
| Surfaces | WebAPI `GET /state/audio/multisound`, `GET /state/audio/midi`, `POST /control/audio/midi` (+ OpenAPI `openapi_multisound.inc`); CLI `multisound [--full|--json]`, `midi [--json]`, `midi panic` (`cli-multisound.h`); MCP aspects `audio_multisound`, `audio_midi`, the panic through `invoke_api`; Lua `multisound_state()`, `midi_state()`, `midi_panic()`; Python `emu.multisound_state()`, `emu.midi_state()`, `emu.midi_panic()` |
| Qt | the card's options in the slots window (DIP check boxes, gsRam, ctrlMask with the note on the unofficial `classic`); the seven mixer rows (MS SSG 1 / 2, MS FM 1 / 2, MS SAA, MS PCM, MS MIDI) are SoundManager devices and show in the audio settings as every device does; the "FM trim" control shows for the card too and drives every YM2203 FM; Tools > MIDI Activity (parts, presets, a 16 x 128 key strip of the notes sounding, Panic); a HUD icon of its own (`multisound`) |
| Recipe | [.recipe/peripherals/multisound.md](../../../.recipe/peripherals/multisound.md) (not `.recipe/sound/`: the library keeps sound cards in `peripherals/`), verified 2026-10-05 on a Pentagon: a TSFM tune (`tech_support.sna`) on the card's FM, a SAA tone (653 Hz), two MIDI notes bit-banged through YM IOA2 (C4, E4 on GeneralUser GS), each captured by its own source (`ms_fm`, `ms_saa`, `ms_midi`), the panic replayed by a TTD seek |

Tests: `MultiSoundDeviceState_Test.*` (3), `CliMultiSound_Test.SummaryAndMidiText`, `McpTools_Test.InspectState_MultiSoundAndMidiAspects`,
`SlotsWindow_Test.MidiActivityListsTheParts` (unreal-qt-tests), `sam2695tests` `Synth.PanicStopsEveryVoiceKeepsTheStream`.
The MultiSound test helpers (`StagedMachine`, `Out`, `ParkCpu`, `WriteTestBank`) moved to
`core/tests/emulator/slots/cards/multisound/multisoundstagedmachine.h`.

## 6. Real-software verification (MS-7)

Each run as a user would run it, TTD recording on (rolling limit), results in the folder TODO:

| Program | Machine | Checks |
|---|---|---|
| A TFM Music Maker tune player | Pentagon 128, ZX-Evo | FM + SSG audible after the player's control bytes; ACB B-center panning |
| VGMPLAY.WMF (`testdata/machines/tsconf/wildcommander/extra-plugins/vgmplay/`) with SAA and AY tracks | ZX-Evo / TS-Conf | SAA clock enable via control byte; dual SAA command handling |
| WC MIDI player (`ay-rs232/midi`, WC history v1.06) | ZX-Evo / TS-Conf | MIDI messages arrive without framing errors at 3.5 / 7 / 14 MHz (turbo changes the bit-bang loop; the line must follow the real timing) |
| A GS MOD player | Pentagon | GS at 16 MHz, 1 MB; hard L / R panning |
| A SounDrive / Covox player | Pentagon | channels 0-1 left, 2-3 right; shared DAC with GS |
| Ball Quest | Pentagon | clicks with `ctrlMask = pro` (as the real card), clean with `classic` |
| ZX-Evo TurboSound shadowing | ZX-Evo | the built-in TurboSound row reports `shadowed`, silent; the card plays |

### 6.1 Results (MS-7, 2026-10-05)

Run on a freshly built `unreal-qt` of its own (WebAPI on its own ports), card created with `slots`, TTD recording
(rolling limit) started before every program, `[MIDI] Bank` = the shipped default. Levels are RMS of the per-source
capture (`ms_fm`, `ms_ssg`, `ms_saa`, `ms_dac`, `ms_midi` at the time - since 2026-10-05 per chip `ms_ssg1/2`, `ms_fm1/2`, `ms_pcm`, 0..1 full scale). The images and the exact keys are in the
test images' README (untracked, `testdata/sound/multisound/software/README.md`).

| Program | Machine | Sources heard | Checks | Result | TTD check | Notes |
|---|---|---|---|---|---|---|
| TFM tune `uzhos.scl` | Pentagon | FM (L = R 0.058) | FM unmuted by the player's control byte; this tune uses no SSG | pass | seek back + replay: screen digest equal; `ms_fm` equal to the sample after 0.6 s | the first 0.6 s differ only by the coupling filters' history (render layer, not state) |
| TSFM `tech_support.sna` | Pentagon | FM 0.064, SSG L 0.016 / R 0.014 | FM + SSG; ACB panning by a register program on the same card: A -> L only (0.061 / 0.000), B centre (0.0315 / 0.0315), C -> R only | **first pass wrong, fixed 2026-10-05** | - | the first pass judged FM by its level only. Owner (live demo): clicks. Before the fix `ms_fm` was RMS 0.057 with 24 zero crossings per second and 63 % of its energy below 20 Hz (a frame-rate staircase: the render cursor ran a block ahead of the chips after the snapshot load's bus reset, TODO). After: RMS 0.064, 4521 crossings / s, spectral peaks 160.8 / 87.5 / 48.8 Hz = the TSFM's FM 2 captured at the same moment on a plain Pentagon; log-band spectrum correlation of the card's FM with the TSFM's FM 1 + FM 2: 1.000 |
| TSFM `tech_support.sna` | ZX-Evo (ATM3) | FM 0.051, SSG 0.014 | the YM2149 is "taken out of its socket" (Q7): no board AY row at all; the card plays | pass | - | the tdd's "built-in row shadowed" became socket removal (Q7) |
| Ball Quest `BQ.TRD` | ATM3 (`BQ   ATM`); Pentagon `BQ   16C` | SSG | the game writes `#F0-#F7` to `#FFFD` at frames 1747 / 1835 / 1847-48 / 2912-2926 / 3077-3109 (the spacing of issue #11's clicks at 18 / 20 / 42 / 45 s); with `pro` the card switches to U4 and unmutes FM there (state by TTD seek), with `classic` (needs `dip` without `saa`) it does not | logic as the real card; audio `pro` = `classic` bit-exact: **no click** | port events from the TTD journal | owner decision 2026-10-05: the click is **not** modeled (FM mute stays silence); the real level is the YM3014B's all-zero word, `S2..S0 = 000` "not allowed" in the datasheet. Pentagon `16C` shows a black screen (no 16-colour mode) |
| SAA test `1099test.trd` | Pentagon | SAA | all 8 test pages audible, SAA clock on; per page L / R follow the voices' amplitudes (L-only, R-only and both pages) | pass | - | keys 1-6 (channel toggles) unreliable through automation taps |
| `kissme2.trd` | Pentagon | SSG only | no SAA write, no control byte | not a card program | - | probably detects a ZXM-SoundCard |
| LnxTracker Demo `lnxtdemo.SCL` | Pentagon | SSG songs: SSG; SAA songs: silent | the program writes only `#FE` / `#FF` to `#FFFD` (its per-frame AY driver), never a control byte with bit 3 = 0, so the SAA clock never starts (reset state, §3.3) | **bug found and fixed** (SAA output with the clock stopped) | - | a ZXM-SoundCard program; silent SAA is what the card does. Before the fix the stopped SAA played its amplitude writes as a 3-7 Hz step "tune" |
| VGMPLAY `saa-tones.vgm` | TS-Conf, WC | SAA | SAA clock started by the plugin's control byte; L-only / R-only / both segments in turn | pass | - | |
| VGMPLAY YM2203 VGM | TS-Conf, WC | FM 0.032, SSG 0.016 | FM + SSG | pass | - | |
| VGMPLAY `saa-dual.vgm` | TS-Conf, WC | SAA (chip 0's part) | a proper dual-SAA VGM (dual flag, 63 commands per chip); on the card's one SAA the plugin plays chip 0 | pass (chip 0); key 2 not effective | - | key 2 shows `SAA:2` but the plugin keeps writing chip 0's stream (register trace): plugin behavior, v0.9.03-beta |
| ZX MIDI Player v3 + test files `midi-test.trd` | Pentagon 3.5 MHz | MIDI | the line decoded from the TTD port journal: note-ons of `scale` 15/15, `chanprog` 75/75, `drums` 96/96, `controls` 7/7 in file order; 0 framing errors; synth bytes = decoded bytes | pass | seek: the synthesizer report equal live / replayed; `ms_midi` equal except the render layers (coupling filters, reverb / chorus tails) | output device "TS chip 2" (= `#FE` = U4) |
| same | ZX-Evo 3.5 / 7 MHz | MIDI | as above, every note of the four files | pass | - | 1 framing error at start: the player makes IOA an output (R7) before it writes R14, the line sits low (a break) - the same on the card |
| same | ZX-Evo 14 MHz | MIDI garbled | the player's bits are 592 T apart instead of 448 (23.6 kbaud): synth framing errors | **program / machine** | - | the ZX-Evo's 14 MHz DRAM waits (the RTL-simulated model) slow the player's delay loop, which assumes none; owner question |
| WC `GSPLAYER.WMF` `.MID` (`-midi_chip=2`) | TS-Conf 14 MHz | MIDI | `chanprog` notes arrive (72 / 75 seen by polling), no framing error after the start | pass | - | 1 framing error at start: R7 before R14, as above |
| Mod Player v2.5 `mplv2_5.trd` | Pentagon | GS (16 MHz, 1024 KB) | L / R correlation -0.05: hard L / R split | pass | - | |
| Soundrive Player `emdig1.scl` | Pentagon | DAC | channel 0 left, channel 2 right (DAC L 0.19 / R 0.12) | pass | - | |
| `MODS20SP.SCL`, `xball.TRD` | Pentagon | - / SSG | the disk holds the player only (no MODs); X Ball's SounDrive part not reached in 24 s | not checked | - | |

Not run in MS-7: Z-Player 5, the other GS / SAA / TSFM disks of the README, WC MOD / TFC / ETC plugins.

## 7. Risks

| Risk | Mitigation |
|---|---|
| `Ym2203Pair` extraction changes TSFM output | golden digests and the TSFM test suite unchanged before / after; one merge for the extraction alone |
| GS RAM 1-2 MB in every TTD checkpoint | resolved by the time-travel engine: the GS RAM is a memory region of 4 KB pieces, only written pieces are stored |
| MIDI bit-bang timing sensitive to contention and turbo | the line follows emulated time only; tests at several CPU speeds; a mismatch is a CPU timing bug, investigated as such |
| Unknown absolute levels of SSG / SAA / SAM | calibrate per chip module from datasheets; owner measurement if available; weights between sources from the schematic are exact |
