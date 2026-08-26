# unreal-ng — Audio Clock, Rate Control and A/V Synchronisation

> **Implemented.** The living operating manual - architecture, latency
> budget, constants and the post-ship field notes (every desync bug found
> and its regression guard) - is `docs/emulator/design/audio/drc-rate-control.md`.
> This document remains the original design and defect analysis.

**Status:** design proposal
**Scope:** `core/src/emulator/sound/`, `core/src/emulator/mainloop.cpp`, `unreal-qt/src/emulator/soundmanager.cpp`, plus the future SDL3 client
**Goal:** a single, drift-free audio clock; continuous (not stepwise) rate correction; video decoupled from both, so that display refresh rate becomes irrelevant to correctness.

---

## 1. Current architecture

Three independent clocks are in play today:

| Clock | Owner | Rate |
|---|---|---|
| Emulation pacing clock | `MainLoop::Run`, `steady_clock` deadline | `config.frame_duration_us` (20480 µs Pentagon / 19968 µs ZX48) |
| Audio DAC clock | miniaudio / PipeWire hardware | 44100 Hz nominal, actual crystal ±50–100 ppm |
| Display clock | compositor vsync | 60 Hz (Deck panel: 45–90 Hz, integer steps) |

They are coupled by exactly one feedback path: `AppSoundManager::audioDataCallback` posts `NC_AUDIO_BUFFER_HALF_FULL` when the ring drops below a low watermark, `MainLoop` wakes early and re-anchors `_nextFrameTime` to *now*.

Relevant sizes as built:

- Ring buffer: `AudioRingBuffer<int16_t, AUDIO_BUFFER_SAMPLES_PER_FRAME * 8>` = 32768 `int16_t` = 16384 stereo frames ≈ **371 ms** @44.1 kHz
- Low watermark: `SAMPLES_PER_FRAME * AUDIO_CHANNELS * 2` = 3528 `int16_t` = 1764 stereo frames ≈ **40 ms**
- Device buffer: `periodSizeInFrames = 256`, `periods = 2` → 512 frames ≈ **11.6 ms**

---

## 2. Defects

### D1 — Fractional samples are discarded every frame (primary drift source)

`SoundManager::handleFrameEnd`:

```cpp
size_t calculated = static_cast<size_t>(
    std::round(frameDuration * (double)AUDIO_SAMPLING_RATE / (double)CPU_CLOCK_RATE));
```

The exact per-frame sample count is not an integer, and the fractional part is dropped rather than carried:

| Machine | T-states | SR | Exact samples/frame | Emitted | Rate error | Exact period |
|---|---|---|---|---|---|---|
| Pentagon | 71680 | 44100 | 903.168 | 903 | **−0.0186 %** | 125 frames |
| Pentagon | 71680 | 48000 | 983.040 | 983 | −0.0041 % | **25 frames** |
| ZX48/128/Scorpion | 69888 | 44100 | 880.5888 | 881 | **+0.0467 %** | 625 frames |
| ZX48/128/Scorpion | 69888 | 48000 | 958.464 | 958 | −0.0484 % | 125 frames |

This is a *systematic* production-rate bias, not jitter. Pentagon under-produces ~8.2 samples/s; ZX48 over-produces ~20.6 samples/s. It is the dominant reason the low watermark fires, well ahead of any real crystal drift — which contradicts the comment in `MainLoop::Run` attributing watermark activity to "clock drift between the CPU pacing clock and the audio DAC crystal".

### D2 — Correction is quantised to a whole frame

When the watermark fires, `_nextFrameTime = now` releases the next frame immediately: a **20.48 ms** step change in the audio timeline. There is no intermediate authority between "on schedule" and "one full frame early". This is the rubber-banding already documented in the source comments.

### D3 — The feedback path is a level trigger used as a rate control

`callbacksPerRequest = 4` exists only to stop a level-triggered condition from posting repeatedly through an asynchronous queue. The rate-limiter is a symptom: what the loop actually needs is *ring occupancy as a continuous scalar*, sampled once per frame, not an edge notification delivered with queue latency.

### D4 — No target occupancy

Only a floor is defined. Nothing pulls occupancy back **down**, so on ZX48 (which over-produces) the ring fills until `AudioRingBuffer::enqueue` starts dropping — silently, incrementing `_enqueueErrorCount`. Ring occupancy *is* the A/V offset, so an undefined equilibrium means an undefined lip-sync error.

### D5 — Frame-unit constants are 50 Hz

`SAMPLES_PER_FRAME` (= 882) is used as the fallback sample count and as the watermark unit, while the machine actually emits 903. Watermark thresholds expressed in "frames" therefore mean different things in different places.

### D6 — Double resampling on modern Linux

`config.sampleRate = AUDIO_SAMPLING_RATE` forces 44100 on a stack whose native rate is 48000 (PipeWire on SteamOS, and the Deck codec itself). PipeWire inserts its own resampler behind miniaudio; quality and latency are then outside our control.

---

## 3. Target design

```
   Z80 / ULA (T-states)
          │
          │  exact integer accumulator          (fixes D1)
          ▼
   chip DSP chains @ CORE_RATE (44100)
          │
          │  DRC fractional resampler, ratio = (DEV_RATE/CORE_RATE)·(1+trim)
          ▼                                      (fixes D2,D3,D4,D6)
   ring buffer @ DEV_RATE  ──occupancy──▶ PI controller ──▶ trim
          │
          ▼
   miniaudio / SDL3 device @ native DEV_RATE
```

Principle: **the audio device is the only master clock.** The `steady_clock` deadline is demoted to a coarse scheduler that keeps the emulation thread from spinning; ring occupancy is the fine control. Video is a consumer of whatever frame is finished — it never gates emulation.

---

## 4. Fix 1 — exact integer sample accumulator

Replace the `round()` in `SoundManager::handleFrameEnd` with a carry:

```cpp
// Fields
uint64_t _sampleAccumulator = 0;   // units: T-states × SR, modulo CPU_CLOCK_RATE

// Per frame
_sampleAccumulator += static_cast<uint64_t>(frameTStates) * CORE_SAMPLING_RATE;
size_t samplesThisFrame = _sampleAccumulator / CPU_CLOCK_RATE;
_sampleAccumulator      %= CPU_CLOCK_RATE;
```

Properties:

- Zero drift **by construction** — the sequence is exactly periodic (903,903,903,903,904,… with period 125 on Pentagon@44.1k).
- Integer arithmetic only; no accumulation of FP rounding.
- `frameTStates` must be `config.frame * speedMultiplier`, exactly as today, so turbo modes stay consistent.

**Reset points:** machine change, hard reset, snapshot load, `SoundManager::reset()`. Add `_sampleAccumulator = 0` to each. Speed-multiplier changes do NOT reset it — the carry stays valid when `frameTStates` changes — but the blip cross-check below must tolerate the transition frame.

**Overflow guard:** `samplesThisFrame` must still be bounded by `MAX_SAMPLES_PER_FRAME` (buffers are sized to it). When the clamp fires (speed multiplier ≥ 3), drop the accumulator remainder and log once — turbo has no realtime constraint and DRC is bypassed there anyway; a silent wrong-size write is worse than a documented drop.

**Interaction with blip_buf.** `Beeper` runs its own internal fractional accumulator via `blip_set_rates(clockRate, samplingRate)`, so `blip_samples_avail()` already alternates 903/904 correctly. Two accumulators driven by the same ratio stay in lockstep, but they must not disagree:

```cpp
int avail = blip_samples_avail(_blipL);
// Logged check, NOT assert: a snapshot load or multiplier change mid-frame can
// legitimately desync the two accumulators by one sample for one frame
if (std::abs(avail - static_cast<int>(samplesThisFrame)) > 1)
    LOGWARNING_ONCE_PER_SECOND(...);
```

Read exactly `samplesThisFrame` from blip and let the remainder stay in blip's buffer for the next frame — do **not** clamp to `MAX_SAMPLES_PER_FRAME` silently, since a clamp discards samples and reintroduces drift. If `avail < samplesThisFrame` (possible after a reset), pad with the last value rather than shortening the frame.

The AY path (`soundchip_turbosound.cpp`, phase-accumulator decimation) needs the same audit: its accumulator must be free-running across frames, never reset at frame boundaries.

---

## 5. Fix 2 — dynamic rate control (DRC)

### 5.1 Controller

Sampled once per frame, in the emulation thread, immediately before enqueue:

```cpp
constexpr double TARGET_MS   = 70.0;   // ring occupancy setpoint (as designed; see update note below)
constexpr double MAX_TRIM    = 0.005;  // ±0.5 %  ≈ ±8.6 cents, inaudible
constexpr double KP          = 0.08;
constexpr double KI          = 0.0008;
constexpr double EMA_ALPHA   = 0.05;

double occMs   = ringOccupancyFrames() * 1000.0 / DEV_RATE;
_occFiltered  += EMA_ALPHA * (occMs - _occFiltered);

double err     = (_occFiltered - TARGET_MS) / TARGET_MS;   // >0 → we are ahead
_errIntegral   = std::clamp(_errIntegral + err, -50.0, 50.0);   // anti-windup

double trim    = std::clamp(-(KP * err + KI * _errIntegral), -MAX_TRIM, MAX_TRIM);
_resampleRatio = (double)DEV_RATE / (double)CORE_RATE * (1.0 + trim);
```

Sign convention: ring too full ⇒ we are producing faster than the DAC consumes ⇒ we must emit **fewer** output samples per input sample ⇒ negative trim.

Why ±0.5 %: 0.5 % is 8.6 cents of pitch shift, below the ~15 cent threshold for steady tones and far below it for chip music with vibrato. The controller normally sits within ±0.05 %; the range exists for cold start and for USB/Bluetooth devices with genuinely off-nominal crystals.

### 5.2 The resampler

Place it as the final output stage in `SoundManager::handleFrameEnd`, after mixing, before the callback. Input is already band-limited (blip_buf for beeper, decimation filters for AY), so a **cubic Hermite / Catmull-Rom** interpolator is sufficient — at ratios within 0.5 % of unity the interpolation error sits below −90 dBFS.

```cpp
// Persistent across frames
double  _phase = 0.0;          // fractional read position
int16_t _hist[4][2];           // 4-tap history per channel

// Produce output until the input frame is consumed
while (_phase < static_cast<double>(inCount))
{
    int   i = static_cast<int>(_phase);
    double t = _phase - i;
    out[o++] = hermite(x[i-1], x[i], x[i+1], x[i+2], t);
    _phase += 1.0 / _resampleRatio;
}
_phase -= inCount;             // carry fractional position into next frame
```

Critical: `_phase` and `_hist` **must persist across frames**. Resetting either at frame boundaries produces a discontinuity at 48.8 Hz — exactly the artefact class already noted in the `handleFrameEnd` comments.

Boundary handling: as sketched the loop reads `x[i+2]`, one past the frame's
last input sample. The implementation must either (a) run the interpolator one
output sample behind and carry the last 3 input samples in `_hist` into the
next frame, or (b) hold back the final input sample per frame. Reading past
the buffer or zero-padding it reintroduces the per-frame discontinuity this
section exists to prevent.

Bit-exactness at unity: when `DEV_RATE == CORE_RATE` and `|trim| < 1e-9`,
bypass interpolation entirely (memcpy) so the default configuration remains
bit-identical to today's output.

### 5.3 What this replaces

Once DRC is in place, delete:

- `NC_AUDIO_BUFFER_HALF_FULL` posts from `audioDataCallback`
- `_watermarkCallbackCounter`, `callbacksPerRequest`
- `_moreAudioDataRequested` in `MainLoop`
- the `_nextFrameTime = now` re-anchor on audio request

Keep `_cv`/`_audioBufferMutex`: they are also `MainLoop::Stop()`'s mechanism
for interrupting the sleep. The deadline wait stays interruptible
(`wait_until` on stop request) or shutdown gains up to one frame of latency.

`MainLoop::Run` keeps only the absolute-deadline `sleep_until` (it is still needed so the thread doesn't busy-spin) plus the existing re-anchor for pause/debugger/lag. The deadline no longer has to be *accurate*, only approximately right — DRC absorbs the residual.

Emergency path to retain: if occupancy hits zero, skip the sleep entirely and produce frames back-to-back until the ring is above 50 % of target. This covers debugger stalls and disk hitches.

### 5.4 Ring buffer changes

- Expose `getAvailableData()` results in stereo frames, not `int16_t` counts, to end the D5 unit confusion.
- Add a `getOccupancyFrames()` accessor with acquire semantics for cross-thread read.
- Size: 371 ms is generous; keep it. Target 70 ms leaves 5× headroom for scheduler hiccups and lets `TARGET_MS` be raised on Bluetooth output without resizing.

> **Update (2026-08, post-ship):** the target IS the audio presentation
> delay - 70 ms proved audibly late and was lowered to **40 ms**
> (`DRC_TARGET_MS`), with video presentation delayed 2 frames to match
> (net A/V ~= +11 ms). Ring capacity later grew to ~1.5 s with 192 kHz
> support, which made overfill bugs far more visible; a hard-resync path
> (one-step discard to target at >160 ms) was added. The emergency-refill
> threshold is now rate-aware (15 ms) and calibrated against the occupancy
> sawtooth trough, not the setpoint. Full rationale and the regression
> history: `docs/emulator/design/audio/drc-rate-control.md`.
- Make `_enqueueErrorCount` / `_dequeueErrorCount` observable (log once per second when nonzero). Under DRC these should be permanently zero; if they aren't, the controller isn't converging and that is worth knowing.

---

## 6. Fix 3 — native device rate

With the DRC resampler in place, running the core at 44100 and the device at 48000 costs **nothing extra** — the ratio is just `48000/44100 · (1+trim)` instead of `1 · (1+trim)`. The same code path serves both purposes.

Therefore:

```cpp
config.sampleRate = 0;                       // miniaudio: use device native rate
// after ma_device_init:
DEV_RATE = _audioDevice.sampleRate;          // read back what we actually got
```

and pass `DEV_RATE` to the resampler. On SteamOS this yields 48000 and eliminates PipeWire's resampler from the chain (D6). On macOS it yields whatever the current output device runs at, which also removes CoreAudio's converter.

**The core stays at 44100.** Changing `AUDIO_SAMPLING_RATE` is *not* a one-line edit — 44100 is baked into filter design, not just buffer sizing:

| File | Dependency |
|---|---|
| `common/sound/filters/filter_unreal.h` | FIR coefficients designed for fs = 2 822 400 (44100 × 64), bw 20 kHz |
| `common/sound/filters/filter_decimator.h` | `OUTPUT_RATE = 44100.0` literal |
| `common/sound/filters/filter_interpolate.h/.cpp` | `setRates(1'750'000, 44100)`, coefficients documented for Fs 44100 |
| `emulator/sound/chips/soundchip_ay8910.h` | `AY_SAMPLING_RATE = 44100`, `AY_OVERSAMPLING_FACTOR = 64` |
| `emulator/sound/audio.h` | `AUDIO_SAMPLING_RATE`, `SAMPLES_PER_FRAME`, `PSG_CLOCKS_PER_AUDIO_SAMPLE` |

A genuine 48 kHz core is a separate, larger piece of work (regenerate every coefficient set). Rename the constant `CORE_SAMPLING_RATE` now to make the distinction explicit in the code, and treat native-48k-core as a later optimisation with a modest quality payoff.

Secondary benefit if it ever happens: at 48000 the Pentagon accumulator closes in **25 frames** instead of 125.

---

## 7. Fix 4 — video decoupling and display rate policy

Neither Spectrum rate is representable on the Deck panel:

| Machine | Emulated | Nearest panel rate | Mismatch | Frame slip |
|---|---|---|---|---|
| ZX48/128 | 50.080128 Hz | 50 Hz | 0.160 % | 1 frame / 12.5 s |
| Pentagon | 48.828125 Hz | 49 Hz | 0.352 % | 1 frame / 5.8 s |

There is no panel rate that makes this exact, so **do not attempt vsync-lock**. Instead:

1. Emulation thread writes completed frames into a triple buffer and never blocks on the presenter.
2. Presenter (SDL3 renderer) draws the most recent complete frame each vsync. At 60 Hz panel and ~49 emulated fps this duplicates roughly every fifth frame; the duplication is regular and reads as far less objectionable than a stutter caused by the audio clock being yanked.
3. Optional quality setting for scrolling-sensitive users: set the panel to 50 Hz for ZX48 machines via `SDL_SetWindowFullscreenMode` with a 50 Hz mode. Judder then drops to one duplicated frame per 12.5 s. Leave Pentagon at 60 Hz — 49 Hz would judder three times as often.
4. Never let the presenter's timing feed back into emulation pacing. A/V offset is now defined solely as ring occupancy (~70 ms as designed; 40 ms as shipped, with a matching 2-frame video present delay - see the operating manual), and is stable because DRC holds it there.

Expose the panel-rate choice as a setting rather than hardcoding it: on desktop the same code runs at 144 Hz or on a VRR display where the question is moot.

---

## 8. Implementation order

Each step is independently testable and independently shippable.

| # | Change | Files | Risk |
|---|---|---|---|
| 1 | Integer sample accumulator; remove `round()`; audit blip/AY accumulator continuity. **Also fixes video-recording drift outright (§10)** | `soundmanager.cpp`, `beeper.cpp`, `soundchip_turbosound.cpp` | low |
| 2 | Ring occupancy accessor in frames; instrument enqueue/dequeue errors | `audioringbuffer.h`, `unreal-qt/.../soundmanager.h` | low |
| 3 | Hermite resampler stage with persistent phase, ratio fixed at 1.0 | `soundmanager.cpp` (+ new `filters/resampler_drc.h`) | low — no behaviour change yet |
| 4 | PI controller drives ratio; delete watermark message path | `soundmanager.cpp`, `mainloop.cpp`, `unreal-qt/.../soundmanager.cpp` | medium |
| 5 | Native device rate; `AUDIO_SAMPLING_RATE` → `CORE_SAMPLING_RATE` | `audio.h`, client soundmanagers | low once 3–4 land |
| 6 | Triple-buffered presentation, display-rate policy | SDL3 client | independent |

Steps 1–4 are entirely inside `core` and the existing Qt client; the SDL3 client inherits the fixed behaviour for free.

---

## 9. Acceptance criteria

Measurable, and the project already has the instrumentation:

**A. Sample-count exactness.** Run 10 emulated minutes headless (`core/automation/cli`), count samples delivered to the callback. Expected Pentagon@44.1k: `round(600 × 48.828125 × 903.168)` = 26 460 000 ± 1. Current code is short by ~4900 samples.

**B. Occupancy stability.** Log ring occupancy each frame for 10 minutes. Criterion: mean within ±3 ms of `TARGET_MS`, standard deviation < 5 ms, no sample outside 20–150 ms, `_enqueueErrorCount == _dequeueErrorCount == 0`.

> **Update (2026-08):** with `TARGET_MS = 40`, instantaneous occupancy is a
> sawtooth swinging ±1 full frame around the setpoint by construction -
> judge the EMA-filtered value against the ±3 ms criterion, and the
> instantaneous band as roughly [target − 1 frame, target + 1 frame].
> Much of this acceptance now runs automatically: see the regression-guard
> table in `drc-rate-control.md` §8.

**C. Pitch stability.** Emit a steady AY tone; capture via `AudioFileHelper`; run `AudioHelper::detectBaseFrequencyFFT` over successive 1-second windows. Criterion: frequency spread < 0.1 % across the run, no step discontinuities (the current watermark kick shows up here as a visible staircase).

**D. Trim behaviour.** Log `trim` each frame. Criterion: after 30 s convergence it stays within ±0.05 % and never saturates at ±0.5 % during normal playback.

**E. Disturbance rejection.** Pause at a breakpoint for 2 s, resume. Criterion: occupancy returns to within 10 % of target inside 3 s, with no audible glitch beyond the underrun during the pause itself.

**F. Turbo mode.** Verify the accumulator tracks `speedMultiplier` and that DRC is bypassed (turbo has no realtime constraint) rather than fighting an unreachable setpoint.

---

## 10. Recording path (perfect sync by construction)

Recording already timestamps both streams in **emulated time**: video PTS =
`frameCount × config.frame / CPU_CLOCK_RATE` (recordingmanager.cpp:698), audio
PTS = `sampleCount / sampleRate` (:818). The only reason recordings drift
today is D1: video advances 20.480 ms/frame while audio delivers 20.476 ms of
samples — ~11 ms/min audio-behind on Pentagon. **Fix 1 alone makes recording
exact**: audio duration equals video duration ±1 sample forever, with no
controller and no dependence on the host clock.

**Invariant — the recording tap sits UPSTREAM of DRC.** `CaptureAudio` reads
`_outBuffer` (soundmanager.cpp:381) before the device callback; the Fix-2
resampler must be inserted AFTER that tap. Recording receives the pure
CORE_RATE emulated stream; only the realtime device path is trimmed. If DRC
ever leaks into the recording tap, recordings inherit realtime's ±0.5%
wobble and lose by-construction exactness. Same applies to Fix 3: recording
stays at CORE_RATE regardless of DEV_RATE.

Turbo recording already works in the emulated-time domain (PTS unaffected by
wall speed; encoder backpressure throttles) and Fix 1 preserves that.

**Acceptance G (recording).** Record 60 emulated seconds headless; assert
delivered audio sample count == `round(frames × exact samples/frame)` and
container audio/video duration delta < 1 ms.

---

## 11. Notes for the SDL3 client

- Open with `SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, ...)` and query the actual device spec; SDL3 will resample if asked, so ask it not to — match its rate exactly, same as the miniaudio change above.
- SDL3's audio stream has an internal queue; do not stack it on top of `AudioRingBuffer`. Either use `SDL_PutAudioStreamData` and let `SDL_GetAudioStreamQueued` be the occupancy signal, or keep the existing ring and use a callback stream. One buffer, one occupancy measurement, one controller.
- On the Deck, raise `periods` from 2 to 3–4 (or the SDL3 equivalent buffer size) — 11.6 ms of hardware buffer is tight once PipeWire's graph latency is added, and the Deck's power management makes wake-up latency spikier than a desktop's.
