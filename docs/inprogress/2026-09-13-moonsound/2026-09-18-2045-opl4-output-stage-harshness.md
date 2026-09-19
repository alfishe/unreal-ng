# OPL4 output stage — why the HAPERT accordion still sounds harsh

**Date:** 2026-09-18 · **Status:** fixed and fenced (§5, §6)
**Scope:** `tools/poc/015-opl4-synthesis` (libopl4) render layer and FM tables
**Related:** [2026-09-15-2114-opl4-ymfm-verification-findings.md](2026-09-15-2114-opl4-ymfm-verification-findings.md) §2.7 (FM key-on / envelope / NTS fixes, commit `86e621cd`), [2026-09-13-0217-opl4-core-tdd.md](2026-09-13-0217-opl4-core-tdd.md) D2, §4.3, §8.2–8.3

## 1. Symptom

After the §2.7 synthesis fixes, MFM sample 4 / HAPERT's accordion (MFM
instrument 3: FB7-FM, modulator TL32, AR2/AR3) still sounded harsh in the
emulator compared with the original: a gritty top end on a voice that should be
a soft, reedy "distorted sine".

## 2. Method

- **Synthesis, native rate.** The captured register stream
  (`scratch/mfm4-hapert-capture-melody1-hapert-deep_*.csv`) is replayed through
  in-tree `Opl4Fm`, the ymfm OPL3 core and Nuked-OPL3 with channel 13 soloed at
  register level, then compared by spectrum: harmonic levels H1..H40 and
  inharmonic energy per band.
- **Full chip, as the emulator runs it.** The same stream goes through
  `opl4::Opl4` (`EnableSplitStreams(true)`, `RenderSplit`, 44.1 kHz output,
  3.5 MHz host axis, 50 Hz frames) in each render mode and with and without
  `BoardAnalog`.
- **Pure-sine THD+N.** One unmodulated carrier (a perfect sine at the native
  rate) through the full chip at 438 Hz, 1.75 kHz and 4.23 kHz. This isolates
  the output stage from the synthesis.

Tools (scratch, zxm-moonsound repo): `scratch/fbnoise/{wave3way,chiprender,sinetest,natsine}.cpp`.

## 3. Findings

### 3.1 The synthesis is clean

| Accordion, native FM rate | H2..H6 (dB re H1) | inharmonic 4–22 kHz |
|---|---|---|
| libopl4 | −3.6 −8.0 −12.1 −16.1 −19.9 | −54…−56 dB |
| ymfm | −3.7 −8.0 −12.2 −16.1 −19.9 | −54…−57 dB |
| Nuked | −3.9 −8.5 −12.9 −17.0 −20.9 | −55…−57 dB |

Pure-sine THD+N at the native rate: libopl4 −50.6 dB, ymfm −54.3…−55.3 dB,
Nuked −55.4…−62.8 dB (see §3.5 for the 3–4 dB gap).

### 3.2 The output stage adds 15–40 dB of distortion

Pure FM sine through the full chip (THD+N):

| Path | 438 Hz | 1.75 kHz | 4.23 kHz |
|---|---|---|---|
| native synthesis | −50.6 | −50.6 | −50.9 |
| **Authentic (HoldDrop) — emulator default** | −35.8 | −23.8 | **−16.1** |
| HiFi, current resampler | −29.8 | −17.8 | **−9.8** |
| HiFi, direction fix only (§4 fix 1a) | −50.8 | −50.1 | −38.9 |
| HiFi, true polyphase (§4 fix 1) | −50.6 | −51.0 | −53.0 |

Accordion (inharmonic energy, dB re tone):

| Path | 0–4k | 4–8k | 8–12k | 12–16k | 16–22k |
|---|---|---|---|---|---|
| Authentic (HoldDrop) | −38.4 | −33.7 | −38.9 | −42.7 | −41.9 |
| HiFi, current | −33.7 | −27.6 | −33.0 | −37.0 | −36.1 |
| HiFi, true polyphase | −45.6 | −57.4 | −57.6 | −56.5 | −57.3 |
| libopl4 native | −42.4 | −56.4 | −56.6 | −55.9 | −55.1 |
| Nuked native | −42.1 | −55.2 | −56.8 | −55.8 | −55.1 |

The harsh top end is the 20–25 dB of extra inharmonic energy the output
stage puts above 4 kHz.

### 3.3 Root cause 1 — HoldDrop is sample-time jitter

`Authentic` presents the most recent 49 516.4 Hz FM sample at each 44.1 kHz
tick (7 FM samples dropped per 57 output ticks, §4.3). Each output sample is
misplaced in time by 0…20 µs (rms ≈ 5.8 µs), an error of roughly
2π·f·Δt. That predicts −36 dB at 438 Hz and −16 dB at 4.2 kHz, which matches
the measurements to 0.3 dB: the implementation does exactly what D2 specifies.
The problem is the premise. D2's "authentic silicon" rests on a die-analysis
claim and ymfm's approximation (ymfm itself flags YMF278B mixing as needing
verification), not on a hardware recording. openMSX, the hardware-validated
MoonSound reference, band-limits its FM output. A 4 kHz FM tone at −16 dB THD+N
would make the real card audibly gritty, which contradicts how MoonSound FM
sounds.

### 3.4 Root cause 2 — `PolyphaseResampler` is not polyphase, and its blend runs backwards

`PolyphaseResampler::Convolve` applies one fixed Kaiser FIR, then blends the
FIR output at the newest input position (`acc0`) with the one a sample older
(`acc1`):

```cpp
return acc0 * (1.0 - frac) + acc1 * frac;
```

`_phase` advances with output time, but this blend moves toward the **older**
sample, so the fractional position is time-reversed inside every input
period. That gives a sawtooth timing error about twice HoldDrop's, which is
the constant 6 dB penalty HiFi shows. Flipping the blend alone (fix 1a) is
not enough: linear interpolation between FIR outputs still lets images of
high harmonics alias back (−39 dB at 4.2 kHz). The same class resamples
the 44.1 kHz chip stream whenever the host rate is not 44.1 kHz, so PCM is
affected there too.

### 3.5 Root cause 3 — the board filter is a no-op on the split path

`Opl4Render::ProcessGroup` (the `RenderSplit` path the emulator uses) runs
`GroupStage::analog`, but `Opl4Render::Configure` only configures `_analog`,
the mixed-path filter. The per-group `BoardAnalog` instances keep their
identity default coefficients, so `BoardAnalog=1` changes nothing in the
emulator. The ZXM-MoonSound board's RC pole at 4.08 kHz plus the Sallen-Key
low-pass (TDD M2) would soften exactly the band where the reducer's
distortion lives.

### 3.6 Minor — native sine purity

libopl4's native sine is 3–4 dB less pure than ymfm's (third harmonic at −54 dB
against −60 dB). The first suspicion, the exponential table's resolution, was
wrong: the 0.09375 dB attenuation index already uses the table at full
resolution. The cause is the sine table itself (§6): it was sampled at
`i·π/512` instead of the silicon's half-step `(i + 0.5)·π/512`.

### 3.7 Minor — mixed `Render()` path anomalies

With the mixed `Render()` API (not used by the emulator), the harness saw HiFi
return silence and `BoardAnalog` drop the level by about 40 dB. Both were real
bugs (§6): unbounded staging in `ProcessSplit`, and a board filter whose two
stages were high-pass.

## 4. Fixes

1. **True polyphase resampler** (`PolyphaseResampler`). `Configure` builds a
   table of 512 fractional-offset kernels: the Kaiser-windowed sinc with its
   centre `m/2 + (1 − f)` input samples behind the newest, each row normalised
   to unity DC. `Convolve` blends the two nearest rows, and output time
   advances with phase. (Fix 1a, the direction flip alone, is only a data
   point.)
2. **Band-limited FM by default, in the emulator.** `[MOONSOUND] RenderMode`
   now defaults to `hifi`; `authentic` (HoldDrop) is opt-in until a
   ZXM-MoonSound recording settles D2. libopl4's own `Opl4Config` default stays
   `Authentic`: the PoC contract (bit-exact 44.1 kHz chip stream, unity bypass,
   per-frame vectors, zero-window TTD restore) is built on it, and hosts choose
   the mode explicitly.
3. **Per-group board filters are configured** alongside `_analog`.
4. **Half-step FM sine table** (§3.6's real cause, see §6): the table is
   `round(4096·sin((i + 0.5)·π/512))`, matching the silicon and ymfm. The
   index-resolution explanation in §3.6 was wrong.
5. **Found while fixing** (§6): the board filter's two stages were high-pass;
   the mixed `Render()` HiFi staging bug; `Configure` dropping user settings.
6. **Fences:** see §6.

## 5. Results

| Measurement | Before | After |
|---|---|---|
| FM sine through the chip, HiFi, 4.23 kHz (THD+N) | −9.8 dB | **−74.0 dB** |
| FM sine through the chip, HiFi, 438 Hz | −29.8 dB | −55.0 dB |
| Resampler alone, 4.23 kHz sine 49 516.4 → 44 100 | −9.8 dB | −77.0 dB |
| Native FM sine purity (libopl4) | −50.6 dB | **−55.0…−56.3 dB** (ymfm −54…−55, Nuked −55…−63) |
| Board filter gain at 438 Hz / 4.23 kHz, split path | 0.00 / 0.00 dB (no-op) | −0.05 / −3.03 dB (analog: −0.05 / −3.03) |
| Mixed `Render()` in HiFi | silence, runaway frame count | exact 882 frames/frame, −74 dB THD+N |
| HAPERT accordion, inharmonic energy 4–22 kHz, emulator default path | −34…−43 dB (Authentic) | **−57…−58 dB** (HiFi); −62…−68 dB with `BoardAnalog=1` |
| Same, Nuked-OPL3 at native rate (reference) | — | −55…−57 dB |

`Authentic` still measures −16.1 dB at 4.23 kHz: it is HoldDrop, unchanged and
now opt-in.

## 6. Fix log

- **Resampler** (`opl4render.{h,cpp}`): the table-driven polyphase kernel
  replaces the single FIR plus backwards linear blend.
- **Emulator default** (`core/src/emulator/config.cpp`, `platform.h`): the
  RenderMode default is `hifi`, and `authentic` is read explicitly. The
  integration TDD's sample ini is updated.
- **Board filter** (`BoardAnalog`): both stages were **high-pass**. The RC
  stage had `b1 = −b0` (a zero at DC) and the Sallen-Key numerator was
  `k², −2k², k²`, so on the mixed path the tone lost 20–40 dB and a resonance
  near 15–16 kHz dominated. A correct bilinear low-pass cannot represent the
  27.7 kHz Sallen-Key corner above the 44.1/48 kHz Nyquist: its forced Nyquist
  zero costs up to 35 dB at 20 kHz, and pre-warping does not help. The filter is
  now a **64-tap minimum-phase FIR** designed at `Configure` from the analog
  magnitude (RC × Sallen-Key) by cepstral folding (radix-2 FFT, N = 1024). It
  matches the analog magnitude to 0.003 dB over 0–20 kHz at 44.1–192 kHz.
- **Per-group board filters**: `_groupFm.analog` / `_groupPcm.analog` are
  configured (they used to keep identity coefficients).
- **Mixed `Render()` in HiFi** (`ProcessSplit`): the stage FIFOs were grown to
  worst-case capacity by `resize` and never trimmed, so the next call counted
  the empty capacity as staged output and emitted silence while the backlog
  grew. The stages are now trimmed to what was produced.
- **`Configure` kept user settings**: `SetRenderMode` / `SetQuality` re-ran
  `Opl4Render::Configure`, which reset BoardAnalog, punch and room. They are
  now stored and re-applied at the new rate.
- **Sine table** (`fmtables.h`): half-step sampling. The old table
  (`i·π/512`) repeated the peak and zero samples at every quadrant fold of
  `SineOf` and added a −54 dB third harmonic.
- **PoC fences** (6169 checks, 0 failures; each new check fails on the old
  sources): `TestResamplerPurityAndDirection` (−77 dB, monotonic ramp),
  `TestChipSinePurityPerMode` (HiFi < −45 dB on both render paths with exact
  frame counts; Authentic pinned to the HoldDrop band), `TestBoardAnalogOnSplitPath`
  (±0.15 dB of the analog response, and the setting survives a mode/quality
  change), `CompareNativeSinePurity` (H2–H5 < −54 dB; old −53.7 dB).
- **Golden oracle** regenerated: 25 FM/mix digests (the sine table), no
  PCM-only digest; cosim-ymfm 6/6.
- **Emulator tests** (MoonSound 35 pass + the 3 pre-existing fixture/loader
  failures; 749 other sound/audio/TTD tests pass):
  `Canary_FmWaveformSelect` and `TTD_SaveNeutralAndRestoreExact` now pin
  `Authentic`. They test raw chip waveforms (HiFi's DC blocker strips the
  half/rectified sines' DC) and the zero-window bypass restore of TTD TDD §7.2.
  New `TTD_RestoreHiFi_ConvergesAfterFilterWindow` covers the default path.
  After a HiFi restore the resampler phase restarts, so the output grid shifts
  by a sub-sample amount. After the 256-sample window, the replay's magnitude
  spectrum is as close to the uninterrupted run as that run is to itself at
  other time offsets (0.99943 vs worst 0.99949), and the level is equal
  (−0.002 dB). Chip state and hash stay exact.

Open: D2 (is HoldDrop real?) still needs a ZXM-MoonSound hardware recording of
a high FM sine; `Authentic` remains available for that comparison.
