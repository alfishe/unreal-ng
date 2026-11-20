# 052 — window-recording-two-stage

The window recording (`unreal-qt/src/widgets/windowrecorder.cpp`, recipe `.recipe/media/window-recording.md`)
encodes while the machine runs. A running machine never waits for the writer: when ffmpeg is more than
`kMaxQueuedFrames` (10) pictures behind, the last picture is held for the frames it owes, and the file stutters.
The native macOS writer (VideoToolbox H.264 / H.265, ProRes) keeps up at 4K; the software encoders (x264 / x265
4:4:4 through ffmpeg: Linux, Windows, the Steam Deck, a Mac without the hardware path chosen) do not always.

Three ideas from the owner (2026-11-17), measured here before any design:

- **Variant `b-two-stage`.** Record losslessly at the picture's own size while the machine runs, close the file at
  once on stop, and transcode it to the chosen codec and frame size in the background.
- **The warning.** Before the recording starts, say how far below real time the chosen settings will run on this
  host. For example: "x265 1080p: about 8 fps, 16 % of real time".
- **Variant `c-encoder-paced`.** Emulation paced by the encoder. Today the stepped mode (`record/frames`) already
  does it: a paused machine runs one frame, and the next frame waits until the window has drawn this one and the
  writer has taken it.

Variant `a-baseline` is today's live encode.

## Goals

1. **Stage 1 rate.** Which lossless (or visually lossless) intermediate keeps up with real time (50 fps) at the
   picture sizes the recorder sends, with headroom on a loaded host?
   - The sizes: the window's picture (about 666x544); the CRT drawn at 1080 (about 1330x1080); 4K (2160); each
     with and without the on-screen keyboard.
   - The cost: data rate on disk, MB per minute.
2. **Stage 2 cost.** How long does the background transcode take, in multiples of the recording's length, for each
   codec, preset and frame size? Does it beat a live encode at the same settings?
3. **The warning.** Can a short probe at the start predict the steady encode rate within ±25 %? The candidates:
   - a calibration encode of the first frames;
   - a table of measured rates per setting, scaled by a host factor.

   What does the probe cost in start-up delay?
4. **Encoder pacing.**
   - Rate: what rate does the stepped mode reach per setting? It gives the emulation speed a paced live recording
     would run at.
   - Sound: what happens to the speakers' sound at that speed? Audio is the master clock (memory
     `feedback-audio-is-master-clock`), and the speed multiplier has no setting below 1x.
5. **Which variant fits which case**, as a proposal with its plane, GUI and file-format consequences.

## Requirements

- Real content, real pictures: frames captured from unreal-qt itself, through `record/frames` with CRT on, at the
  sizes the recorder sends.
  - Two workloads: a full-motion demo (`testdata/loaders/sna/eyeache1.sna`, the worst case) and a game
    (`Dizzy X.sna`, mostly still).
  - The pipe already drops repeated pictures (3556601ed), so a still screen costs little.
- Encoders:
  - the shipped ones: libx264 / libx265 4:4:4 with the presets the recorder uses, VideoToolbox H.264 / H.265,
    ProRes;
  - the lossless candidates: ffv1, utvideo, ffvhuff, magicyuv, libx264rgb `-qp 0`, rawvideo to disk.
- Hosts:
  - this Mac (M-series, shared and loaded: the load average is recorded with every run);
  - one host without VideoToolbox if reachable: a Linux build host or the Steam Deck.

## Constraints

- No change to `core/` or `unreal-qt/` in this POC. Measurement scripts only (`python/`). Raw data in
  `scratch/2026-11-19-poc-052/` of the worktree.
- My own unreal-qt instance only: offscreen, port 8731, muted.
- The shared Mac is loaded by other sessions:
  - every rate is the median of at least 3 interleaved runs, with the load average logged;
  - ratios between variants count more than absolute numbers.
- A few hours of machine time. No GUI windows.

## Acceptance criteria / expected results

- **Stage 1.** Go for `b-two-stage` if one intermediate runs at ≥ 2x real time (≥ 100 fps) at 1330x1080 with the
  keyboard on this loaded Mac, and at ≥ 1x on the host without VideoToolbox. Expected: ffv1 / utvideo at 150-300 fps
  at the window's size (POC run 2026-11-17: ffv1 252 fps, x264 qp0 200 fps at 702x576) and 2-3x slower at 1080.
- **Disk.** Acceptable at ≤ 2 GB per minute at 1080. Expected: ffv1 0.3-1 GB/min (the CRT scanlines compress
  worse than flat ZX pixels).
- **Stage 2.** Reported as a factor of the recording's length. Expected: x265 ultrafast 1080p 5-10x the length on
  this Mac, x264 1-2x, VideoToolbox below 0.5x.
- **The warning.** Good enough if the calibration probe of ≤ 1 s lands within ±25 % of the steady rate in 8 of 10
  runs.
- **Encoder pacing.** It works if the stepped mode's rate is within 20 % of the encoder's own rate (the capture and
  pipe cost little), so the warning's number is also the paced emulation speed.
