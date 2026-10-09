# libsam2695: technical design

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | Draft for owner review; SAM-0 to SAM-4 built 2026-10-04 (branch `multisound`, as built: §10) |
| **Decisions** | [open-questions.md](open-questions.md) Q2 (our own vendored library, like libopl4; the most elegant, highest-quality solution), Q3 (SF2 banks, default GeneralUser GS), Q4 (the default bank tracked in `data/midi/`) |
| **Users** | ZX-MultiSound ([requirements.md](requirements.md) R-MS-14); later any MIDI-capable device (128K MIDI out on the AY port, ZXM cards, Sprinter MIDI) |
| **Template** | libopl4: `core/src/3rdparty/opl4/` (layout, CMake, README, test framework, state API) |
| **Effort scale** | S < 1 week, M 1-2 weeks, L 2-4 weeks |

## 1. Goal and honest limits

**Goal.** A self-contained C++20 library that takes timestamped MIDI input, either as bytes or as the raw serial
line, and renders it the way the Dream SAM2695 does: its General MIDI voice model, its controller and System
Exclusive implementation, its effects, its polyphony and voice stealing. It plays samples from an SF2 bank, is
deterministic, does not allocate on the audio path, and serializes its full state for TTD.

**Limit.** The SAM2695's CleanWave sample ROM and its DSP microcode are not available, so the *timbre* is the bank's,
not the chip's. Everything the chip does with MIDI data (which notes sound, how long, at what volume and pitch,
which voice is stolen, what a controller or SysEx changes) is modeled to the datasheet. A community bank that
approximates CleanWave can be loaded with `[MIDI] Bank=`.

**Worked example.** WC's MIDI player sends Note On, channel 1, key 60, velocity 100 on the serial line:
1. The line goes low at T0 (start bit). The library's UART samples the line at the chip's bit rate (31 250 baud,
   32 µs per bit) and assembles `#90`, `#3C`, `#64` after 3 × 10 bits = 960 µs.
2. The MIDI parser emits `NoteOn(ch 0, key 60, vel 100)` stamped with the stop bit's time of the last byte.
3. The voice allocator finds the preset for channel 0's program (default 0, Acoustic Grand Piano) in the bank, picks
   the zones that match key 60 / velocity 100, and starts one voice per zone.
4. Each voice reads its sample with interpolation, applies the volume envelope, filter, pan and the reverb / chorus
   sends; the mix goes through the master EQ into the stereo output at the host rate.

## 2. Layout (libopl4 template)

```
core/src/3rdparty/sam2695/
  CMakeLists.txt          static library "sam2695", C++20, -Wall -Wextra -Wpedantic; tests EXCLUDE_FROM_ALL
  README.md               Using it / Time / Banks / Effects / State / Taps / Layout / Tests / Design and history
  include/sam2695/
    sam2695.h             class Synth: the public API (§4)
    sam2695config.h       SynthConfig, constants of the chip (polyphony, bit rate)
    soundbank.h           ISoundBank (read-only bank view), Sf2Bank loader
  src/
    midi/                 uart.{h,cpp} (serial line receiver), parser.{h,cpp} (running status, realtime, SysEx)
    voice/                voice.{h,cpp}, envelope, lfo, filter, interpolator, allocator
    channel/              channel state: program, controllers, RPN / NRPN, pitch bend
    fx/                   reverb, chorus, eq
    bank/                 sf2 parser, preset / zone resolution, generator and modulator model
    render/               mixer, output-rate resampling, render modes
  tests/                  testfw.h (as libopl4), unit tests, bank fixtures generated in code
```

Integration into core as libopl4: `add_subdirectory(3rdparty/sam2695 EXCLUDE_FROM_ALL)`, excluded from core's glob,
`UNREALNG_HAVE_SAM2695=1`. Namespace `sam2695`. Standard library only.

## 3. The model

### 3.1 Input: line and bytes

- **`WriteLine(t, level)`**: the raw serial line (the MultiSound drives it from a YM2203 I/O port pin, see
  [tdd-midi-line.md](tdd-midi-line.md)). The UART detects the start bit on the falling edge, samples each bit at its
  middle (bit time 32 µs; the sample point and the tolerance follow the datasheet), checks the stop bit (a framing
  error drops the byte, as a hardware UART does, and is counted in the report), and emits a byte at the stop bit's
  middle.
- **`WriteByte(t, byte)`**: for devices that hand over whole bytes (a future 16550-based MIDI interface).
- **Parser:** running status, realtime bytes inside messages (`#F8-#FF` never break a message), SysEx assembly with a
  bounded buffer (overflow drops the message, counted), Active Sensing timeout if the datasheet says the chip honors
  it, All Sound Off / All Notes Off / Reset All Controllers, Omni / Mono / Poly modes as the chip implements them.

### 3.2 The chip's MIDI implementation

Taken from the datasheet (`testdata/midi/docs/SAM2695.pdf`; SAM-0 extracts the chart into `README.md` with page
references). Notable chip behaviors to honor: about 50 ms after reset before MIDI input is processed; the MT-32
sound variation bank (#127); 2-layer instruments. The chart covers:
program change per channel, the GM drum channel 10, bank select (the chip's variation / MT-32-compatible banks if
present), pitch bend with RPN 0 range, RPN 1 / 2 fine / coarse tuning, modulation, volume, pan, expression, sustain,
sostenuto, soft pedal, reverb and chorus send levels, NRPN for the chip's own parameters (vibrato, filter, envelope
offsets, drum instrument parameters), and SysEx: GM System On, master volume, master tuning, the chip's effect,
equalizer and mode settings.

Each chart row becomes one row in the library's conformance table (README) with its test, so nothing in the chart is
silently ignored. A row the library cannot honor is listed with the reason.

### 3.3 Voices

| Element | Design |
|---|---|
| Polyphony | the chip's own accounting (datasheet §5, NRPN `375Fh`): 64 voices without effects; reverb costs 13, chorus 3, spatial 1, mike 1, mike echo 3, 2-band EQ 4, 4-band EQ 8, plus 1 for internal mixing in some configurations; power-up value `3Bh` (the p.35 table's "Default": reverb, chorus, spatial, 4-band EQ; `45h` = "reset all" restores it) gives 38 voices. A 2-layer instrument (datasheet §8-1) takes two voices |
| Allocation | per the datasheet's stealing order where documented; otherwise: release-phase voices first, then the quietest, never the drum voice of a still-held note before melodic voices; exclusive classes (SF2 generator 57: hi-hat open / closed) |
| Sample playback | 32-bit fixed-point phase with a 32-bit fraction (no float drift over long notes), loop modes from SF2, `sm24` 24-bit samples supported |
| Interpolation | `Linear`, `Cubic` (4-point Hermite) and `Sinc` (8-tap windowed sinc, default for quality); a render option, not part of the state |
| Envelopes | SF2 volume and modulation DAHDSR in the SF2 timecent / centibel domain, exact to SF2 2.04 |
| LFOs | modulation and vibrato LFO (triangle), SF2 delays and frequencies |
| Filter | resonant 2-pole low-pass, SF2 cutoff / Q, coefficient update per block with smoothing |
| Modulators | the SF2 default modulators plus bank-defined ones |

### 3.4 Effects

From the datasheet: reverb (8 programs, default 4; volume, time, feedback for the delay programs 6-7), chorus (8
programs: chorus 1-4, feedback chorus, flanger, short delay, feedback delay; default 2; volume, delay, feedback, rate,
depth), the spatial effect, a 4-band stereo equalizer (bands and corner frequencies per NRPN `3700h-370Bh`, defaults
`60h / 40h / 40h / 60h`, corners `0Ch / 1Bh / 72h / 40h`), "post effects" (spatial + EQ) optionally applied to the
reverb / chorus returns (NRPN `371Ah`), GS-style send levels (CC 91 / 93) and per-drum-note sends (NRPN `1Drr` /
`1Err`). The microphone input is unused on the MultiSound (MICIN grounded) and not modeled. Reverb:
a Dattorro-style plate with the chip's room types mapped onto its parameters. Chorus: modulated delay lines. EQ:
biquad shelves and peaks at the chip's band frequencies. Effects run at the internal rate and are part of the state
(delay lines included) so a TTD restore continues a reverb tail exactly.

### 3.5 Rates and determinism

- **Internal rate.** The engine runs at a fixed internal rate, the chip's own 37 500 Hz (AN_2695: "nominal
  sampling rate 37.5 kHz"), then resamples to the host rate. A host-rate change never changes the synthesis.
- **Time.** `hostTickRate` is the axis of every call, exactly as `Opl4Config::hostTickRate`. Events are applied at
  their sample position inside the block (sample-accurate note starts).
- **Determinism.** The same input on the same build gives bit-identical output. The state blob is platform
  independent (fixed-width integers, explicit endianness); float state is stored as IEEE bits.
- **No allocation** after `Configure` and `LoadBank`; the voice pool, delay lines and buffers are sized there.

## 4. Public API (sketch)

```cpp
namespace sam2695 {

struct SynthConfig
{
    uint32_t hostTickRate = 3500000;
    uint32_t outputRate = 44100;
    uint32_t polyphony = 0;                  // 0 = the chip's default
    Interpolation interpolation = Interpolation::Sinc;
    bool effects = true;
};

class Synth
{
public:
    bool Configure(const SynthConfig& cfg);
    bool LoadBank(std::shared_ptr<const ISoundBank> bank);  // outside the audio path; bank identity = SHA-256
    void Reset(uint64_t t);                                 // power-on state

    void WriteLine(uint64_t t, bool level);                 // serial line, idle = high
    void WriteByte(uint64_t t, uint8_t byte);               // parsed path

    void Run(uint64_t t);                                   // advance to t (applies events)
    void Render(float* stereo, size_t frames);              // interleaved L/R at outputRate
    void SetOutputRate(uint32_t rate);

    size_t StateSize() const;                               // constant after Configure + LoadBank
    void SaveState(uint8_t* out) const;                     // includes the bank SHA-256
    bool LoadState(const uint8_t* in, size_t size);         // refuses another bank

    void SetChannelMute(int channel, bool mute);            // taps for the UI / tests
    void Describe(SynthReport& out) const;                  // channels, programs, voices, UART counters
};

}
```

## 5. The SF2 loader

- Full RIFF `sfbk` parsing (INFO, `sdta` `smpl` / `sm24`, `pdta` `phdr pbag pmod pgen inst ibag imod igen shdr`),
  validated against the spec; a malformed bank is refused with a precise reason, never half-loaded.
- Samples converted once to the internal format at load; the bank is shared read-only between instances.
- SF3 (Ogg-compressed) is not supported in v1 (owner Q4 chose the uncompressed default); the loader says so clearly.
- DLS is a later loader behind the same `ISoundBank`.

## 6. Verification

There is no SAM2695 to compare against bit for bit, so verification is layered:

| Layer | Reference | Method |
|---|---|---|
| UART and parser | the MIDI 1.0 specification, the datasheet timing | unit tests with exact line timelines |
| MIDI implementation | the datasheet chart | one test per chart row (conformance table) |
| SF2 semantics | **FluidSynth** (installed on the dev machine) as the reference SF2 renderer | `tools/verification/sam2695/`: render the same MIDI file and bank with both, compare note on/off times, pitch, envelope levels per block within tolerances; differences explained in the README |
| Character | recordings of a real Dreamblaster S2 / SAM2695 board (community recordings, or a capture if the owner has hardware) | informative spectral comparison only |
| Banks | every bank in `testdata/midi/` (git-ignored) | load and render a GM test file: no crash, no refusal of a valid bank, no denormal / NaN |

Golden digests use a small synthetic bank generated in code (a sine and a noise sample with known
zones), so the unit tests need no external file (as built: fingerprints in the library suite, §10.4). Integration tests that use GeneralUser GS pin it by SHA-256 from
`data/midi/` (Q4).

## 7. Tests (library suite `sam2695tests`, home-grown framework as libopl4)

| Test | Checks |
|---|---|
| `Uart.ByteTiming` | a byte written as a line timeline arrives at the stop-bit middle |
| `Uart.FramingError` | a missing stop bit drops the byte and counts it |
| `Uart.BaudTolerance` | ±2 % bit timing still decodes; ±6 % fails as the datasheet tolerance says |
| `Parser.RunningStatus`, `Parser.RealtimeInsideMessage`, `Parser.SysExOverflow` | MIDI 1.0 parsing |
| `Chart.<row>` | one per MIDI implementation chart row |
| `Voice.EnvelopeTimecents` | DAHDSR stages against SF2 formulas |
| `Voice.LoopModes` | no loop / continuous / loop until release |
| `Voice.ExclusiveClass` | closed hi-hat cuts open hi-hat |
| `Allocator.StealingOrder` | polyphony limit + stealing order |
| `Fx.ReverbTailAcrossState` | save mid-tail, load, identical continuation |
| `Render.OutputRateIndependent` | synthesis state identical across host rates |
| `State.RefusesOtherBank` | loading a state saved with another bank fails cleanly |
| `Golden.*` | digests of synthetic-bank renders |

## 8. Phases

| Phase | Content | Size |
|---|---|---|
| SAM-0 | Datasheet study: MIDI chart, effects, polyphony, timing into `README.md`; FluidSynth harness; bank corpus check | S |
| SAM-1 | Library skeleton, UART, parser, SF2 loader, single-voice playback, state API | M |
| SAM-2 | Full voice model (envelopes, LFOs, filter, modulators, allocation, exclusive classes) | L |
| SAM-3 | MIDI implementation chart complete (RPN / NRPN / SysEx) | M |
| SAM-4 | Effects (reverb, chorus, EQ) and render modes | M |
| SAM-5 | Verification against FluidSynth, bank corpus, golden digests | M |
| SAM-6 (later) | Host MIDI output (CoreMIDI / WinMM / ALSA) as an alternative sink; DLS loader | M |
| SAM-7 (research) | **Dream-native banks** (`.DXB` / `.B16`, header `Bank`): Dream's own GM banks GMBK5X128 / GMBK5X64 and the Serdaco DreamBlaster banks are in `testdata/midi/` with Dream's bank maps and the MakeRom guide. If the format can be read (reverse engineering from the files and the guides), an `ISoundBank` for it brings Dream's own samples and instrument parameters - the closest available approach to the chip's sound. The SAM2695's CleanWave ROM itself is not among them. **License:** these banks are
licensed for use on DreamBlaster cards only, so they are never shipped or committed; the loader reads a bank the user
supplies (like a ROM), and the research only studies the file format for interoperability | M-L |

## 9. Sources

- [Dream SAM2695 product page](https://docs.dream.fr/items.php?item=4)
- [Dreamblaster S2 review (VOGONS)](https://www.vogons.org/viewtopic.php?t=56166)
- Datasheets and application notes downloaded to `testdata/midi/docs/` (index in `testdata/midi/README.md`)
- [SoundFont 2.04 specification](https://www.synthfont.com/sfspec24.pdf)
- [FluidSynth](https://www.fluidsynth.org/), the reference SF2 renderer of the comparison harness

## 10. As built (SAM-0 to SAM-4, 2026-10-04)

Branch `multisound`. The library is `core/src/3rdparty/sam2695/`; its [README](../../../core/src/3rdparty/sam2695/README.md)
holds the datasheet extraction (**the MIDI implementation conformance table**: every chart row with its page,
status and test; effects and their parameters; polyphony accounting; UART timing and tolerance) and the voice
model. Core links it (`add_subdirectory(3rdparty/sam2695 EXCLUDE_FROM_ALL)`, excluded from core's source glob,
`UNREALNG_HAVE_SAM2695=1`); nothing in the emulator uses it yet.

### 10.1 Layout as built (differences to §2)

- `src/synthcore.{h,cpp}` holds the chip (MIDI implementation, channels, voice pool, allocation, polyphony
  accounting); allocation is not a separate file. `src/sam2695.cpp` is the `Synth` façade (time, input queue,
  state, render layer).
- `src/midi/uart.h` and `parser.h` are header-only; `src/common/` adds 128-bit time arithmetic (`wideint.h`), the
  state archive (`statearchive.h`: one field list per struct for size / save / load) and unit conversions.
- `src/fx/` does not exist yet (SAM-4). `tools/` holds `sam2695render` (SMF + bank to WAV, `--info`, `--check`,
  `--line` through the UART) and the SMF reader; target `sam2695render`, `EXCLUDE_FROM_ALL` like `sam2695tests`.
- The public API is §4 as sketched, plus `Render()` returning the frames written, `DiscardPendingAudio()`,
  `SetInterpolation()`, `InternalPosition()`, `HostTimeOfSample()`, and `Sf2Bank::LoadFile / LoadMemory` with
  a `BankError` + reason.

### 10.2 Decisions taken while building (each in the README with its reason)

| Topic | As built |
|---|---|
| Internal rate | 37 500 Hz (AN_2695); control blocks of 32 samples on an absolute grid, so output never depends on `Run()` slicing; up to one block of latency |
| Power-up effects word | `3Bh` (38 voices); `45h` = reset all (restores it, 50 ms busy). The p.35 note says "Spatial Effect OFF" for 45h while its row shows spatial on; both give 38 voices |
| Mixing voice | one extra voice whenever an effect is on, except with the mike echo on and except reverb + chorus alone: reproduces all 23 table rows |
| Stealing order | not in the datasheet: release, then pedal-held, then held (melodic before rhythm); quietest, then oldest; never the note being started. Stolen / exclusive-class voices fade 64 samples in 16 spare slots outside the count |
| UART | standard 16x receiver, majority of ticks 7-8-9, byte at the stop-bit middle, +-4.6 % tolerance (the datasheet is silent) |
| Reset window | MIDI dropped for 50 ms after `Reset()` / 45h / power-on (`resetDelay`, on by default) |
| SF2 conventions | E-mu / FluidSynth practice where the banks depend on it: static initialAttenuation x 0.4, concave curve -20/96 log10((1-x)^2), convex mod-envelope attack, the delay holds the whole voice. Spec kept where FluidSynth differs: 100 dB envelope scale, unity DC gain of a non-resonant filter, per-sample envelopes |
| Interpolation | Sinc = 8-tap full-band Kaiser (beta 6) windowed sinc; a narrower cutoff only added droop (measured error table in `interpolator.cpp`) |
| Bank loading | `LoadFile` streams (hash pass, lists in memory, samples read in place: a 4 GB bank needs its own size once); SF3 refused with a clear reason; odd-sized chunks without a pad byte are refused and named (FluidSynth refuses them too) |
| Denormals | filter state flushed below -300 dB per block, the mix bus below -600 dB |
| Golden tests | fingerprints (RMS per 1024-sample block, 0.05 dB) plus a bit-identical repeat check, not float digests: `libm` differs between platforms; bit identity holds on one build |

### 10.3 Verification results

- **Library suite** `sam2695tests`: 64 tests, 389 checks, all pass (about 50 ms). §7 rows covered: `Uart.ByteTiming`,
  `Uart.FramingError`, `Uart.BaudTolerance`, `Parser.*`, `Chart.*` (the rows built so far), `Voice.EnvelopeTimecents`,
  `Voice.LoopModes`, `Voice.ExclusiveClass`, `Allocator.StealingOrder`, `Render.OutputRateIndependent`,
  `State.RefusesOtherBank`, `Golden.*` (`Fx.ReverbTailAcrossState` came with SAM-4, §10.8).
- **FluidSynth 2.6.1 comparison** (`tools/verification/sam2695/compare.py`): 40 checks, 0 failed; the four expected
  deviations D1-D4 are explained in its README (envelope dB scale, FluidSynth's filter gain, its 64-sample block
  quantization, its truncated curve tables). Velocity curve, attenuation, pan, filter level within 0.1 dB; pitch
  exact; envelope stage times within 3 ms; tremolo, vibrato, loop-until-release, key-scaled decay and the
  modulation envelope agree.
- **Bank corpus** (`bankcorpus.py`, 255 SF2 / SF3 files, 55.6 GB under `testdata/midi/`): 249 load and play the
  GM test file; 6 are refused, each correctly: 3 SF3 (Ogg samples), 1 renamed `sfpk` (SFPack-compressed) file,
  1 with an odd-sized INFO list without its pad byte (FluidSynth also rejects it), 1 without presets (an empty
  shell of the ESI-32 collection). After the denormal flush: no NaN, no denormal in any render. 11 banks peak
  above full scale at the default output gain 0.25 (loud banks played with 15 channels of chords; the chip's
  soft clipping is SAM-4). 132 banks load with warnings (median 1: mostly loops outside their samples, clamped).
  Largest bank (4.24 GB) loads in 22 s.
- Full emulator build (`tools/build/build.sh`): zero compiler warnings; `tools/build/test.sh`: 0 failed.

### 10.4 Open points for the owner

- The power-up effects word: 3Bh (as built, the table's "Default") or 45h literally? Both mean 38 voices; only
  the stored value and `Describe()` differ.
- The stealing order is ours (the datasheet has none); a capture from a real SAM2695 / Dreamblaster S2 under
  voice pressure would settle it (SAM-5).
- Parallel (MPU) mode is not modeled: the MultiSound drives the serial line. A card that uses the parallel port
  would need it (status register, 3Fh / FFh / BEh controls, IRQ).

### 10.5 SAM-3 and SAM-4 as built

The library README holds the detail: "MIDI implementation: conformance table" (every chart row with its status
and test), "GS parts and SysEx", "Pedals and portamento", "Effects and output".

- **Parts.** The 16 channels became the 16 GS parts: each has a receive channel (40 1p 02, 10h = off), a
  rhythm flag (40 1p 15), voice reserve, velocity sense, CC1 / CC2 numbers, scale tuning, the 5 x 7 controller
  destination matrix (40 2p xx) and the GS part NRPN offsets (01xxh). A channel message reaches every part
  receiving that channel. The chip NRPNs 37xxh are tracked per MIDI channel and applied once, outside the
  parts. Two drum edit tables (channel 10, the rest) hold NRPN 18rr-1Err.
- **SysEx.** GM System On, GM master volume, Roland GS data set (any length: master tune / volume / key shift /
  pan, GS reset, voice reserve, reverb and chorus, part and controller blocks; the checksum is not verified,
  p.29 "don't care"), the Dream port write (codec ports 12h / 14h), device ID filtering (3757h).
- **Voice.** New per-voice state: the MIDI key beside the sounding key (key shift), sostenuto and soft flags,
  portamento offset and rate, LFO1 to cutoff / amplitude and its rate offset, the reverb / chorus send and its
  ramp. The part's GS controls are evaluated with the modulators (re-evaluated when a source of the part
  changes); the vibrato LFO rate follows live.
- **Effects** (`src/fx/`): reverb (Dattorro plate tank, six programs, plus the two delay programs on the same
  pool), chorus (eight programs on one modulated line), spatial effect, 4-band / 2-band equalizer, post-effects
  routing, GM volume / pan, master volume, soft / hard clipping, codec gain and mute. The chip core renders the
  voices into the GM bus and two send buses per control block and runs the effects at the block's end.
- **Render modes.** `SynthConfig::effects` keeps its name: `false` is now the dry mode (voices and gains, no
  effects, no clipping). The voice-model tests and the FluidSynth comparison of the voice model use it; the
  MIDI line tests (ML) set it too.
- **State** layout version 2: parts, chip-level MIDI state, drum tables, effect settings and every delay line
  and filter memory (292 KB at the default configuration, 130 KB of it delay lines).
- **Tests.** `tests/charttests.cpp` (SAM-3 rows), `tests/fxtests.cpp` (SAM-4), `Golden.EffectsChain`; the test
  bank gained a looped noise preset (0:3) for measuring the reverb; `TestSynth::RunTo` now runs in steps inside
  the 1 s stream buffer (a single `Run()` over more than 1 s had silently dropped the oldest audio).

### 10.6 Decisions and datasheet ambiguities (SAM-3 / SAM-4)

| Topic | Datasheet | As built |
|---|---|---|
| Aftertouch and modulation wheel defaults | CAF "effect set using SysEx 40 2n 20-26" with all destinations 0 at power-up; mod LFO1 pitch depth 0Ah | the GS matrix replaces the SF2 default modulators 8.4.3 / 8.4.4: aftertouch does nothing by default, the wheel gives 47 cents (SF2: 50) |
| GM / GS master volume vs 3707h / 3722h | 3723h "same as" GS master pan; 3722h "General MIDI volume"; nothing on the SysEx volumes | GM and GS master volume SysEx = 3722h (the GM bus), 3707h = the output master volume; both linear |
| What GM / GS reset resets | not stated | parts, GS system parameters, drum edits, reverb / chorus programs, sound off; not the chip's 37xxh settings nor the master volume (DreamBlaster X16 note); no busy window |
| Device ID | 3757h 0-1Fh, 20h = all; the chart's Roland messages use device 00h | 20h accepts any; else that ID, plus 7Fh on universal messages |
| Roland checksum | "xx means don't care" | not verified |
| Channel of the 37xxh NRPNs | not stated (examples on channel 1; the X16 requires channel 1) | any channel, applied once |
| Drum edit tables | per channel 10 implied | two tables (channel 10, the rest), as the X16 specification documents |
| Drum level / pan curves | 0-7Fh; pan "40h = middle" | level 7Fh = the kit's, 40 log10(v / 127) dB; pan absolute, replacing the kit's |
| GS part NRPN scales (01xxh) | "40h -> no modif" only | 20 cents (vibrato rate), 1 cent (depth), 60 cents (cutoff), 3 cB (resonance), 75 timecents (delay, envelope) per step; lengthening from at least 2^-5 s |
| Soft pedal | the row only | -3 dB and one octave darker, latched at note on |
| Portamento | CC 5 / CC 65 rows only | constant-rate glide, 5 ms x 2^(v / 14) per octave, from the part's last key |
| Omni modes | only CC 126 / 127 in the chart | CC 124 / 125 act as All Notes Off; the chip stays Omni Off |
| Reverb character vs type | type 0-7, "character, default 04h" | the type is a macro (character + level / time / feedback defaults); the character selects the algorithm |
| Reverb / chorus level units | defaults given in parallel units (0-FFh: 90h, C0h, FFh) | SysEx units = parallel / 2 (48h, 60h, 7Fh) |
| REV_TIME, chorus delay / rate / depth / feedback curves | 0-7Fh "shortest to longest" | our curves (library README): decay x 2^((t - 127) / 32), echo 2.8 ms x (t + 1), chorus delay 40 ms x 2^((d - 127) / 24), rate 0.05-10 Hz, depth 0.1 ms per step, feedback x 0.85 |
| EQ band Q, gain step | levels 00h / 20h / 40h / 60h / 7Fh = -12 / -6 / 0 / +6 / +12 dB | 12/64 dB per step (7Fh = +11.8 dB); peaking Q 0.707 (the X16's default) |
| EQ high corner | 0-18.75 kHz, the Nyquist frequency | held at 0.45 fs |
| Spatial delay range | "shortest to longest" | 0.25 ms + 0.15 ms per step (19.3 ms at 7Fh) |
| Soft clipping curve | "soft clip" | linear to 0.75, then a rational knee to full scale; hard clipping at +-1 |
| Codec gain position | after the DAC (block diagram p.39) | after the clipper; at +6 dB the output may exceed 1.0 |
| Effect off in 375Fh | "Reverb ON / OFF", voice cost | the effect stops processing and loses its state; switched on again it starts empty |
| Reverb send scale | CC 91 / 93 "send level" | the SF2 default modulators (200 per mille at 127), as E-mu / FluidSynth; x 3715h / 3716h / 40h, x the drum note depth |

### 10.7 Verification results (SAM-3 / SAM-4)

- **Library suite** `sam2695tests`: 108 tests, 654 checks, all pass, each under 20 ms (SAM-2: 64).
  New: 26 `Chart.*` rows (`charttests.cpp`), 16 `Fx.*` and `Render.Modes` (`fxtests.cpp`), `Golden.EffectsChain`;
  `Chart.ModulationWheelAndAftertouch` updated to the chip's defaults. The SAM-2 golden fingerprints are
  unchanged (the voice model renders as before in the dry mode).
- **Reverb calibration** (noise burst, Schroeder T20): measured RT60 within 4-9 % of each program's design
  (README table); `Fx.ReverbPrograms` holds them to 25 %.
- **FluidSynth 2.6.1 comparison:** 47 checks, 0 failed: the 40 voice checks unchanged (now in the dry mode) and
  7 effects checks - both synthesizers' reverb and chorus sends are linear in CC 91 / 93 (5.91 / 5.95 dB for
  127 vs 64, FluidSynth 5.94 / 5.99, 20 log(127/64) = 5.95), nothing at 0. Decay times and wet levels are
  reported as deviation D5 (different algorithms): reverb RT60 2.37 s vs 2.26 s, tail -23.8 vs -21.9 dB,
  chorus wet -16.9 vs -7.8 dB (FluidSynth's chorus level 2).
- **Bank corpus** (255 files, 55.6 GB, the chip's full output path, 406 s): 249 load and play, the same 6
  refused for the same reasons as in SAM-2; no NaN, no denormal; **no bank above full scale** (SAM-2: 11 hard
  over). The soft clipper engages on 42 banks (peak above the 0.75 knee); the loudest peaks at 0.9999
  (eapci8m Hedsound), the next at 0.965.
- **Cost:** GeneralUser GS through the GM test file, 25 s of audio: 474 ms with the effects, 426 ms dry.
  Idle effects (2026-10-08): an effect whose state is all +0.0 skips the blocks without input, bit-identically
  (library README "Idle effects", `Fx.IdleEffects*`, `Fx.IdleEqualizerTakesTheSpatialTail`; suite now 114 tests).
  Tail floor (2026-10-08): a block without input that leaves the reverb's lines and filters, the chorus line or
  the equalizer's memory below 2^-24 of full scale (-144 dBFS, half the LSB of a 24-bit word, the finest word
  length documented in the SAM2695's family) sets them to +0.0, so every tail ends (hall2 5.8 s after the
  note-off, pan delay 8.0 s) and the effects go idle after playback; part of the model with the skip on or off,
  TTD-exact (library README "Tail floor", `Fx.TailsEndAtTheFloor`, `Fx.TailOutAcrossState`; suite 116 tests).
  Output change at most 1.0e-7 (0.003 of a 16-bit LSB); the card's int16 MIDI row is unchanged.
- Full emulator build (`tools/build/build.sh`): zero warnings; `tools/build/test.sh`: 6918 tests in 20 shards, 6845 passed, 73 skipped, 0 failed.

### 10.8 Left for SAM-5

- **Real-chip check done (owner, 2026-10-05):** the synthesizer compared by ear with recordings of real SAM2695
  boards (YouTube): "very similar, good enough" - accepted. No official CleanWave bank exists to compare the bank
  itself against; the items below stay as refinements, not blockers.
- Recordings of a Dreamblaster S2 / SAM2695 board for an informative comparison - now of the effects too: the
  curves this phase had to choose (REV_TIME, chorus parameters, the GS part NRPN scales, soft pedal, portamento,
  the soft-clip knee, the EQ Q) are the first things a capture would settle.
- `data/midi/generaluser-gs.sf2` tracked and pinned by SHA-256 (`9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe`
  for GeneralUser GS 2.0.3 BETA as in `testdata/midi/`).
- The corpus check and the FluidSynth comparison as one repeatable report (both scripts exist; SAM-5 makes the
  report a single command with its history).
- Open owner points: §10.4 (power-up word 3Bh vs 45h, the stealing order, the parallel MPU mode) and the
  chosen curves above.
