# Clicks in the mix: the device and the mixer disagreed on the frame's sample count

Date: 2026-09-13. Found while analysing two recordings of *Tech Support from Moe-bius* (`~/Music/20260913-142853-recording.flac`, `…-150102-…`), reported as "clicks and noise at the end", with Xpeccy Plus rendering the same tune without them.

## What the recordings showed

- Both are 184 s of the tune from the 128K snapshot. The tail from about 181 s is the tune's own end state: the player has stopped (it only writes SSG mixer register 7 = `0x3F` on both chips every frame; both SSGs have all volumes at 0), and one FM carrier is left keyed at a sub-audio pitch: a smooth sine of about 0.2–0.5 Hz at one-carrier level (±5750 after the 7.4 dB trim), bit-identical on left and right, so it is the mono FM path. On the real board the output coupling capacitors (`C16/C17`, hardware-reference §5) remove such a wave; our FM path has no high-pass, so it is reproduced and the recording ends mid-swing, which is a click at the cut. This part is the tune's state, not an emulation defect.
- On top of it: a click train. Reproduced in the emulator's own end-state capture: **one exactly-zero sample every 6 frames** (5419 samples at 44.1 kHz), sometimes every 12, with the HQ character chain on and off alike. With those samples interpolated away the residual drops to −70 dBFS, i.e. the rest is clean. During music the same dropouts are masked but audible as a faint 8 Hz click/buzz ("high-frequency modulation").

## Mechanism

`SoundManager::handleFrameEnd` decides how many samples it mixes this frame with an exact integer accumulator (`frame T-states × core rate`, one sample per `CPU_CLOCK_RATE`): 903, 903, …, 904 at 44.1 kHz. The TurboSound device filled its buffers from its own free-running `double` PLL fed with the T-states of every `handleStep`. Two things made the counts disagree on individual frames:

1. **Double-counted overshoot.** The last instruction of a frame runs a few T-states past the boundary; the device counted them at the end of frame N, then `handleFrameStart` zeroed `_lastTStates` while `z80->t` had been rebased to the overshoot, so the same T-states were counted again at the top of frame N+1.
2. **Different arithmetic and phase.** A `double` phase with `> 1.0` against an integer floor with `>=`, restarted at different moments.

When the device rendered 903 and the mixer consumed 904, the 904th sample of the AY and FM buffers was the frame-start `memset` zero: a one-sample dropout in every active source. When the device rendered 904 and the mixer consumed 903, a sample was silently dropped. `FrameSampleCount_Test` (new) models real frames with varying overshoot: before the fix 458 of 1500 frames disagreed at 44.1 kHz, 183 at 48 kHz, 345 at 96 kHz.

Why Xpeccy does not have it: its YM2203 wrapper produces the "last FM sample" on demand for whatever sample count its mixer asks for, so there is no second accumulator to disagree with.

## Fix

Both TurboSound devices (`soundchip_turbosound.*`, `soundchip_turbosoundfm.*`, identical loop per §11):

- The `double _ayPLL` became `uint64_t _samplePhase`, the same accumulator as the mixer's: `phase += ΔT × coreRate; while (phase >= CPU_CLOCK_RATE) emit`.
- `handleStep` clips the counted T-state position at `config.frame × speedMultiplier`, so every frame feeds exactly one frame of T-states into the accumulator; the overshoot is counted once, at the start of the next frame.
- The phase restarts in `reset()` and `setCoreRate()`, which is where `SoundManager` restarts its own accumulator (`reset()`, `applyCoreRate()`), so the two stay phase-locked from the first frame.

With this, the device's per-frame count equals the mixer's on every frame by construction. The average rate is unchanged (903.168 samples per Pentagon frame at 44.1 kHz; `Multirate_Test` and `SoundAdaptivity_Test` unchanged), the legacy/TSFM bit-identity gate still holds, and the end-state capture of the tune has no zero samples.

## Not changed

- The sub-audio FM content is what the tune leaves on the chip. A first-order high-pass on the FM path, matching the board's coupling capacitors, would hide it; it is not part of this fix.
- The YM3014 float quantization of a full-level slow sine is real on hardware too and is at −70 dBFS here.
