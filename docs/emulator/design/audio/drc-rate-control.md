# Dynamic Rate Control (DRC) — Architecture, Latency Budget and Field Notes

**Status:** implemented and shipping (audio-drc branch, 2026-08).
**Companions:** `docs/inprogress/2026-08-17-audio-sync/audio-sync-design.md` (original design and defect analysis), `multirate-core-evaluation.md` / `multirate-core-implementation-plan.md` (multi-rate core).
**Code:** `core/src/emulator/sound/soundmanager.{h,cpp}` (controller), `core/src/common/sound/filters/resampler_drc.h` (resampler), `core/src/common/sound/audioringbuffer.h` (ring), `core/src/common/sound/audiodevicedescriptor.h` (monitoring), `core/src/emulator/mainloop.cpp` (pacing + refill), frontends' `AppSoundManager` (device, ring ownership, resync).

This document is the operating manual: how the pieces fit, why every
constant has the value it has, and — most importantly — the **field notes**:
every desync bug found after the initial design shipped, what it looked
like, and which regression test now guards it. Read section 6 before
touching any constant.

---

## 1. The problem in one paragraph

The emulator produces audio and video for a frame **at the same moment**,
but they travel different pipelines. Video is latched at frame end and
painted by the GUI within milliseconds. Audio is queued in a ring buffer
and drained by the sound card at its own crystal's pace. Two independent
clocks (the emulator's frame pacing and the audio device) drift apart
unless something continuously reconciles them — and whatever audio sits in
the queue at any moment **is** the audio's presentation delay. Keeping
that delay small, constant, and equal to the video path's delay is the
whole game.

## 2. Architecture

```
emulation thread (frame-paced, 48.83 fps Pentagon)
  chips -> mix -> [recording tap] -> DRC resampler -> ring buffer
                                                        |
device thread (callback every ~5.8 ms)                  v
  ring buffer -> device HW buffer -> speaker
```

- **Frame clock is the master.** The main loop paces emulation with an
  absolute deadline (`wait_until`, accumulated `frame_duration_us`). The
  audio device never dictates emulation speed.
- **Exact sample accumulator** (Fix 1 in the design doc): per-frame sample
  counts derive from `frameTStates x rate / CPU_CLOCK` with the remainder
  carried in integers — drift-free by construction, e.g. Pentagon @44.1k
  emits the exact pattern 903,903,...,904 with period 125.
- **DRC resampler** (Fix 2): a Catmull-Rom resampler between the mixed
  core-rate stream and the ring. Its ratio is `deviceRate / coreRate x
  (1 + trim)` where trim comes from the controller below. At matched rates
  and zero trim it is a memcpy bypass.
- **Recording tap sits UPSTREAM of the resampler** — recordings are
  sample-exact in emulated time and never see rate-control artifacts.
  This is an invariant; do not move it.
- **PI controller** (`updateDrcControl`, once per emulated frame):
  process variable = ring occupancy in ms (EMA-filtered), setpoint =
  `DRC_TARGET_MS`, output = trim clamped to ±0.5%.

## 3. The latency budget (why every number is what it is)

Audio presentation delay = ring occupancy + device HW buffer + device
output latency. Video presentation delay = present-queue delay + paint.
The **net A/V offset is their difference** — the number shown in the audio
settings ⓘ popup.

| Component | Value | Where set | Why |
|---|---|---|---|
| Ring target `DRC_TARGET_MS` | **40 ms** | `soundmanager.h` | Floor is structural: production is bursty (one whole ~20.5 ms frame lands at once) while the DAC drains continuously, so occupancy must cover ≥1 frame + scheduler jitter. 40 ms = ~7 callback periods of underrun margin. 70 ms (the original value) was audibly late. |
| Device HW buffer | ~11.6 ms | frontend `init()` (2 x 256 frames) | miniaudio low-latency profile. |
| Device output latency | device-dependent | queried from CoreAudio (device latency + safety offset) | **Below our pipeline.** Virtual loopback devices (Background Music, BlackHole) report large values here — this is how a desync living outside the emulator is identified. |
| Video present delay | **2 frames** (~41 ms), `[VIDEO] AVSyncDelayFrames=auto\|0..3` | `Screen` present queue | Instead of shrinking the ring into underrun territory, video is delayed to match audio: both land at the same constant latency, net offset ≈ **+11 ms**. `0` = lowest input latency (audio trails by the full ring). Recording is unaffected (taps emulated time upstream of presentation). |
| Emergency refill `EMERGENCY_REFILL_MS` | **15 ms** | `soundmanager.h`, used by `mainloop.cpp` | Must sit BELOW the occupancy sawtooth trough (target − 1 frame ≈ 19.5 ms) — see field note 6.4. Rate-aware (converted to device frames at the live device rate). |
| Hard resync `HARD_RESYNC_MS` | **160 ms** (4x target) | `soundmanager.h`, used by frontend device callback | Overfill beyond this is unrecoverable by ±0.5% trim in reasonable time; the consumer discards down to target in one step — see field note 6.6. |

Controller gains: `KP=0.08`, `KI=0.0008`, `EMA_ALPHA=0.05`, trim ±0.5%.
Convergence from a disturbance takes a few seconds by design — big,
fast corrections are what the refill (low side) and hard resync (high
side) are for; the PI only does fine tracking.

## 4. The occupancy sawtooth (read this before trusting any reading)

Ring occupancy is **not a flat line**. A whole emulated frame of audio
(~903 samples, ~20.5 ms) is enqueued at once at frame end, then drains
continuously until the next frame. Instantaneous occupancy therefore
swings a full frame around the setpoint every 20.5 ms:

```
occupancy
  50ms |   /|    /|    /|
       |  / |   / |   / |      <- burst at frame end
  30ms | /  |  /  |  /  |
       +------------------- time
```

Consequences:

- The **actual per-sample latency is constant** — every frame's audio
  waits behind the same queue. The sawtooth is a sampling artifact of
  *when you look*.
- Any consumer of occupancy must decide which value it needs:
  - the **controller** and the **A/V readout** use the EMA-filtered value;
  - the **emergency refill** compares against the instantaneous value and
    must therefore be calibrated against the **trough**, not the setpoint.

## 5. Device lifecycle

- **Native rate** (Fix 3): devices open with `sampleRate = 0` (native);
  the DRC base ratio handles core↔device conversion — no hidden OS
  resampler. The granted rate is published process-wide
  (`PublishDefaultDeviceSampleRate`) **before any emulator exists**, so
  `CoreRate=auto` resolves to the device family (resolution priority:
  explicit config → per-emulator cell → process-wide default → 44100).
- **Reroute** (default output change / hotplug): miniaudio notification →
  GUI-thread re-init at the new output's native rate → ring dropped →
  `deviceReinitialized(rate)` → DRC re-bases next frame. A device is
  never uninitialized from its own callback thread.
- **Same-device nominal rate change** (Audio MIDI Setup, Background Music
  48k→192k): fires **no** miniaudio notification. A CoreAudio
  `kAudioDevicePropertyNominalSampleRate` listener on the active device
  triggers the same re-negotiation (macOS; WASAPI invalidates the stream
  on format changes, which routes through the reroute path anyway).
- **Live core re-rate**: with `CoreRate=auto`, a device rate change
  requests `SoundManager::requestCoreRate` — applied at the next frame
  boundary on the emulation thread, re-deriving every DSP stage (blip
  resamplers, AY PLL + decimation FIRs, character chains, sample
  accumulator, recording rate). Deferred while a recording is active.
  On every rate republish the DRC controller state is reset
  (`resetDrcController`) so tracking restarts from fresh occupancy.

## 6. Field notes — every desync bug found after the design shipped

Each entry: symptom → root cause → fix → regression guard. These are the
document's reason to exist; the same mistakes are easy to remake.

### 6.1 Rubber-banding under the old watermark (pre-DRC)
Level-triggered "ring below 50%, send me a frame" messages queued up
during refills; stale requests released extra frames after recovery,
permanently inflating occupancy. **Lesson:** level triggers + async
queues make a rate controller with unbounded windup. Replaced by the
lock-free occupancy cell + per-frame PI sampling.

### 6.2 The target IS the latency (70 → 40 ms)
The original 70 ms setpoint was chosen for safety, but ring occupancy is
the audio presentation delay — 70 ms + HW buffer ≈ 82 ms audio-late,
clearly beyond the ~45 ms lip-sync perception threshold.
**Guard:** `SoundAdaptivity.AVLatencyBudget` pins target + HW buffer
against the perception threshold AND a minimum underrun margin.

### 6.3 Readouts sampling the sawtooth
The A/V display wandered +1..+21 ms — it sampled instantaneous occupancy
at random phase (section 4). **Fix:** display uses the DRC's filtered
occupancy; instantaneous shown alongside as "(N now)".

### 6.4 Emergency refill fighting the DRC (the 70→40 regression)
When the target moved 70→40 ms, the refill threshold (2048 device frames
≈ 46 ms) silently ended up ABOVE the new target — the sawtooth trough
crossed it every cycle, so the "emergency" path fired routinely, skipped
the pacing sleep, injected an extra frame and spiked occupancy ~+20 ms.
Intermittent +20..30 ms audio-late excursions; the DRC trimmed them down
only for the refill to re-inject. **Lesson:** any constant calibrated
against the target must be re-derived when the target changes — so it is
now expressed as a rate-aware ms constant next to the target, and the
trough relationship is asserted by `AVLatencyBudget` plus the closed-loop
`EmergencyRefill_NeverFiresAtSteadyState`.

### 6.5 `clear()` that didn't clear (200–1000 ms after reroute)
`AudioRingBuffer::clear()` zeroed the buffer BYTES but never moved the
read/write pointers. The audio produced during a slow device re-init
window (virtual devices take hundreds of ms to init) stayed queued as
SILENCE — occupancy 200–1000 ms after every reroute, and the ±0.5% trim
would need minutes to drain it. **Fix:** `clear()` advances the read
pointer to the write pointer (drops content), then zeroes.
**Guard:** `AudioRingBuffer_Test.ClearDropsQueuedContent`.

### 6.6 Overfill is unrecoverable by trim — hard resync
±0.5% trim drains excess at ~5 ms per second of playback; a 500 ms
overfill would take minutes. The device callback (consumer thread, the
only SPSC-safe place to advance the read pointer) now discards down to
the target in one step when occupancy exceeds `HARD_RESYNC_MS`, logs the
event, and tracking continues from there. Reached only through abnormal
events. **Guard:** threshold relationships in `AVLatencyBudget`;
`AudioRingBuffer_Test.DiscardAdvancesReadPointer`.

### 6.7 Ring capacity growth changed failure modes
Raising `MAX_SAMPLES_PER_FRAME` 2048→8192 (192 kHz support) quadrupled
ring capacity to ~1.5 s at 44.1k. What used to self-limit at ~370 ms of
lag could now hold a full second — which is how 6.5 became so visible.
**Lesson:** capacity changes alter how badly other bugs can hurt.

### 6.8 `CoreRate=auto` resolved before the device was known
Auto resolution read the per-emulator device-rate cell, which frontends
fill at BIND time — after the emulator (and its sound stack) is built.
Auto therefore always resolved to 44100; a 48 kHz device got a 44.1k core
and 44.1k FLAC recordings. **Fix:** process-wide default published at
device init (section 5). **Guard:** `Multirate_Test.CoreRateResolution`.

### 6.9 Same-device rate switches were invisible
miniaudio only notifies on device *changes*; a nominal-rate switch on the
same device kept the old negotiated rate behind a hidden OS resampler.
**Fix:** CoreAudio nominal-rate listener (section 5).

## 7. Measurement and monitoring

- `AudioDeviceDescriptor` (lock-free, owned by the frontend, registered
  via `SetAudioCallback`, readable via `GetAudioDeviceDescriptor`):
  device name/rate/channels, ring capacity, HW buffer, device output
  latency, reinit count, live occupancy, lifetime frame totals,
  over/underrun counters. Error counters must stay at zero permanently
  under converged control — growth is the first sign of trouble.
- Video side: `Screen::LatchFramebuffer` stamps a steady-clock timestamp;
  the GUI frame source computes paint−latch (+ present delay) as a signed
  EMA into `pVideoPresentLatencyUs`. (The EMA must be signed — the first
  version used unsigned arithmetic and wrapped to −4.2M ms.)
- UI: audio settings shows a static identity line (device @ native rate,
  core rate) and an ⓘ popup with the full component breakdown refreshing
  at 2 Hz while open. The popup is the first stop for any future "feels
  laggy" report: the component that grew names the culprit.

## 8. Regression guards (run with `core-tests`)

| Test | Pins |
|---|---|
| `SoundAdaptivity.AVLatencyBudget` | target vs perception threshold, underrun margin, refill-below-trough, hard-resync band |
| `SoundAdaptivity.EmergencyRefill_NeverFiresAtSteadyState` | closed loop: refill count 0 at converged steady state, sampled at the sawtooth trough |
| `SoundAdaptivity.DRC_ConvergesToTargetOccupancy` | PI convergence from both directions |
| `SoundAdaptivity.DRC_NativeDeviceRate48k_ConvergesAndConverts` | native-rate base ratio |
| `SoundAdaptivity.DRC_RebasesOnDeviceRateChangeMidRun` | reroute re-base + re-convergence |
| `SoundAdaptivity.SoundManager_ExactSampleCountOverAccumulatorPeriod` | drift-free accumulator |
| `AudioRingBuffer_Test.ClearDropsQueuedContent` / `DiscardAdvancesReadPointer` | ring drop semantics |
| `Multirate_Test.*` | core-rate resolution, live re-rate, pitch invariance |
| `VideoModeChange_Test.PresentQueue_DelaysVideoByConfiguredFrames` | A/V-sync video delay |

## 9. Glossary

- **Occupancy** — audio currently queued in the ring, in stereo frames or
  ms. Equals the audio presentation delay contributed by the ring.
- **Sawtooth** — the within-frame occupancy oscillation caused by bursty
  production (section 4).
- **Trim** — the DRC's ±0.5% adjustment to the resample ratio.
- **Trough** — the sawtooth minimum, `target − 1 frame`; the calibration
  point for the emergency refill.
- **Reroute** — the OS moving playback to a different device (or the same
  device at a different rate).
- **Hard resync** — one-step discard of overfilled ring content down to
  the target (consumer thread only).
