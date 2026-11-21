# Proposal

From [conclusions.md](conclusions.md). Feature: the window recording (`.recipe/media/window-recording.md`,
`unreal-qt/src/widgets/windowrecorder.cpp`, `core/src/emulator/video/windowrecording.cpp`).

## 0. Fix first: the stepped mode with the native writer (done, 1eb9ae71d)

`WindowRecording::RunFrames` waits, before the first `RunFrame`, until the status reports `encoderReadyMs > 0`.
It keeps the same 10 s deadline and fails with "the encoder did not start" instead of "recorded nothing". This is
a small core change with a unit test: a fake provider that becomes ready late. Independent of the rest.

## 1. Adopt b-two-stage as an option: "lossless now, encode after stop"

- **Request.** A new request field on every plane: `encode: "live" | "after"` (default `live`).
  - CLI, Python, Lua, MCP, REST and WebSocket, as the automation-planes guideline requires.
  - GUI: one checkbox in the recording widget.
- **Stage 1.**
  - Codec: ffmpeg `libx264rgb -preset ultrafast -qp 0 -pix_fmt bgr0`, Matroska. The same Matroska BGRA pipe
    and VFR timestamps as today.
  - Size: the output size, CRT drawn at the output height. The pad / letterbox (`-vf pad`) moves to stage 2.
  - Sound: copied as PCM into the same file. No AAC in stage 1.
  - File: a hidden `.<name>.stage1.mkv` beside the target.
- **Stage 2.**
  - Starts on stop. A background `ffmpeg` with today's encoder arguments: codec, preset, CRF, pixel format, pad,
    AAC.
  - Runs at low priority (`nice` / `IDLE_PRIORITY_CLASS`).
  - Status:
    - `finalizing` stays true;
    - a new `transcode_progress` (0..1) and `transcode_eta_s` come from ffmpeg's `-progress`;
    - the intermediate is deleted on success and kept with an error on failure.
  - A new recording may start while a transcode runs. Transcodes queue, one at a time.
- **When to offer it.**
  - Any software encoder path (x264 / x265 through ffmpeg). Hosts without VideoToolbox are where it matters.
  - Not for the native Mac writer (VT / ProRes already keep up).
  - Not for 4K: stage 1 itself falls behind there (0.4-0.9x). The GUI greys it out with the reason.
- **Disk check before the start.** About 1.6 GB per minute at 1080 for full motion. Refuse or warn when the free
  space is under 10 minutes' worth.
- **Gains.**
  - The live run keeps up where x265 runs at 0.3-0.5x.
  - Slow presets (x265 `medium`) become usable, at 2.5-10x the length on Linux in the background.

## 2. The warning: two parts, the live one is the main one

1. **While recording (always right).** Add `frames_repeated` (the frames `repeatLast` filled) and `encoder_fps`
   (unique pictures encoded per second over the last 2 s) to the recording status on all planes.
   - GUI: the recording widget turns amber, "encoder behind: 12 % of frames repeated", with a one-click hint to
     restart in `after` mode.
   - This works on a busy host, where no prediction does.
2. **Before the start (an estimate).** For software encoders only:
   - Probe: 12 frames of a synthetic picture at the output size through the chosen arguments, with ffmpeg's
     start-up measured apart and subtracted. The 1-frame run gives the start-up.
   - Cost: about 1 s.
   - Shown as "≈ 24 fps, about half of real time: choose 'encode after stop' or a smaller size".
   - Only when the estimate is under 1.3x real time; silence otherwise.
   - Cached per (encoder, size) for the session, refreshed on demand.
   - Wording says "estimate", since a loaded host moves it 2-10x.

## 3. Drop c-encoder-paced as a live mode

- **The rate is wrong.** The frame path, not the encoder, sets the paced rate (3.5-16x below the encoder). The
  speed shown would not be the speed run.
- **The speakers.** They would play chopped sound below 1x: mute them, or build slow-motion audio that does not
  exist today.
- **Deadlock risk.** Pacing the emulation thread on a writer fed by the GUI thread is the deadlock shape fixed in
  9624e5291.
- **Already covered.** b-two-stage covers the live cases up to 1080. The stepped mode (`record/frames`) remains
  the exact, every-frame path for scripted recordings, including 4K.

## 4. Follow-up measurements before building (small)

- **Two-stage paths not measured here:**
  - stage 2 from an x264rgb intermediate;
  - stage 1 through the real pipe in unreal-qt, with PCM in the same file.
- **The GPU window's grab cost** at 1080 and with the keyboard (cocoa on the Mac, the Deck). The live recording
  pays it every frame.
- **The frame-path cost in stepped mode with the keyboard** (21 fps): profile it. It limits every exact
  recording.

## 5. Since then (2026-11-19)

- **Linux has GPU encoders in the window recorder.** NVENC 4:4:4 (Turing and later) and VA-API (HEVC 4:4:4 in the
  low-power encoder) keep up at 4K: the two-stage option matters less there, for hosts without either.
- **Open:**
  - the stepped mode under Xvfb on Linux stalls ("recorded nothing within 10 s") at the same frame each run, GPU
    window or software screen; real time records;
  - QSV refused the recorder's VFR stream on Linux ("Current frame rate is unsupported"); Windows still offers it
    and is not checked.
