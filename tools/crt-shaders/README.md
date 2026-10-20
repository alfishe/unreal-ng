# CRT shader tools

The tools behind unreal-qt's CRT presets and shader models ([docs/features/crt-effects.md](../../docs/features/crt-effects.md)):
the import of the libretro presets, the gallery that chose them, and the comparison of unreal-qt's integer models
(`CRTBeamModel`) with the original shaders.

| Tool | What it does |
|------|--------------|
| `import.py` | The libretro presets into `data/shaders/crt/<key>/` (GLSL 330 passes, `preset.json`, LUTs) |
| `presets.py` | The preset list, unreal-qt's profiles, the gallery's descriptions (shared by all tools) |
| `crt-render.cpp` | unreal-qt's own picture of still frames: CMake target `crt-render` (`-DTESTS=ON`) |
| `render.py` | Gallery frames through every preset (librashader-cli) and every unreal-qt profile (`crt-render`) |
| `compare.py` | The shader models against the librashader renders: mean, 99th percentile, largest difference |
| `gallery.py` | `index.html`: one shader, an A / B wipe, a matrix of 1:1 crops |
| `capture.py` | Still frames from snapshots over the WebAPI |
| `clips.py` | Motion clips (scrolling, a shooter, a tape loading) frame by frame over the WebAPI |
| `render_clips.py` | The clips through every preset and profile, one VP9 4:4:4 webm per clip and shader |
| `videos.py` | `videos.html`: the clips side by side or as a wipe, synchronised |
| `make_demo.py` | `demo/crt-shaders-demo.{webm,mp4}`: every preset against the plain picture |
| `scr2png.py` | A `.scr` screen as a 352 x 288 frame |
| `probe.py` | Every pass transpiled to GLSL 1.20: what fails, what each preset needs |

## Requirements

- [librashader-cli](https://github.com/SnowflakePowered/librashader) (`--cli` or `$LIBRASHADER_CLI`) and a
  [slang-shaders](https://github.com/libretro/slang-shaders) checkout (`--slang` or `$SLANG_SHADERS`; the revision in
  `data/shaders/crt/SOURCE`) for anything that renders the originals.
- Python 3 with numpy and Pillow; ffmpeg (libvpx-vp9, libx264) for the videos.
- `crt-render`: `tools/build/build.sh crt-render` (into `cmake-build-agent-release/bin/`; `--render` for another).

Everything goes into a work directory, `scratch/crt-gallery` by default (`--work`).

## The models against the originals

```bash
tools/build/build.sh crt-render
python3 tools/crt-shaders/render.py --slang ~/src/slang-shaders --cli ~/bin/librashader-cli \
    crt-lottes crt-geom crt-easymode crt-gdv-mini crt-consumer crt-pi zfast-crt ours-crt-lottes ours-crt-geom \
    ours-crt-easymode ours-crt-gdv-mini ours-crt-consumer ours-crt-pi ours-zfast-crt
python3 tools/crt-shaders/compare.py
```

The frames are `testdata/video/crt-gallery` (10 frames: a keyboard, screens, colour bars, a gradient, a photo, games,
text, a checkerboard), at 4x (1408 x 1152), FrameCount 60. The test does the same on its own:

```bash
UNREAL_QT_TEST_PLATFORM=cocoa UNREAL_CRT_REFERENCE=scratch/crt-gallery \
    cmake-build-agent-release/bin/unreal-qt-tests --gtest_filter='CRTBeamModel_Test.AgainstLibrashader'
```

Mean difference over the frames, 0..255, 2026-10-20 (librashader on Metal): zfast-crt 0.06, crt-easymode 0.07,
crt-lottes 0.10, crt-gdv-mini 0.15, crt-geom 0.22, crt-consumer 0.22, crt-pi 0.28.

The presets on unreal-qt's runner against librashader: `UNREAL_CRT_REFERENCE=<work>
unreal-qt-tests --gtest_filter='SlangPresetRunner_Test.MatchesLibrashader'`.

## The gallery

```bash
python3 tools/crt-shaders/render.py --slang ... --cli ...     # all 19 presets and all profiles, ~10 min
python3 tools/crt-shaders/gallery.py                          # scratch/crt-gallery/index.html
```

View at 100 % (`1:1 device pixels`): a scaled picture shows moire, not the mask. Keys: left / right the shader,
up / down the frame, space held shows B, `g` matrix, `w` wipe, `s` one shader.

## Motion

With an unreal-qt running (its WebAPI port and an emulator id, e.g. from `GET /api/v1/emulator`):

```bash
python3 tools/crt-shaders/clips.py --port 8090 --emulator <id>   # clips/edge, clips/aleste, clips/tape
python3 tools/crt-shaders/render_clips.py --slang ... --cli ...  # video/<clip>/<key>.webm, hours for all presets
python3 tools/crt-shaders/videos.py                              # videos.html
python3 tools/crt-shaders/make_demo.py                           # demo/crt-shaders-demo.webm / .mp4
```

librashader-cli renders one frame per call: presets with history or feedback passes see every frame on its own, so
their motion here is not quite what the window shows. The window is the reference for motion.

## New frames

`capture.py --port <p> --emulator <id> name:testdata/loaders/sna/<file>.sna:3,0.5` saves the presented frame of a
snapshot after each wait into `<work>/frames`; `scr2png.py out.png file.scr [border]` turns a screen file into a
frame. `render.py --frames <dir>` takes any directory of 352 x 288 PNGs.
