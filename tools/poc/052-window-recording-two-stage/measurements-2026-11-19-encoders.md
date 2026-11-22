# Measurements 2026-11-19: encoders, the warning probe, the stepped rate

POC 052, charter in [README.md](README.md). Raw data: `scratch/2026-11-19-poc-052/` of the worktree
(`streams/`, `bench-*.jsonl`, `probe-*.jsonl`, `pacing-mac.jsonl`, `capture.jsonl`).

## Setup

| | Mac | Linux host |
|---|---|---|
| CPU | Apple M1 Ultra, 20 cores | Intel i7-11850H, 8 cores / 16 threads, 61 GB |
| ffmpeg | 9.0.2 (Homebrew), VideoToolbox | 8.0.1 (Ubuntu), no VideoToolbox |
| Load during the runs | load average 12-45 (bench), 72-165 (probe), up to 245 (stepped runs): other sessions | 17-48: another session's build ran throughout |

Both hosts were shared and busy. Every rate below is a median of 3 interleaved rounds (bench) or 10 (probe),
and still swings: the ratios between variants in one run count more than absolute numbers.

## Method

1. **Streams.** unreal-qt from this worktree, offscreen (the software CRT path: the offscreen platform has no GL),
   port 8731, muted, CRT `crt-lottes`. `python/capture_streams.py` loads the snapshot, runs it (40 s for the demo,
   past its title; 10 s for the game), pauses, starts a recording (`engine: ffmpeg`, h264) and steps
   `record/frames` 250 frames (100 at 4K). The ffmpeg wrapper `python/ffmpeg-tee.sh`, first on the instance's
   PATH, copies the Matroska BGRA stream the recorder sends to ffmpeg into `streams/<workload>-<size>.mkv` and its
   arguments into `.args`. The streams are what the recorder hands ffmpeg: VFR, repeated pictures not sent.

   | Stream | Picture sent | Unique frames / emulated | Recorder output |
   |---|---|---|---|
   | demo-native | 662x542 | 250 / 250 | as is |
   | demo-fhd | 1318x1080 | 250 / 250 | padded to 1920x1080 by ffmpeg |
   | demo-kbd | 1324x1720 (keyboard `pentagon2plus`, scale 2) | 250 / 250 | as is |
   | demo-uhd | 2638x2160 | 100 / 100 | padded to 3840x2160 |
   | game-* | same sizes | 64 / 250 (26 / 100 at 4K): Dizzy X is mostly still | |

   Workloads: demo `testdata/loaders/sna/eyeache1.sna` (full-screen motion every frame), game `Dizzy X.sna`.

2. **Encoders** (`python/bench_encoders.py`): every stream through every variant, output to a file, wall time
   including ffmpeg's start. `x real time` = stream length / wall time (1.0 = keeps up with a running machine).
   - `s1-*`: stage-1 candidates. `ffv1` (level 3, 16 slices, GOP 50 / 1), `utvideo` gbrp, `ffvhuff` bgra,
     `magicyuv` gbrp, `libx264rgb -qp 0 -preset ultrafast` bgr0, `prores_videotoolbox` 4444, `rawvideo` (NUT).
   - `live-*`: the recorder's own arguments from `.args` (pad, VFR), i.e. today's live encode.
   - `s2-*`: stage 2, the same codecs (plus `medium` presets) reading the `s1-ffv1` intermediate.

3. **The warning probe** (`python/probe_warning.py`): per round, back to back: the whole stream with the live
   arguments (steady), its first 25 frames (`first`), 25 frames of a synthetic `testsrc2` picture of the same size
   (`synth`) and a 1-frame run (ffmpeg's start-up). `_net` = the probe with the start-up subtracted.

4. **Stepped rate** (`capture_streams.py` with `CODEC` / `ENGINE` / `HARDWARE`): `record/frames` 150 frames of the
   demo per encoder, 3 rounds: the speed a machine paced by the recording reaches.

## Results

### Stage 1, live and stage 2: x real time, Mac

| variant | demo-native | demo-fhd | demo-kbd | demo-uhd | game-native | game-fhd | game-kbd | game-uhd |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| s1-ffv1 | 9.2 | 1.5 | 2.5 | 0.7 | 17.7 | 6.0 | 5.6 | 1.9 |
| s1-ffv1-g1 | 9.7 | 1.5 | 2.5 | 0.6 | 14.8 | 6.2 | 5.5 | 1.6 |
| s1-utvideo | 19.9 | 3.5 | 7.3 | 1.5 | 43.5 | 20.1 | 18.4 | 5.0 |
| s1-ffvhuff | 10.9 | 9.4 | 7.7 | 0.9 | 43.1 | 21.0 | 19.0 | 6.7 |
| s1-magicyuv | 17.4 | 9.2 | 7.4 | 1.2 | 30.5 | 22.2 | 14.8 | 5.0 |
| s1-x264rgb-qp0 | 30.9 | 12.2 | 11.8 | 0.9 | 61.8 | 24.6 | 22.2 | 8.2 |
| s1-prores-vt | 8.8 | 6.6 | 4.7 | 0.8 | 15.4 | 13.1 | 11.7 | 4.8 |
| live-x264 | 23.0 | 7.0 | 6.8 | 0.6 | 37.4 | 12.0 | 11.8 | 3.0 |
| live-x265 | 4.4 | 3.2 | 3.0 | 0.3 | 22.2 | 7.3 | 7.0 | 1.8 |
| live-h264-vt | 1.7 | 6.4 | 6.2 | 0.7 | 14.4 | 10.4 | 10.5 | 2.5 |
| live-hevc-vt | 8.0 | 6.5 | 6.6 | 0.9 | 13.4 | 8.0 | 8.1 | 2.0 |
| s2-x264 | 5.7 | 3.3 | 3.6 | 0.7 | 19.4 | 7.9 | 9.3 | 1.7 |
| s2-x264-medium | 6.7 | 2.4 | 2.5 | 0.6 | 16.7 | 6.1 | 5.2 | 1.2 |
| s2-x265 | 3.5 | 2.2 | 2.2 | 0.4 | 17.1 | 6.5 | 4.7 | 1.4 |
| s2-x265-medium | 0.8 | 0.8 | 0.7 | 0.2 | 9.1 | 2.8 | 2.6 | 0.8 |
| s2-h264-vt | 5.5 | 3.5 | 3.9 | 0.6 | 10.5 | 6.1 | 6.6 | 1.6 |
| s2-hevc-vt | 8.6 | 3.3 | 3.8 | 0.6 | 11.1 | 5.8 | 5.6 | 1.4 |
| s1-raw | 10.6 | 5.5 | 0.7 | 1.0 | 25.5 | 13.2 | 6.5 | 2.1 |

### The same, Linux host (no VideoToolbox)

| variant | demo-native | demo-fhd | demo-kbd | demo-uhd | game-native | game-fhd | game-kbd | game-uhd |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| s1-ffv1 | 1.9 | 0.9 | 0.8 | 0.2 | 7.8 | 2.4 | 1.8 | 0.7 |
| s1-ffv1-g1 | 1.6 | 0.9 | 0.7 | 0.2 | 7.5 | 2.3 | 1.9 | 0.8 |
| s1-utvideo | 5.3 | 3.0 | 2.2 | 0.5 | 17.2 | 5.9 | 4.8 | 2.0 |
| s1-ffvhuff | 6.4 | 2.7 | 2.1 | 0.5 | 19.4 | 6.6 | 5.2 | 2.3 |
| s1-magicyuv | 5.5 | 2.8 | 2.1 | 0.5 | 16.6 | 6.5 | 5.0 | 2.4 |
| s1-x264rgb-qp0 | 4.3 | 2.9 | 1.8 | 0.4 | 17.4 | 7.2 | 6.3 | 4.8 |
| live-x264 | 2.8 | 1.2 | 0.9 | 0.2 | 14.8 | 4.5 | 4.2 | 1.8 |
| live-x265 | 1.1 | 0.5 | 0.3 | 0.1 | 6.1 | 2.3 | 1.9 | 1.5 |
| s2-x264 | 1.6 | 0.7 | 0.5 | 0.1 | 8.4 | 2.5 | 2.7 | 1.7 |
| s2-x264-medium | 0.9 | 0.5 | 0.3 | 0.1 | 6.5 | 2.2 | 2.2 | 1.3 |
| s2-x265 | 0.8 | 0.4 | 0.2 | 0.1 | 4.9 | 1.7 | 1.6 | 1.2 |
| s2-x265-medium | 0.2 | 0.1 | 0.1 | 0.0 | 3.1 | 0.9 | 0.9 | 0.5 |

### Data rate, MB per minute of recording (same on both hosts)

| variant | demo-native | demo-fhd | demo-kbd | demo-uhd | game-native | game-fhd | game-kbd | game-uhd |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| s1-ffv1 | 1092 | 3486 | 2070 | 11680 | 210 | 626 | 436 | 2230 |
| s1-ffv1-g1 | 1145 | 3504 | 2089 | 11665 | 225 | 633 | 441 | 2233 |
| s1-utvideo | 1226 | 5618 | 4891 | 21647 | 295 | 1150 | 1210 | 4538 |
| s1-ffvhuff | 1985 | 8570 | 9046 | 33162 | 469 | 1810 | 2238 | 7172 |
| s1-magicyuv | 1226 | 5632 | 4891 | 21806 | 295 | 1155 | 1210 | 4568 |
| s1-x264rgb-qp0 | 487 | 1563 | 1542 | 6443 | 8 | 24 | 27 | 153 |
| s1-prores-vt | 1343 | 3496 | 4717 | 11128 | 360 | 975 | 1141 | 2680 |
| live-x264 | 89 | 158 | 179 | 305 | 2 | 5 | 6 | 42 |
| live-x265 | 101 | 126 | 184 | 212 | 2 | 2 | 5 | 12 |
| live-h264-vt | 163 | 380 | 434 | 918 | 8 | 20 | 29 | 72 |
| live-hevc-vt | 197 | 421 | 462 | 831 | 12 | 23 | 42 | 74 |
| s2-x264 | 89 | 158 | 179 | 305 | 2 | 5 | 6 | 42 |
| s2-x264-medium | 89 | 153 | 164 | 239 | 3 | 7 | 7 | 50 |
| s2-x265 | 101 | 161 | 236 | 212 | 2 | 3 | 7 | 12 |
| s2-x265-medium | 97 | 136 | 209 | 186 | 2 | 4 | 7 | 19 |
| s2-h264-vt | 163 | 380 | 434 | 918 | 8 | 20 | 29 | 72 |
| s2-hevc-vt | 197 | 421 | 462 | 831 | 12 | 23 | 42 | 74 |
| s1-raw | 4205 | 16683 | 26689 | 66790 | 1077 | 4271 | 6833 | 17365 |

### The warning probe

Each cell: runs within ±25 % of the steady rate measured just before, and the median error. A negative error
predicts slower than it ran.

Linux host (load steady at 17-35):

| setting | steady fps, median (min-max) | load | first_fps | first_fps_net | synth_fps | synth_fps_net | probe s | start-up s |
|---|---|---|---|---|---|---|---|---|
| live-x264 demo-fhd | 67 (57-163) | 17-33 | 1/10, -51 % | 2/10, -29 % | 1/10, -48 % | 4/10, -25 % | 0.73 | 0.23 |
| live-x265 demo-fhd | 24 (23-85) | 20-32 | 0/10, -35 % | 8/10, -9 % | 0/10, -37 % | 8/10, -14 % | 1.61 | 0.49 |
| live-x264 game-fhd | 57 (51-65) | 30-33 | 0/10, -37 % | 10/10, -2 % | 0/10, -42 % | 9/10, -12 % | 0.76 | 0.27 |
| live-x265 game-fhd | 30 (23-31) | 30-32 | 2/10, -29 % | 8/10, +20 % | 0/10, -46 % | 4/10, -27 % | 1.63 | 0.51 |
| live-x264 demo-kbd | 60 (50-67) | 31-35 | 0/10, -52 % | 2/10, -31 % | 0/10, -47 % | 7/10, -23 % | 0.78 | 0.28 |
| live-x265 demo-kbd | 24 (23-25) | 32-35 | 0/10, -39 % | 10/10, -15 % | 0/10, -44 % | 7/10, -25 % | 1.86 | 0.54 |

Mac (load 72-165, changing within seconds):

| setting | steady fps, median (min-max) | load | first_fps | first_fps_net | synth_fps | synth_fps_net | probe s | start-up s |
|---|---|---|---|---|---|---|---|---|
| live-x264 demo-fhd | 44 (26-254) | 84-164 | 0/10, -48 % | 4/10, -18 % | 0/10, -44 % | 4/10, -26 % | 1.29 | 0.29 |
| live-x265 demo-fhd | 16 (13-76) | 102-165 | 4/10, -18 % | 4/10, +30 % | 2/10, -28 % | 6/10, -7 % | 2.06 | 0.29 |
| live-hevc-vt demo-fhd | 39 (27-313) | 111-164 | 1/10, -56 % | 5/10, +21 % | 1/10, -58 % | 1/10, +60 % | 1.34 | 0.72 |
| live-x264 game-fhd | 89 (43-120) | 78-130 | 4/10, -25 % | 8/10, +4 % | 4/10, -27 % | 7/10, -6 % | 0.36 | 0.11 |
| live-x265 game-fhd | 51 (25-73) | 78-124 | 3/10, -19 % | 5/10, +10 % | 1/10, -39 % | 4/10, -25 % | 0.71 | 0.17 |
| live-hevc-vt game-fhd | 75 (38-95) | 76-124 | 1/10, -47 % | 3/10, +78 % | 0/10, -46 % | 2/10, +50 % | 0.69 | 0.43 |
| live-x264 demo-kbd | 114 (65-322) | 74-92 | 0/10, -47 % | 2/10, -32 % | 0/10, -52 % | 1/10, -36 % | 0.44 | 0.10 |
| live-x265 demo-kbd | 41 (24-145) | 72-96 | 4/10, -27 % | 6/10, -5 % | 4/10, -33 % | 5/10, -20 % | 0.82 | 0.17 |
| live-hevc-vt demo-kbd | 159 (78-368) | 72-95 | 0/10, -69 % | 4/10, +31 % | 0/10, -71 % | 6/10, +21 % | 0.56 | 0.45 |

### Stepped rate (record/frames), Mac, offscreen

At load 17-32, h264 through ffmpeg, while the streams were captured (`capture.jsonl`):

| Size | demo fps | game fps |
|---|---:|---:|
| native 662x542 | 152 | 122 |
| fhd 1920x1080 | 99 | 58 |
| uhd 3840x2160 | 31 | 26 |
| kbd 1324x1720 | 21 | 75 |

Per encoder, 3 rounds, at load 19-245 (`pacing-mac.jsonl`):

| size | encoder | stepped fps, median (runs) | failed runs | load |
|---|---|---:|---:|---|
| fhd | h264 VT native | 20 (20) | 2 | 69-69 |
| fhd | h264 ffmpeg | 17 (17, 12, 67) | 0 | 52-65 |
| fhd | h265 VT native | 32 (32) | 2 | 37-37 |
| fhd | h265 ffmpeg | 20 (20, 20, 18) | 0 | 41-61 |
| fhd | prores native | 8 (8, 8, 30) | 0 | 36-126 |
| kbd | h264 VT native | 15 (15) | 2 | 97-97 |
| kbd | h264 ffmpeg | 11 (16, 10, 11) | 0 | 58-118 |
| kbd | h265 VT native | 22 (15, 29) | 1 | 55-77 |
| kbd | h265 ffmpeg | 8 (7, 8, 16) | 0 | 54-115 |
| kbd | prores native | 21 (112, 5, 21) | 0 | 49-245 |
| native | h264 VT native | 44 (36, 52) | 1 | 60-109 |
| native | h264 ffmpeg | 36 (126, 6, 36) | 0 | 29-220 |
| native | h265 VT native | 43 (43) | 2 | 92-92 |
| native | h265 ffmpeg | 38 (119, 7, 38) | 0 | 19-165 |
| native | prores native | 31 (31) | 2 | 39-39 |

### A defect found on the way: stepped mode with the native writer

`record/frames` with `engine: native` (VideoToolbox, ProRes) failed in 12 of 27 runs (3 of 4 more in the check below) with "The window recorded
nothing within 10 s (frame 0)"; through ffmpeg 0 of 45. The cause: `WindowRecording::RunFrames` runs the first
frame at once; while the writer is not `ready` (the encoder still starting), `WindowRecorder` drops what ran
(`windowrecorder.cpp`, "The encoder is still starting"), and a paused machine makes no further frame. Waiting for
`encoder_ready_ms > 0` before stepping (`WAIT_READY=1`): 4 of 4 runs passed; without: 0 of 2 (same load, ~96).

Fixed in 1eb9ae71d: `RunFrames` waits for the encoder before the first frame. After the fix: 8 of 8 native and
ffmpeg runs passed at load ~105, 50 frames in each file.
