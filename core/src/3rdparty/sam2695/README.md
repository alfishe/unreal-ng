# libsam2695 — Dream SAM2695 General MIDI synthesizer

In-house model of the Dream SAM2695 as fitted to the ZX-MultiSound card (and, later, any MIDI-capable device):
its serial MIDI input, its MIDI implementation, its polyphony accounting and voice model, playing samples
from a SoundFont 2 bank. The chip's CleanWave sample ROM is not available, so the *timbre* is the bank's;
everything the chip does with MIDI data (which notes sound, how long, how loud, at what pitch, which voice is
stolen) follows the datasheet. Standard library only; no allocation on the audio path after `Configure()` and
`LoadBank()`; deterministic; the chip state serializes to a platform-independent blob.

Status: phases SAM-0 to SAM-2 of
[tdd-libsam2695.md](../../../../docs/inprogress/2026-10-03-zx-multisound/tdd-libsam2695.md) (datasheet study,
skeleton with UART / parser / SF2 loader / state, full voice model). The rest of the MIDI implementation chart
(SAM-3) and the effects (SAM-4) are open; the conformance table below says which row is where.

## Using it

```cpp
#include "sam2695/sam2695.h"

sam2695::SynthConfig cfg;
cfg.hostTickRate = 3500000;          // the time axis of every call below
cfg.outputRate = 48000;              // 8000 .. 192000

sam2695::Sf2Bank::LoadResult bank = sam2695::Sf2Bank::LoadFile("GeneralUser-GS.sf2");
if (!bank.bank)
    report(bank.reason);             // e.g. "pdta/shdr is missing", "SF3 bank (ifil 3.x ...)"

sam2695::Synth chip;
chip.Configure(cfg);
chip.LoadBank(bank.bank);            // shared, read-only: any number of instances may use one bank
chip.Reset(0);                       // power-on; the chip ignores MIDI for the next 50 ms

chip.WriteLine(t, level);            // the serial MIDI IN line (idle high), or
chip.WriteByte(t, byte);             // whole bytes from a host-side UART or a file player
chip.Run(frameEnd);                  // synthesize up to a time
size_t n = chip.Render(stereo, maxFrames); // interleaved float L/R at outputRate
```

`sam2695render` (in `tools/`, an `EXCLUDE_FROM_ALL` target) renders a Standard MIDI File to a WAV, prints a
bank's presets (`--info`) or checks a bank (`--check`); `--line` sends the bytes through the serial line model.

## Time

- **Host axis.** Every input carries a timestamp in host ticks (`hostTickRate`), as in libopl4. Products of
  times and rates are formed in 128 bits (`src/common/wideint.h`), so no axis drifts.
- **Internal grid.** The chip synthesizes at **37 500 Hz**, its own rate (AN_2695: "nominal sampling rate
  37.5 kHz"; the equalizer's top corner, 18.75 kHz, is its Nyquist). A MIDI byte takes effect at the first
  internal sample at or after its time: notes start and release on their own sample.
- **Control blocks.** Modulators, pitch, filter coefficients and the static gain update on a fixed grid of
  32 samples (0.85 ms), absolute from time 0. `Run(t)` synthesizes only whole blocks before `t`, so the output
  never depends on how the host slices its `Run()` calls (`Render.RunSlicingIndependent`); the cost is up to
  one block of latency.
- **Render layer.** `Run()` fills a buffer of internal-rate frames (`streamFrames`, 1 s); `Render()` resamples
  it to the output rate (Kaiser-windowed sinc, 32 taps upward, wider downward, exact rational step; equal rates
  copy). A host-rate change never touches the chip (`Render.OutputRateIndependent`). A turbo host may call
  `Run()` and never `Render()`; frames beyond the buffer are dropped and counted.

## Banks

- **SoundFont 2.04** (`Sf2Bank`): full RIFF `sfbk` parse (INFO `ifil` / `INAM`, `sdta` `smpl` + `sm24`, `pdta`
  `phdr pbag pmod pgen inst ibag imod igen shdr`). A structural fault refuses the bank with a precise reason
  (`BankError` + text naming the chunk, record or index); a bank is never half-loaded. Irregularities the
  specification itself says to tolerate (a misplaced key range, a zone without its terminal generator, an
  invalid or linked modulator, a duplicate preset) and slips with one obvious repair (a loop outside its
  sample, sample rate 0, a padded `sm24`) are fixed and listed in `BankModel::warnings`.
- **Sample data** stays in the file's format: 16-bit frames plus the `sm24` low bytes when present (24-bit
  playback). `LoadFile` streams: one pass hashes the file, the lists are read into memory and the samples go
  straight into the model, so a 4 GB bank needs its own size once.
- **Identity.** A bank is named by the SHA-256 of its file; the state blob carries it.
- **SF3** (Ogg Vorbis samples) is refused with a clear reason in v1; DLS is a later loader behind the same
  `ISoundBank` interface.
- **Preset lookup.** A melodic part plays `bank select MSB : program` and falls back to bank 0 (a GS
  variation the bank lacks plays the capital tone); a rhythm part (channel 10 at power-up) plays bank 128 and
  falls back to kit 0; bank select does nothing on a rhythm part (datasheet p.26). A melodic program the bank
  does not have is silent.
- The default bank is GeneralUser GS (owner decision Q3/Q4: `data/midi/generaluser-gs.sf2`, not yet tracked).

## The voice model (SF2 2.04)

One voice per matching instrument zone (so the chip's 2-layer instruments, datasheet §8-1, take two voices
exactly when the bank layers them).

| Element | Model |
|---|---|
| Generators | instrument global + local (absolute, local wins) plus preset global + local (relative, only the generators allowed at preset level); final values clamped to the 2.04 ranges |
| Modulators | the ten 2.04 default modulators, superseded by identical instrument modulators (global, then local), plus the preset level's own list added on top; re-evaluated at the next control block whenever a source of the channel changes; linked modulators are ignored (warned) |
| Sample playback | 32.32 fixed-point phase (`Voice.PhaseFixedPoint`); address offset generators (fine + 32768 x coarse); loop modes 0 / 1 / 3 (`Voice.LoopModes`) |
| Interpolation | Linear, Cubic (Catmull-Rom), **Sinc** (8 taps, Kaiser beta 6, 256 phases interpolated; default) - a render option (`Voice.Interpolation`) |
| Volume envelope | delay / attack / hold / decay / sustain / release in whole samples from timecents, per sample exactly: attack linear in amplitude, decay and release linear in dB at 100 dB per time constant, sustain in centibels, keynum-to-hold / decay scaling; the voice ends at -100 dB (`Voice.EnvelopeTimecents`) |
| Modulation envelope | same timing, value 0..1, convex attack, to pitch and filter cutoff (`Voice.ModEnvelope`) |
| LFOs | modulation LFO (pitch, cutoff, volume) and vibrato LFO (pitch): triangle from 0 rising, delay and frequency from the generators, 32-bit phase (`Voice.Lfo`) |
| Filter | resonant 2-pole low-pass (RBJ biquad, TDF-II), cutoff in absolute cents, Q as the SF2 peak height (q = 10^((Q/10 - 3.01)/20)), DC gain 1 / sqrt(q) above Butterworth, coefficients interpolated across each block, bypassed at >= 0.45 fs with Q = 0 (`Voice.FilterResponse`) |
| Gain | static attenuation = 0.4 x the bank's initialAttenuation + the modulators' centibels, minus mod-LFO tremolo; pan by the constant-power sin / cos law (`Voice.StaticAttenuation`, `Voice.Pan`, `Voice.VelocityCurve`) |
| Exclusive classes | a new note cuts every voice of the same class on its channel with a 64-sample fade (`Voice.ExclusiveClass`) |
| Re-struck key, mono | a key struck again releases its previous note; mono mode (CC 126) releases every note of the channel |

### Deviations from the letter of SF2 2.04, and why

The model follows the E-mu hardware conventions the banks are voiced for (GeneralUser GS lists FluidSynth and
BASSMIDI as its reference players); the FluidSynth comparison (`tools/verification/sam2695/`) confirms each one.

- The bank's **static initialAttenuation counts 0.4 x** (E-mu, FluidSynth, BASSMIDI); modulator contributions
  count fully.
- The **concave / convex curves** are -20/96 x log10((1 - x)^2) and its mirror (40 dB per decade of velocity),
  continuous rather than FluidSynth's truncated 128-step tables.
- The **modulation envelope attack is convex**; the volume envelope attack is linear in amplitude.
- The **envelope delay holds the whole voice**: the sample starts with the attack.
- Where FluidSynth itself differs (96 dB envelope scale, +1.5 dB filter gain at Q = 0, 64-sample block
  quantization) the library keeps the specification and the comparison lists the difference as expected.

## Voice allocation and polyphony

The chip's own accounting (datasheet §5, NRPN 375Fh, p.34-35): 64 voices without effects; each effect costs
voices. The limit applies to sounding voices; a stolen or exclusive-class voice fades for 64 samples in one of
16 spare slots, outside the count.

| Bit (375Fh data) | Effect | Voices |
|---|---|---|
| 5 REV | reverb | 13 |
| 4 CHR | chorus | 3 |
| 3 OM | spatial effect | 1 |
| 2 MIC | microphone input | 1 |
| 6 ECH | microphone echo | 3 |
| 1:0 EQ2 EQ1 | `10` 2-band / `11` 4-band equalizer | 4 / 8 |

"In some configurations, polyphony is decreased by 1 for reason of internal mixing": the one rule that
reproduces all 23 rows of the p.35 table is one more voice whenever any effect is on, except with the mike
echo on and except reverb + chorus alone (30h) (`Allocator.EffectsWordPolyphony`). The table marks **3Bh**
(reverb, chorus, spatial, 4-band EQ: **38 voices**) as the default, and 45h ("reset all") restores it, so the
power-up word is 3Bh. The note under the table calls the 45h state "Spatial Effect OFF", but its own 45h and
3Bh rows show spatial ON at 38 voices; the spatial volume (NRPN 3720h) defaults to 0, so the effect is
inaudible either way. `SynthConfig::polyphony` can fix another limit.

**Stealing order** (the datasheet does not document one): voices in their release first, then voices held only
by the sustain pedal, then held keys, melodic before rhythm; inside a tier the quietest, then the oldest. The
voices of the note being started are never taken (`Allocator.StealingOrder`, `Allocator.RhythmProtected`).
Lowering the limit (375Fh) steals the excess at once.

## Serial MIDI input (UART timing)

MIDI IN (pin 16) is "connected to the built-in synthesizer at power-up" (p.2): serial mode, 31 250 baud, 8N1
(MIDI 1.0: +-1 % transmitter tolerance). The datasheet does not describe the receiver, so it is modeled as a
standard UART: a free-running 16x receive clock (500 kHz, absolute from time 0), start detection on the first
tick that sees the line low, every bit decided by the majority of ticks 7, 8 and 9 of its 16. A start bit that
reads high at its middle is a false start; a low stop bit is a framing error (the byte is dropped and counted,
the receiver waits for the line to return high). A good byte is delivered at its stop bit's middle, 9.5 bit
times (304 us) after the start edge. The receiver tolerates +-(0.5 - 1/16) / 9.5 = **+-4.6 %** of the bit time:
+-2 % decodes every byte back to back, +-6 % does not (`Uart.BaudTolerance`).

Worked example (tdd §1): Note On 90 3C 64 from a Z80 at 3.5 MHz (112 T-states per bit), start edge at T0:
the three bytes arrive at T0 + 1064, T0 + 2184 and T0 + 3304 (`Uart.ByteTiming`).

After a reset (power-on, `Reset()`, NRPN 375Fh = 45h) the chip ignores MIDI for **50 ms** (p.9: "around
50 ms before a MIDI IN or MPU message can be processed"; p.35: the 45h reset stops the firmware about 50 ms).
Bytes in that window are dropped and counted (`SynthConfig::resetDelay`, `Timing.ResetBusy`). "Audio will begin
after 500 ms, maximum" (p.9) is an upper bound, not modeled.

## MIDI implementation: conformance table

Every row of the datasheet's implementation chart (§3, p.26-29), its special NRPNs (§2-1, p.15-18), the
polyphony NRPN (§5) and the codec SysEx (§6), with the library status and the test that holds it. "SAM-3" /
"SAM-4" are the open phases; "not modeled" rows say why.

| Message | Datasheet | Status | Test |
|---|---|---|---|
| Note On 9n kk vv (vv = 0: Note Off) | p.26 | done, sample-accurate | `Chart.NoteOnOff`, `Timing.SampleAccurateNoteOn` |
| Note Off 8n kk vv (vv ignored) | p.26 | done | `Chart.NoteOnOff` |
| Pitch Bend En, +-1 tone at power-up | p.26 | done (default modulator, RPN 0 range) | `Voice.PitchAndBend` |
| Program Change Cn; channel 10 selects the drum set | p.26, p.30-33 | done (part assignment by SysEx: SAM-3) | `Chart.ProgramChangeAndDrums` |
| Channel Aftertouch Dn | p.26 | partial: SF2 default (vibrato 50 cents); GS destinations (SysEx 40 2n 20-26): SAM-3 | `Chart.ModulationWheelAndAftertouch` |
| MIDI Reset FFh: power-up condition | p.26 | done | `Chart.MidiReset` |
| CC 0 Bank select (variation, MT-32 set 127; none on drums) | p.26, p.31 | done (latched at Program Change, capital-tone fallback) | `Chart.BankSelectVariation` |
| CC 1 Modulation | p.26 | done (default modulator); GS rate / depth SysEx: SAM-3 | `Chart.ModulationWheelAndAftertouch` |
| CC 5 Portamento time, CC 65 Portamento on/off | p.26 | SAM-3 | |
| CC 6 Data entry (RPN / NRPN) | p.26 | done | `Chart.RpnTuning` |
| CC 7 Volume (default 100) | p.26 | done | `Chart.VolumeExpression` |
| CC 10 Pan (default 64) | p.26 | done | `Voice.Pan` |
| CC 11 Expression (default 127) | p.26 | done | `Chart.VolumeExpression` |
| CC 64 Sustain | p.26 | done | `Chart.SustainPedal` |
| CC 66 Sostenuto, CC 67 Soft pedal | p.26 | SAM-3 | |
| CC 80 Reverb program, CC 81 Chorus program | p.26 | SAM-4 | |
| CC 91 Reverb send, CC 93 Chorus send | p.26 | stored and fed to the send generators (default modulators); the effects: SAM-4 | |
| CC 120 All Sound Off (abrupt) | p.26 | done (64-sample fade) | `Chart.AllSoundAndNotesOff` |
| CC 121 Reset All Controllers | p.26 | done (RP-015: volume, pan, sends kept) | `Chart.ResetAllControllers` |
| CC 123 All Notes Off (respects the pedal) | p.26 | done | `Chart.AllSoundAndNotesOff` |
| CC 126 Mono On, CC 127 Poly On | p.26 | done | `Chart.MonoMode` |
| Assignable CC1 / CC2 (SysEx 40 1x 1F / 20, functions 40 2x 40-5A) | p.26-27 | SAM-3 | |
| RPN 0000h pitch bend sensitivity (default 2) | p.27 | done | `Voice.PitchAndBend` |
| RPN 0001h fine tuning (+-100 cents, 14-bit) | p.27 | done | `Chart.RpnTuning` |
| RPN 0002h coarse tuning (+-64 semitones) | p.27 | done | `Chart.RpnTuning` |
| RPN 7F7Fh (null) | MIDI 1.0 | done | `Chart.RpnTuning` |
| NRPN 0108h / 0109h / 010Ah vibrato rate / depth / delay | p.27 | SAM-3 | |
| NRPN 0120h / 0121h TVF cutoff / resonance | p.27 | SAM-3 | |
| NRPN 0163h / 0164h / 0166h envelope attack / decay / release | p.27 | SAM-3 | |
| NRPN 18rrh / 1Arrh / 1Crrh drum pitch / level / pan | p.27 | SAM-3 | |
| NRPN 1Drrh / 1Errh drum reverb / chorus send | p.27 | SAM-3 (sends) + SAM-4 (effects) | |
| NRPN 3700h-3703h EQ band levels (60h / 40h / 40h / 60h) | p.15, p.18 | SAM-4 | |
| NRPN 3707h master volume | p.15-16 | SAM-3 | |
| NRPN 3708h-370Bh EQ corner frequencies (0Ch / 1Bh / 72h / 40h) | p.15, p.18 | SAM-4 | |
| NRPN 3713h clipping mode (soft) | p.15 | SAM-4 (render modes) | |
| NRPN 3715h / 3716h GM reverb / chorus send scaling | p.15, p.23-24 | SAM-4 | |
| NRPN 3718h-371Ah post-effect routing | p.15, p.17 | SAM-4 | |
| NRPN 3720h / 372Ch / 372Dh spatial effect volume / delay / input | p.15-16, p.42 | SAM-4 | |
| NRPN 3722h / 3723h GM volume / pan | p.15-16 | SAM-3 | |
| NRPN 3724h-3735h mike volume / pan, echo level / time / feedback / pans | p.15-17, p.43 | not modeled: MICIN is grounded on the MultiSound; the echo's voice cost is honored (375Fh) | |
| NRPN 3751h auto-test (sine tones, PASS 295 Hz) | p.16, p.40 | not modeled: a production test | |
| NRPN 3757h SysEx device ID (20h = all) | p.16 | SAM-3 | |
| NRPN 375Fh effects on/off, polyphony; 45h = reset all | p.16, p.34-35 | done (voice accounting; the effects themselves: SAM-4) | `Allocator.EffectsWordPolyphony`, `Allocator.DefaultAndNrpnPolyphony`, `Chart.ResetAll45h` |
| SysEx GM reset F0 7E 7F 09 01 F7 | p.27 | SAM-3 | |
| SysEx master volume F0 7F 7F 04 01 00 ll F7 | p.27 | SAM-3 | |
| SysEx GS master tune / volume / key shift / pan (40 00 00-06), GS reset (40 00 7F) | p.27 | SAM-3 | |
| SysEx GS voice reserve (40 01 10) | p.27 | SAM-3 (an allocator input) | |
| SysEx GS reverb type / character / level / time / delay feedback (40 01 30-35) | p.27 | SAM-4 | |
| SysEx GS chorus type / level / feedback / delay / rate / depth (40 01 38-3E) | p.28 | SAM-4 | |
| SysEx GS part: channel assign (1p 02), rhythm allocation (1p 15), velocity slope / offset (1p 1A / 1B), scale tuning (1p 40) | p.28 | SAM-3 | |
| SysEx GS controller destinations (40 2p xx: mod, bend, CAF, CC1, CC2 to pitch / TVF / amplitude / LFO1) | p.28-29 | SAM-3 | |
| SysEx Dream port write F0 00 20 00 00 00 12 33 77 pp v3 v2 v1 v0 xx F7 | p.27, p.36-38 | codec ports 12h / 14h (output gain, DAC mute): SAM-4; other ports not modeled ("not recommended") | |
| Serial MIDI IN, 31 250 baud 8N1 | p.2, p.13 | done | `Uart.*` |
| Running status, realtime inside messages, SysEx assembly | MIDI 1.0 | done (128-byte SysEx buffer; overflow dropped and counted) | `Parser.*` |
| Active Sensing FEh | MIDI 1.0 | ignored: not in the chip's chart | `Parser.RealtimeInsideMessage` |
| ~50 ms after reset before MIDI is processed | p.9, p.35 | done | `Timing.ResetBusy`, `Chart.ResetAll45h` |
| Parallel (MPU) mode: 3Fh / FFh / BEh, DATA8, status TE / RF, IRQ, 3.5 us write cycle, 1 ms read timeout | p.8, p.13-14, p.19-20 | not modeled in v1: the target cards drive the serial line; the parallel controls mirror NRPNs | |
| 64 voices, 38 with effects | p.1, p.34-35 | done | `Allocator.*` |
| 2-layer instruments take two voices | p.41 | done by construction (one voice per bank zone) | |
| Drum sets, [EXC] groups | p.32-33 | done via SF2 bank 128 and exclusive classes (the kits are the bank's) | `Voice.ExclusiveClass` |
| MT-32 sound variation #127 | p.31 | done via SF2 bank 127 when the bank has it (GeneralUser GS has no melodic bank 127: capital tones) | `Chart.BankSelectVariation` |
| Sampling rate 37.5 kHz | AN_2695 | done (`kInternalRate`) | `Render.ResampledSine` |

## Effects (datasheet; built in SAM-4)

Extracted for SAM-4; nothing here is rendered yet (`SynthConfig::effects` only reserves the switch).

- **Signal flow** (p.42): MIDI synthesis to a stereo bus and the reverb / chorus sends; reverb, chorus and the
  mike echo return to the main out; "post effects" (spatial effect + equalizer) can be applied separately to
  the GM bus (NRPN 3718h, on), the mike and echo (3719h, off) and the reverb / chorus returns (371Ah, on).
- **Reverb** (p.23): programs room1, room2, room3, hall1, hall2, plate, delay, pan delay (0-7, default 4 =
  hall2). Default level 90h (hall1 C0h, delays FFh), time 7Fh (delay 18h), delay feedback 22h / 26h for the
  two delay programs. Selected by CC 80, SysEx 40 01 30 or control 69h; GM send scaling 3715h (40h = as sent).
- **Chorus** (p.24): chorus1-4, feedback chorus, flanger, short delay, feedback delay (0-7, default 2 =
  chorus3). Defaults per program - level 90h (delays FFh), delay 4Bh / 40h / 40h / 2Bh / 7Fh / 56h / 7Fh / 7Fh,
  feedback 00h / 07h / 09h / 0Ch / 48h / 7Fh / 00h / 50h, rate 03h / 09h / 03h / 09h / 02h / 01h / 00h / 00h,
  depth 05h / 13h / 13h / 10h / 0Ch / 03h / 00h / 00h.
- **Equalizer** (p.18): 4 bands, levels 00h = -12 dB, 40h = 0 dB, 7Fh = +12 dB (defaults 60h / 40h / 40h /
  60h: +6 dB bass and treble); corner frequencies linear 0-4.7 kHz (low), 0-4.2 kHz (mid bands), 0-18.75 kHz
  (high), defaults 0Ch / 1Bh / 72h / 40h (about 444 Hz, 893 Hz, 3.77 kHz, 9.45 kHz); 2-band mode by 375Fh.
- **Spatial effect** (p.16, p.42): a delay line fed with L - R (stereo wide, default) or L + R (mono to pseudo
  stereo), added to one side and subtracted from the other; volume 3720h (default 0), delay 372Ch (1Dh).
- **Master and GM volume / pan, clipping** (p.15-16): master volume 3707h, GM volume 3722h, GM pan 3723h,
  soft (default) or hard clipping 3713h.
- **Output stage** (p.7, p.36-37): codec output gain +6 to -40 dB in 1 dB steps (power-up 0 dB), DAC mute;
  DAC THD+N -75 dB, dynamic range 86 dB.

## State

`StateSize()` / `SaveState()` / `LoadState()` capture the chip: UART, parser, queued bytes not yet synthesized,
the 16 channels, all 80 voice slots (envelopes, LFOs, filter memory, phase), the effects word, the reset window.
The blob is little-endian at declared widths, floats as IEEE bits; its size is constant after `Configure()`.
It names the bank by its SHA-256: `LoadState()` refuses a blob saved with another bank, another layout version
or another queue size, and leaves the chip untouched (`State.RefusesOtherBank`). A loaded blob is not trusted:
indices into the bank are clamped. The render layer (resampler history, produced but unrendered audio) is not
captured and restarts (`State.RoundTrip`).

## Taps

`SetChannelMute()` removes a channel from the mix without touching its voices; `Describe()` reports every
channel (program, bank, preset, volume, pan, expression, bend, voices), the polyphony limit and counts of
received bytes, framing errors, bytes dropped in the reset window or on a full queue, SysEx overflows, stolen
voices and stream overruns.

## Layout

```
include/sam2695/    public API: sam2695.h (Synth), sam2695config.h (constants, SynthConfig), soundbank.h (banks)
src/sam2695.cpp     Synth: time model, input queue, state blob, render layer
src/synthcore.*     the chip: MIDI implementation, channels, voice pool, allocation, polyphony accounting
src/midi/           uart.h (serial line receiver), parser.h (running status, realtime, SysEx)
src/channel/        channel (part) state
src/voice/          voice, envelope, lfo, filter, interpolator
src/bank/           SF2 loader, generator table and default modulators, SHA-256
src/render/         output resampler, Kaiser window
src/common/         128-bit time arithmetic, state archive, unit conversions
tools/              sam2695render, Standard MIDI File reader
tests/              self-contained test harness (no third-party framework), banks generated in code
```

## Tests

```bash
tools/build/build.sh sam2695tests && cmake-build-agent-release/bin/sam2695tests   # embedded
cmake -S . -B build -G Ninja && ninja -C build sam2695tests && ./build/sam2695tests  # standalone
```

`sam2695tests [substring]` runs the named tests (`Group.Name`, as in the conformance table). Every bank is
built in code (`tests/sf2builder.h`), so no test needs a file. Groups: `Uart`, `Parser`, `Sha256`, `Bank`,
`Voice`, `Allocator`, `Timing`, `State`, `Render`, `Chart`, `Golden`.

The golden tests pin each scenario as a fingerprint - the RMS of every 1024-sample block in dB, to 0.05 dB -
and check that two renders are bit-identical. A fingerprint survives the last-bit differences of `libm` between
platforms, which a digest of the float output would not; bit-identical output is promised on the same build.
`SAM2695_PRINT_GOLDEN=1` prints the fingerprints and the SHA-256 of each render.

The SF2 semantics are checked against FluidSynth and every bank of the development corpus is loaded and played
by the scripts in `tools/verification/sam2695/` (README there).

## Design and history

Design: `docs/inprogress/2026-10-03-zx-multisound/tdd-libsam2695.md` (§10 "As built"), owner decisions in
`open-questions.md` (Q2: our own library, vendored like libopl4; Q3: SF2 banks, default GeneralUser GS; Q4: the
default bank tracked in `data/midi/`). Sources: the SAM2695 datasheet (Dream, June 2015), AN_2695 (SAM2195 to
SAM2695 migration), the [SoundFont 2.04 specification](https://www.synthfont.com/sfspec24.pdf), MIDI 1.0 and
GM / GS recommended practice (RP-015).
