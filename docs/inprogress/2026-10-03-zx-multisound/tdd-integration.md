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
| MS-4 | Slot adapter (`ICard` + `CardType` entry `multisound`, §3.1) once slots SL-4 lands; the five `SoundManager` rows from `MultiSoundCard::Row`, HUD sources (`MultiSoundMixer` is in the card since MS-3) | MS-3, slots SL-4 |
| MS-5 | TTD: card blob + SAA + SAM ids, registry through `SlotManager`, round-trip and session-match tests | MS-3, slots SL-5 |
| MS-6 | Automation (card options through the slot surfaces; card state report `multisound` on every surface), OpenAPI, Qt card panel, recipe | MS-3, slots SL-6 |
| MS-7 | Real-software verification (§6) with TTD recording on | MS-4, MS-5 |
| MS-8 | Docs: `docs/features/` user page, `.recipe/sound/multisound.md`, machine recipes updated where the card is listed | MS-6 |

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
| mixer rows | `MS FM`, `MS SSG`, `MS SAA`, `MS DAC`, `MS MIDI` |
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

## 7. Risks

| Risk | Mitigation |
|---|---|
| `Ym2203Pair` extraction changes TSFM output | golden digests and the TSFM test suite unchanged before / after; one merge for the extraction alone |
| GS RAM 1-2 MB in every TTD checkpoint | resolved by the time-travel engine: the GS RAM is a memory region of 4 KB pieces, only written pieces are stored |
| MIDI bit-bang timing sensitive to contention and turbo | the line follows emulated time only; tests at several CPU speeds; a mismatch is a CPU timing bug, investigated as such |
| Unknown absolute levels of SSG / SAA / SAM | calibrate per chip module from datasheets; owner measurement if available; weights between sources from the schematic are exact |
