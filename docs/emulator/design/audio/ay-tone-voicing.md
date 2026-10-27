# AY Tone Voicing

**What it is:** a fixed EQ ("voicing") on the AY / YM2149 output, including the SSG half of
TurboSound FM. It shapes how much very low bass and how much treble reach your speakers or
headphones. It is chosen per emulator instance, from a small set of named profiles, and can be
changed while music plays without a click.

Related settings on the same output: **punch** (transient enhancement) and **room** (headphone
crossfeed) for the AY, and **punch** for the beeper — see [Related settings](#related-settings).

Design history and measurements:
[docs/inprogress/2026-09-25-ay-tone-voicing](../../../inprogress/2026-09-25-ay-tone-voicing/)
([proposal](../../../inprogress/2026-09-25-ay-tone-voicing/proposal.md),
[TDD](../../../inprogress/2026-09-25-ay-tone-voicing/ay-tone-voicing-tdd.md)).

## Which profile to choose

| If you… | Choose |
|:--|:--|
| listen on headphones or full-range speakers and want a comfortable sound with softer highs | **Headphones** |
| want the bass balance the emulator had before the accurate output model, with untouched treble (**default**) | **Classic** |
| analyse the AY output, compare with a real board's line out, or measure levels / spectra | **Flat** |
| find Headphones still bright or the bass still heavy, but TV speaker too thin | **Warm** |
| want the sound most people knew in the 1980s–90s: the music through a TV set | **TV speaker** |
| want the sound of a clone's built-in speaker or a cheap amplifier | **Small speaker** |

The profiles run from "no processing" to "strongest colouring" in this order, which is also the
order of the menu in unreal-qt.

## Profiles

| Profile | Setting value | What you hear | Curve |
|:--|:--|:--|:--|
| **Flat** | `flat` | The AY as the hardware line output delivers it: full low bass, volume-change thumps included. Bit-identical to having no voicing at all | none (exact bypass) |
| **Classic** | `classic` (also `legacy`) | Softer bass: the very low bass and the thump of volume changes are trimmed, with a gentle lift around 100 Hz. Treble untouched | 1st-order high-pass 64.2 Hz + peak 106.9 Hz / +3.06 dB / Q 1.0 |
| **Headphones** | `headphones` | Classic's bass plus gently softened highs, so the AY square waves are less harsh | Classic + low-pass 10 kHz, Q 0.5 |
| **Warm** | `warm` | Between Headphones and TV speaker: less deep bass and softer highs | 2nd-order high-pass 90 Hz, Q 0.6 + low-pass 8 kHz, Q 0.5 |
| **TV speaker** | `tv` | The music through a TV set's small speaker: no deep bass, softened highs | 2nd-order high-pass 130 Hz + low-pass 6 kHz, both Q 0.7071 (Butterworth) |
| **Small speaker** | `small_speaker` | A clone's built-in speaker or a cheap amplifier: thin, with a forward midrange and no highs | 2nd-order high-pass 250 Hz + peak 1.5 kHz / +3 dB / Q 1.0 + low-pass 4.5 kHz, Q 0.7071 |

Classic and Flat are exact: Classic reproduces a measured filter (see [Classic](#classic)).
Headphones, Warm, TV speaker and Small speaker are listening curves: their values are starting
points chosen from what those listening situations sound like, and may be refined by ear. Their
setting values stay the same when that happens.

### Frequency response

Level change in dB caused by each profile (the 5 Hz output coupling, which every profile
shares, is not included). A negative number means quieter than Flat.

| Frequency | Flat | Classic | Headphones | Warm | TV speaker | Small speaker |
|--:|--:|--:|--:|--:|--:|--:|
| 30 Hz | 0.0 | −7.2 | −7.2 | −19.5 | −25.5 | −36.8 |
| 41.2 Hz (E1) | 0.0 | −4.8 | −4.8 | −14.4 | −20.0 | −31.3 |
| 60 Hz | 0.0 | −2.1 | −2.1 | −8.9 | −13.6 | −24.8 |
| 100 Hz | 0.0 | +1.5 | +1.5 | −3.6 | −5.9 | −16.0 |
| 130 Hz | 0.0 | +1.7 | +1.7 | −2.1 | −3.0 | −11.6 |
| 250 Hz | 0.0 | +0.4 | +0.4 | −0.5 | −0.3 | −2.9 |
| 500 Hz | 0.0 | +0.1 | +0.1 | −0.1 | 0.0 | +0.1 |
| 1 kHz | 0.0 | 0.0 | −0.1 | −0.2 | 0.0 | +1.7 |
| 1.5 kHz | 0.0 | 0.0 | −0.2 | −0.3 | 0.0 | +2.9 |
| 3 kHz | 0.0 | 0.0 | −0.7 | −1.1 | −0.3 | +0.1 |
| 5 kHz | 0.0 | 0.0 | −1.9 | −2.9 | −1.7 | −3.7 |
| 8 kHz | 0.0 | 0.0 | −4.3 | −6.0 | −6.2 | −10.3 |
| 10 kHz | 0.0 | 0.0 | −6.0 | −8.2 | −9.4 | −14.0 |
| 15 kHz | 0.0 | 0.0 | −10.2 | −13.1 | −16.0 | −20.9 |

How to read it, with two examples:

- A bass line on E1 (41.2 Hz) is 4.8 dB quieter with Classic or Headphones than with Flat, and
  20 dB quieter — practically gone — with TV speaker.
- The "fizz" of an AY square wave lives above ~5 kHz: Headphones takes 2–6 dB off it, TV speaker
  6–16 dB, Classic and Flat leave it alone.

### The same sound at every sample rate

The emulator's core audio rate can be 44.1, 48, 88.2, 96, 176.4 or 192 kHz. Every profile gives
the same curve at all of them (the numbers above hold within 0.15 dB up to 6 kHz and 0.5 dB up
to 15 kHz; `FilterVoicing_Test.EveryPresetMatchesDesignCurveAtEveryCoreRate` checks every
profile at every rate).

That needs care for the low-pass. The usual digital low-pass (bilinear transform, "RBJ
cookbook") is squeezed towards zero at the Nyquist frequency, and at 44.1 kHz Nyquist is close
to the audio band: a 6 kHz low-pass would cut 15 kHz about 8 dB more at 44.1 kHz than at
192 kHz, so TV speaker would sound duller at 44.1 kHz. The low-pass sections are therefore
*magnitude-matched* (M. Vicanek, "Matched Second Order Digital Filters", 2016): their level
follows the analog curve all the way up. The high-pass and peak sections sit far below Nyquist,
where the usual design is already exact.

## Why voicing exists

The AY DAC only outputs positive voltages, so every volume change a music driver makes (usually
once per 50 Hz frame) shifts the average level. On a real board the output capacitor passes that
shift as a very low "thump". Until `45812176` the emulator removed the average with a
moving-average filter that, as a side effect, also cut the bass below ~60 Hz and added a small
bump at 110-160 Hz. `45812176` replaced it with a model of the real capacitor (a 5 Hz
high-pass, `SoundChip_AY8910::OUTPUT_HIGHPASS_HZ`). That is accurate, but it made the bass much
heavier than people were used to: a 41 Hz bass note (E1) got 4.7 dB louder, and on a tune with
per-frame volume steps the energy below 35 Hz rose by about 15 dB.

Voicing sits on top of the accurate model and lets the listener choose the balance: Flat keeps
the model as it is, Classic puts the old balance back, and the other profiles go further for
particular listening situations.

### Classic

Classic is the best fit to the old moving-average filter's response (20 Hz – 1 kHz,
1/3-octave smoothed: 0.39 dB mean error, 1.3 dB max). Above ~200 Hz the old filter had comb
ripple that Classic deliberately does not copy.

| Frequency | Old filter | Flat | Classic |
|--:|--:|--:|--:|
| 30 Hz | −7.3 dB | −0.1 | −7.2 |
| 41.2 Hz | −4.7 | −0.1 | −4.8 |
| 100 Hz | +1.2 | 0.0 | +1.5 |
| 130 Hz | +2.0 | 0.0 | +1.7 |
| 1 kHz | +0.3 | 0.0 | 0.0 |

(Flat's −0.1 dB here is the 5 Hz output coupling.)

### Headphones

The AY makes square waves: their edges carry strong harmonics up to the top of the audio band,
which on headphones and modern full-range speakers sounds harsh and tires the ear over a long
session. Headphones keeps Classic's bass and adds a gentle, critically damped low-pass at 10 kHz
(Q 0.5: no resonance, the gentlest 2nd-order slope). It is a choice, not the default: the built-in
default is Classic (`FilterVoicing::DEFAULT_PRESET`).

### Warm

For listeners who find Headphones still bright but TV speaker too thin. Both ends move a step:
the bass through a 2nd-order high-pass at 90 Hz (Q 0.6, a soft knee), the treble through a
critically damped low-pass at 8 kHz.

### TV speaker

Most people heard the 128K and its clones through a TV set: a small speaker that rolls off
steeply below ~100–150 Hz and cannot reproduce much above ~6 kHz. TV speaker models that with
2nd-order Butterworth filters at 130 Hz and 6 kHz.

### Small speaker

Many clones had a built-in speaker or drove a cheap amplifier: no real bass below ~250 Hz, a
resonance that pushes the midrange forward, and little above ~4.5 kHz. Small speaker models that
with a 2nd-order high-pass at 250 Hz, a +3 dB peak at 1.5 kHz and a 2nd-order low-pass at
4.5 kHz. It is the most coloured profile and the most different from TV speaker.

## Where it runs

```
AY / SSG generators @218.75 kHz → 5 Hz coupling high-pass → decimator → chip buffer @core rate
   → VoicingStage (voicing)          ← here: HQ and LQ alike
   → punch / room chain              (Sound HQ only)
   → mixer → PCM recording / analyzer taps → output
```

- **HQ and LQ alike.** Voicing is tonal balance, not an HQ effect: toggling *Sound HQ* or leaving
  turbo mode does not change it. It is not reset when HQ comes back.
- **Reset on a time-travel restore.** Seeking in the time-travel history, returning to the live
  state, a machine reset or a snapshot load clears the voicing's filter state and pre-roll history
  (socket chips and card SSG rows alike), so nothing played before the jump leaks into the first
  frame after it. The character chains are reset at the same point, the beeper's included.
- **Not applied to** the beeper, Covox / SoundDrive, TSFM's FM channels, General Sound or
  MoonSound.
- **Sound off / turbo without audio:** nothing is voiced.
- **Cost:** about 10 µs per chip per 20 ms frame at 48 kHz for Classic
  (`BM_VoicingStage_SteadyFrame`); the three-section profiles (Small speaker) cost proportionally
  more, still well under 1% of a frame.

Implementation: `FilterVoicing` (`core/src/common/sound/filters/filtervoicing.h`), a table of
profiles run as up to three biquad sections (high-pass, peak, low-pass) in double precision
(a 64 Hz pole is 0.998 at 192 kHz; float state drifts there).

### Changing the profile while music plays

`VoicingStage` (`core/src/common/sound/filters/voicingstage.h`) owns the switch:

1. A request from any thread (GUI, WebAPI, scripts) is stored atomically. Reading the setting
   right after writing it returns the new value.
2. At the next frame boundary the new filter is warmed up over the last two frames of input
   (kept in a small history), then the frame crossfades from the old to the new profile.
3. Without the warm-up a switch would thump: a fresh high-pass passes the current level as a
   step (about 525 LSB at −6 dBFS in simulation). One frame of warm-up leaves ~1.3 LSB, two
   leave < 0.01.

The history is dropped after a reset, a core-rate change or a gap (sound off, turbo without
audio); a switch right after that starts cold.

## Settings

### `ay_voicing`

One setting per emulator instance. Its value is resolved in this order (highest wins):

1. A runtime change on the instance (GUI, WebAPI, CLI, Lua, Python, MCP)
2. The unreal-qt user's saved preference, applied only to instances the GUI creates itself
3. `[SOUND] AYVoicing=` in the machine's `unreal.ini` — any setting value from the
   [profile table](#profiles), case-insensitive
4. Built-in default: `classic`

The shipped `unreal.ini` files do not set `AYVoicing`, so every machine starts with the
built-in default unless you add the key:

```ini
[SOUND]
AYVoicing=classic      ; classic (default) | headphones | flat | warm | tv | small_speaker
```

Runtime changes are never written back to the ini. An unknown value is rejected everywhere
with the list of accepted values; in an ini file it logs a warning and the default is used.

### Related settings

Three more sound-character settings use the same mechanism (same names, values and errors on
every surface; applied at the next frame boundary):

| Setting | Values | Default | What it does | Needs Sound HQ |
|:--|:--|:--|:--|:--|
| `ay_voicing` | see [Profiles](#profiles) | `classic` | Tonal balance of the AY / SSG output | no |
| `ay_punch` | `on` / `off` | `on` | Transient enhancement for the AY: a slight treble tilt plus a boost on note attacks, tuned gently for square waves. It was tuned by ear on Classic's bass; with Flat, unreal-qt shows a hint | yes |
| `ay_room` | `off`, `15db`, `14db`, `13db`, `12db`, `9db`, `6db`, `3db`, `2db`, `1db` | `9db` | Headphone crossfeed: each ear also gets the other channel, delayed 2 ms (no low-pass: the square wave's harmonics are kept), at the given level below the direct signal (see [Room delay per chip type](#room-delay-per-chip-type)). Reduces fatigue from the hard left/right panning of ABC / ACB stereo. The default `9db` is a clear reduction; on very transient-heavy music `14db`–`15db` avoid the slight comb coloring stronger levels can add to fast attacks; `6db` and below approach mono (`1db` is almost mono); `off` keeps the full stereo separation | yes |
| `beeper_punch` | `on` / `off` | `off` | Attack enhancement for the beeper (digidrums, 1-bit music) | yes |

Bool settings also accept `true` / `false` / `1` / `0` on input; the WebAPI returns JSON
booleans for them. `ay_punch`, `ay_room` and `beeper_punch` have no ini key: they are runtime
settings, persisted only by the GUI preference. Voicing always applies; punch and room apply
only while *Sound HQ* is on. All three apply to the AY / SSG chips in the AY socket (AY,
TurboSound, TurboSound FM) and to the SSG rows of a ZX-MultiSound card in a ZX-bus slot
(`MS SSG 1` / `MS SSG 2`, since 2026-10-07), never to FM, SAA, PCM or MIDI rows.

### Room delay per chip type

The room's delay and low-pass come from the chain's chip type (`AudioCharacterChain::ChipType`),
the level from `ay_room`:

| Chip type | Delay | Low-pass | Used by |
|:--|:--|:--|:--|
| `AY` | 2 ms (88 samples at 44.1 kHz, 96 at 48 kHz) | none | every chain in unreal-ng: the AY / SSG chips of the socket, the TSFM FM rows, the beeper, the ZX-MultiSound SSG rows |
| `Paula` | 3 ms | one-pole, about 10 kHz | not used (kept from the Amiga project the chain was ported from) |

With a 2 ms delay the crossfeed's comb notches sit at 250 Hz, 750 Hz, 1250 Hz and so on.

### Switching and bypass

- **Off means untouched.** A chain with punch and room off, or any chain while *Sound HQ* is
  off, leaves the audio bit-for-bit as the chip produced it and does no per-sample work. The FM
  and (by default) the beeper chains are always in this state.
- **No click on a switch.** Turning punch or room on or off, changing the room level, or
  toggling *Sound HQ* takes effect at the next frame boundary and ramps linearly across that one
  frame (about 20 ms), like a voicing change.
- **No old audio.** An effect that is switched on starts from the current input; once it has
  ramped out, its delay line and envelope are cleared. A gap (sound off, turbo without audio,
  a time-travel restore, a machine reset or snapshot load, a sample-rate change) clears every
  chain, which then starts at the current settings without a ramp.

Cost (A/B 2026-10-07, `dd64db70d` against the change, Apple Silicon, 44.1 kHz, two interleaved runs of ten rounds,
load below 12 at every round start): `BM_AudioCharacterChain_Frame` per 882-sample call - off 1.58-1.65 µs ->
0.05 µs (-97 %), punch 3.47-3.61 -> 3.21-3.37 µs (-7 %), room and punch + room unchanged within 1-2 %. The
whole-machine `BM_TurboSoundFrame_*` (Pentagon TSFM, about 2.3 ms per frame) and `BM_MultiSoundFrame` (the card
alone, no chain on that path) stay within the ±2 % noise in both runs, with effects on and off.

The voicing and punch/room stack in this order: voicing → punch → room. For example, Headphones
with punch on softens the highs first and then sharpens the attacks, so the result is less harsh
than Flat with punch but keeps its definition.

### Examples

```bash
# WebAPI
curl -s "$BASE/emulator/$EMU_ID/settings/ay_voicing" | jq .
# {"name":"ay_voicing","value":"headphones",
#  "allowed":["flat","classic","headphones","warm","tv","small_speaker"],...}
curl -s -X PUT "$BASE/emulator/$EMU_ID/settings/ay_voicing" \
     -H "Content-Type: application/json" -d '{"value":"tv"}'
curl -s -X PUT "$BASE/emulator/$EMU_ID/settings/ay_punch" \
     -H "Content-Type: application/json" -d '{"value":false}'
curl -s -X PUT "$BASE/emulator/$EMU_ID/settings/ay_room" \
     -H "Content-Type: application/json" -d '{"value":"14db"}'
```

```text
# CLI
setting ay_voicing warm
setting ay_room 14db
setting beeper_punch on
```

```lua
-- Lua (Python: emulator.set_sound_character / get_sound_character)
set_sound_character("ay_voicing", "flat")   -- {ok=true, value="flat"}
get_sound_character().ay_room               -- "9db"
```

```text
# MCP (through the WebAPI router)
invoke_api {"method":"PUT","path":"/api/v1/emulator/{id}/settings/ay_voicing","body":{"value":"small_speaker"}}
```

All surfaces use one parser (`SoundCharacterSettings`,
`core/src/emulator/sound/soundcharactersettings.h`), so names, values and errors are identical.
A bad value returns, for example:
`Invalid ay_voicing value 'loud'. Use flat, classic, headphones, warm, tv, small_speaker`.

## Frontends

- **unreal-qt:** Audio Settings → TurboSound → *EQ profile* (the profile list, in the order of
  the profile table), *Punch* and *Room* on the row below (Room offers Off, −15, −12, −9, −6 and
  −3 dB; the other levels are reachable through automation and are shown when set). The Beeper
  group has its own *Punch*. The voicing dropdown stays enabled when Sound HQ is off; punch and
  room take effect only with Sound HQ. With Flat and Punch on, a hint notes that punch was tuned
  on Classic's bass.
  Built-in defaults: Classic, Punch on, Room −9 dB, beeper Punch off.
  Choices are saved in the application QSettings, group `Sound`, keys `ay_voicing`, `ay_punch`,
  `ay_room`, `beeper_punch`, and applied to instances the window creates
  (`EmulatorOrigin::CreatedByGui`). Instances created through automation and then shown in the
  GUI keep their own values. A value saved before the current defaults (for example
  `ay_voicing` = `headphones` or `ay_room` = `off`) is kept: it was an explicit choice.
- **unreal-videowall:** no setting or settings file of its own. Each tile takes the voicing from
  its machine's config (`[SOUND] AYVoicing`, default `classic`); only the active tile is
  audible, so you hear the active emulator's config. Automation can still change the active tile
  at runtime.

## Recording

- PCM recordings and the analyzer tap are taken after the mixer: they contain the voiced sound.
  For an accurate AY spectrum, set `ay_voicing` to `flat` first.
- DSD native mode reads the AY at 218.75 kHz before voicing, so `DSDEncoder` applies the profile
  itself (`SetVoicingPreset`, latched at start: one recording keeps one voicing). The native tap
  is not wired to a production recording path yet; the encoder side is ready and tested.

## Adding or tuning a profile

A profile is one row in the table in `FilterVoicing::profile()` plus one value in
`FilterVoicing::Preset`:

- The row fields are: setting value (stable ID), optional alias, UI label, one-line
  description, visible flag, then the three sections — high-pass (order 0/1/2, frequency, Q for
  order 2), peak (frequency, gain dB, Q; frequency 0 = none), low-pass (frequency, Q ≥ 0.5;
  frequency 0 = none).
- Enum order is menu order, and the table row for a value must sit at that value's index
  (`FilterVoicing_Test.TableRowsFollowEnumOrder`). Setting values, not enum numbers, are what
  ini files, the GUI preference and automation store, so reordering is safe.
- `visible = false` keeps a work-in-progress profile out of every menu and rejects it on every
  input, so an untuned profile can never be selected by accident.
- Tuning a listening profile means changing its numbers only; keep the setting value. The
  design-curve test derives the expected curve from the row, so it follows automatically.
- Update the profile table and the frequency-response table in this document.

## Tests and benchmarks

| Where | What it pins |
|:--|:--|
| `core/tests/common/filtervoicing_test.cpp` | Flat bypass; Classic vs its design curve and vs the real old `FilterDC` (per band, infrasonic energy, peak level); **every profile vs its analog design curve at every core rate (44.1–192 kHz)**; table rows in enum order; parsing (aliases, case); denormal flush |
| `core/tests/common/voicingstage_test.cpp` | switch equals an ideal continuous crossfade (±2 LSB), no click, history rules, cross-thread requests |
| `core/tests/emulator/sound/soundmanager_test.cpp` | configured default live from frame 1, HQ/LQ, not reset on HQ return, gaps, rate change, TSFM FM untouched, punch/room handoff |
| `core/tests/common/audio_character_chain_test.cpp` | chain off / gated off is bit-identical; a switch ramps over one frame with no step above the signal's own; an effect switched on again (after effects off, HQ off, a reset, a rate change) replays nothing |
| `core/tests/emulator/sound/soundcharactersettings_test.cpp` | the shared automation parser: defaults, round trips, accepted values in menu order, errors |
| `core/tests/emulator/config_test.cpp` | `[SOUND] AYVoicing` parsing, the `classic` default |
| `core/tests/emulator/recording/dsd_native_test.cpp` | DSD native mode applies the voicing |
| `core/benchmarks/emulator/sound/filtervoicing_benchmark.cpp` | per-frame cost, switch-frame cost |
| `core/benchmarks/emulator/sound/audio_character_chain_benchmark.cpp` | punch / room chain per frame: off, punch, room, both |

Tests that check mixer arithmetic or the punch/room bypass exactly
(`device_mixer_test`, `soundhq_chain_bypass_test`) pin `flat`.

## Glossary

| Term | Meaning |
|:--|:--|
| dB | Level change; −6 dB is about half the amplitude, −20 dB a tenth |
| High-pass / low-pass | A filter that lets through what is above / below its frequency and turns the rest down |
| 1st / 2nd order | How steeply a filter turns down: 6 / 12 dB per octave beyond its frequency |
| Q | How sharp a filter's corner or peak is; 0.5 = softest corner without overshoot, 0.7071 = Butterworth (flattest), higher = a bump at the corner |
| Peak | A bell-shaped boost (or cut) around one frequency |
| Core rate | The emulator's internal audio sample rate (44.1–192 kHz) |
| Nyquist frequency | Half the sample rate: the highest frequency a sample rate can carry |
