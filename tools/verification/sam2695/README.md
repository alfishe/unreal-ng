# libsam2695 verification

libsam2695 (`core/src/3rdparty/sam2695/`, design:
[tdd-libsam2695.md](../../../docs/inprogress/2026-10-03-zx-multisound/tdd-libsam2695.md)) is our own SAM2695
synthesizer. There is no SAM2695 to compare against sample by sample, and its CleanWave sample ROM is not
available, so the library plays SoundFont 2 banks. This folder checks the part that can be checked against a
reference: the SF2 semantics, against [FluidSynth](https://www.fluidsynth.org/), the reference SF2 renderer
(installed on the development machine with `brew install fluid-synth`). It also loads every bank of the
development corpus. Neither script is part of the build; the library's own suite (`sam2695tests`) freezes the
results.

## Layout

| Path | What |
|---|---|
| `compare.py` | FluidSynth comparison: writes a synthetic bank and a MIDI scenario, renders both, checks per note |
| `bankcorpus.py` | bank corpus check: every `.sf2` / `.sf3` under a folder through a GM test file |
| `sf2write.py` | a minimal SoundFont 2.04 writer (independent of the library's C++ test bank builder) |
| `smfwrite.py` | a minimal Standard MIDI File writer |
| `out/` | renders, the generated bank and MIDI files, `bankcorpus.jsonl` (git-ignored) |

Both scripts drive `sam2695render`, the library's command-line renderer (an `EXCLUDE_FROM_ALL` target):

```sh
tools/build/build.sh sam2695render          # -> cmake-build-agent-release/bin/sam2695render
./compare.py                                # exit status 0 when every check passes
./bankcorpus.py --root <folder with banks>  # default: testdata/midi of the main checkout
```

Python 3 with `numpy` and `soundfile`; FluidSynth 2.x on the `PATH`.

## FluidSynth comparison

Both synthesizers render at the chip's internal rate, 37 500 Hz, with no polyphony limit (`--polyphony 64`;
the chip's 38 voices are not the subject here). The voice scenario runs with reverb and chorus off: FluidSynth
`-R 0 -C 0`, libsam2695 in its dry render mode (`sam2695render --dry`: the voice mix, no effects, no
equalizer, no clipping). The scenario puts every MIDI event on
FluidSynth's 64-sample block grid (one SMF tick = 64 samples, `smfwrite.py`), so both receive each event at the
same sample. The synthetic bank has one preset per feature: a looped 440 Hz sine, an ADSR envelope, static
attenuation, pan, a low-pass filter, a tremolo (mod LFO to volume), a vibrato, loop-until-release with a
half-level tail, key-scaled decay and the modulation envelope on pitch.

Last run (2026-10-04, FluidSynth 2.6.1): **47 checks, 0 failed** (40 voice checks, 7 effects checks; 5
informative rows).

| Check | libsam2695 | FluidSynth | Tolerance | Note |
|---|---|---|---|---|
| gain offset (vel 127, CC 7 / CC 11 = 127) | | +1.91 dB | 0.05 dB | D2 + D4 |
| onset of each of 17 notes | 37 samples + 1 | 64-65 samples (130 for quiet notes) | within one block | D3 |
| velocity 100 / 64 / 32 vs 127 | -4.15 / -11.91 / -23.95 dB | -4.10 / -11.90 / -23.91 dB | 0.1 dB | |
| static attenuation 120 cB | -4.80 dB | -4.80 dB | 0.1 dB | E-mu 0.4 scaling in both |
| low-pass at 523 Hz, 440 Hz tone | -1.84 dB | -1.76 dB | 0.1 dB | |
| pan -300 (L/R) | 9.76 dB | 9.77 dB | 0.05 dB | sin / cos law |
| pitch at keys 69 / 81 / 57 | 0.000 cents | | 0.5 cents | |
| attack 0.5 s: time to -1 dB of peak | 0.451 s | 0.449 s | 10 ms | |
| attack + hold end | 0.624 s | 0.622 s | 10 ms | |
| decay slope | -99.8 dB/s | -96.0 dB/s | 1 % of 100/96 x FluidSynth | D1 |
| sustain level 300 cB | -30.3 dB | -29.1 dB | 0.1 dB of 100/96 x FluidSynth | D1 |
| release slope | -200.0 dB/s | -191.9 dB/s | 1 % of 100/96 x FluidSynth | D1 |
| tremolo depth (60 cB, 2 Hz) / first minimum | 12.31 dB / 0.178 s | 12.31 dB / 0.176 s | 0.3 dB / 10 ms | |
| vibrato swing (50 cents, 5 Hz) | 98.1 cents | 97.8 cents | 3 cents | |
| loop until release: tail start / tail level / sample end | 765 samples / -19.12 dB / 0.120 s | 765 / -18.19 dB / 0.121 s | 128 samples / 0.6 dB / 3 ms | D1, D3 |
| key-scaled decay (keys 48 vs 72) | 2.00 | 1.99 | 0.02 | |
| mod envelope to pitch, 0.25 s / 0.6 s into a 0.5 s attack | 0.0 cents from FluidSynth | | 6 cents | convex attack |

### Effects

The effects scenario (`effects.mid`) plays a 0.1 s noise burst at CC 91 = 127, 64 and 0 (3.5 s apart), then a
0.4 s burst at CC 93 = 127, 64 and 0, with both synthesizers' reverb and chorus on; the chip runs its full
output path with the equalizer and the spatial effect off (NRPN 375Fh = 30h at the start; FluidSynth ignores
it). Each file is rendered twice, effects on and off; the difference is the wet signal.

| Check | libsam2695 | FluidSynth | Tolerance | Note |
|---|---|---|---|---|
| peak of the render below the soft-clip knee (the path is linear) | 0.39 | | < 0.75 | |
| reverb tail, CC 91 127 vs 64 | 5.91 dB | 5.94 dB | 0.1 dB of each other and of 20 log(127/64) | linear send, SF2 8.4.8 |
| reverb tail at CC 91 = 0 vs 127 | -87.6 dB | -39.3 dB (info) | < -80 dB (ours) | D5 |
| chorus wet, CC 93 127 vs 64 | 5.95 dB | 5.99 dB | 0.15 dB of each other and of 20 log(127/64) | linear send, SF2 8.4.9 |
| chorus wet at CC 93 = 0 vs 127 | -209 dB | -151 dB | < -80 dB | |
| reverb RT60 (T20), CC 91 = 127 | 2.37 s | 2.26 s | info | D5 |
| reverb tail (first 0.5 s) vs the dry burst | -23.8 dB | -21.9 dB | info | D5 |
| chorus wet vs dry, CC 93 = 127 | -16.9 dB | -7.8 dB | info | D5 |

### Expected deviations

- **D1 - envelope decibel scale.** SF2 2.04 defines decay and release as the time of a "100 %" change, 100 dB,
  and the sustain level in centibels. FluidSynth runs its volume envelope on a 96 dB scale, so its slopes are
  96/100 of ours and its sustain levels 4 % shallower in dB. The times (when each stage ends) agree.
- **D2 - FluidSynth's filter gain.** FluidSynth's low-pass is always on and scales by 1/sqrt(q) even without
  resonance: +1.5 dB on every note. libsam2695 keeps the DC gain at 1 until the resonance exceeds Butterworth
  (q > 1), then scales like FluidSynth, so engaging the filter never changes the level.
- **D3 - block quantization.** FluidSynth applies events and advances envelopes per 64-sample block: its 1 ms
  default delay becomes one or two whole blocks and a release starts on a block boundary. libsam2695 starts a
  note and a release on their own sample and runs the volume envelope per sample.
- **D4 - modulator curves.** FluidSynth reads its concave / convex curves from 128-step tables by truncation;
  at value 127 its velocity, CC 7 and CC 11 attenuations are 0 where the continuous SF2 curve gives 1.36 cB
  each (0.41 dB together). Elsewhere the curves agree within 0.05 dB.

- **D5 - effect algorithms.** The two reverbs and choruses are different designs: libsam2695 maps the chip's
  programs onto a Dattorro plate tank and a two-tap modulated delay (library README "Effects and output"),
  FluidSynth runs its own FDN reverb (room size 0.2) and a three-voice chorus at level 2. Decay times and wet
  levels are reported, not compared; the send semantics (linear in CC 91 / 93, the SF2 default modulators) are
  compared and agree. At CC 91 = 0 FluidSynth's reverb still receives a little of the burst (39 dB below
  CC 91 = 127; its send ramps at the voice start); libsam2695 sends nothing (what it shows is the previous
  burst's tail, 3.5 s later).

Shared conventions the comparison confirms (both follow the E-mu hardware, not the letter of the
specification): the bank's static initialAttenuation counts 0.4 x (modulator contributions count fully), the
velocity / CC 7 / CC 11 curve is -20/96 x log10((1 - x)^2), the modulation envelope attack is convex, and the
envelope delay holds the whole voice (the sample starts with the attack).

## Bank corpus

`bankcorpus.py` loads every bank under `--root` (the development corpus `testdata/midi/`, git-ignored, index in
[testdata/midi/README.md](../../../testdata/midi/README.md)) and renders `gm-test.mid` through it with the
chip's default polyphony and its full output path (effects, equalizer, soft clipping). Each bank gives one JSON
line: the load result (or the loader's refusal reason),
presets, samples, loader warnings, sm24, NaN and denormal counts of the output, the peak, load and render time.
The script exits non-zero when any output holds a NaN or a denormal. The last run's results are in
[tdd-libsam2695.md](../../../docs/inprogress/2026-10-03-zx-multisound/tdd-libsam2695.md) §10.
