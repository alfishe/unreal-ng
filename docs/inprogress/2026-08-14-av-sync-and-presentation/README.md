# A/V Synchronization, Frame Pacing, and Tear-Free Presentation

**Date:** 2026-08-14 .. 2026-08-16
**Status:** Implemented, verified on hardware demos (Insult megademo, tape loading)
**Related:** `2026-08-09-video-timing-analysis-mister-vs-emulator` (INT position work)

## Problems addressed

1. **>100 ms audio lag behind video during tape loading.** The main loop's
   free-run fallback used a hardcoded 20 ms timeout while a Pentagon frame is
   20.48 ms (71680 t-states @ 3.5 MHz). Audio was overproduced by ~2.4%,
   slowly filling the 371 ms playback ring buffer to its cap.
2. **Variable speed "rubber-banding" in demos.** The first fix used a relative
   `wait_for(frame_duration)` per frame; scheduler wake-up latency added to
   every period, draining the ring to its watermark and triggering bursts of
   catch-up frames.
3. **Mid-frame tearing** in the Qt display (seam, e.g. at x=175/176): the GUI
   painted a zero-copy QImage over the emulator's live framebuffer while the
   emulation thread was overwriting it.
4. **1.5-2x music speedup in EI:HALT-synced IM2 demos** (Insult megademo):
   a stale INT latch across the frame wrap delivered a second interrupt per
   frame (see below).
5. **Constant 100-200 ms video-ahead-of-audio offset** after the pacing fix:
   stale audio-hunger messages piled up during startup refill, permanently
   inflating ring occupancy.

## Architecture (as implemented)

### Frame pacing: the frame clock is the master

`MainLoop::Run` paces emulation with an **absolute deadline**:
`_nextFrameTime += frame_duration; cv.wait_until(_nextFrameTime, ...)`.

- `config.frame_duration_us` is computed once at config load by
  `CalculateFrameDurationUs()` (`core/src/emulator/sound/audio.h`),
  **ceil-rounded** from `config.frame` t-states at 3.5 MHz.
  Pentagon: 20480 us = **48.828 fps**. ZX48/128/Scorpion: 19968 us.
- `wait_until` against an accumulated deadline self-corrects scheduler
  latency; a relative `wait_for` accumulates it (problem 2).
- The audio ring low-watermark request (`NC_AUDIO_BUFFER_HALF_FULL`) is only
  **rare slip correction** for clock drift between the host clock and the
  audio DAC crystal. It wakes the loop early and re-anchors the deadline.
  It is NOT the pacer.
- The deadline re-anchors after pause/stall (never "catches up" more than one
  frame from a stale deadline).

### A/V offset model

Video is presented from the frame-end latch (below) essentially immediately;
audio plays `ring occupancy + device buffer (11.6 ms)` later. Therefore:

> **A/V offset = audio ring occupancy.** Bounded, constant, and set by the
> 40 ms low watermark plus at most ~1 frame of refill overshoot (~50-70 ms).

The hunger notification is rate-limited to one post per ~4 audio callbacks
(~23 ms) in `unreal-qt/src/emulator/soundmanager.cpp`. Without the limit,
posts every 5.8 ms pile up in the MessageCenter queue during refill; each
stale post releases one extra 20.5 ms frame of audio, permanently inflating
occupancy (problem 5). Occupancy never drains by itself - deadline pacing is
drift-free - so overshoot at startup is frozen in.

To reduce the offset further: lower the watermark to 1 frame, shrink the
miniaudio period (256 frames), or (endgame, not implemented) present video
against the ring's read cursor with a timestamped frame queue.

### Tear-free presentation: the frame-end latch

`Screen::LatchFramebuffer()` (called from `MainLoop::OnFrameEnd` after
rendering, both batch and ScreenHQ modes) snapshots the completed frame into
a presentation buffer under `_presentMutex`. Consumers call
`Screen::CopyPresentedFramebuffer(dst, size)` from any thread.

- Copies use `VideoUtils::CopyFrameBuffer` (`core/src/common/video/videoutils.h`):
  64 B/iteration NEON/SSE2 with a scalar memcpy tail (~40 us for 340x284x4).
  The mutex is held only for the copy, so the emulation thread is never
  blocked by GUI paint duration.
- `DeviceScreen` (unreal-qt) pulls via a `FrameCopyFn` set in
  `MainWindow::adoptEmulator` into an owned QImage; the legacy zero-copy path
  remains as fallback when no frame source is set. `detach()` clears the
  callback before the Screen can die.
- ScreenHQ per-t-state rendering still works: the latch happens at frame end,
  so every displayed frame is a complete raster (multicolor effects intact).

### INT stale-latch fix (double interrupts)

`ProcessInterrupts` clears `int_pending` via `t >= int_end`, but the Pentagon
window `[71635, 71667)` ends only 13 t-states before the 71680 wrap. When INT
acceptance itself carries `t` across the frame boundary (HALT-phase
dependent), that clear never fires; the stale flag then delivered a second
interrupt after the program's next `EI` - doubling music tempo in EI:HALT
demos, intermittently (hence "floating" 1.5-2x).

Fix: `Core::AdjustFrameCounters` drops `int_pending` on frame wrap. This is
hardware-correct - the ULA INT line is always deasserted by frame end when
the window does not wrap; legitimately wrapping windows (`int_end >= frame`)
are re-armed at the start of the next `Z80FrameCycle`.

### Sound generation adaptivity

All generators and DSP stages derive their per-frame sample count from the
machine's frame length, never from the 50 Hz `SAMPLES_PER_FRAME` constant
(882): Pentagon produces 903 samples/frame, ZX48/128 881, +2A/+3 893.
Fixed during this work:

- AY character chains processed a hardcoded 882 pairs while ~903 were
  generated and mixed - the unfiltered tail plus per-frame DSP-state
  discontinuity was audible as frame-rate harmonics/hiss on pure tones.
  Now processed with the actual per-frame count (`soundmanager.cpp`).
- Covox read/padded/DC-filtered exactly 882 samples; the mixer consumed a
  stale tail. Now adaptive (`covox.cpp`).
- Beeper and Covox blip_buf accumulators were sized `SAMPLES_PER_FRAME + 64`
  (946) - Pentagon's 903 barely fit and any speed multiplier overflowed.
  Now sized `MAX_SAMPLES_PER_FRAME + 64`.

`core/tests/emulator/sound/sound_adaptivity_test.cpp` sweeps five frame
lengths (69888/70908/71680/58333/80000 t-states) through SoundManager,
Beeper, Covox, and TurboSound, asserting the output count follows the frame
length within one sample.

## Invariants (do not break)

1. `config.frame_duration_us` must be recomputed whenever `config.frame`
   changes, and must never round down (overproduction fills the ring).
2. The pacing wait must be `wait_until` on an accumulated deadline, never a
   relative `wait_for` per frame.
3. GUI/capture consumers must read the latched presentation buffer, never the
   live framebuffer.
4. `LatchFramebuffer()` must run at frame end after all rendering, in both
   batch and ScreenHQ modes.
5. `int_pending` must not survive a frame wrap (except via the explicit
   wrapped-window re-arm in `Z80FrameCycle`).
6. Audio-hunger posts must stay rate-limited (< 1 per frame duration).

## Files

- `core/src/emulator/mainloop.cpp/.h` - deadline pacing, latch call
- `core/src/emulator/sound/audio.h` - `CalculateFrameDurationUs`
- `core/src/emulator/config.cpp`, `core/src/emulator/platform.h` - `frame_duration_us`
- `core/src/emulator/video/screen.cpp/.h` - presentation latch
- `core/src/common/video/videoutils.h` - SIMD frame copy
- `core/src/emulator/cpu/core.cpp` - INT stale-latch clear
- `unreal-qt/src/emulator/soundmanager.cpp/.h` - watermark rate limit
- `unreal-qt/src/widgets/devicescreen.cpp/.h`, `unreal-qt/src/mainwindow.cpp` - latched paint path

## Tests

- `core/tests/emulator/frame_pacing_test.cpp` - frame duration math, Pentagon
  48.83 fps, audio/video per-frame duration agreement (sub-sample), config
  load integration
- `core/tests/emulator/cpu/int_pending_wrap_test.cpp` - stale INT latch
  cleared on wrap, in-window request preserved mid-frame
- `core/tests/emulator/video/present_latch_test.cpp` - SIMD copy exactness
  (tail sizes, overrun guard), latch snapshot isolation from live-buffer
  writes, undersized-destination rejection
