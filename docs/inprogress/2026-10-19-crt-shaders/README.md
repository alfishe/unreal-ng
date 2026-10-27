# CRT shaders: the libretro set in unreal-qt

Status: **complete 2026-10-20** ([DONE.md](DONE.md)). Phases 1-3 and the GPU part of 5 landed 2026-10-19 (all 19 presets in View > CRT Profile and as
`shader:<key>`, see [crt-effects.md](../../features/crt-effects.md)); phase 4 (the seven integer models, CPU = GPU
bit for bit, `CRTBeamModel`) done 2026-10-20; the gallery and comparison tools moved to
[tools/crt-shaders](../../../tools/crt-shaders/README.md) the same day, and the models' CPU path got twice as fast (SIMD, reused rows) and builds the same bits on every compiler (crtmath.h, no FMA contraction). Owner decisions: implement every candidate that survived the gallery and the motion review
(19 presets); the shipped Aperture / Shadow mask / Slot mask / "Megatron" profiles go (their masks are wrong, below);
Basic stays; a shader that cannot run on the CPU in real time is GPU only. Cross-platform compatibility must hold:
Windows (MSVC, MinGW), Linux (gcc, clang), macOS (clang), x86_64 and ARM64.

## Selection

Gallery (10 frames from bright to dark) and motion review (tape loading stripes, a moving checkerboard, a dotted
scroller) of 21 libretro presets rendered by librashader, against the shipped profiles. Dropped by the owner:
`pal-r57shell` (artefacts on vertical edges), `koko-aio-ng` (no point). Kept:

| Preset | Passes | Licence | Backends |
|--------|--------|---------|----------|
| crt-lottes | 1 | public domain | CPU + GPU |
| crt-geom | 1 | GPL-2+ | CPU + GPU |
| crt-easymode | 1 | GPL | CPU + GPU |
| crt-gdv-mini (guest) | 1 | GPL-2+ | CPU + GPU |
| crt-consumer | 1 | GPL-2+ | CPU + GPU |
| crt-pi | 1 | GPL-2+ | CPU + GPU |
| zfast-crt | 1 | GPL-2+ | CPU + GPU |
| crt-lottes multipass glow | 7 | public domain | GPU |
| crt-hyllian | 5 | check | GPU |
| crt-beans RGB | 12 | MIT | GPU |
| crt-guest-advanced | 12 | GPL-2+ | GPU |
| crt-guest-advanced fast | 9 | GPL-2+ | GPU |
| crt-guest-advanced PAL | 19 | GPL-2+ | GPU |
| guest-advanced PAL composite (decoupled) | 10 | GPL-2+ | GPU |
| crt-royale | 12 | GPL-2+ | GPU |
| crt-yah | 13 | check | GPU |
| crt-yo6 KV-M1420B | 3 | check | GPU |
| newpixie-crt | 4 | public domain | GPU |
| Sony Megatron v2 (SDR) | 2 | check | GPU |

## Why the shipped masks look wrong

Measured on a flat light area at 4x (1408 px wide):

- the mask pitch is `width / 640` = 2.2 px: stripes of 2 and 3 px alternate, the triad is 6.6 px and beats with the
  4-px source pixel - the wide vertical bands, worst in "Megatron" (strength 0.7: 253 against 111);
- the shadow mask puts three dots into a 2.2 x 2.2 cell under a round falloff: no pattern survives (a flat 114), only a
  darkening and noise;
- the mask is applied in gamma space without compensation: the picture drops from a mean of 143 to 101-118.

Good shaders use an integer mask at device pixels (one pixel per phosphor), in linear light, with the lost brightness
given back.

## Architecture

### GPU: a slang preset runner on our OpenGL window

The presets are RetroArch slang (Vulkan GLSL + a `.slangp` pass list). They are not compiled at run time:

- **Offline**: `tools/crt-shaders/import.py` (librashader-cli `parse`, `transpile`, `reflect`) turns every preset into
  `data/shaders/crt/<key>/`: per pass GLSL 330 vertex and fragment sources, the reflection (uniform names of the UBO
  and push block members, texture semantics and their bindings), the pass metadata (scale, filter, wrap, float / sRGB
  framebuffer, mipmaps, frame count modulo, alias), the LUT images and the parameter defaults. The output is checked in
  and compiled into the app as a qrc: the build needs no Rust, no SPIRV-Cross, nothing new on any platform.
- **Run time**: `SlangPresetRunner` (C++, Qt OpenGL functions only): per pass an FBO of its scale, the semantics
  `Original`, `Source`, `OriginalHistory#`, `PassOutput#`, `PassFeedback#`, aliases, LUTs; the uniforms `MVP`,
  `OutputSize`, `OriginalSize`, `SourceSize`, `FinalViewportSize`, `FrameCount`, `FrameDirection` and the parameters.

GLSL 1.20 is not enough: the transpiled passes use `uint`, `textureSize`, `texelFetch`, float framebuffers. The GL
window moves to a 3.3 core context (4.1 core on macOS, whose compatibility context stops at 2.1); the existing
composition shader and the user's GLSL 1.20 custom shaders get a small compatibility prelude (`in`/`out`,
`texture2D` -> `texture`, an output variable). Where no 3.3 context exists the window falls back to the software
screen, as it does today without OpenGL.

Optional later: a `300 es` set from the same import for GLES-only Linux boards.

### CPU + GPU, bit for bit: integer models of the single-pass shaders

The seven single-pass shaders get an integer model in the `CRTRenderer` style, which both backends execute, so the two
pictures stay identical (the requirement from the previous round). The pipeline, per output pixel:

1. linearise the source through a LUT (8 bit -> 16 bit linear);
2. horizontal filter: 2-4 taps, the taps and weights per output column from a table (Lanczos, gaussian, the shader's
   kernel), fixed point;
3. vertical beam: 2 rows, the weight from a LUT indexed by the row phase and the brightness (beam width by
   brightness: geom, lottes, gdv-mini);
4. the phosphor mask and the geometry factor per output pixel from a table (integer pitch);
5. back to gamma through a LUT (16 bit -> 8 bit).

Every product is an integer below 2^24, exact in a float: the GPU shader does the same arithmetic on texture fetches
and cannot round differently. Float math only builds the tables (once per size and profile), which both backends
read; the CPU runs in rows on `std::thread` as today. No SIMD intrinsics are needed for the first version, so no
per-compiler code. Each model is validated against the original shader rendered by librashader on the gallery
frames (mean difference, maximum, a visual check).

The original GLSL of these seven also goes through the runner, as the reference and for an A/B.

### What goes

`CRTProfile::Aperture`, `ShadowMask`, `SlotMask`, `Megatron`, their `CRTMaskType` code, the automation words
`aperture`, `shadow-mask`, `slot-mask`, `megatron`, `data/shaders/megatron.glsl`. Basic and None stay. The automation
words of the new profiles are the preset keys (`crt-lottes`, `guest-advanced`, ...); `shader:<name>` stays for user
shaders.

## Phases

1. Remove the broken profiles (code, automation, tests, docs).
2. GL 3.3 / 4.1 core context with the compatibility prelude; the existing parity check still exact.
3. Importer + runner; the GPU-only presets, each compared with its librashader render (same frame, same size).
4. Integer models of the seven single-pass shaders, CPU = GPU bit for bit, close to the original.
5. Menu, automation (WebAPI / CLI / Python / Lua / MCP), docs, the gallery and demo tools in `tools/crt-shaders/`.

Each phase builds on macOS, Linux (docker gcc) and Windows (MSVC + MinGW on the Windows host) before it lands.
