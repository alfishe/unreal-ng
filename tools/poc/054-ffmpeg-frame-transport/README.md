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
c++ -std=c++20 -O2 ../tools/poc/054-ffmpeg-frame-transport/transport.cpp -o poc054
../tools/poc/054-ffmpeg-frame-transport/run.sh ./poc054 3
```

## Results (2026-11-21)

ffmpeg ingest (`-c copy -f null`), 3 interleaved rounds, the range of the rounds. Mac: M1 Ultra, ffmpeg 9.0.2, a
loaded host (load average 80-120 on 20 cores). Linux: i7-11850H, ffmpeg 8.0.1, `pipe-max-size` 1 MB.

| Transport | Mac 4K RGBA | Mac 4K NV12 | Linux 4K RGBA | Linux 4K NV12 |
|-----------|-------------|-------------|---------------|---------------|
| A FIFO (today) | 6-19 fps, 0.2-0.6 GB/s | 17-54 fps | 37-47 fps, 1.2-1.6 GB/s | 112-181 fps |
| B FIFO 1 MB (`F_SETPIPE_SZ`) | n/a (no such call) | n/a | 32-46 fps | 156-173 fps |
| C unix socket, 8 MB buffer | 60-184 fps, 2.0-6.1 GB/s | 146-513 fps | 38-42 fps | 178-185 fps |
| D TCP loopback, 8 MB buffer | 100-194 fps, 3.3-6.4 GB/s | 376-534 fps | 37-43 fps | 151-165 fps |

- macOS: the FIFO is the limit, as in the live recording (~10 fps at 4K). A socket moves 10x more. FIFO and socket
  both take ~0.4 MB frames at thousands of fps: only the large frames suffer.
- Linux: the transport is never the limit; a bigger FIFO changes nothing. ffmpeg's own ingest of a 33 MB packet is
  (~40 fps RGBA); NV12 frames, 2.7x smaller, ingest 4x more frames.
- Less data (E): sending the 352x288 picture and scaling in ffmpeg. The graph of today in that order (`scale=neighbor`
  of packed RGBA to 4K, pad, convert) runs at 15-25 fps against 28-37 fps for today's 4K RGBA input (Mac, from the
  page cache): slower. Converting first (yuv444p at the source size, neighbor scale of planes, 444 -> 420) runs at
  69-115 fps, but its chroma is 0.05-0.2 dB below today (the 8-bit intermediate), with `flags=neighbor` also on the
  420 step 3 dB below; its nearest mapping is ffmpeg's, not FrameScaler's (the window's). An NV12 of our own
  (FrameScaler, BT.709) was 0.12-0.5 dB below swscale after x264 (2x2 box chroma against swscale's filter).

## Recommendation

- macOS (and Linux, harmless there): feed the video through a `unix:` socket instead of the FIFO. The producer
  listens, ffmpeg connects (`-i unix:PATH`); writes stay blocking, whole frames. Expected: the 4K software recording
  goes from the FIFO's ~10 fps to the next limit (ffmpeg's conversion + x264, 25-45 fps at 4K ultrafast on the Mac).
- Windows: not measured (the Windows host has no ffmpeg); a named pipe against TCP loopback is the open question.
- Status 2026-11-22: recorded only, the switch is not scheduled.
- Keep RGBA: every lever that sends fewer bytes measured a quality loss against today.
