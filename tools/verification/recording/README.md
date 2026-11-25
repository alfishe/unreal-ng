# Window recording checks

Tools for the window recorder (Record > Window..., `unreal-qt/src/widgets/windowrecorder.cpp`) and the frame scaler
it uses (`core/recording/src/common/framescaler.cpp`). Results go to the repo's `scratch/`. Designs and measured
numbers: [POC 054](../../poc/054-ffmpeg-frame-transport/README.md),
[4K recording profile](../../../docs/inprogress/2026-10-05-recording-4k-profile/README.md), the recipe
[window-recording.md](../../../.recipe/media/window-recording.md).

## The writer's time per picture

`UNREAL_WINDOW_RECORDER_TIMING=1` makes unreal-qt print one stderr line per picture its writer thread handles:

```
window-recorder frame nv12 8.512 ms
```

The path is `native` (the Mac's VideoToolbox writer), `nv12` (built in NV12 for `transfer nv12`), `composed`
(composed as B,G,R,A, then sent as it is or converted) or `same` (unchanged since the last one: not sent, nothing to
compare). The time runs from the writer taking the picture to the bytes handed to the encoder.

## `live.py`: record and measure

```bash
python3 tools/verification/recording/live.py cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt \
    scratch/recording-live 3            # 3 rounds of picture + nv12, ffmpeg, software h264
PH=700 MODES=nv12 python3 tools/verification/recording/live.py <unreal-qt> scratch/recording-live 1 hardware
```

It starts its own unreal-qt (WebAPI port `PORT`, default 8731; the sound muted; `QT_QPA_PLATFORM` offscreen unless
set), loads `SNA` (default `testdata/loaders/sna/7threality.sna`) before every file, runs `SKIP` frames (300) and
records 200 stepped frames per mode at 3840 x 2160 `fit`, `picture_height` = `PH`. One line per file:

```
r0 nv12    3840x2160: 200 frames  0.98 s, writer n=85 med   8.47 p90   9.44 ms path=nv12,same, ffmpeg CPU 41.0 ms/frame, ...
```

`n` counts the sent pictures (stepped frames repeat pictures: only changed ones are sent). `ENGINE=native` records with
the platform writer. ffmpeg and ffprobe must be on `PATH`; the ffmpeg CPU figure is missing on Windows.

On Linux without a display: `QT_QPA_PLATFORM=xcb xvfb-run -a -s "-screen 0 1920x1080x24" python3 live.py ...` (the
`offscreen` platform composes differently from a real window).

## `ab.py`: two builds

```bash
python3 tools/verification/recording/ab.py <unreal-qt A> <unreal-qt B> scratch/recording-ab 6 0,700,2160 nv12
ENV_A=MY_SWITCH=1 python3 tools/verification/recording/ab.py <unreal-qt> <unreal-qt> scratch/recording-ab 6 0 picture
```

A fresh application per run, A and B interleaved, the order swapped every round; per picture height the median of the
writer medians and the rounds B won. `ENV_A` / `ENV_B` add variables to one side, for a switch in a test build. A busy
machine (load above the core count) gives noise, not numbers: measure on an idle host, at least 6 rounds.

## `planescaler/`: the scaler's A/B and exactness

```bash
tools/verification/recording/planescaler/ab.sh            # HEAD against the working tree, 6 rounds
tools/verification/recording/planescaler/ab.sh master 10
```

Builds `bench.cpp` and `exact.cpp` against `framescaler.{h,cpp}` of the revision and of the working tree (`c++ -O2`,
in `scratch/recording-planescaler`). `exact` writes `PlaneScaler::Rows` / `RowsInterleaved` output for many sizes,
noise and a 0/255 checkerboard: the two must be the same bytes (a vector body gives the scalar code's bytes), the
script stops otherwise. `bench` converts a picture with `PackedToYuv` and scales it into a 4K NV12 frame in 4 bands as
the recorder does, 4 cases, median and minimum of 40 runs.

Windows (MSVC, a vcvars64 prompt): put each side's `framescaler.h` / `framescaler.cpp` in a folder
(`git show REV:core/recording/src/common/framescaler.cpp > A\framescaler.cpp`), then `planescaler\ab.bat A B`.

## `colors.py`: are the colors right in players

```bash
python3 tools/verification/recording/colors.py <unreal-qt> scratch/recording-colors
python3 tools/verification/recording/colors.py <unreal-qt> scratch/recording-colors window:ffmpeg:h264:nv12 core:h265:mkv:software
python3 tools/verification/recording/colors.py --analyze scratch/recording-colors-windows   # files from Windows
```

Puts the 16 ZX colors on the screen as bands (the CPU parked, nothing redraws) and records a few frames per way:
`window:ENGINE:CODEC:TRANSFER[:hw]` for the window recorder (reference: the window's picture, `window.png`),
`core:FORMAT:CONTAINER:ACCELERATION` for the emulator's frame recording at 1080p (reference: the frame,
`frame.png`). Default: the window recorder's native h264 (Mac), ffmpeg h264 picture and nv12, ffmpeg h265, core h264
mp4 `auto`. Per file: the tags as ffprobe reads them, the stored Y'CbCr per color and the worst RGB error when decoded
by the tags, as BT.601, as BT.709 and as full range. On a Mac also what macOS shows (`macshow.swift`, built into
`scratch/recording-colors-tools`): AVFoundation decodes by the tags, ColorSync converts to sRGB, as QuickTime, Safari
and previews do. Without ffprobe (Windows) the files are only recorded: copy the folder and run `--analyze`.

## `demo.py`: demo videos

```bash
python3 tools/verification/recording/demo.py <unreal-qt> scratch/recording-demo 20 \
    testdata/loaders/sna/7threality.sna,testdata/loaders/sna/insult-1.sna window:native:h264:picture:hw
```

Every snapshot recorded through every way (`ENGINE:CODEC:TRANSFER[:hw]`, as `colors.py`'s window ways without the
`window:`), stepped frames with sound, 3840 x 2160 fit (`WIDTH` / `HEIGHT`), `TAG` added to the names (e.g.
`before`); prints each file's color tags.

What the numbers mean (2026-11-24, before the fix): ffmpeg's ways stored BT.601 without tags, which macOS, mpv and
VLC decode as BT.709 for HD: greens and magentas off by up to 36-40. The native writer stored BT.709 correctly but
tagged the BT.709 transfer, which macOS shows lighter: mid greys up to 11 levels (0x77 as 0x82). The fix: BT.709
everywhere (FrameScaler, ffmpeg's last scaler), tagged BT.709 matrix and primaries, limited range and the sRGB
transfer (`FrameScaler::FfmpegColorTags`, the native writer's color properties, NVENC's VUI). Expected now: `tags`
within 2-3, `macOS shows` within 2-3 and a mean shift near 0.
