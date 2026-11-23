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
```

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
- Windows: not measured (no ffmpeg on the Windows host).

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
