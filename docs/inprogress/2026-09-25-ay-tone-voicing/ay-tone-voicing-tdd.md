# AY Tone Voicing — Technical Design

**Status:** design reviewed twice, ready for phase 1 implementation and testing (see §13) · **Date:** 2026-09-25 · **Verified against:** `af072f83`
**Scope:** AY-3-8910 / YM2149 output, including the SSG half of TurboSound FM (TSFM)
**Related:** `45812176` (the AY DC-filter replacement that caused this), `4c49fb6` (the reference sound),
[TSFM design](../2026-09-10-turbosound-fm/tsfm-tdd.md), [multirate plan](../2026-08-17-audio-sync/multirate-core-implementation-plan.md)

---

## Proposal under review

The design reviews a proposal from an earlier session, translated with commit IDs, code locations
and a reproduction of each number: **[proposal.md](proposal.md)**. In short: keep the 5 Hz AY
coupling from `45812176`, and add a switchable EQ (HPF1 64.2 Hz + peak 106.9 Hz / +3.06 dB / Q 1.0)
that recreates the old `FilterDC` bass balance, before punch, for AY/SSG only.

The only commit that changes the AY bass band between `4c49fb6` and HEAD is `45812176`. For A/B
listening, `d90421bb` (its parent) is the closest build with the old filter.

## 0. Verdict: does the proposal make sense?

**Yes, with four changes.** The proposal: keep the physical 5 Hz output coupling in the chip model, and
add a separate, switchable "voicing" stage that recreates the tonal balance of `4c49fb6` (1st-order
high-pass at 64.2 Hz plus a +3.06 dB peak at 106.9 Hz, Q = 1.0), with presets "Flat" and "Legacy".
The numbers in it were checked independently (§2.2) and they hold.

Changes to the original suggestion:

| # | Original suggestion | Change | Why |
|:--|:--|:--|:--|
| 1 | Put voicing inside `AudioCharacterChain` | Separate `FilterVoicing` stage owned by `SoundManager`, placed **before** the chain | The character chain runs only in HQ mode and is reset every time HQ comes back. Voicing sets tonal balance, not an "enhancement": the bass must not change when the user toggles *Sound HQ* or turbo mode ends. |
| 2 | Filter at core rate, precision not specified | Coefficients and state in `double` | At 192 kHz a 64 Hz pole sits at 0.998; `float` state gives audible noise and drift there. |
| 3 | Settings mechanism not specified | A typed sound setting (`ay_voicing`), **not** a `FeatureManager` feature | Features are on/off performance/debug toggles. The WebAPI cannot set a feature's *mode*, and `features.ini` is written but never loaded (§6). |
| 4 | Preset names "Flat" / "Legacy 4c49fb6" | IDs `flat` / `classic` (`legacy` accepted as an alias), `tv` reserved | A commit hash means nothing to users; the ID must stay stable after the curve is tuned. |

**Default: `classic`.** The complaint is that the current bass is too heavy; users (and the punch
presets) were tuned against the old balance. `flat` stays one click away for "like real hardware".

## 1. Glossary

| Term | Meaning here |
|:--|:--|
| **Voicing** | A fixed EQ curve that sets the overall tonal balance, like the tone controls on an amplifier. It does not react to the music. |
| **High-pass filter (HPF)** | Lets high frequencies through and turns down low ones. "HPF at 64 Hz" = 64 Hz is turned down by 3 dB, 32 Hz by about 7 dB. |
| **Peaking EQ** | Boosts a band around one frequency. "+3 dB at 107 Hz, Q = 1" = a wide bump centred on 107 Hz. |
| **DC / infrasonic "thumps"** | The AY DAC only outputs positive voltages. Every volume change the player makes (usually once per 50 Hz frame) moves the average level. That is a step at very low frequency, heard as a thump on headphones or a subwoofer. |
| **Coupling capacitor** | The capacitor between the chip and the line output. It removes the average (DC) level slowly. The emulator models it as a 5 Hz one-pole high-pass (`FilterDCBlocker`). |
| **Core rate** | The sample rate of the mixed emulator audio (44.1–192 kHz, see `SoundManager::targetCoreRate`). |
| **Generator rate** | The rate the AY generators run at inside the chip model: 1.75 MHz / 8 = 218.75 kHz. |
| **HQ / LQ** | The `soundhq` feature. HQ = FIR decimation + character chain (punch/room); LQ = boxcar, no chain. |
| **Punch / Room** | The existing `AudioCharacterChain` stages: transient emphasis and headphone crossfeed. HQ-only. |

## 2. Background

### 2.1 What changed

Until `45812176` the AY output went through `FilterDC`: *x minus the mean of the last 1024 samples* at
218.75 kHz (a 4.68 ms window). That filter removed DC, and it also shaped the bass. `45812176`
replaced it with a 5 Hz RC high-pass, which is acoustically correct (a model of the coupling
capacitor) but takes away the shaping people got used to. It is now at
`core/src/emulator/sound/chips/soundchip_ay8910.h` (`OUTPUT_HIGHPASS_HZ = 5.0`).

### 2.2 Independent check of the numbers

Frequency responses computed analytically (script in the session scratchpad, reproduced in the
`FilterVoicing` tests, §9). "Legacy" = `x - MA1024` at 218.75 kHz; "Flat" = 5 Hz HPF; "Classic" =
64.2 Hz HPF1 + 106.9 Hz / +3.06 dB / Q 1.0 peak.

| Frequency | Legacy | Flat (today) | Classic (proposed) |
|--:|--:|--:|--:|
| 30 Hz | −7.33 dB | −0.12 | −7.23 |
| 41.2 Hz (E1) | −4.69 | −0.06 | −4.80 |
| 55 Hz (A1) | −2.50 | −0.04 | −2.74 |
| 100 Hz | +1.20 | 0.00 | +1.50 |
| 130 Hz | +1.96 | 0.00 | +1.69 |
| 160 Hz | +1.81 | 0.00 | +1.17 |
| 300 Hz | −0.39 | 0.00 | +0.25 |
| 1 kHz | +0.27 | 0.00 | +0.02 |
| **Max boost** | +2.00 @ 139 Hz | — | +1.82 @ 117 Hz |

- The "+4.7 dB at 41 Hz" and "hump of about 2 dB at 110–160 Hz" claims check out exactly.
- Fit error without 1/3-octave smoothing: mean 0.34 dB, max 1.5 dB. That agrees with the
  claimed 0.39 / 1.3 dB after smoothing.
- Above ~210 Hz the legacy filter has comb ripple (±0.2–0.5 dB, nulls of the moving average at
  multiples of 213.6 Hz). Classic does not reproduce that ripple. That is intended: the ripple
  was an artefact, not part of the sound people remember.
- Peak level on a unipolar square wave (full scale = 1.0, 1.5 s steady state):

  | Tone | Legacy | Flat | Classic |
  |--:|--:|--:|--:|
  | 41.2 Hz | 0.999 | 0.594 | 1.014 |
  | 110 Hz | 0.972 | 0.536 | 0.953 |
  | 220 Hz | 0.514 | 0.518 | 0.621 |

  So Classic puts back the legacy edge overshoot. That matters for headroom (§4.8).

**Side note:** the `45812176` commit message says the old filter was "−21 dB at 50 Hz, −10 dB at
100 Hz". The actual steady-state response is −3.2 dB at 50 Hz and +1.2 dB at 100 Hz. This does not
affect the design; noting it so nobody re-derives the curve from that message.

### 2.3 Where AY audio flows today

```
SoundChip_AY8910::updateMixer()   @218.75 kHz   (per chip; TSFM: the SSG half, same class)
   └─ FilterDCBlocker 5 Hz L/R                  ← physical coupling, stays
   └─ decimator (HQ: FIR, LQ: boxcar) → core rate
ITurboSoundDevice::getChipBuffer(0/1)  int16 stereo @ core rate
   └─ _ayChain0 / _ayChain1  (AudioCharacterChain: punch → room)   HQ only, reset on HQ return
SoundManager mixer (registry mute/solo/volume, saturating int16 add)
   └─ recording tap / analyzer tap        (post-mix, pre-mute, pre-DRC)
   └─ DRC resampler → device
```

Beeper, Covox and the FM half of TSFM never went through the legacy `FilterDC`, so they are out of
scope.

## 3. Goals and non-goals

**Goals**

- G1. With `classic`, AY/SSG bass balance matches `4c49fb6` on test signals (§9). In 1/3-octave
  bands that contain a harmonic, the match is within **±0.75 dB below 200 Hz** and **±1.5 dB from
  200 Hz to 1 kHz**. Infrasonic thump energy is back to the legacy level (within 1 dB). The wider
  tolerance above 200 Hz is intentional: the old filter's comb ripple lands on harmonics there, and
  `classic` deliberately does not copy it (§2.2).
- G2. `flat` is **bit-identical** to current master (true bypass).
- G3. Same voicing in HQ and LQ. Toggling HQ or leaving turbo mode does not change the bass.
- G4. Settable per emulator at runtime from Qt, WebAPI, CLI, Lua, Python and MCP (videowall: from the active emulator's config file, §8.2),
  persisted for the GUI user, configurable in `unreal.ini` for headless runs.
- G5. Switching presets while music plays does not click.
- G6. Correct at every supported core rate (44.1–192 kHz).

**Non-goals**

- Changing the 5 Hz coupling HPF in the chip model.
- Voicing for beeper, Covox, FM, General Sound or MoonSound (possible later; the class is reusable).
- Retuning the punch presets (tracked as an open question, §12).
- Making the "TV speaker" curve final. The ID is reserved and the curve is decided by listening
  (phase 3).

## 4. Core design

### 4.1 `FilterVoicing` (new, `core/src/common/sound/filters/filtervoicing.h`)

Header-only, same style as `filterdcblocker.h`. One instance processes one stereo stream.

```cpp
/// Fixed tonal-balance EQ for a chip's output (AY/SSG today). Presets are
/// target curves, not effects: a first-order high-pass followed by a peaking
/// EQ, both in double (the 64 Hz pole is 0.998 at 192 kHz - float state drifts).
class FilterVoicing
{
public:
    enum class Preset : uint8_t { Flat = 0, Classic, Tv, COUNT };

    void setup(double sampleRate);            // re-derives coefficients, keeps state
    void setPreset(Preset preset);            // immediate; SoundManager handles crossfade
    Preset preset() const;
    bool isBypass() const;                    // Flat -> true, process() is a no-op
    void reset();                             // clear state
    void processInt16(int16_t* interleaved, size_t frames);   // saturating

    static const char* presetId(Preset);      // "flat" / "classic" / "tv"
    /// Accepts "legacy" for Classic. Hidden presets (visible == false, e.g. "tv"
    /// until phase 3) are REJECTED unless allowHidden - every user-facing input
    /// (ini, WebAPI, CLI, Lua, Python, QSettings) calls it with the default
    static bool parsePreset(std::string_view, Preset& out, bool allowHidden = false);
};
```

Profiles are table-driven: each row holds an ID, aliases, UI text, an optional HPF (order 1/2),
an optional peak and an optional LPF (order 2, reserved for `tv`), plus a `visible` flag. Adding a
profile means adding one row. The full field list is in [proposal §7.1](proposal.md#71-profile-model).
Curve table (`Tv` stays `Flat`-equivalent and hidden until phase 3):

| Preset | HPF (order 1) | Peak f0 | Peak gain | Peak Q |
|:--|--:|--:|--:|--:|
| `flat` | — (bypass) | — | — | — |
| `classic` | 64.2 Hz | 106.9 Hz | +3.06 dB | 1.0 |
| `tv` | TBD (phase 3); listening starting point HPF2 ~130 Hz Q 0.7 + LPF2 ~6 kHz | | | |

Coefficients (bilinear transform; with f ≪ fs no pre-warping is needed, and the RBJ peak formula
is exact anyway):

```
HPF1:  K = tan(pi*fc/fs)
       b0 = 1/(1+K);  b1 = -b0;  a1 = (K-1)/(K+1)
       y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1]          <- note the MINUS on a1

Peak:  A = 10^(g/40);  w0 = 2*pi*f0/fs;  alpha = sin(w0)/(2Q)
       a0 = 1 + alpha/A                               (normaliser, not a coefficient)
       b0 = (1 + alpha*A)/a0;  b1 = -2cos(w0)/a0;  b2 = (1 - alpha*A)/a0
       a1 = -2cos(w0)/a0;      a2 = (1 - alpha/A)/a0
       y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
```

Sign convention: denominators are `1 + a1*z^-1 + a2*z^-2`, so the feedback terms are
**subtracted** (the scipy/RBJ convention). With HPF1 at 64.2 Hz / 44.1 kHz, `a1 ≈ -0.9909`, so the
feedback adds `+0.9909*y[n-1]`. A sign error there turns the filter into a low-pass at Nyquist, and
`ClassicMatchesDesignCurve` catches it.

Structure: HPF1 → peak biquad in transposed direct form II, `double` state per channel, the same
denormal flush as `FilterDCBlocker` (`|y| < 1e-30 → 0`). Output is rounded and clamped to int16.

The two "simpler" fits from the analysis (single HPF1 at 55 Hz; HPF2 at 39 Hz, Q 0.62) are **not**
presets. They go into the test file as documented reference points only.

### 4.2 Placement

```
getChipBuffer(0) ─► _ayVoicing[0] ─► (HQ) _ayChain0 ─► mixer
getChipBuffer(1) ─► _ayVoicing[1] ─► (HQ) _ayChain1 ─► mixer
```

- New members in `SoundManager`: `FilterVoicing _ayVoicing[2]`, next to `_ayChain0/1`.
- Runs in `handleFrameEnd` in the "Process AY" region, **before** the `chainsActive` block, when
  `!soundOff && !isSynthesisSuppressed()` and the device has chip buffers. It runs for the legacy AY pair
  and for TSFM (SSG buffers). The `getFmBuffer()` streams are not touched.
- It comes before punch, so the punch presets (tuned by ear on the legacy balance) see the same
  spectrum they were tuned on.
- It is **not** reset when HQ comes back (`_chainsBypassed` path): its state has been running
  continuously through LQ frames. It **is** reset in `SoundManager::reset()`.
- AY↔FM switch: **no hook needed.** The TurboSound device is created only in the `SoundManager`
  constructor (`soundmanager.cpp:71/75`, from `config.sound.turboSoundKind`). Switching kinds
  rebuilds the whole sound stack, so `_ayVoicing` starts fresh with it.

### 4.3 Frame and rate handling

- `SoundManager::applyCoreRate(rate)` (the core-rate change path that already calls
  `_ayChain0.setup(rate)`) also calls `_ayVoicing[i].setup(rate)`.
- `FilterVoicing::setup()` **keeps** filter state across a rate change, while
  `applyCoreRate()` resets the chains (their `setup()` clears the delay lines). This difference is
  intentional. Voicing's state is a few low-frequency samples that remain valid within a few
  samples at the new rate, and clearing them would pass a step (a click).
- Processing uses `samplesThisFrame` exactly like the chains. The voicing does no resampling and adds
  no latency, so A/V sync (`av-sync-pacing`) is unaffected.

### 4.4 Runtime preset change (thread safety and no clicks)

Today the Qt widget writes punch/room fields directly from the GUI thread while the emulation thread
reads them. That is a data race (it happens to be harmless for bools). Voicing coefficients must not
be torn mid-frame, so:

1. `SoundManager::setAYVoicing(Preset)` stores into `std::atomic<uint8_t> _requestedAYVoicing` (any
   thread) and returns.
2. At the top of the AY region in `handleFrameEnd` (emulation thread) it compares requested vs.
   active. On a change it keeps the old filter pair as `_ayVoicingOut[2]` and starts a crossfade.
3. **Warm the new filter, then crossfade over one frame** (~20 ms). A plain `reset()` is not
   enough. From zero state, HPF1 passes the current input level as a step that decays with
   τ = 1/(2π·64.2 Hz) ≈ 2.5 ms, so after 3 ms about 30 % is still there. With a bass note at
   several thousand LSB, that is an audible thump even under a small crossfade weight. Instead:
   - `SoundManager` keeps a copy of the previous **two** frames of **unvoiced** chip buffers (a
     small ring: `int16_t _ayVoicingHistory[2 chips][2 frames][MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS]`
     plus lengths). It is refreshed every frame while sound runs (one `memcpy` per chip, taken
     before voicing).
   - On a switch, the new filter is `reset()` and then **pre-rolled** over that history (oldest
     frame first), and its output is discarded.
   - Why two frames: HPF1 settles fast (τ ≈ 2.5 ms), but the peak biquad's poles have Q·A ≈ 1.42,
     so τ ≈ 4.2 ms. Simulated at 48 kHz, −6 dBFS, 41–110 Hz squares, max error of the crossfaded
     switch frame against an ideal continuous run: bare reset **525 LSB**, one frame of pre-roll
     **1.3 LSB** (too close to the ±2 LSB test bound once rounding is added), two frames
     **< 0.01 LSB**.
   - The switch frame is then processed by both filters, with output `(1-t)*old + t*new` and `t`
     linear over the frame.
   - If there is no valid history (first frame after `reset()`, after a core-rate change, or after
     a frame with sound off or synthesis suppressed), the pre-roll is skipped and the plain
     `reset()` behaviour applies. It is rare, and the device output is discontinuous at those
     points anyway.
   - `applyCoreRate()` marks the history invalid, because it holds samples at the old rate.
4. The next frame uses only the new filter.
5. `getAYVoicing()` returns the **requested** value (the atomic), not the active one. A WebAPI
   `GET` right after a `PUT` then shows the new value even though it takes effect at the next frame
   boundary. Tests that need the active value use a test-only `getActiveAYVoicing()`.

Cost: one extra filter pass for one frame per user click.

### 4.5 Gate interaction matrix

| Condition | Voicing | Chain (punch/room) | Notes |
|:--|:--|:--|:--|
| `sound` off | skipped (buffers zeroed upstream) | skipped | unchanged |
| synthesis suppressed (turbo, no audio request, not recording) | skipped | skipped | state kept; the HPF resettles within ms after resume |
| turbo LQ override, audio audible | **runs** | skipped | voicing is cheap; the bass stays consistent |
| `soundhq` off | **runs** | skipped | G3 |
| `soundhq` on | runs | runs | |
| preset `flat` | no-op (early return) | as configured | G2 |
| recording active | runs (it sits upstream of the tap) | as configured | recordings sound like playback |

### 4.6 TTD, snapshots, determinism

- Voicing state is **not** machine state. It is not saved in `.sna`/`.z80`/TTD, same as the chains.
- A TTD seek restores chip state and the sample phase (`adoptSamplePhase`). The voicing filter then
  sees a discontinuity, like the chains do today. HPF1 turns it into a short decaying blip, no worse
  than the 5 Hz blocker's own behaviour. No action.
- **Must verify:** any TTD / corpus test that compares *post-chain mixed output* bit-for-bit across a
  restore. `ttdcorpus_test` and `ttdtsfm_test` turn on `soundhq`, so the chains are already in their
  path. If they compare mixed audio, they already tolerate chain state and voicing adds nothing new.
  If one fails, the fix is to pin `flat` in that test (it tests restore, not tone). Do **not** add
  voicing to TTD.

### 4.7 Taps and consumers

- The recording and analyzer taps are post-mix, so they get voiced audio. This is intended:
  "what you hear is what you record". Consumers who want hardware-accurate output set `flat` (the
  MCP recipe for audio analysis says so, §7.4).
- `getStateAudioAY` (register state) is unaffected.
- Nothing outside `SoundManager` reads the per-chip buffers at HEAD. `getChipBuffer` is called only
  by the mixer and by `SoundManager::deviceBuffer()`, which only tests use. The combined
  `SoundChip_TurboSound::getAudioBuffer()` has no callers.
- **Exception: DSD native recording.** `SoundChip_TurboSound` feeds `_nativeTap` with
  `mixedLeft/Right()` at 218.75 kHz, **upstream** of voicing (`soundchip_turbosound.cpp`, render
  loop). `DsdEncoder` records from it (`core/recording/src/encoders/dsd/dsd_encoder.cpp`). So DSD
  recordings stay `flat` whatever the setting.
  - Phase 1: document it in the recording docs and the permanent voicing page as "DSD native
    capture is always hardware-flat".
  - Phase 2 (optional): a `FilterVoicing` instance on the native tap in `SoundChip_TurboSound`
    (it works at 218.75 kHz in double). It would pick up the active preset at frame start. It runs
    only while the tap is active, so it costs nothing otherwise.
  - PCM recording and the analyzer tap are post-mix and do follow the setting.

### 4.8 Headroom

Classic puts back the legacy edge overshoot (§2.2: 1.01 vs. 0.59 peak on a 41 Hz square). Master ran
with legacy peaks until `45812176` on 2026-09-24, so the gain staging has already lived with them.
Two checks:

1. Confirm no AY gain/volume constant changed between `4c49fb6` and HEAD that assumed the lower
   post-`45812176` peaks (`git log -p 4c49fb6..HEAD -- core/src/emulator/sound`).
2. Test (§9): three channels at max volume, in-phase E1 square, Classic. The int16 peak must be no
   higher than the legacy `FilterDC` peak on the same input **+ 0.25 dB**. Classic is about 0.12–0.13 dB
   above legacy at 41 and 55 Hz (1.014 vs 0.999 in §2.2), so "not above legacy" would always fail.
   The test drives the chips at about −6 dBFS, so neither path saturates and the comparison measures
   the filters, not the clamp.
3. At full scale the per-chip samples are `static_cast<int16_t>(c * INT16_MAX)` in the device
   (`soundchip_turbosound.cpp`), so there is no headroom above 1.0. In the worst synthetic case
   (three channels at max volume, in phase, E1) Classic peaks at 1.014, and the `processInt16` clamp
   engages by ≤ 0.13 dB, a negligible amount. The clamp is a safety net there, not the mechanism.
   (Side issue: the device's own cast does not clamp values above 1.0; that is pre-existing and not
   part of this work.)

### 4.9 Cost

Per sample per channel: 1 first-order section + 1 biquad ≈ 9 multiply-adds in double. At 48 kHz,
2 chips × 2 channels: ~1.7 M MAC/s, which is noise next to the 218.75 kHz generators. The
pre-roll history (§4.4) adds one `memcpy` of each chip buffer per frame (≤ 3.8 k stereo samples at
192 kHz) and about 2 × 2 × 2 × `MAX_SAMPLES_PER_FRAME` × 2 bytes of memory. The pre-roll itself
(two frames of filtering) runs only on a preset change. A benchmark
in `core/benchmarks/` (`BM_FilterVoicingClassic`, 1 s at 48 kHz and 192 kHz) tracks it.

## 5. Settings model

### 5.1 One setting, one precedence chain

The setting is `ay_voicing ∈ {flat, classic, tv}`. It resolves with the same "one pure function,
fixed priority" pattern as `targetCoreRate()`:

```
runtime set (WebAPI / CLI / GUI on this instance)
  > frontend-persisted user preference (unreal-qt QSettings, applied when the GUI creates the instance; the videowall has none, §8.2)
  > [SOUND] AYVoicing in the machine's unreal.ini
  > built-in default: classic
```

- **Core never reads QSettings.** Frontends apply their stored preference through the same public
  setter that automation uses.
- **`unreal.ini`**: new key `[SOUND] AYVoicing=classic|flat|tv` (+ `legacy` alias), parsed in
  `config.cpp` next to `CoreRate`, into `config.sound.ayVoicing`. Unknown value → `MLOGWARNING` +
  default, same as `CoreRate`. Shipped inis do **not** get the key (the built-in default covers
  them). That also avoids a whole-file diff on the CRLF `pentagon128k/unreal.ini`.
- Applied in the `SoundManager` constructor from config. Frontends override it right after
  creation.

### 5.2 Punch / room ride along (optional, same phase)

`AYPunch`, `AYRoom`, `BeeperPunch` are set today only from the Qt widget. They are neither persisted
nor automatable. Since voicing and punch are one panel and interact (§12), phase 2 moves all four
into a small value type:

```cpp
struct SoundCharacterSettings
{
    FilterVoicing::Preset ayVoicing = FilterVoicing::Preset::Classic;
    bool ayPunch = true;
    AudioCharacterChain::RoomMode ayRoom = AudioCharacterChain::RoomMode::Off;
    bool beeperPunch = false;
};
```

`SoundManager::getCharacterSettings()` / `setCharacterSettings()` apply it through the same
frame-boundary handoff (§4.4), which also fixes the existing punch/room data race. If this grows
the change too much it can split off. Voicing alone does not depend on it.

## 6. Feature gates: decision

**No new `FeatureManager` feature.** Reasons:

1. `FeatureManager` is for runtime on/off *performance and debug* toggles (categories debug /
   analysis / performance). Voicing is a user audio preference with 2–3 values.
2. Modes are half-wired. The CLI can `feature <x> mode <m>`, but `PUT /feature/{name}` only accepts
   `enabled`.
3. `features.ini` is **write-only**. `FeatureManager::loadFromFile` has no caller in core, Qt or
   videowall, so a feature would not persist anyway. (Side issue, tracked separately; not fixed here.)
4. A disable switch already exists: `flat` is an exact bypass (G2).

Existing gates still apply as shown in §4.5. The build needs no compile-time switch: the code is
tiny and always compiled.

## 7. Automation surface

It follows the existing `audio_rate` setting end to end. That setting is already wired through
every automation module, so each module gets one more branch next to it.

### 7.1 WebAPI (`core/automation/webapi/src/api/settings_api.cpp`)

- `GET /api/v1/emulator/{id}/settings` → `settings.audio.ay_voicing: "classic"` (next to
  `audio_rate`).
- `GET /api/v1/emulator/{id}/settings/ay_voicing` → `{name, value, description, allowed:
  ["flat","classic","tv"]}`. `tv` is listed only once it has a curve.
- `PUT /api/v1/emulator/{id}/settings/ay_voicing` `{"value":"flat"}` → 200 with the new value;
  unknown value → 400 + allowed list.
- Phase 2: `ay_punch` (bool), `ay_room` (`off`, `15db` … `1db`), `beeper_punch` (bool) the same way.
- Update `openapi_settings.inc` / `openapi_schemas.inc`. The MCP router (`search_api` / `invoke_api`)
  picks the new setting up from `openapi.json` with no MCP code change.

### 7.2 CLI (`cli-processor-settings.cpp`)

`setting ay_voicing [flat|classic|tv]`, listed in `settings` output next to `audio_rate`.

### 7.3 Lua / Python

Add `ay_voicing` wherever `audio_rate` is handled in `lua_emulator.h` / `python_emulator.h`
(string get/set).

### 7.4 Recipe and docs

- `.recipe/`: in the audio-capture/analysis recipe, one line: "set `ay_voicing=flat` for
  hardware-accurate spectra".
- `docs/emulator/design/audio/`: permanent page `ay-tone-voicing.md` once it lands (curve table,
  reasoning, precedence chain).

## 8. Frontend integration

### 8.1 unreal-qt

**UI** (`unreal-qt/src/debugger/widgets/audiosettingswidget.cpp`, AY group, above *Punch*):

```
Bass voicing:  [ Classic (softer bass, as in earlier versions) ▾ ]
               [ Flat (like real hardware line out)              ]
```

- Tooltip in plain language: "Real AY boards pass very low bass and the thump of volume changes
  straight to the output. Classic trims them the way earlier Unreal versions did. Recordings use
  the same setting."
- The combo stays **enabled when Sound HQ is off**, because voicing runs in LQ too. Only the
  punch/room controls depend on HQ.
- If voicing is `flat` and punch is on, a small hint label shows under Punch: "Punch was tuned for
  Classic voicing". It is informational and blocks nothing.
- Changing the combo calls `pSoundManager->setAYVoicing()`, which is thread-safe (§4.4), and
  writes QSettings.
- `loadSettings()` (the block that reads `getAYChain().isPunchEnabled()` today) reads
  `getAYVoicing()` back, so the UI shows the instance's real state. That covers a value set via
  WebAPI.

**Persistence:** `QSettings(IniFormat, UserScope, "Unreal", "Unreal-NG")`, the same store `mainwindow.cpp`
already uses. Key `Sound/AYVoicing` (string ID). Phase 2 adds `Sound/AYPunch`, `Sound/AYRoom`,
`Sound/BeeperPunch`.

**When it is applied (phase 2 prerequisite: origin flag):**

`MainWindow` does **not** currently know where an instance came from. Instances the GUI creates
(`mainwindow.cpp:1286` startup/create, `:2860` model switch) and instances it picks up from
automation (`:3491`, `:4139`) all go through the same `adoptEmulator(std::shared_ptr<Emulator>)`.
Phase 2 adds an explicit origin:

```cpp
enum class EmulatorOrigin { CreatedByGui, Adopted };
void adoptEmulator(std::shared_ptr<Emulator> emulator, EmulatorOrigin origin);
```

The two creating call sites pass `CreatedByGui`, the two pick-up sites pass `Adopted`, and the
QSettings preference is applied only for `CreatedByGui`. Phase 1 does not need this.

| Instance origin | Behaviour |
|:--|:--|
| Created by the Qt GUI (startup, model switch, reset that recreates the instance) | Apply QSettings value right after creation, before first audio frame |
| Adopted (created via WebAPI/MCP, then shown in the GUI) | **Do not overwrite**; read its state into the widget. The automation owner's choice wins. |
| User changes the combo | Apply to the bound instance + save to QSettings |

The apply happens inside `adoptEmulator` when `origin == CreatedByGui`, before the instance is
bound to the audio device. That is before its first audible frame.

### 8.2 unreal-videowall

**No videowall-specific setting, UI or settings file for now.** The videowall uses the active
emulator's config file:

- Each tile is an ordinary emulator instance. Its `SoundManager` takes the voicing from the tile's
  machine config (`[SOUND] AYVoicing` in its `unreal.ini`), or the built-in `classic` default when
  the key is missing (§5.1, levels 3–4).
- Only the active tile plays sound (the others get `sound`/`soundhq` off in `VideoWallWindow.cpp`),
  and voicing is skipped on silent tiles for free (§4.5). So what you hear is always the active
  emulator's config.
- If tiles run machine configs with different `AYVoicing` values, switching the active tile
  switches the voicing too. That is expected, because the config decides. The shipped inis have no
  key, so in practice every tile is `classic`.
- A runtime change is still possible through automation on the active tile (WebAPI/CLI/MCP
  `ay_voicing`). It is not persisted, and the next tile or restart reads the config again.
- **No code changes in `unreal-videowall/` for phases 1–2.** A Sound menu with its own persistence
  can be added later if needed; it would be a frontend preference (§5.1, level 2).
- Punch/room: core defaults, as today.

### 8.3 Headless / tests / MCP-only

No frontend, so the value comes from `[SOUND] AYVoicing` or the built-in `classic`. `EmulatorTestHelper`
does **not** force a preset globally. Tests that assert on hardware-level audio pin `flat` explicitly
(§9.2).

## 9. Tests

All new files `*_test.cpp`, classes `*_Test`, each test < 50 ms (pure DSP on synthetic buffers, no
ROM boot).

### 9.1 New: `core/tests/common/filtervoicing_test.cpp`

| Test | Pins |
|:--|:--|
| `FlatIsExactBypass` | buffer unchanged bit-for-bit |
| `ClassicMatchesDesignCurve` | steady-state gain at 30/41.2/55/100/130/160/300/1000 Hz within **±0.1 dB** of the analog design curve (§2.2 Classic column), at 44.1, 48, 96 and 192 kHz. This test checks the implementation against its own design, and catches coefficient and sign errors; the comparison with the old filter is `ClassicTracksLegacyFilterDC` |
| `ClassicTracksLegacyFilterDC` | per-band levels (1/3-octave, 20 Hz–1 kHz) vs. the real old `FilterDC<double>` at 218.75 kHz on 41.2 / 55 / 110 Hz squares, bands that contain a harmonic only: **±0.75 dB below 200 Hz, ±1.5 dB 200 Hz–1 kHz** (G1). Measured worst cases: ≤ 0.62 dB below 200 Hz (160 Hz band); 0.97–1.45 dB above it (254/320 Hz bands, the old filter's comb ripple) |
| `ClassicRestoresInfrasonicRatio` | synthetic "volume-step" signal (AY square with per-20 ms level steps) → energy 1–35 Hz vs. rest: Classic within 1 dB of legacy `FilterDC`, Flat at least 10 dB above legacy. Asserted relative, because absolute values depend on the signal (ours: −21.4 / −21.7 / −6.5 dB; proposal's: −17.3 / −17.6 / −3.3 dB) |
| `RateIndependentCoefficients` | same response at all 6 core rates (G6) |
| `DenormalFlushAfterSilence` | 10 s of zeros after a burst → state exactly 0 |
| `ParsePresetAcceptsLegacyAlias` | `legacy` → Classic; unknown → false |
| `ReferenceFitsDocumented` | HPF1 55 Hz / HPF2 39 Hz Q 0.62 errors ≈ 1.24 / 0.99 dB (keeps the analysis reproducible) |

### 9.2 New / extended SoundManager-level tests

| Test | File | Pins |
|:--|:--|:--|
| `VoicingRunsInLQ` | `core/tests/emulator/sound/ay_voicing_test.cpp` | HQ off, Classic: chip buffer after `handleFrameEnd` ≠ Flat output; HQ toggle does not change the voiced bass level (G3) |
| `VoicingNotResetOnHQReturn` | same | HQ on→off→on with a steady tone: a test-only `SoundManager::ayVoicingStateForTest(chip)` shows non-zero filter state carried across the HQ return (not zeroed, unlike the chains). An audio-level comparison is not used, because the HQ switch changes the decimator output itself (FIR vs boxcar, stale-stage flush from `ec66d3bc`), so a "continuous run" reference does not exist |
| `PresetSwitchIsClickFree` | same | G5, two checks on a steady AY tone switched Flat→Classic→Flat. (a) **HF energy:** energy above 8 kHz in the switch frame ≤ the mean of the two neighbouring frames + 1 dB; a click is broadband and shows up there, while square-wave edges are the same in every frame. (b) **Reference:** the whole switch frame within ±2 LSB of an offline ideal crossfade between two filters that ran continuously. This passes only with the §4.4 two-frame pre-roll (simulated: < 0.01 LSB). A bare `reset()` gives ~525 LSB, and one frame ~1.3 LSB, which is marginal. (c) Same as (b) with history invalidated (first frame after `reset()`): no assertion on (b); only checks that nothing crashes and the frame is finite. "Max sample delta" was dropped: on a square wave the edges already give the maximum delta, so it can't detect a click |
| `SetFromOtherThreadAppliesAtFrameBoundary` | same | set from a second thread while frames run; the active preset changes only between frames. Race-freedom holds by construction (the only shared state is one `std::atomic<uint8_t>`); the repo has no TSan build, so none is claimed |
| `PreRollHistoryInvalidatedOnRateChange` | same | after `applyCoreRate()` the next switch skips pre-roll (no samples at the old rate reach the new filter) |
| `TsfmSsgIsVoicedFmIsNot` | same | TSFM: SSG buffers voiced, `getFmBuffer` byte-identical to Flat |
| `ClassicHeadroomNotWorseThanLegacy` | same | §4.8 check 2: Classic peak ≤ legacy peak + 0.25 dB |
| `GetReturnsRequestedPreset` | same | `setAYVoicing(Flat)` then immediate `getAYVoicing()` → Flat before any frame runs (§4.4 step 5) |
| `HiddenPresetRejected` | `filtervoicing_test.cpp` | `parsePreset("tv")` → false while `tv` is hidden; `true` only with `allowHidden` |
| `IniAYVoicingParsed` | config tests | `AYVoicing=flat/legacy/garbage` → Flat / Classic / default + warning |

### 9.3 Existing tests to audit

These may shift with the new `classic` default. Each one either keeps passing or pins `flat` with a
one-line reason.

**Why the audit is wider than it looks:** `EmulatorTestHelper` turns `soundhq` **off** for every
test (test-speed defaults, 2026-09-13), so until now no helper-based test ran the character chains.
Voicing runs in LQ too, so with the `classic` default it is active in **every** test that goes
through `SoundManager::handleFrameEnd`. The helper deliberately does not pin `flat` (§8.3), because
voicing should stay exercised. Tests that assert exact sample values pin it themselves.

Files that touch `SoundManager` audio (from `git grep` at `af072f83`):
`soundhq_chain_bypass_test`, `sound_mute_contract_test`, `device_mixer_test`, `tsfm_output_test`,
`soundchip_ay8910_test` (chip-level, should not be affected: voicing sits in `SoundManager`),
`ttdcorpus_test`, `ttdtsfm_test` (§4.6), and also everything else that runs
`SoundManager::handleFrameEnd`: `multirate_test`, `frame_sample_count_test`,
`sound_adaptivity_test`, `idle_silence_test`, `audio_activity_notification_test`,
`device_registry_test`, `tsfm_soundmanager_test`, `beeper_test`, `covox_test`,
`ttdreplaymode_test`, `frame_pacing_test`. Voicing outputs
exact zero on silent input (no DC path, denormal flush), so the silence/idle tests should pass
unchanged, but each still needs a run.
Not affected (checked): the FM trim calibration (+7.4 dB) in `tsfm_volume_replay_test` measures
at ~1165 Hz at device level, upstream of `SoundManager`, and `classic` is +0.02 dB at 1 kHz anyway.

### 9.4 Automation

WebAPI settings test: GET/PUT `ay_voicing`, 400 on unknown, visible in `/settings`. Manual check with
the AGENTS.md WebAPI verification sequence.

## 10. Rollout

| Phase | Content | Exit |
|:--|:--|:--|
| **1 — core** | `FilterVoicing`, `SoundManager` wiring (§4.2–4.5), atomic handoff + two-frame pre-roll history + crossfade (§4.4), test-only accessors (`getActiveAYVoicing`, `ayVoicingStateForTest`), `[SOUND] AYVoicing`, §9.1–9.3 tests (full audit list), benchmark, DSD-is-flat note in the recording docs (§4.7) | build + core-tests green, zero warnings; A/B listening vs. a `d90421bb` build (parent of `45812176`; same bass as `4c49fb6`) on 3 tracks (one bass-heavy, one with volume-envelope "barrels", one TSFM) |
| **2 — surfaces** | WebAPI/CLI/Lua/Python `ay_voicing` + openapi (hidden presets rejected); Qt: `EmulatorOrigin` on `adoptEmulator` (§8.1), combo + QSettings + create/adopt rule; optional voicing on the DSD native tap (§4.7); videowall: no changes (uses the active emulator's config, §8.2); optional `SoundCharacterSettings` (punch/room persist + race fix) | settings reachable from every surface; GUI survives restart with the chosen preset |
| **3 — TV voicing** | Design the `tv` curve by listening (likely HPF2 ~120–150 Hz + gentle LPF ~5–7 kHz), then expose it | curve signed off by ear; tests extended |
| **4 — docs** | Permanent `docs/emulator/design/audio/ay-tone-voicing.md`, recipe line, folder → `DONE.md` | links valid, `tools/fix-absolute-paths.py` clean |

## 11. Files touched (expected)

| Area | Files |
|:--|:--|
| Core DSP | `core/src/common/sound/filters/filtervoicing.h` (new) |
| Core wiring | `core/src/emulator/sound/soundmanager.h/.cpp` (voicing members, pre-roll history, handoff, test accessors) |
| DSD tap (phase 2, optional) | `core/src/emulator/sound/chips/soundchip_turbosound.h/.cpp` (`FilterVoicing` on `_nativeTap`) |
| Config | `core/src/emulator/platform.h` (`config.sound.ayVoicing`), `core/src/emulator/config.cpp` |
| Automation | `core/automation/webapi/src/api/settings_api.cpp`, `openapi_settings.inc`, `openapi_schemas.inc`, `core/automation/cli/src/commands/cli-processor-settings.cpp`, `core/automation/lua/src/emulator/lua_emulator.h`, `core/automation/python/src/emulator/python_emulator.h` |
| Qt | `unreal-qt/src/debugger/widgets/audiosettingswidget.h/.cpp`, `unreal-qt/src/mainwindow.h/.cpp` (`EmulatorOrigin` parameter on `adoptEmulator`, 4 call sites; apply-on-create) |
| Videowall | none (uses the active emulator's config file, §8.2) |
| Tests | `core/tests/common/filtervoicing_test.cpp`, `core/tests/emulator/sound/ay_voicing_test.cpp`, config + WebAPI settings tests; `flat` pins in audited tests where needed (§9.3) |
| Bench | `core/benchmarks/…/filtervoicing_benchmark.cpp` |
| Docs | this folder; recording docs (DSD native capture is always flat); later `docs/emulator/design/audio/ay-tone-voicing.md`, `.recipe/` audio line |

## 12. Risks and open questions

| # | Item | Plan |
|:--|:--|:--|
| R1 | Punch with `flat`: the punch envelope follows the whole spectrum, including volume-step thumps. Punch may pump on "barrels". | Hint label (§8.1) for now. If listening confirms pumping, make punch presets voicing-aware later (separate item). |
| R2 | Headroom: Classic brings back edge overshoot (+4.6 dB vs. Flat at 41 Hz). | §4.8 checks; the clamp is only a safety net. |
| R3 | A test compares post-chain mixed audio across a TTD restore. | Pin `flat` in that test (§4.6); do not put voicing into TTD. |
| R4 | Adopted instances: GUI and automation disagree about who owns the setting. | Rule in §8.1: the creator owns it, adopting never overwrites. |
| R5 | "Classic" is a single-listener target (fit to one old filter). | It is a preset, not a replacement. `flat` stays hardware-true; `tv` gives a third option later. |
| R6 | DSD native recordings stay `flat` whatever the setting (tap is upstream of voicing). | Documented in phase 1; optional phase 2 voicing on the tap (§4.7). |
| R7 | Preset switch without valid pre-roll history (first frame after reset, rate change, sound gap) starts cold and can thump. | Rare, at points where the output is discontinuous anyway; covered by `PresetSwitchIsClickFree` (c) and `PreRollHistoryInvalidatedOnRateChange`. |
| R8 | Voicing is newly active in every `SoundManager` test (the helper forces `soundhq` off, and voicing runs in LQ). | Full-suite run in phase 1; `flat` pinned only where a test asserts exact samples (§9.3). |
| Q1 | Should the default be `classic` for headless/MCP runs too? | Proposed: yes (one sound everywhere). Analysis recipes pin `flat`. |
| Q2 | Apply voicing to the beeper later? | Out of scope. The beeper never had the legacy filter, so there is no "missing" balance to restore. |
| Q3 | `features.ini` never loaded (§6) | Side issue, noted only. |

## 13. Review log

**Review 2 — 2026-09-25, against `af072f83`: ready for phase 1 after document fixes; no
architecture change.**

Confirmed in code: chains are HQ-only and reset on HQ return (`chainsActive = isHQActive() &&
!soundOff`; `_chainsBypassed` → `reset()`), so moving voicing out of `AudioCharacterChain` is
right. The anchor points exist: `applyCoreRate`, `isSynthesisSuppressed()`, `[SOUND] CoreRate`,
and `audio_rate` wiring in all automation modules. `FeatureManager::loadFromFile` has no caller.
The `45812176` message error (§2.2) is real. FM trim calibration is unaffected (§9.3).

Fixed in this revision:

| # | Finding | Where fixed |
|:--|:--|:--|
| 1 | Qt: the "created vs adopted" distinction did not exist; all four sites call one `adoptEmulator()` | §8.1: `EmulatorOrigin` parameter (phase 2 prerequisite) |
| 2 | ±0.5 dB per band not achievable (old filter's comb ripple on harmonics) | G1, `ClassicTracksLegacyFilterDC`: ±0.75 dB < 200 Hz, ±1.5 dB 200 Hz–1 kHz. The review proposed ±0.5 dB below 200 Hz; the re-run measured 0.58–0.62 dB in the 160 Hz band, so it was widened to ±0.75 |
| 3 | Headroom "not above legacy" always fails (+0.12/+0.13 dB) | §4.8, test: legacy + 0.25 dB |
| 4 | Peak formula normalised the leading 1 of `a`; HPF1 sign ambiguous | §4.1: explicit coefficients and difference equations, sign convention |
| 5 | `parsePreset("tv")` behaviour for hidden presets undefined | §4.1: hidden IDs rejected on every user-facing input; `HiddenPresetRejected` |
| 6 | `getAYVoicing()` semantics (requested vs active) | §4.4 step 5: returns requested; `GetReturnsRequestedPreset` |
| 7 | AY↔FM reset described as needing a hook | §4.2: the device exists only in the constructor; a switch rebuilds the stack |
| 8 | §9.3 audit list incomplete | §9.3: five more `handleFrameEnd` tests |
| 9 | Click metric (max sample delta) cannot see a click on a square wave | `PresetSwitchIsClickFree`: HF energy > 8 kHz + offline reference |
| 10 | (minor) `setup()` keeps state while chains reset on a rate change | §4.3: noted as intentional |
| 11 | (minor) Videowall QSettings | Superseded by the owner's decision: the videowall has no settings file of its own and uses the active emulator's config file (§8.2) |

**Review 3 — 2026-09-25, against `af072f83`: ready for phase 1 implementation and testing.**
The architecture is unchanged again. These findings would have failed tests or caused audible
defects during phase 1 and are now fixed in the design:

| # | Severity | Finding | Where fixed |
|:--|:--|:--|:--|
| 1 | **blocking** | Crossfade with a bare `reset()` new filter leaves the HPF/peak start-up transient: 525 LSB error at −6 dBFS in simulation. It is audible on bass, and `PresetSwitchIsClickFree` (b) would fail | §4.4 step 3: pre-roll the new filter over a saved copy of the previous **two** unvoiced frames (one frame leaves 1.3 LSB, which is marginal; two leave < 0.01); history invalidated on reset / rate change / gaps; new test `PreRollHistoryInvalidatedOnRateChange` |
| 2 | **blocking** | `VoicingNotResetOnHQReturn` compared audio against a "continuous run" that doesn't exist, because the HQ switch changes the decimator output itself | §9.2: asserts carried filter state through a test-only accessor |
| 3 | gap | DSD native recording (`_nativeTap` at 218.75 kHz) is upstream of voicing, so "what you hear is what you record" did not hold for DSD | §4.7: documented as always-flat in phase 1; optional phase 2 voicing on the tap |
| 4 | test impact | `EmulatorTestHelper` forces `soundhq` off, so voicing (LQ too) is newly active in every `SoundManager` test; six more files were missing from the audit | §9.3: rationale + full list |
| 5 | test | `ClassicMatchesLegacyResponse` compared against its own design curve under a misleading name and a loose ±0.5 dB | renamed `ClassicMatchesDesignCurve`, ±0.1 dB |
| 6 | test | TSan claimed; the repo has no TSan build | §9.2: race-freedom by construction |
| 7 | test | Headroom test at full scale would measure the int16 clamp, not the filters | §4.8: test at −6 dBFS; full-scale clamp ≤ 0.13 dB documented |
| 8 | doc | §4.7 speculated about an oscilloscope reading chip buffers | §4.7: verified that nothing outside `SoundManager` reads them |

Checked and fine: the mixer reads per-chip buffers in **both** HQ and LQ (`soundmanager.cpp`,
`AY1_All`/`AY2_All` → `getChipBuffer`), so voicing on them reaches the output in both modes. The
WebAPI `setSetting` body is `{"value": ...}`, matching §7.1. `tsfm_volume_replay_test` is
device-level (`device->handleFrameEnd()`), confirming it is unaffected.
