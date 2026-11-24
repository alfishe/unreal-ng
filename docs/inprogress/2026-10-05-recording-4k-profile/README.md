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

- ~~Window recorder: hand ffmpeg the frame at the target size as NV12~~ done 2026-11-23: `transfer` = `picture`
  (default) / `nv12` on every plane; worth it with the picture drawn at the frame's height (measurements in
  [POC 054](../../../tools/poc/054-ffmpeg-frame-transport/README.md#the-window-recorders-transfer-switch-2026-11-23)).
- ~~`transfer nv12`: build the frame in NV12 (no 4K B,G,R,A compose and conversion)~~ done 2026-11-24: the picture
  converted at its own size and scaled in its planes (`FrameScaler::PackedToYuv` + `PlaneScaler`, bicubic), the
  writer 1.2-1.8x faster per changed frame on `.238` / `.20`, the stages 2-13x on `.40`
  ([POC 054](../../../tools/poc/054-ffmpeg-frame-transport/README.md#transfer-nv12-built-in-nv12-2026-11-24)).
- ~~SIMD for `PackedToNv12`; benchmark `FrameScaler` at 3840x2160 and decide on SIMD / threads~~ done 2026-11-20: NEON
  (arm64) and SSE2 (x86-64) row kernels for luma, per-pixel chroma and the 2x2 chroma blocks, byte-identical to the
  scalar formula (`PackedToNv12_VectorRowsMatchTheFormulaAtEveryWidth`). `core-benchmarks
  --benchmark_filter=FrameScaler` (`core/benchmarks/emulator/recording/framescaler_benchmark.cpp`), CPU time per
  3840x2160 NV12 frame, interleaved A/B:

  | Source | M1 Ultra scalar | M1 Ultra NEON | i7-11850H scalar (gcc 15) | i7-11850H SSE2 |
  |:--|--:|--:|--:|--:|
  | 3840x2160 (1x, the videowall grab) | 13.96 ms | 2.24 ms | 15.4 ms | 5.6 ms |
  | 1920x1080 (2x) | 6.23 ms | 2.33 ms | 7.5 ms | 3.4 ms |
  | 960x540 (4x) | 2.37 ms | 1.07 ms | 2.4 ms | 1.26 ms |
  | 352x288 (fit, 7.5x) | 2.95 ms | 2.66 ms | 3.2 ms | 2.9 ms |

  No threads: a 4K frame is now near the memory bandwidth on arm64 and well under a frame's time on both. The RGBA
  `ScaleInto` is 1-2 ms at 4K. Its row expansion was measured 2026-11-21 and stays scalar: for k = 2..4 the
  compiler's fills beat NEON vst2q / vst3q / vst4q, and broadcast stores for k >= 5 halve the row on arm64 but
  that is 5-8 % of the frame (the repeated-row copies and bars are memory-bound) and slower on x86. The fit path
  chroma is the item below.
- ~~VideoToolbox integer upscale (`video/record` `scale` 2..4) as a per-pixel swizzle loop on the emulation
  thread~~ done 2026-11-21: `FrameScaler::ScaleInto` with the red / blue swap (SIMD swap, a factor-3 expand case, the
  1x swap straight into the output), byte-identical to the loop it replaced. Per frame on an M1 Ultra: 352x288 x2
  154 -> 41 us, x3 271 -> 89 us, x4 458 -> 229 us, 960x540 x2 819 -> 252 us (the x86-64 build, SSE2, run under
  Rosetta: 2-3x as well). The encoder's own NEON / SSSE3 copy stays for 1x (ScaleInto is 6-19 % slower there).
- ~~NV12 chroma of the fitted picture and of odd whole factors (a per-block loop with four lookups and branches)~~
  done 2026-11-21: `ChromaPlaneBuilder` reduces each source row once to the R, G, B sums of the two pixels under each
  chroma column; a block row is the sum of two such rows through a NEON / SSE2 kernel. Byte-identical on arm64 and
  x86-64 (the old and new outputs compared, `ScaleIntoNv12_MatchesTheFullFrameConversionByteForByte` widened). To
  3840x2160 on an M1 Ultra: 352x288 (7.5x fit) 2.40 -> 1.38 ms, 320x256 2.17 -> 1.27 ms, 2048x1536 (fit) 9.2 ->
  6.4 ms, 1280x720 (3x) 5.17 -> 2.4 ms, 768x432 (5x) 4.43 -> 2.98 ms; even factors and 1x unchanged.
- ~~GIF (the `video/record` default, `Auto` palette) on the emulation thread~~ done 2026-11-21. `GifWriteFrame` made
  the median-split palette of the whole frame twice per frame, and a repeated frame turned pixel (0,0) black (a
  1x1 frame of the previous index with a palette of no colors). Now: a repeated frame is one transparent pixel; the
  changed pixels of a frame with at most 255 colors get an exact palette (one entry per color, the fewest code bits)
  in two passes, no median split; more colors keep the median split. Quality: every decoded frame equals its source
  on six ZX programs (the old encoder was off in 2-200 of 200 frames, by up to 255 per channel) and on 16-color
  noise; the GIF bytes are the same on macOS, Linux and Windows. Mean ms per frame, 5 interleaved rounds (M1 Ultra
  clang / i7-11850H gcc 15 / Windows host MSVC 2022): earshaver 5.6 -> 2.2 / 5.6 -> 1.2 / 3.3 -> 1.0; aleste 2.9 ->
  2.1 / 2.3 -> 1.4 / 1.5 -> 1.0; Dizzy 1.1 -> 0.6 / 0.7 -> 0.4 / 0.7 -> 0.5; across-the-edge 10.7 -> 1.3 / 11.6 ->
  1.2 / 7.0 -> 1.0; 16-color noise 48 -> 3.8 / 54 -> 6.3 / 28 -> 3.4; a 1024-color frame (median split) 42 -> 33 /
  49 -> 37 / 30 -> 21. Files 4-49 % smaller; a live 5 s recording records the same frames, 37-50 % smaller.
- ~~NVENC frames at the native size (`scale` 1..4, not a fixed profile) converted by a scalar loop~~ done 2026-11-21.
  Only the fixed profiles lent the NVENC input buffer to FrameScaler; the native-size path converted every output
  pixel (BT.601, nearest scale) into a staging NV12 buffer and copied it into the locked buffer. Now
  `FrameScaler::ScaleIntoNv12` writes straight into the locked buffer: the same bytes (checked for x1..x5, odd source
  sizes, a 256-byte pitch), no staging buffer. Per frame, MSVC 2022 on the Windows host: 352x288 x1 1389 -> 46 us,
  x2 5.6 -> 0.19 ms, x3 12.6 -> 0.57 ms, x4 22.4 -> 0.35 ms (clang arm64 / x86_64: 5-24x). Live 8 s recordings on an
  RTX 4090, 3 interleaved rounds: at x4 the old encoder kept 236-316 frames (29-39 fps), the new one 392-394
  (48.8 fps, every frame); at x2 and x3 both keep every frame; the first decoded frames are the same.
- ~~Window recorder (ffmpeg and VideoToolbox paths): a fresh frame image per picture, a scalar red/blue swap~~ done
  2026-11-22. The swap loop read `picture.width()` in its condition; every `uint32_t` store may alias the image, so
  the width was reloaded and the loop stayed scalar. A local width vectorizes it. The ffmpeg path composed into a new
  4K `QImage` every picture (page faults); two reused buffers now hold the picture being composed and the last one
  sent (the still-screen `memcmp` stays). Microbenchmark, 4K, 3 interleaved rounds, swap on 4 threads / on one:
  M1 Ultra clang 5.2 / 16-42 -> 1.2 / 2 ms; i7-11850H gcc 15 12.7 / 29 -> 4.9 / 7.1 ms; Ryzen MSVC 4.4 / 16.8 ->
  2.4 / 3.1 ms; filling a reused image 2.0 ms against 4.1 fresh (MSVC). Live on Linux (xvfb, stepped 4K libx264,
  5 interleaved rounds of 200 frames): the same decoded frames (framemd5) in both builds, no slower; the stepped
  call runs at the real-time pace either way. Transport to ffmpeg measured apart: POC 054 (the macOS FIFO is the
  4K limit, a unix socket moves 10x more). The window recorder writes to ffmpeg's stdin, an anonymous pipe 2.6x
  faster than the FIFO on macOS: at the 4K CRT size (2640x2160) 58-131 fps against 115-283 through a socket, with
  the encoders the limit before either; no socket (2026-11-23, POC 054 "Window recorder").
- ~~ffmpeg (software and `-c:v` hardware through the pipe) with a fixed profile: the 33 MB 4K frame through the FIFO
  and ffmpeg's RGB conversion of 8.3 M pixels~~ done 2026-11-22 (POC 054). The pipe carries the source picture
  (352x288 RGBA, 0.4 MB) and ffmpeg builds the frame at the target size given explicitly: the colour conversion at
  the source size (4:4:4), then whole factors `neighbor` + `pad`, other factors `remap` with FrameScaler's own
  mapping as gray16le tables (byte-identical 4:4:4 to FrameScaler's frame for 352x288 / 320x240 / 256x192 into 4K,
  1440p, 1080p), then `area` 4:2:0 (+0.06..0.10 dB over swscale's conversion of the RGB frame). A raw input has one
  size, so each picture size is a segment (one ffmpeg, its end on a thread of its own so no frame waits for the
  flush; the queue holds 5 s of source pictures); the sound goes into a PCM file and Stop joins the segments (stream
  copy) with the sound encoded. Live, `video/record` h264 software, 9 s, interleaved rounds, frames in the file:

  | Host | 1080p old / new | 1440p old / new | 4K old / new |
  |:--|--:|--:|--:|
  | M1 Ultra, 3 rounds | 440 / 440 | 177-381 / 440 | 59-83 / 440 |
  | i7-11850H Linux (gcc 15), 3 rounds | 440 / 440 | 440 / 440 | 440 / 440 |
  | Windows 64 threads (MSVC 2022), 3 rounds | 441 / 441 | 441 / 441 | 440-442 / 440-441 |

  CPU of the app and its ffmpeg per recorded frame (psutil): M1 Ultra 4K 185 -> 108 ms (old 255 frames, new 440),
  1080p 82 -> 77 ms; Windows 4K: the app 5.4 -> 2.6 s (the emulation thread halves), ffmpeg 20.8 -> 25.5 s (`remap`
  at 4K), 7 % more in total where the old path kept up. The same pictures (PSNR between the files 52-54 dB, the
  1-frame start offset removed). With sound the old fixed profiles left an mp4 without `moov` on macOS in every run
  (6 of 6); the new path's sound is one AAC track of the whole recording. Overscan toggled twice mid-recording:
  3 segments, 443 frames, no frame lost. Side fix: `FFmpegProbe::searchPath` looked for `ffmpeg.exe.exe` on Windows,
  ffmpeg on PATH was never found there.
- ~~The fixed-profile ffmpeg graph: `remap` over the whole 4K frame (~40-60 ms of ffmpeg CPU per frame), the bars
  padded before 4:2:0~~ done 2026-11-23. swscale's nearest (`neighbor`) takes FrameScaler's source pixel at some
  factors (7.5x: 352x288 into 4K, 256x192 into 1440p) and is one pixel off at others (3.75x, 4.5x, 5.625x, 11.25x,
  8.4375x), the same on ffmpeg 8.0.1, 9.0.2 and master: so each segment asks its ffmpeg
  (`FFmpegPipeEncoder::NeighborIsFrameScaler`: two thin pictures holding their column / row index, cached) and uses
  `neighbor` where it agrees, `remap` tables of the picture only (not the frame) elsewhere. The bars come after
  4:2:0 when the picture sits on whole chroma blocks (even size and offsets). Every variant is the same bytes as
  before (yuv420p md5, 9 source / target pairs, three ffmpeg builds). ffmpeg CPU per frame, the graph alone,
  3 interleaved rounds:

  | Case | Windows (64 threads) | Linux i7-11850H | macOS M1 Ultra |
  |:--|--:|--:|--:|
  | 352x288 -> 4K, `neighbor` instead of `remap` | 41 -> 17 ms | 61 -> 22 ms | |
  | 256x192 -> 4K, `remap` of the picture | 44 -> 38 ms | 61 -> 52 ms | |
  | 352x288 -> 1080p, `remap` of the picture | 14 -> 10 ms | 16 -> 13 ms | |
  | 352x288 -> 4K, bars after 4:2:0 | 16.5 -> 10.7 ms | -17 % | 18.6 -> 13.3 ms |

  The `remap` of the picture costs Windows wall time (1.2 -> 1.5 s per 300 frames, still 200 fps). Live on macOS
  (windowless unreal-qt, `video/record` libx264, 9 s): 4K 447 frames, 1080p 445, the picture and bars in place.
- ~~VA-API in `FFmpegPipeEncoder`: never worked~~ fixed 2026-11-23. The encoder chose `h264_vaapi` / `hevc_vaapi`
  whenever the build listed them (every Ubuntu ffmpeg does) and gave ffmpeg neither the device nor the upload:
  ffmpeg refused the graph ("Impossible to convert between the formats"), so with acceleration `auto` (the default)
  every core recording failed on a Linux machine with an Intel driver. Now a trial encode per render node finds the
  device (none: NVENC / software as before), the frame goes up as NV12 (`format=nv12,hwupload`) and the bars are
  added on the GPU (`pad_vaapi`, ffmpeg 7.0 and later; before that on the CPU). Tiger Lake, 4K from 352x288, 500
  frames: 4K frame uploaded 36 ms CPU per frame at 34 fps; the picture uploaded and padded on the GPU 25 % less
  CPU at ~60 fps; same bytes after `hwdownload`. Live on Linux (windowless unreal-qt, `video/record`, 9 s): 4K
  `auto` (VA-API, High profile) 444 frames with 10 s of ffmpeg CPU against libx264's 445 frames with 18 s, 1080p
  `auto` 450 frames; the picture and bars in place. Scaling on the GPU (`scale_vaapi`, the POC 054 plan) was dropped:
  2.6 ms CPU per frame, but every mode blurs the pixels (no nearest) and the colour drifts (PSNR 15 dB against
  the CPU frame).
- Real 4K recording check per encoder: VideoToolbox (h264, hevc), ffmpeg libx264 / libx265, NVENC on Windows;
  `ffprobe` shows 3840x2160. Linux window recording done 2026-11-19 (NVENC on a T1200 and VA-API on Tiger Lake, both
  3840x2160, every frame, PSNR 41-52 dB against the source; `.recipe/media/window-recording.md`).
- ~~Stepped window recording (`video/window/record/frames`) through ffmpeg with sound stalls ("recorded nothing
  within 10 s", then "the recording stopped")~~ fixed 2026-11-21, found under Xvfb on Linux. ffmpeg analysed the raw
  sound input for its default 5 s before reading more picture; a paused machine makes sound only as its pictures are
  taken, so both sides waited. The FIFO input gets `-probesize 32 -analyzeduration 0` (its format is all given).
  3 x 50 steps now take 0.4-0.6 s each, the file 150 frames = 3.072 s of picture and sound; running is unchanged
  (195 frames in 4 s, both streams from 0). The Mac's default `native` engine does not use ffmpeg; `engine: ffmpeg`
  has the same arguments.
- ~~Videowall: the `QWidget::grab()` + `toImage()` + RGBA convert per tick~~ done 2026-11-21. With the GPU grid
  (`--gpu`, a `QOpenGLWindow` in a window container) the recording and the wall screenshot were an empty grey frame:
  `grab()` of the container does not see the GL window. The GL grid now reads its framebuffer back after each paint
  while recording (`TileGridGL::setCaptureEnabled`, `glReadPixels` RGBA, 3.4-3.6 ms at 2048x1536 on an M1 Ultra) and
  the recorder takes the last frame; the screenshot uses `grabFramebuffer()`. The CPU grid is painted with `render()`
  into a reused RGB32 image instead of a new pixmap per tick (16 tiles, 2048x1536 into the 4K profile, three
  interleaved rounds: 8.7-10.3 ms against 10.6-15.2 ms; painting into RGBA8888 directly was 2-2.5x slower, the
  raster engine's slow path). The CPU grid's cost is the tiles' own painting.
- ~~Videowall GL grid: the synchronous `glReadPixels` after each paint~~ done 2026-11-22. `GLFrameReader` queues the
  copy into a pixel-pack buffer at the paint and maps it at the recorder's tick (`TileGridGL::capturedFrame`), the
  newest frame, no extra latency. A software renderer (llvmpipe, softpipe, SwiftShader, GDI Generic) and contexts
  without the buffers (ES 2, GL < 2.1) keep the synchronous read. GUI-thread time per frame (paint + take, median of
  interleaved rounds; offscreen bench: 16 tiles of 352x288 uploaded and drawn per frame, 50 Hz, the tick 10 ms after
  the paint), frames byte-identical to the old read on every driver:

  | GPU / driver | 2048x1536 old -> new | 3840x2160 old -> new |
  |--------------|----------------------|----------------------|
  | M3 Ultra (idle) | 2.5 -> 0.35 ms | 3.6 -> 1.1 ms |
  | M1 Ultra (load 160-300) | 11-82 -> 1.1-27 ms (9 of 9 pairs better) | 10-25 -> 6-36 ms (7 of 9 better) |
  | RTX 4090, Windows | 8.8 -> 1.4 ms | 17.4 -> 3.4 ms |
  | Intel UHD (TGL), Mesa 26, Wayland | 12.1 -> 2.3 ms | 20.1 -> 5.9 ms |
  | T1200 (PRIME offload), Linux | 14.9 -> 5.5 ms | 30.9 -> 21.2 ms |
  | llvmpipe (Mesa 26; Qt's opengl32sw on Windows) | unchanged (synchronous) | unchanged |

  The buffer on Qt's Windows llvmpipe was 4.3 -> 5.0 ms (one more copy in memory), hence the software exclusion.
  Live wall recording through the wall's menu and dialog (2026-11-23, macOS, `--gpu`, 5 tiles): 403 frames in
  8 s at 800x600, the picture upright, colours right, AAC sound.
- Qt toolbar widget offers h264 only (the dialog also h265).
