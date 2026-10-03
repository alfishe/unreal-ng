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
| MS-1 | `Ym2203Pair` extracted from `SoundChip_TurboSoundFM` with `masterClockHz` + ratio accumulator and the I/O port listener; TSFM bit-identical (golden digests before / after, A/B benchmark) | - |
| MS-2 | GS profile (clock, RAM up to 2 MB, host port set, DAC sink); classic GS bit-identical | - |
| MS-3 | `MultiSoundCard` (`ICard`, `CardType` entry `multisound`), `MultiSoundLogic` wired to `Ym2203Pair`, `Saa1099`, GS, `MultiSoundDacs`, `MidiLine`, `sam2695::Synth` | slots SL-4, card logic CL-1, SAA-1, SAM-1, ML-2 |
| MS-4 | `MultiSoundMixer` (board weights) and the five `SoundManager` rows, HUD sources | MS-3 |
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

## 4. TTD

- Blobs: `MultiSoundCard` (logic latches, DACs, YM pair, MIDI line), `Saa1099`, `Sam2695`, GS (id 5). New ids are taken
  in landing order from the next free ones (48+ on master today; the ttd-engine branch's registry rules apply).
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
| GS RAM 1-2 MB in every TTD checkpoint | TTD v2 per-device dedup; GS RAM as a memory region is PLAN #45 |
| MIDI bit-bang timing sensitive to contention and turbo | the line follows emulated time only; tests at several CPU speeds; a mismatch is a CPU timing bug, investigated as such |
| Unknown absolute levels of SSG / SAA / SAM | calibrate per chip module from datasheets; owner measurement if available; weights between sources from the schematic are exact |
