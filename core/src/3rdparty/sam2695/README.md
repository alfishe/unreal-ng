# libsam2695 — Dream SAM2695 General MIDI synthesizer

In-house model of the Dream SAM2695 as fitted to the ZX-MultiSound card (and, later, any MIDI-capable device):
its serial MIDI input, its MIDI implementation, its polyphony accounting and voice model, playing samples
from a SoundFont 2 bank. The chip's CleanWave sample ROM is not available, so the *timbre* is the bank's;
everything the chip does with MIDI data (which notes sound, how long, how loud, at what pitch, which voice is
stolen) follows the datasheet. Standard library only; no allocation on the audio path after `Configure()` and
`LoadBank()`; deterministic; the chip state serializes to a platform-independent blob.

Status: phases SAM-0 to SAM-4 of
[tdd-libsam2695.md](../../../../docs/inprogress/2026-10-03-zx-multisound/tdd-libsam2695.md) (datasheet study,
skeleton with UART / parser / SF2 loader / state, full voice model, the whole MIDI implementation chart, the
effects and the output stage). The conformance table below names the test of every chart row.

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
- The default bank is GeneralUser GS (owner decision Q3/Q4: `data/midi/generaluser-gs.sf2`, tracked since 2026-10-05 with its license and README; the build ships it next to the executables).

## The voice model (SF2 2.04)

One voice per matching instrument zone (so the chip's 2-layer instruments, datasheet §8-1, take two voices
exactly when the bank layers them).

| Element | Model |
|---|---|
| Generators | instrument global + local (absolute, local wins) plus preset global + local (relative, only the generators allowed at preset level); final values clamped to the 2.04 ranges |
| Modulators | the 2.04 default modulators (except 8.4.3 / 8.4.4, replaced by the chip's GS controller matrix: "GS parts and SysEx"), superseded by identical instrument modulators (global, then local), plus the preset level's own list added on top; then the part's GS controls (controller destinations, part NRPNs, scale tuning, drum edits, soft pedal); re-evaluated at the next control block whenever a source of the part changes; linked modulators are ignored (warned) |
| Sample playback | 32.32 fixed-point phase (`Voice.PhaseFixedPoint`); address offset generators (fine + 32768 x coarse); loop modes 0 / 1 / 3 (`Voice.LoopModes`) |
| Interpolation | Linear, Cubic (Catmull-Rom), **Sinc** (8 taps, Kaiser beta 6, 256 phases interpolated; default) - a render option (`Voice.Interpolation`) |
| Volume envelope | delay / attack / hold / decay / sustain / release in whole samples from timecents, per sample exactly: attack linear in amplitude, decay and release linear in dB at 100 dB per time constant, sustain in centibels, keynum-to-hold / decay scaling; the voice ends at -100 dB (`Voice.EnvelopeTimecents`) |
| Modulation envelope | same timing, value 0..1, convex attack, to pitch and filter cutoff (`Voice.ModEnvelope`) |
| LFOs | modulation LFO (pitch, cutoff, volume) and vibrato LFO (pitch): triangle from 0 rising, delay and frequency from the generators, 32-bit phase (`Voice.Lfo`) |
| Filter | resonant 2-pole low-pass (RBJ biquad, TDF-II), cutoff in absolute cents, Q as the SF2 peak height (q = 10^((Q/10 - 3.01)/20)), DC gain 1 / sqrt(q) above Butterworth, coefficients interpolated across each block, bypassed at >= 0.45 fs with Q = 0 (`Voice.FilterResponse`) |
| Gain | static attenuation = 0.4 x the bank's initialAttenuation + the modulators' centibels, minus mod-LFO tremolo; pan by the constant-power sin / cos law (`Voice.StaticAttenuation`, `Voice.Pan`, `Voice.VelocityCurve`) |
| Exclusive classes | a new note cuts every voice of the same class on its channel with a 64-sample fade (`Voice.ExclusiveClass`) |
| Re-struck key, mono | a key struck again releases its previous note; mono mode (CC 126) releases every note of the part |
| Portamento | a glide offset in cents moving toward 0 at a constant rate, advanced by the samples rendered, pitch updated per control block (`Chart.Portamento`) |
| Effect sends | the voice's static gain x its reverb / chorus send, ramped per block like the pan gains, into the two mono send buses (`Fx.SendLevels`) |

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

**Stealing order** (the datasheet does not document one): voices of parts within their GS voice reserve (40 01
10) are protected from other parts; then voices in their release first, then voices held only by a pedal, then
held keys, melodic before rhythm; inside a tier the quietest, then the oldest. The voices of the note being
started are never taken (`Allocator.StealingOrder`, `Allocator.RhythmProtected`, `Chart.VoiceReserve`).
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
polyphony NRPN (§5) and the codec SysEx (§6), with the library status and the test that holds it. A row the
library does not honor says why. SAM-3 built the chart rows, SAM-4 the effects.

| Message | Datasheet | Status | Test |
|---|---|---|---|
| Note On 9n kk vv (vv = 0: Note Off) | p.26 | done, sample-accurate | `Chart.NoteOnOff`, `Timing.SampleAccurateNoteOn` |
| Note Off 8n kk vv (vv ignored) | p.26 | done | `Chart.NoteOnOff` |
| Pitch Bend En, +-1 tone at power-up | p.26 | done (default modulator, RPN 0 range) | `Voice.PitchAndBend` |
| Program Change Cn; channel 10 selects the drum set | p.26, p.30-33 | done; any part can be a rhythm part (40 1p 15) | `Chart.ProgramChangeAndDrums`, `Chart.PartRhythmAllocation` |
| Channel Aftertouch Dn | p.26 | done: no default effect; destinations by 40 2p 20-26 (replaces the SF2 default modulator 8.4.3, "GS parts and SysEx") | `Chart.ModulationWheelAndAftertouch`, `Chart.ControllerDestinations` |
| MIDI Reset FFh: power-up condition | p.26 | done (parts, effects word, effect settings) | `Chart.MidiReset`, `Fx.ProgramSelect` |
| CC 0 Bank select (variation, MT-32 set 127; none on drums) | p.26, p.31 | done (latched at Program Change, capital-tone fallback; CC 32 not used) | `Chart.BankSelectVariation`, `Chart.BankSelectLsbIgnored` |
| CC 1 Modulation (rate and depth by SysEx) | p.26 | done: GS 40 2p 00-06, LFO1 pitch depth 0Ah = 47 cents at power-up | `Chart.ModulationWheelAndAftertouch`, `Chart.ControllerDestinations` |
| CC 5 Portamento time, CC 65 Portamento on/off | p.26 | done ("Pedals and portamento") | `Chart.Portamento` |
| CC 6 Data entry (RPN / NRPN) | p.26 | done | `Chart.RpnTuning` |
| CC 7 Volume (default 100) | p.26 | done | `Chart.VolumeExpression` |
| CC 10 Pan (default 64) | p.26 | done | `Voice.Pan` |
| CC 11 Expression (default 127) | p.26 | done | `Chart.VolumeExpression` |
| CC 64 Sustain | p.26 | done | `Chart.SustainPedal` |
| CC 66 Sostenuto | p.26 | done | `Chart.Sostenuto` |
| CC 67 Soft pedal | p.26 | done: -3 dB and one octave darker for notes started under it (the chart gives no amounts) | `Chart.SoftPedal` |
| CC 80 Reverb program, CC 81 Chorus program (0-7; others ignored) | p.26 | done | `Fx.ProgramSelect`, `Fx.ReverbPrograms`, `Fx.ChorusPrograms` |
| CC 91 Reverb send, CC 93 Chorus send | p.26 | done (SF2 send generators, linear; GS power-up 40 / 0) | `Fx.SendLevels` |
| CC 120 All Sound Off (abrupt) | p.26 | done (64-sample fade) | `Chart.AllSoundAndNotesOff` |
| CC 121 Reset All Controllers | p.26 | done (RP-015: volume, pan, sends kept; pedals lifted) | `Chart.ResetAllControllers` |
| CC 123 All Notes Off (respects the pedals) | p.26 | done | `Chart.AllSoundAndNotesOff` |
| CC 126 Mono On, CC 127 Poly On | p.26 | done; CC 124 / 125 (not in the chart) act as All Notes Off only: the chip is always Omni Off | `Chart.MonoMode`, `Chart.OmniModeMessages` |
| Assignable CC1 / CC2 (SysEx 40 1p 1F / 20, functions 40 2p 40-46 / 50-56) | p.26-27 | done | `Chart.AssignableControllers` |
| RPN 0000h pitch bend sensitivity (default 2) | p.27 | done (= GS 40 2p 10, 0-24 semitones) | `Voice.PitchAndBend`, `Chart.ControllerDestinations` |
| RPN 0001h fine tuning (+-100 cents, 14-bit) | p.27 | done | `Chart.RpnTuning` |
| RPN 0002h coarse tuning (+-64 semitones) | p.27 | done | `Chart.RpnTuning` |
| RPN 7F7Fh (null) | MIDI 1.0 | done | `Chart.RpnTuning` |
| NRPN 0108h / 0109h / 010Ah vibrato rate / depth / delay | p.27 | done (relative, "GS parts and SysEx") | `Chart.PartNrpnVibrato` |
| NRPN 0120h / 0121h TVF cutoff / resonance | p.27 | done | `Chart.PartNrpnFilter` |
| NRPN 0163h / 0164h / 0166h envelope attack / decay / release | p.27 | done | `Chart.PartNrpnEnvelope` |
| NRPN 18rrh / 1Arrh / 1Crrh drum pitch / level / pan | p.27 | done (two edit tables: channel 10, the other channels) | `Chart.DrumNrpn` |
| NRPN 1Drrh / 1Errh drum reverb / chorus send | p.27 | done | `Fx.DrumNoteSends` |
| NRPN 3700h-3703h EQ band levels (60h / 40h / 40h / 60h) | p.15, p.18 | done | `Fx.EqualizerBands` |
| NRPN 3707h master volume | p.15-16 | done (linear on the output) | `Chart.NrpnMasterVolume` |
| NRPN 3708h-370Bh EQ corner frequencies (0Ch / 1Bh / 72h / 40h) | p.15, p.18 | done | `Fx.EqualizerBands` |
| NRPN 3713h clipping mode (soft) | p.15 | done | `Fx.SoftClipping` |
| NRPN 3715h / 3716h GM reverb / chorus send scaling | p.15, p.23-24 | done (value / 40h) | `Fx.SendScaling` |
| NRPN 3718h-371Ah post-effect routing | p.15, p.17 | done for the GM bus (3718h) and the returns (371Ah); 3719h (mike) stored only | `Fx.PostEffectsRouting` |
| NRPN 3720h / 372Ch / 372Dh spatial effect volume / delay / input | p.15-16, p.42 | done | `Fx.SpatialEffect` |
| NRPN 3722h / 3723h GM volume / pan | p.15-16 | done (= GM / GS master volume, GS master pan) | `Chart.GmVolumeAndPan` |
| NRPN 3724h-3735h mike volume / pan, echo level / time / feedback / pans | p.15-17, p.43 | not modeled: MICIN is grounded on the MultiSound; the echo's voice cost is honored (375Fh) | |
| NRPN 3751h auto-test (sine tones, PASS 295 Hz) | p.16, p.40 | not modeled: a production test | |
| NRPN 3757h SysEx device ID (20h = all) | p.16 | done | `Chart.SysExDeviceId` |
| NRPN 375Fh effects on/off, polyphony; 45h = reset all | p.16, p.34-35 | done: the voice cost and the effects themselves follow the bits | `Allocator.EffectsWordPolyphony`, `Allocator.DefaultAndNrpnPolyphony`, `Chart.ResetAll45h`, `Fx.EffectsWordSwitchesEffects` |
| SysEx GM reset F0 7E 7F 09 01 F7 | p.27 | done | `Chart.GmSystemOn` |
| SysEx master volume F0 7F 7F 04 01 00 ll F7 | p.27 | done | `Chart.GmMasterVolume` |
| SysEx GS master tune / volume / key shift / pan (40 00 00-06), GS reset (40 00 7F) | p.27 | done; the checksum is "don't care" | `Chart.GsMasterTune`, `Chart.GmVolumeAndPan`, `Chart.GsMasterKeyShift`, `Chart.GsReset`, `Chart.GsChecksumDontCare` |
| SysEx GS voice reserve (40 01 10) | p.27 | done (an allocator input) | `Chart.VoiceReserve` |
| SysEx GS reverb type / character / level / time / delay feedback (40 01 30-35) | p.27 | done | `Fx.ProgramSelect`, `Fx.ReverbTimeLevelFeedback`, `Fx.ReverbDelayPrograms` |
| SysEx GS chorus type / level / feedback / delay / rate / depth (40 01 38-3E) | p.28 | done | `Fx.ProgramSelect`, `Fx.ChorusPrograms`, `Fx.ChorusParameters` |
| SysEx GS part: channel assign (1p 02), rhythm allocation (1p 15), velocity slope / offset (1p 1A / 1B), scale tuning (1p 40) | p.28 | done | `Chart.PartChannelAssign`, `Chart.PartRhythmAllocation`, `Chart.VelocitySense`, `Chart.ScaleTuning` |
| SysEx GS controller destinations (40 2p xx: mod, bend, CAF, CC1, CC2 to pitch / TVF / amplitude / LFO1) | p.28-29 | done | `Chart.ControllerDestinations`, `Chart.AssignableControllers` |
| SysEx Dream port write F0 00 20 00 00 00 12 33 77 pp v3 v2 v1 v0 xx F7 | p.27, p.36-38 | codec port 12h (output gain, DAC select / mute) done, 14h stored (ADC side); other ports not modeled ("not recommended") | `Fx.CodecGain` |
| Serial MIDI IN, 31 250 baud 8N1 | p.2, p.13 | done | `Uart.*` |
| Running status, realtime inside messages, SysEx assembly | MIDI 1.0 | done (128-byte SysEx buffer; overflow dropped and counted) | `Parser.*` |
| Active Sensing FEh | MIDI 1.0 | ignored: not in the chip's chart | `Parser.RealtimeInsideMessage` |
| ~50 ms after reset before MIDI is processed | p.9, p.35 | done | `Timing.ResetBusy`, `Chart.ResetAll45h` |
| Parallel (MPU) mode: 3Fh / FFh / BEh, DATA8, status TE / RF, IRQ, 3.5 us write cycle, 1 ms read timeout | p.8, p.13-14, p.19-20 | not modeled in v1: the target cards drive the serial line; the parallel controls mirror NRPNs | |
| 64 voices, 38 with effects | p.1, p.34-35 | done | `Allocator.*` |
| 2-layer instruments take two voices | p.41 | done by construction (one voice per bank zone) | `Chart.TwoLayerInstrument` |
| Drum sets, [EXC] groups | p.32-33 | done via SF2 bank 128 and exclusive classes (the kits are the bank's) | `Voice.ExclusiveClass` |
| MT-32 sound variation #127 | p.31 | done via SF2 bank 127 when the bank has it (GeneralUser GS has no melodic bank 127: capital tones) | `Chart.BankSelectVariation` |
| Sampling rate 37.5 kHz | AN_2695 | done (`kInternalRate`) | `Render.ResampledSine` |

Not in the SAM2695 chart, so not implemented: the GM2 sound controllers CC 71-78 and portamento control CC 84
(the DreamBlaster X16 has them), GS reverb pre-LPF / pre-delay (40 01 32 / 37), chorus pre-LPF / send to reverb
(40 01 39 / 3F), the GS EQ SysEx (40 02 xx; the chip's EQ is NRPN 3700h-370Bh).

## GS parts and SysEx (SAM-3)

- **Parts.** The 16 parts of GS: part i receives MIDI channel i at power-up (`SynthReport::channels` are the
  parts); 40 1p 02 moves a part to another channel or switches it off (10h), so several parts may play one
  channel. The GS block number p maps to the part as Roland's does: block 0 = part 10, blocks 1-9 = parts 1-9,
  A-F = parts 11-16 (the voice reserve bytes come in the same order). The chip's special NRPNs 37xxh are
  chip-level: accepted on any MIDI channel and applied once, whatever the part assignment (the DreamBlaster X16
  specification wants channel 1 for them; the SAM2695 datasheet does not say).
- **Resets.** MIDI Reset FFh and NRPN 375Fh = 45h restore the power-up condition (everything; the latter with
  the 50 ms busy window). GM System On and GS reset restore the parts, master tune / key shift / pan, the drum
  edits and the reverb / chorus programs, and stop the sound; the chip's own NRPN 37xxh settings and the master
  / GM volume stay (X16 specification: "not reset by GS reset"). Neither has a busy window (the datasheet gives
  none).
- **Device ID** (NRPN 3757h): 20h (power-up) accepts every device; 0-1Fh only that one, plus the universal
  all-call 7Fh on universal messages. The datasheet's own Roland messages carry device 00h. The Roland checksum
  is "don't care" (p.29) and is not verified. The Dream port SysEx has no device field.
- **Master volume and pan.** The GM and GS master volume SysEx and NRPN 3722h are one parameter, the GM bus
  volume (linear); GS master pan and NRPN 3723h are one parameter ("same as", p.16), a balance on the GM bus
  (40h: both sides whole). NRPN 3707h is the output master volume, linear.
- **Tuning.** GS master tune: four nibbles, 0400h = 0, 0.1 cent units, +-100 cents. Master key shift
  (+-24 semitones) transposes the key of melodic parts before the zones are chosen; drums stay. Scale tuning
  (cents by note name) applies to melodic parts only.
- **Velocity sense** (40 1p 1A / 1B): velocity x depth / 40h + (offset - 40h), within 1-127, before zone
  selection.
- **Voice reserve** (40 01 10): a voice of a part that has no more voices than its reserve is not stolen for
  another part ("Voice allocation").
- **Controller destinations** (40 2p xx). Five sources (modulation wheel, bend, channel aftertouch, CC1, CC2)
  times seven destinations, in Roland's units: pitch +-24 semitones (40h = 0), TVF cutoff 150 cents per step,
  amplitude -100 % .. +100 %, LFO1 rate +-10 Hz (the wheel only, as the chart lists), LFO1 pitch depth
  0-600 cents, LFO1 TVF depth 0-2400 cents, LFO1 TVA depth 0-100 %. LFO1 is the SF2 vibrato LFO. Bend's pitch
  control is the bend range (RPN 0). Pitch, cutoff and amplitude follow the source's signed value, the LFO
  terms its magnitude. Power-up: only the wheel's LFO1 pitch depth (0Ah = 47 cents). This **replaces the SF2
  default modulators 8.4.3 (aftertouch to vibrato) and 8.4.4 (wheel to vibrato)**: the chip's aftertouch does
  nothing until a destination is set; a bank's own modulators are kept.
- **GS part NRPNs** (01xxh, 40h = no change), relative to the sound: vibrato rate 20 cents per step on the
  vibrato LFO frequency, vibrato depth 1 cent per step added to its pitch depth, TVF cutoff 60 cents per step,
  resonance 3 cB per step; vibrato delay and the volume / modulation envelope attack, decay and release
  75 timecents per step, where a lengthening starts from at least 2^-5 s (an instant attack made "slower"
  becomes audible: 7Fh gives 0.48 s). The datasheet gives no scales; these are ours, sized so 00h / 7Fh are
  clearly audible and 40h is exact.
- **Drum edits** (18rr-1Err): pitch in semitones (40h = 0), level (7Fh = the kit's; 40 log10(v / 127) dB), pan
  absolute (40h center; replaces the kit's pan), reverb / chorus send depth (x v / 127). Two tables as on the
  DreamBlaster X16: one for parts receiving channel 10, one for the others; an edit goes to the table of the
  channel it arrives on. GS / GM reset clears them.

## Pedals and portamento

- **Sostenuto** (CC 66) holds the notes that are down when it is pressed, and only those; the damper (CC 64)
  holds every released note. A note held by either pedal releases when both have let it go.
- **Soft pedal** (CC 67): a note started under it plays 3 dB softer and one octave darker (initial cutoff -1200
  cents); notes already sounding do not change. The datasheet gives no amounts.
- **Portamento** (CC 65 on): a new note glides from the part's last key to its own at a constant rate, linear in
  pitch; CC 5 = v sets 5 ms x 2^(v / 14) per octave (5 ms at 0, 0.12 s at 40h, 2.7 s at 7Fh; the datasheet gives
  no curve). Not on rhythm parts. In mono mode (CC 126) the previous note releases and the new one glides.

## Effects and output (SAM-4)

`src/fx/`: `effects.*` (signal flow, routing, output stage), `reverb.*`, `chorus.*` (chorus, spatial effect,
equalizer), `dsp.h` (delay lines, LFO sine, biquad, denormal guard), `fxparams.h` (the settings with the
datasheet's power-up values). Everything runs at the internal rate inside the control block, after the voices.

- **Signal flow** (p.42): the voices fill the GM bus (stereo) and two mono sends, reverb and chorus (pre-pan:
  each voice sends its static gain x its send). The GM bus and the sends are scaled by the GM volume, the bus
  balanced by the GM pan. Reverb and chorus run only while their NRPN 375Fh bit is on; switching one off clears
  its state. The **post effects** (spatial effect, then the equalizer) process the GM bus when 3718h is on
  (power-up) and the reverb + chorus returns when 371Ah is on (power-up); what is not routed through them
  bypasses them. Then the output stage.
- **Sends.** A voice's reverb / chorus send is the SF2 send generator (CC 91 / 93 through the default
  modulators 8.4.8 / 8.4.9: 200 per mille at 127, the E-mu / FluidSynth scale; plus a bank's own send
  generators), x NRPN 3715h / 3716h / 40h, x the drum note's send depth. Linear in the controller.
- **Reverb** (p.23), eight programs. Programs 0-5 run J. Dattorro's plate ("Effect Design, Part 1", JAES 1997):
  predelay, input band limit, four input diffusers (0.75 / 0.625), a figure-eight tank of two halves (a
  modulated all-pass -0.70 with 0.85 Hz modulation, a delay, damping, the decay, an all-pass, a delay), fourteen
  output taps. Delays scale from Dattorro's 29 761 Hz to 37 500 Hz and by the program size. Each program:

  | Program | Size | Decay (REV_TIME 7Fh) | Predelay | Input bandwidth | Damping | Measured RT60 (T20, noise) |
  |---|---|---|---|---|---|---|
  | 0 room1 | 0.40 | 0.55 s | 2 ms | 0.70 | 0.45 | 0.53 s |
  | 1 room2 | 0.50 | 0.85 s | 4 ms | 0.75 | 0.40 | 0.79 s |
  | 2 room3 | 0.62 | 1.25 s | 6 ms | 0.80 | 0.33 | 1.17 s |
  | 3 hall1 | 0.82 | 1.90 s | 12 ms | 0.85 | 0.25 | 1.76 s |
  | 4 hall2 (power-up) | 1.00 | 2.60 s | 18 ms | 0.88 | 0.20 | 2.38 s |
  | 5 plate | 0.75 | 2.10 s | 0 | 0.9995 | 0.05 | 1.93 s |

  The decay multiplier g (four per loop) is 10^(-3 x loop time / (4 x RT60)). REV_TIME t scales the decay time
  by 2^((t - 127) / 32) (each 32 steps down halve it). Programs 6 (delay) and 7 (pan delay: echoes alternate
  left and right) are a feedback delay of 2.8 ms x (t + 1) (70 ms at the delay default 18h, 358 ms at 7Fh) with
  feedback REV_FEED / 128 (at most 0.95) and a gentle high cut in the loop. Level: REV_VOL / 127 (the
  per-program defaults of p.23, halved to SysEx units: 48h, hall1 60h, delays 7Fh). The GS "character"
  (40 01 31) selects the algorithm alone; a program (CC 80, 40 01 30, parallel 69h) sets the character and the
  level / time / feedback defaults. The two layouts share one 30 k-sample pool; a new character clears it.
- **Chorus** (p.24), eight programs on one modulated delay line, mono in, stereo out (two taps, the right LFO a
  quarter cycle behind, cubic interpolation; the taps' mean feeds back). Mapping of the 7-bit parameters (the
  datasheet gives the ranges only): delay 40 ms x 2^((d - 127) / 24) (1 ms .. 40 ms; 6.2 ms at 40h), rate
  0.05 + r x 9.95 / 127 Hz, depth p x 0.1 ms, feedback f / 127 x 0.85, level l / 127. The programs are their
  defaults: chorus 1-4, feedback chorus and flanger (feedback 48h / 7Fh), short delay (40 ms, no modulation),
  feedback delay.
- **Spatial effect** (p.16, p.42): L - R (stereo, power-up) or L + R (mono, 372Dh = 7Fh) through a delay of
  0.25 ms + v x 0.15 ms (4.6 ms at 1Dh), x volume / 127, added to the left and subtracted from the right. Its
  power-up volume is 0: inaudible although on.
- **Equalizer** (p.18): low shelf, two peaking bands (Q 0.707, the DreamBlaster X16's documented default; the
  SAM2695 datasheet gives none), high shelf; RBJ biquads in double precision. Gains 00h = -12 dB, 40h = 0,
  12/64 dB per step (7Fh = +11.8 dB); corners linear over 0-4.7 kHz (low), 0-4.2 kHz (mid bands), 0-18.75 kHz
  (high), held inside 20 Hz .. 0.45 fs. Power-up: +6 dB shelves at 444 Hz and 9.45 kHz, the mid bands flat.
  The 2-band mode (375Fh EQ bits 10) keeps the shelves.
- **Output stage.** x `SynthConfig::outputGain` (the mixer's headroom, 0.25 = -12 dB) x master volume (3707h)
  -> **soft clipping** (power-up): transparent up to 0.75, then 0.75 + 0.25 x u / (1 + u) with u = (|x| - 0.75)
  / 0.25 (slope 1 at the knee, approaches full scale and never reaches it), or hard clipping at +-1 (3713h >=
  40h) -> the codec output gain (port 12h OUTG: 39h = 0 dB, 1 dB steps from +6 dB to -40 dB, then -43.5 /
  -58.5 dB; DACSEL = 0 or DACMUTE = 1 mute). The codec gain follows the clipper as on the chip (an analog
  stage): at +6 dB the output can exceed 1.0.
- **Render modes.** `SynthConfig::effects = true` (default) renders all of the above. `false` is the dry mode:
  the voice mix with every gain (GM volume / pan, master volume, output gain, codec) but no reverb, chorus,
  spatial effect, equalizer or clipping - a linear signal for analysis (the FluidSynth comparison of the voice
  model uses it). The NRPN 375Fh voice accounting is the chip's in both modes (`Render.Modes`).
- **Denormals.** Every value written into a feedback path passes (x + 1e-20) - 1e-20, which rounds anything
  below ~1e-27 to zero: no denormal circulates, on any CPU, deterministically.
- **Idle effects.** An effect whose whole state is +0.0 (every bit clear: delay lines, filter memory) and whose
  input block is all +0.0 computes +0.0 everywhere, so the block is skipped: only its write positions and LFO
  phase move, by the n samples processing would have taken. Output and state are bit-identical to processing
  (`SynthConfig::skipIdleEffects = false` processes every block; `Fx.IdleEffects*` render both ways and compare
  every output bit and the state blob). Idle is derived, never serialized: true after power-up, a reset, an
  effect switched off or a new reverb character (all clear the state) and after a load whose state is all
  +0.0; the reverb, chorus and spatial line become idle again after a run of +0.0 writes as long as the line,
  the equalizer when its filter memory is +0.0. `Describe()` reports the flags and the skipped blocks. In
  practice the effects are idle until the first note with a send (the MultiSound card at rest) and after a
  reset. A tail does not always end in exact zeros: the delay reverbs (6, 7), the chorus programs 0-4 and 6
  and the spatial line do (after a 0.2 s note at full sends: delay 3.8 s, pan delay 36 s, chorus 1 0.3 s,
  feedback chorus 3.2 s); the tank reverbs (0-5), the flanger and feedback delay (chorus 5, 7) and the equalizer settle into a
  limit cycle of the denormal guard's quantum (about 1e-27 in float, 1e-215 in double, 2e-27 at the output,
  -530 dBFS) and keep running.
- **Cost.** GeneralUser GS through the corpus GM test file: 25 s of audio in 474 ms with the effects, 426 ms dry
  (Apple M-series, one core).
- **Not modeled:** the microphone input and its echo (MICIN is grounded on the MultiSound; their voice cost is).

## State

`StateSize()` / `SaveState()` / `LoadState()` capture the chip: UART, parser, queued bytes not yet synthesized,
the 16 parts (with their GS parameters), all 80 voice slots (envelopes, LFOs, filter memory, phase, portamento),
the chip-level MIDI state (NRPN selection per channel, master tune, key shift, device ID, drum edit tables), the
effects word, every effect setting and the effects' whole state - the reverb's 30 k-sample delay pool, the
chorus and spatial lines, the equalizer's filter memory, the LFO phases - so a restore continues a reverb tail
sample for sample (`Fx.ReverbTailAcrossState`), and the reset window. Layout version 2 (SAM-3 / SAM-4); the
blob is 292 KB at the default configuration, about 130 KB of it the effects' delay lines.
The blob is little-endian at declared widths, floats as IEEE bits; its size is constant after `Configure()`.
It names the bank by its SHA-256: `LoadState()` refuses a blob saved with another bank, another layout version
or another queue size, and leaves the chip untouched (`State.RefusesOtherBank`). A loaded blob is not trusted:
indices into the bank and the delay lines are clamped (a reverb whose layout does not match its settings
starts empty). The render layer (resampler history, produced but unrendered audio) is not
captured and restarts (`State.RoundTrip`).

## Taps

`SetChannelMute()` removes a part from the mix (and its sends) without touching its voices; `Describe()` reports
every part (program, bank, preset, volume, pan, expression, bend, voices, receive channel, voice reserve), the
polyphony limit, the effects (reverb program / character and its decay time, chorus program, master and GM
volume, GM pan, master tune, key shift, device ID, clipping mode, codec gain / mute) and counts of received
bytes, framing errors, bytes dropped in the reset window or on a full queue, SysEx overflows, stolen voices and
stream overruns.

## Layout

```
include/sam2695/    public API: sam2695.h (Synth), sam2695config.h (constants, SynthConfig), soundbank.h (banks)
src/sam2695.cpp     Synth: time model, input queue, state blob, render layer
src/synthcore.*     the chip: MIDI implementation (parts, RPN / NRPN, GM / GS / Dream SysEx), voice pool,
                    allocation, polyphony accounting, the effects hookup
src/midi/           uart.h (serial line receiver), parser.h (running status, realtime, SysEx)
src/channel/        GS part state
src/fx/             effects: reverb, chorus, spatial effect, equalizer, routing and output stage
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
`Voice`, `Allocator`, `Timing`, `State`, `Render`, `Chart` (`synthtests.cpp`, `charttests.cpp`), `Fx`
(`fxtests.cpp`), `Golden`. The voice tests render the dry mode (a linear mix); the `Fx` tests the chip path, and
take an effect's wet signal as the difference of two renders that differ only in the send.

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
