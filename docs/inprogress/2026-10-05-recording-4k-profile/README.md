# Recording profiles: 1080p / 1440p / 4K

Owner request (2026-10-05): a 4K recording profile (H.264 / H.265, any container); the output frame scaled sharply to
4K without blur, with the algorithm of the UI window; fast frame formation; GPU-accelerated encoding and software
encoding; in the video recording dialog and the recording widget; the dialog opens from the unreal-qt menu; every
automation plane (WebAPI, MCP, CLI, Lua, Python) with docs and the OpenAPI manifest; the videowall records too,
1080p and 4K fullscreen are a must.

## Decisions

- **Algorithm**: the UI window samples with Nearest (GPU `GL_NEAREST`, CPU `LosslessImageRendering`), so the profile is
  nearest neighbor too, **fitted** into the frame with the aspect kept (owner, 2026-10-05, after a 320x255 recording sat
  in 71% of the height with an integer 6x: "fit with maintaining ratio"). A non-whole factor makes source pixels k and
  k+1 output pixels wide, as in the window; an exact multiple (1080p in 4K) is exact blocks. Same code for every
  machine and region.
- **Where**: `FrameScaler` in `core/recording/src/common`, called from `RecordingManager::CaptureFrame`; the encoders
  take the finished frame (`scaleFactor = 1`), so the native macOS encoder, ffmpeg and NVENC all work unchanged.
- **GPU or not** is the encoder, not the scaler: `EncoderAcceleration` Auto / Hardware / Software (ffmpeg
  `useHardwareAccel`, previously declared and unused; Auto backend skips the native encoder for Software).
- **Codecs**: a fixed frame takes h264 / h265 only (`RecordingRequest::ValidateProfile`), any container.
- **Videowall**: the grab goes in unscaled for a fixed profile (it used `QImage::scaled(SmoothTransformation)` = blur);
  the dialog's backend and quality choices are now applied (they were ignored).

## Status

Core, automation (CLI, WebAPI, OpenAPI, MCP, Lua, Python), Qt dialog + toolbar + menu, videowall, docs: written.

## Zero-copy (added)

`FrameTarget` lending: VideoToolbox (CVPixelBuffer) and ffmpeg (queue slot + buffer pool) take the scaled frame with no
second copy. Measured on this Mac (load ~20, 3 rounds, 10 s each): process CPU native 17%, 4K 27%, idle 11-15%; before
zero-copy 4K was 36% against native 21%. Colors checked on a decoded frame (mean RGB equal to the native recording).
NVENC (Windows) lends its locked NV12 input buffer (branch `nvenc-zero-copy`): `FrameScaler::ScaleIntoNv12` converts
RGBA -> NV12 once per source pixel, byte-identical to the full-frame conversion (fuzzed, 3000 sizes, ASan clean).
Run on Windows 2026-10-26 (RTX 4090, driver 610.88, MSVC 2019 Release, `acceleration: hardware`, `profile: 4k`, 30 s of a
static 48K screen with a red border and eight PAPER bands, one app at a time, three rounds h264 and two rounds hevc):

| | frames in 30 s | recent fps | process CPU, 30 s |
|:--|--:|--:|--:|
| before (lent packed buffer + RGBA -> NV12 inside the encoder) | 370-381 | 12.5-13.0 | 28.7-28.9 s (one core saturated) |
| zero-copy NV12 | 1498-1508 | 50.1 | 8.9-10.0 s |

The baseline could not keep real time at 4K; the zero-copy path does, with a third of the CPU. Both files are h264 / hevc
3840x2160 yuv420p and decode in full; mean RGB of the picture is identical in both builds (180, 97, 83) and the side
bars are 0, 0, 0; against libx264 the NVENC frames score PSNR Y 43.9 / U 51.5 / V 48.9 dB (h264 and hevc alike).
`core-tests` (FrameScaler, RecordingManager: 29 tests) pass on Windows.

## TODO

- SIMD for `PackedToNv12` (the 4K-to-4K videowall case is bound by it).

- Benchmark `FrameScaler` at 3840x2160 (A/B, quiet machine) and decide on SIMD / threads (`SIMD-CANDIDATE(frame scaler
  row expand)` in the code).
- Real 4K recording check per encoder: VideoToolbox (h264, hevc), ffmpeg libx264 / libx265, NVENC on Windows;
  `ffprobe` shows 3840x2160.
- Videowall: the `QWidget::grab()` + `toImage()` + RGBA convert per tick is the remaining 4K cost; a direct
  `QOpenGLWidget` / window-buffer read would remove it.
- Qt toolbar widget offers h264 only (the dialog also h265).
