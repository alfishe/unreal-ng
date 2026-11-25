# POC 054: ffmpeg frame transport

## Question

The ffmpeg pipe encoder (`core/recording/src/encoders/ffmpeg_pipe_encoder.cpp`) and the window recorder hand every
frame to an ffmpeg process through a named pipe (FIFO on macOS / Linux, a named pipe on Windows). A 4K RGBA frame is
33 MB, 1.66 GB/s at 50 Hz. A live software 4K recording on macOS kept ~10 frames a second; the `ffmpeg-video` thread
spent all its time in `write()` while ffmpeg itself ran at 35-46 % of one core. How should frames reach ffmpeg so that
the transport is never the limit?

## Goals

- Measure the ingest rate of ffmpeg (no encoding: `-c copy -f null`) per transport and frame size, on macOS, Linux
  and Windows.
- Measure what the bytes cost: 4K RGBA (33 MB), 4K NV12 (12 MB), the source picture (352x288 RGBA, 0.4 MB) with
  the scaling done inside ffmpeg.
- Recommend one transport per platform, or a change of what is sent, with the numbers.

## Requirements

- The producer writes frames the way the encoder does (one thread, whole frames, blocking writes) and starts ffmpeg
  itself with the encoder's input options (`-probesize 32 -analyzeduration 0 -f rawvideo`).
- Every rate is a median of interleaved rounds; each run states the host and its load.
- Only transports a stock ffmpeg reads: `file` (FIFO), `pipe`/`fd`, `unix`, `tcp` (ffmpeg has no shared-memory input).

## Constraints

- No change to the product while measuring; the POC lives in this folder and `scratch/`.
- Same machines as the A/B runs of the recording work: the Mac, the Linux host, the Windows host.

## Acceptance criteria

- A table: transport x frame size x platform, frames per second ingested and GB/s.
- A recommendation that names the expected 4K software-recording gain and its cost (code, portability, quality).

## Candidates

| # | Transport | Notes |
|---|-----------|-------|
| A | FIFO, default size | today (macOS 16-64 KB in the kernel, Linux 64 KB) |
| B | FIFO, `F_SETPIPE_SZ` to `/proc/sys/fs/pipe-max-size` | Linux only (1 MB default limit for a user) |
| C | `unix:` stream socket, large `SO_SNDBUF` | ffmpeg connects to the producer's listening socket |
| D | `tcp://127.0.0.1` loopback, large buffers | the one transport every platform has |
| E | send the source picture, `scale=...:flags=neighbor,pad` in ffmpeg | 80x fewer bytes at 4K; quality against FrameScaler to be checked |

## Running

From the repo's `scratch/` (the FIFO and the socket are made in the working directory):

```bash
c++ -std=c++20 -O2 ../tools/poc/054-ffmpeg-frame-transport/transport.cpp ../core/recording/src/common/framescaler.cpp -o poc054
../tools/poc/054-ffmpeg-frame-transport/run.sh ./poc054 3              # ingest only, per transport
../tools/poc/054-ffmpeg-frame-transport/e2e.sh ./poc054 SRC.rgba 3       # 4K libx264 end to end, SRC = 352x288 RGBA frames
c++ -std=c++20 -O2 ../tools/poc/054-ffmpeg-frame-transport/fsdump.cpp ../core/recording/src/common/framescaler.cpp -o fsdump
python3 ../tools/poc/054-ffmpeg-frame-transport/remap.py SRC.rgba         # remap tables against FrameScaler, 4:4:4 bytes
../tools/poc/054-ffmpeg-frame-transport/winrec.sh ./poc054 SRC.bgra 662x542 3   # window recorder: FIFO / stdin / unix
../tools/poc/054-ffmpeg-frame-transport/intel.sh ./poc054 SRC.bgra /dev/dri/renderD128 3   # Linux, Intel VA-API / QSV
```

Windows (x64 Native Tools prompt): build `transport_win.cpp` as its header says into `scratch\tw.exe`, put a
352x288 BGRA `src.bgra` beside it, set `FFMPEG` to ffmpeg.exe and run `win.cmd 3` from there.

## Answer

**Do not send 4K frames to ffmpeg at all.** For a fixed profile (4K / 1080p) send the emulator's picture at its own
size (352x288 RGBA = 0.4 MB, 80x less than 33 MB) and let ffmpeg build the frame:

```
-vf scale=in_range=full:out_range=limited:out_color_matrix=bt709,format=yuv444p,
    scale=PW:PH:flags=neighbor,pad=W:H:X:Y:black,scale=flags=area,format=yuv420p
```

(PW x PH at X, Y = `FrameScaler::ComputeLayout`). The colour conversion runs on the 0.1 M source pixels instead of the
8.3 M of a 4K frame and the transport carries 20 MB/s at 50 Hz: neither the pipe nor ffmpeg's RGB conversion is a
limit any more, on every platform and with the FIFO kept as it is. The scaling path of `scale > 1` already works this
way; only the fixed profiles upscale on our side and ship 33 MB.

End to end, 4K libx264 ultrafast, 200 real (changing) frames, 3 interleaved rounds:

| Host | Today: FrameScaler 4K RGBA, FIFO | Today through a unix socket | Source picture, ffmpeg builds 4K |
|------|---------------------------------|-----------------------------|----------------------------------|
| M1 Ultra (load 33-63) | 12.9-17.3 fps | 16.8-42.5 fps | **33.8-95.4 fps** (2.2-5.5x) |
| i7-11850H (load 74-84 on 16) | 8.8-9.5 fps | 8.3-10.0 fps | **16.9-17.6 fps** (1.8-2x, x264 is the limit) |

Picture fidelity against today, measured on real frames (`exact.py`, `q420.py`):

- 4:4:4 output (what the window recorder and x264 4:4:4 use): byte-identical to today at whole factors and at
  352x288 -> 4K (7.5x). Converting a pixel and then repeating it is repeating the converted pixel.
- Other fractional factors (352x288 -> 1080p is 3.75x, 256x192 -> 4K 11.25x): swscale's nearest mapping puts the
  k / k+1 column boundaries elsewhere than FrameScaler (0.5-2.9 % of pixels differ). Exact instead: ffmpeg's `remap`
  filter with FrameScaler's own mapping as two gray16le tables of the output size (`x = i * src / picture`, 65535 on
  the bars, `fill=black`), read once and repeated with `loop`. Byte-identical 4:4:4 for 352x288, 320x240 and
  256x192 into 4K, 1440p and 1080p (`remap.py`).
- 4:2:0 output: not the same bytes (today swscale averages the chroma from the 4K RGB), PSNR against the exact 4K
  picture after a round trip: today 33.02 / 32.30 / 38.92 / 38.85 dB, `area` 33.12 / 32.37 / 39.00 / 38.91 dB
  (+0.06..0.10, 4 pictures). `bicubic` was -0.14, `bilinear` -0.5, `neighbor` -2.6.

## Transport alone (ingest, `-c copy -f null`, 2026-11-21)

When the bytes cannot shrink (the window recorder sends the window's picture), the transport matters on macOS only:

| Transport | Mac 4K RGBA | Mac 4K NV12 | Linux 4K RGBA | Linux 4K NV12 |
|-----------|-------------|-------------|---------------|---------------|
| A FIFO (today) | 6-19 fps, 0.2-0.6 GB/s | 17-54 fps | 37-47 fps, 1.2-1.6 GB/s | 112-181 fps |
| B FIFO 1 MB (`F_SETPIPE_SZ`) | n/a | n/a | 32-46 fps | 156-173 fps |
| C unix socket, 8 MB buffer | 60-184 fps, 2.0-6.1 GB/s | 146-513 fps | 38-42 fps | 178-185 fps |
| D TCP loopback, 8 MB buffer | 100-194 fps, 3.3-6.4 GB/s | 376-534 fps | 37-43 fps | 151-165 fps |

- macOS: the FIFO moves 0.2-0.6 GB/s; a socket 10x more. Linux: every transport ~1.2 GB/s for 33 MB frames; ffmpeg
  reads 64 KB per `read` (strace: 10k reads of 65536 for 20 frames); a 1 MB pipe changes nothing, nor does glibc's
  mmap threshold (33 MB packets above 32 MB). ffmpeg has no shared-memory input; an in-process libavcodec would be
  the only copy-free way and a new build dependency on every platform.
- Windows and the GPU encoders: below.

## Next steps

1. ~~Fixed profiles in `FfmpegPipeEncoder`~~ done 2026-11-22: the source picture goes through the pipe, whole
   factors with `neighbor` + `pad`, others with the `remap` tables, `area` 4:2:0 at the target size given
   explicitly; one segment per picture size, joined with the sound at the end. Results in
   `docs/inprogress/2026-10-05-recording-4k-profile/README.md`.
2. ~~VA-API through ffmpeg: the picture uploaded small, `scale_vaapi`~~ dropped 2026-11-23: no nearest mode, the
   pixels blur and the colour drifts (PSNR 15 dB). VA-API takes the CPU graph's 4:2:0 picture and pads on the GPU
   (recording-4k-profile README).
3. ~~Window recorder on macOS: a unix socket instead of stdin~~ not needed (measured 2026-11-23, below).

## Window recorder: stdin against a unix socket (macOS, 2026-11-23)

The window recorder hands ffmpeg the window's picture through its standard input (QProcess: an anonymous pipe, not
a FIFO). The largest it sends is the CRT drawn at a profile's height: 1320x1080 BGRA (5.7 MB) for 1080p, 2640x2160
(22.8 MB) for 4K. `winrec.sh`: 19 real window grabs scaled to those sizes, M1 Ultra at load 9-33, frames per second
over 3-4 interleaved rounds:

| Output | 1320x1080 stdin | 1320x1080 unix | 2640x2160 FIFO | 2640x2160 stdin | 2640x2160 unix |
|:--|--:|--:|--:|--:|--:|
| ingest (`-c copy`) | 242-526 | 384-927 | 22-50 | 58-131 | 115-283 |
| `h264_videotoolbox` | 67-365 | 149-384 | | 45-95 | 53-98 |
| `libx264` 4:4:4 ultrafast | 171-358 | 278-575 | | 41-102 | 63-171 |

- On macOS an anonymous pipe moves 2.6x what a FIFO does (1.3-3.0 GB/s against 0.5-1.1 GB/s, same rounds): the
  FIFO numbers above are not the window recorder's.
- At the 1080p CRT size the pipe has 5-10x headroom over 50 fps. At the 4K CRT size it has about 2x idle, near 1x
  under load, where the encoders are the limit as well: VideoToolbox through ffmpeg takes ~95 fps with either
  transport, and x264 4:4:4 on real CRT frames does ~42 fps at 1080p (recipe `window-recording.md`), about a quarter
  of that at 4K. The default engine on the Mac (`native`, AVAssetWriter) does not use ffmpeg at all.
- A socket would gain only for a 4K CRT recording with `engine: ffmpeg` on a loaded Mac, where the encoder stays far
  below real time anyway. Not worth a second transport (and a Windows path).
- Live recording of a drawn window was not measured: the offscreen platform does not redraw the window (19 of 210
  frames changed), and visible windows are not opened on this Mac without the owner.

## 4K into the GPU encoders: Windows NVENC, Linux Intel (2026-11-23)

Can a 4K frame reach ffmpeg and its GPU encoder at 50 fps? `transport_win.cpp` + `win.cmd` on Windows (a named pipe
as `FFmpegPipeEncoder`'s `NamedPipe` with 64 KB or 1 MB buffers, a pipe as stdin as QProcess hands it with 1 MB or
16 MB, TCP loopback), `intel.sh` on Linux (FIFO, FIFO 1 MB, stdin, unix socket). Moving pictures (testsrc2 352x288
scaled by FrameScaler; NV12 a fixed pattern), 3 interleaved rounds, frames per second min-max.

Windows, Threadripper PRO 3975WX + RTX 4090, idle (NVENC `-preset p1 -tune ll -rc constqp -qp 18`):

| Frame, output | npipe 64 KB | npipe 1 MB | stdin 1 MB | stdin 16 MB | TCP |
|:--|--:|--:|--:|--:|--:|
| 4K BGRA, ingest | 80-85 | 65-86 | 61-84 | 80-84 | 27-59 |
| 4K BGRA, h264_nvenc (BGRA into NVENC) | 60-62 | 61-63 | 59-62 | 62-63 | 37-39 |
| 4K BGRA, h264_nvenc, `-pix_fmt yuv420p` (CPU) | 61-64 | 60-63 | 60-63 | 60-63 | 29-41 |
| 4K BGRA, hevc_nvenc | 60-64 | 60-64 | 60-62 | 60-64 | 24-39 |
| 4K BGRA, h264_nvenc 4:4:4 | 56-59 | 58-60 | 57-60 | 57-60 | 19-38 |
| 4K NV12, ingest | 286-318 | 286-289 | 255-294 | 255-280 | 129-181 |
| 4K NV12, h264_nvenc | 180-185 | 185-191 | 180-190 | 178-187 | 94-112 |
| 4K NV12, hevc_nvenc | 186-193 | 192-193 | 187-193 | 187-190 | 108-128 |
| 2640x2160 BGRA (4K CRT window), h264_nvenc | 11-82 | 27-84 | 67-84 | 72-83 | 31-58 |

Linux, i7-11850H Tiger Lake (Iris Xe), load 13-18 (`-vf format=nv12,hwupload -c:v h264_vaapi -qp 18`;
`h264_qsv -pix_fmt nv12 -global_quality 18`):

| Frame, output | FIFO | FIFO 1 MB | stdin | unix |
|:--|--:|--:|--:|--:|
| 4K BGRA, ingest | 39-60 | 42-60 | 41-59 | 44-110 |
| 4K BGRA, h264_vaapi | 27-44 | 24-43 | 25-43 | 25-64 |
| 4K BGRA, hevc_vaapi | 26-30 | 26-28 | 22-28 | 21-29 |
| 4K BGRA, h264_qsv | 30-42 | 23-43 | 21-42 | 29-59 |
| 4K NV12, ingest | 129-246 | 152-275 | 124-224 | 212-360 |
| 4K NV12, h264_vaapi | 86-94 | 85-90 | 85-92 | 77-94 |
| 4K NV12, hevc_vaapi | 51-56 | 49-55 | 52-55 | 52-56 |
| 4K NV12, h264_qsv | 82-84 | 82-83 | 82-82 | 81-82 |
| 2640x2160 BGRA, h264_vaapi | 52-68 | 66-74 | 52-70 | 42-88 |

- **The pixel format decides, not the transport.** 4K BGRA (33 MB) makes 50 fps on Windows with ~20 % to spare
  (NVENC 56-64 fps through every pipe; ffmpeg's handling of 33 MB per frame is the limit, NVENC alone takes NV12
  at 180-190) and not at all on the Intel laptop (21-44 fps: ffmpeg's single-threaded BGRA -> NV12 of 8.3 M pixels
  plus the upload). 4K NV12 (12.4 MB) gives NVENC 3.5x real time and Intel H.264 1.7x; Intel HEVC at 4K is the
  encoder's own limit (~55 fps) in any format.
- Pipe buffers (64 KB .. 16 MB) change nothing on Windows; TCP loopback is the slowest there (half the pipes). On
  Linux a unix socket helps the BGRA ingest only, never enough for 4K BGRA into VA-API.
- So a producer that must hand ffmpeg a 4K frame should send NV12 (`FrameScaler::ScaleIntoNv12` /
  `PackedToNv12` exist) and keep the named pipe / FIFO / stdin. Today neither recorder ships a 4K BGRA frame: the
  fixed profiles send the source picture (ffmpeg builds the frame), the window recorder the window's picture (at
  most 2640x2160 with the CRT at 4K: 67-84 fps into NVENC through stdin, 52-70 into VA-API).
- Intel on Windows (QSV) not measured: neither Windows host has an Intel GPU enabled (an RTX 4090 alone on both).

## The window recorder's `transfer` switch (2026-11-23)

Following the answer above, the window recorder got `transfer` (all planes; default `picture`, today's behaviour):
`nv12` composes the frame at the file's size in the writer, converts it with `FrameScaler::PackedToNv12` (now taking
B,G,R,A as well, the same bytes; 4 bands: 1.2 ms per 4K frame on an M1 Ultra, 1.8 ms on a 3975WX, single-threaded
RGBA path unchanged in an interleaved A/B on both) and sends NV12 in the Matroska stream (`V_UNCOMPRESSED`,
ColourSpace `NV12`); ffmpeg only encodes (`-pix_fmt nv12` to NVENC / QSV / AMF and VideoToolbox, `hwupload` alone
to VA-API, `yuv420p` to x264 / x265; ProRes refused: 4:2:0).

Live, `.238` under xvfb (i7-11850H, T1200 + Iris Xe), 7th Reality demo reloaded before every file so both modes
record the same frames (84-85 distinct of 210), 4K H.264, `video/window/record/frames 200`, 3 interleaved rounds:

| Picture, encoder | `picture`: wall s, ffmpeg CPU ms / frame | `nv12`: wall s, ffmpeg CPU ms / frame |
|:--|--:|--:|
| `picture_height 2160` (CRT at 4K), h264_nvenc | 3.00-3.25, 24-25 | 2.50-2.87, 4.3-4.4 |
| `picture_height 2160`, h264_vaapi | 2.96-2.99, 24-25 | 2.57-2.88, 5.6-5.9 |
| window size picture (1080 screen), h264_nvenc | 1.14-1.21, 11-12 | 2.99-4.19, 3.9-4.0 |
| window size picture, h264_vaapi | 0.91-1.01, 7.5-8.0 | 3.47-3.92, 4.5-5.6 |

- With the picture already at the frame's height (the 4K CRT) `nv12` is faster and takes ffmpeg's CPU down 4-6x.
- With a smaller picture, `nv12` scales it up in the writer (`QImage::scaled`, smooth, on the CPU, the path the Mac's
  own writer takes too) and loses to ffmpeg's scaler: there `picture` stays the right choice.

## `transfer nv12` built in NV12 (2026-11-24)

The frame of `transfer nv12` is now built in NV12 itself (`composeNv12` in the window recorder), no 4K B,G,R,A frame:

- the background is written once per buffer, then only the picture's rectangle every frame; the keyboard is drawn
  over the background and converted only when it changes;
- a picture of its place's size is converted into it (`PackedToNv12`); a smaller one is converted at its own size
  (`FrameScaler::PackedToYuv`, per-pixel Y, U, V) and scaled in its planes by `FrameScaler::PlaneScaler`: separable
  bicubic, ffmpeg's kernel (Keys a = -0.6), Y to the place, U,V to half of it (a third of the samples of the 4K
  B,G,R,A frame), 4 bands;
- same frames are found by the picture and the keyboard (hundreds of KB), not a 4K memcmp;
- the layout is made even in `nv12` (a pixel at most: NV12 colors 2x2 blocks). A picture larger than its place
  (scaled down) keeps the old path (Qt's smooth scaling, `PackedToNv12` of the frame).

Writer time per changed 4K frame (`old`: compose B,G,R,A + `PackedToNv12`; `new`: `composeNv12`), 7th Reality, the
same 85 frames, h264, 4 interleaved rounds (a temporary diag build: `WRDIAG` per frame, `WR_NODIRECT` for the old
path), median of the per-round medians:

| Host | `picture_height` | old ms | new ms | new faster | wall of 200 stepped frames, old / new s |
|:--|:--|--:|--:|:--|:--|
| `.238` i7-11850H, VA-API, gcc | 0 (659x540 to 2636x2160) | 22.4 | 12.6 | 4/4 | 3.1-8.2 / 1.2-3.5 |
| | 700 | 19.0 | 10.3 | 4/4 | 2.7-2.8 / 1.1-1.2 |
| | 2160 | 17.2 | 11.1 | 4/4 | 2.3 / 1.3 |
| `.20` M3 Ultra, VideoToolbox (via ffmpeg) | 700 | 10.2 | 8.5 | 4/4 | 1.35 / 1.0 |
| | 0, 2160 | load 35 to 150 from other work: 3/4 and 4/4 rounds to new, numbers not usable | | | |

The changed stages alone (`scratch` bench: fill + Qt smooth scale + drawImage + `PackedToNv12` in 4 bands vs
`PackedToYuv` + `PlaneScaler` in 4 bands, RGB32 pictures as the window grabs, median of 20):

| Case | `.40` 3975WX, MSVC: old / new ms | `.20` M3 Ultra (load 150): old / new ms |
|:--|--:|--:|
| 659x540 to 2636x2160 | 12.0 / 6.1 | 10.3-13.3 / 5.2-6.9 |
| 854x700 at its own size | 5.5 / 0.4 | 2.2-3.9 / 0.5-1.2 |
| 659x540 to 1708x1400 | 8.2 / 3.2 | 8.6-9.6 / 4.1-5.5 |
| 352x288 to 2640x2160 | 25.8 / 5.4 | 10.0-16.0 / 6.4 |

Quality, `.238`, old vs new path on the same frames: PSNR Y 41-44 dB, U,V 54-62 dB (bicubic instead of Qt's
smooth scaling; nothing visible side by side); `picture_height 2160` gives the same bytes (PSNR inf). `.40` has no
ffmpeg, so Windows was measured on the stages only.


## `PlaneScaler` in SIMD, the crop, colors (2026-11-24)

`PlaneScaler` (both passes) has NEON and SSE2 bodies that give the scalar code's bytes (`planescaler/ab.sh` in
`tools/verification/recording` compares many sizes, noise and a checkerboard: same bytes on every host). Picture
converted and scaled into a 4K NV12 frame in 4 bands, min ms, median of 6 interleaved rounds, old -> new:

| Case | Mac (M1 Ultra, load 50-67) | `.20` M3 Ultra | `.238` i7-11850H gcc | `.40` 3975WX MSVC |
|:--|--:|--:|--:|--:|
| 659x540 to 2636x2160 | 4.14 -> 1.35 | 1.82 -> 0.56 | 3.28 -> 0.99 | 5.85 -> 1.23 |
| 659x540 to 1708x1400 | 2.17 -> 0.87 | 0.90 -> 0.34 | 1.71 -> 0.63 | 2.94 -> 0.78 |
| 352x288 to 2640x2160 | 3.34 -> 1.15 | 1.62 -> 0.41 | 2.70 -> 0.68 | 5.06 -> 0.84 |
| 1320x1080 to 2640x2160 | 5.11 -> 2.20 | 2.35 -> 0.94 | 4.84 -> 2.08 | 7.94 -> 2.38 |

The `picture` path's fast copy took a picture only when it fit the frame: the pipe's frame is made even, so a 659
wide window picture in a 658 place went through Qt's smooth scaling (soft edges). It now loses its edge pixel
instead (at most 4 per side), sharp; the live A/B on `.238` was in the noise of the host's load (2.6 / 3.0 ms).

Colors: FrameScaler is BT.709 now and every file is tagged BT.709 / limited / sRGB transfer (the 4K profile plan,
"Washed-out colors"); `tools/verification/recording/colors.py` checks it.
