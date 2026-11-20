# Conclusions

Analysis in [analysis.md](analysis.md), numbers in
[measurements-2026-11-19-encoders.md](measurements-2026-11-19-encoders.md).

| Goal | Answer | Criterion |
|---|---|---|
| 1. Stage 1 rate | `libx264rgb -qp 0 -preset ultrafast` (lossless RGB) at 11.8x real time at 1324x1720 with the keyboard on the Mac, 1.8x on Linux without VideoToolbox; 2.9x at 1080 there. ffv1 is too slow (0.8-0.9x on Linux). | **Met** (≥ 2x Mac, ≥ 1x non-VT), except 4K: 0.4-0.9x everywhere, no-go |
| 1. Disk | x264rgb qp0: 1.5-1.6 GB/min at 1080 / kbd for full motion, 8-27 MB/min for a mostly still game; 6.4 GB/min at 4K | **Met** at ≤ 1080 (≤ 2 GB/min); not at 4K |
| 1. Size | Stage 1 must capture at the output size when CRT is on: the CRT is drawn for the output's height, a small capture scaled later loses it | Charter assumption corrected |
| 2. Stage 2 cost | Per minute recorded, demo at 1080: x264 18 s (Mac) / 1.4 min (Linux); x265 ultrafast 27 s / 2.5 min; x265 medium 1.3 min / 10 min; VT 17 s. Still games cost a fraction. Slower presets become usable. | Reported; the Mac was 10-20x faster than expected, Linux as expected |
| 3. Warning | On a steady host, a 25-frame probe with ffmpeg's start-up subtracted predicts within ±25 % in 8-10 of 10 runs for x265 and for x264 on still content. It under-estimates x264 on full motion by ~30 %, which is the safe side. A synthetic picture works as well as real frames. Cost: 0.7-0.8 s for x264, 1.6-1.9 s for x265. On the heavily shared Mac the steady rate itself swung 5-10x, so no probe can work there. | **Partly met**: accuracy yes on a steady host for x265; ≤ 1 s only with a shorter x265 probe; the shared Mac fails |
| 4. Encoder pacing | The stepped rate is limited by the frame path (direct frame, window draw, CRT compose, grab, keyboard), not by the encoder: 3.5-16x below the encoder at low load. Sound in the file is complete at any speed. The speakers would play chopped sound below 1x. | **Not met** (stepped within 20 % of the encoder): the warning's number is not the paced speed |
| 5. Proposal | See [proposal.md](proposal.md) | – |

## Side finding

`record/frames` with the native macOS writer (VideoToolbox, ProRes) fails intermittently: 12 of 27 runs, then 3 of
4 more. The first stepped frame runs before the encoder is ready and is dropped, and a paused machine makes no
second one. A 10 s timeout follows. Waiting for `encoder_ready_ms > 0` first fixed it in 4 of 4 runs. This is a
defect in landed code, separate from the POC. **Fixed in 1eb9ae71d** (2026-11-19): the step waits for the encoder;
8 of 8 runs (7 native, 1 ffmpeg) passed at load ~105.

## Limits of these measurements

- **Noise.** Both hosts were loaded by other sessions (the Mac up to load 245); absolute rates swing 2-10x.
- **Offscreen only.** The GPU window path (Mac cocoa, the Deck) was not measured, neither in stepped mode nor as
  live grab cost.
- **Not measured:**
  - stage 2 from an x264rgb intermediate;
  - quality per byte of the slower presets;
  - Windows and the Steam Deck.
