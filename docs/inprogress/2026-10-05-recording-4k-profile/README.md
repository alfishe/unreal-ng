# Recording profiles: 1080p / 1440p / 4K

Owner request (2026-10-05): a 4K recording profile (H.264 / H.265, any container); the output frame scaled sharply to
4K without blur, with the algorithm of the UI window; fast frame formation; GPU-accelerated encoding and software
encoding; in the video recording dialog and the recording widget; the dialog opens from the unreal-qt menu; every
automation plane (WebAPI, MCP, CLI, Lua, Python) with docs and the OpenAPI manifest; the videowall records too,
1080p and 4K fullscreen are a must.

## Decisions

- **Algorithm**: the UI window samples with Nearest (GPU `GL_NEAREST`, CPU `LosslessImageRendering`), so the profile is
  nearest neighbor too - with the largest **integer** factor that fits, bars for the rest, so every source pixel is the
  same k x k block (a non-integer nearest fill makes uneven pixel widths). Same code for every machine and region.
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
It compiles with mingw (syntax check) but has NOT run on Windows: do not merge to master before an NVENC run there
(h264 + hevc, 4K, mp4 plays, colors, bars black).

## TODO

- Windows run of the NVENC zero-copy path (branch `nvenc-zero-copy`), then merge.
- SIMD for `PackedToNv12` (the 4K-to-4K videowall case is bound by it).

- Benchmark `FrameScaler` at 3840x2160 (A/B, quiet machine) and decide on SIMD / threads (`SIMD-CANDIDATE(frame scaler
  row expand)` in the code).
- Real 4K recording check per encoder: VideoToolbox (h264, hevc), ffmpeg libx264 / libx265, NVENC on Windows;
  `ffprobe` shows 3840x2160.
- Videowall: the `QWidget::grab()` + `toImage()` + RGBA convert per tick is the remaining 4K cost; a direct
  `QOpenGLWidget` / window-buffer read would remove it.
- Qt toolbar widget offers h264 only (the dialog also h265).
