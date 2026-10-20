# CRT effects in the Qt window

View > CRT Effects and View > CRT Profile draw the picture like a CRT: scanlines, curvature, rounded corners, vignette,
bloom, phosphor masks and colour grading. The effects work on both screen backends, the GPU one (View > GPU
Acceleration, `DeviceScreenGLWindow`) and the software one (`DeviceScreen`), and **the two pictures are the same bit
for bit**. Background ideas (phosphor persistence, offline filters): [crt-phosphor-effect.md](crt-phosphor-effect.md).

## Profiles

| Profile | Automation word | What it draws |
|---------|-----------------|---------------|
| None | `none` | The plain picture |
| Basic | `basic` | Scanlines, slight curvature, a little bloom |
| CRT Lottes | `crt-lottes` | Model of crt-lottes: gaussian beams, shadow mask, curvature, bloom |
| CRT Geom | `crt-geom` | Model of crt-geom: lanczos, beams wider when bright, curved tube, aperture mask |
| CRT Easymode | `crt-easymode` | Model of crt-easymode: sharp lanczos, scanlines by brightness, flat |
| CRT GDV Mini | `crt-gdv-mini` | Model of crt-gdv-mini: beams by brightness, aperture mask |
| CRT Consumer | `crt-consumer` | Model of crt-consumer: a soft consumer TV, glow, curvature |
| CRT Pi | `crt-pi` | Model of crt-pi: light scanlines, RGB mask, curvature |
| zfast CRT | `zfast-crt` | Model of zfast-crt: fast scanlines and a fine mask |

The parameters are in `unreal-qt/src/widgets/crtprofiles.cpp`. The phosphor mask profiles (Aperture, Shadow Mask, Slot
Mask, "Megatron") were removed on 2026-10-19: their mask pitch was fractional (2.2 px at 4x, stripes beating with the
source pixels), the shadow mask showed no pattern, and the mask darkened the picture in gamma space. The libretro
shaders replacing them: [docs/inprogress/2026-10-19-crt-shaders](../inprogress/2026-10-19-crt-shaders/README.md).

## One model for both backends

`unreal-qt/src/widgets/crtrenderer.{h,cpp}` (`CRTRenderer`) is the only definition of None and Basic (the shader models
below have their own, shared the same way):

1. **Grading**, once per frame, at the source's resolution, in integers: bloom (a separable 1-4-6-4-1 blur, taps
   `bloomRadius` apart, added by `bloomStrength`), gamma, saturation (luma 77/150/29), contrast and brightness.
2. **Geometry tables**, once per output size and profile: for every output pixel (device pixels) the source pixel it
   shows (curvature, the viewport crop) and a per-channel factor (scanline, corners, vignette; 128 = 1.0). Float math here is fine: the tables are integers both backends read, and every platform builds the same ones (below).
3. **Composition**, per output pixel and channel: `min(255, graded * factor / 128)`.

The software screen composes on the CPU (rows in parallel) and draws the image 1:1 at device pixels. The GPU screen
runs on an OpenGL 3.3 core context (4.1 core on macOS); without one (or on a software rasteriser such as llvmpipe)
the window uses the software screen. It uploads the graded frame and the two tables as nearest-sampled textures and runs `CRTRenderer::kCompositionShader`
over a viewport of exactly the picture's device pixels. Every value in the shader's product is an integer below 2^24,
exact in a float, so the GPU cannot round differently (`CRTRenderer_Test.TheShaderArithmeticIsTheIntegerComposition`
checks all 65536 pairs).

## Shader models (CPU and GPU)

The seven single-pass libretro shaders also exist as integer models, `CRTBeamModel`
(`unreal-qt/src/widgets/crtbeammodel.{h,cpp}`, the shapes in `crtbeamshapes.cpp`), on both backends and the same bit
for bit: the software screen composes with `CRTBeamModel::compose()`, the GPU screen uploads the same tables as integer
textures and runs `CRTBeamModel::kShader` (`CRTBeamModelGPU`), which does the same integer arithmetic. One pipeline
with per-model tables (float math builds them once per output size and profile):

1. once per frame: the source through the input table (14 bits; linear light, or the shader's own space);
2. per output pixel from the geometry table: the source pixel and the beam's phase (1/128 pixel), curvature and
   offsets baked in; past the tube black;
3. horizontal: four taps, per-phase and per-channel weights (gaussian, lanczos, linear, sharpened), clamped;
4. vertical: two beams weighted by distance and value (or the row's brightness), or an interpolation of up to four
   rows followed by a per-channel factor from phase, brightness and mask state;
5. a gain from the brightest channel, the glow (crt-lottes' bloom, crt-consumer's squared glow) summed around the
   beam position, the per-pixel factor (mask, corners), the output table back to 8 bits.

On the CPU the sums take the four channels at once (NEON, SSE2, plain C++ elsewhere: the same integers, so the same
picture), an output pixel reuses the filtered rows of the output pixel above it when it shows the same source column
and phase, and the glow's columns are a table per frame (rows, then columns, then x phases). A 352x288 frame at
1408x1152 costs 27-80 ms of CPU time over the threads, half of what the plain per-pixel code took (2026-10-20).

**The same bits on every platform.** The tables are built in floats, and two things would make them differ between
compilers by one here and there (a different picture on Windows, Linux and macOS, arm64 and x86): the C libraries'
`pow`, `exp2`, `sin`, `cos`, `acos` differ in the last bit, and clang and gcc fuse a multiply and an add into one
instruction on arm64 (FMA, one rounding instead of two). So the tables use `unreal-qt/src/widgets/crtmath.h` (those
functions from `+ - * /` and `sqrt` only, in doubles: the correctly rounded float almost always), and
`unreal-qt/CMakeLists.txt` builds `crtrenderer.cpp`, `crtbeammodel.cpp` and `crtbeamshapes.cpp` with
`-ffp-contract=off` (`/fp:precise` on MSVC). The rest is integers. Checked on 2026-10-20: clang arm64 and x86_64
(macOS), gcc 13 arm64 and x86_64 (Linux), MSVC 2019 and MinGW (Windows) give the same tables and pictures for all
nine profiles at five sizes on the ten gallery frames; `*_Test.TheSameBitsOnEveryCompiler` keeps the hashes.

Against librashader on the gallery frames (352x288 at 1408x1152) the models differ by a mean of 0.06-0.28 / 255
(zfast-crt 0.06, crt-easymode 0.07, crt-lottes 0.10, crt-gdv-mini 0.15, crt-geom 0.22, crt-consumer 0.22, crt-pi
0.28). The original presets stay under their `shader:<key>` names (GPU only) for an A/B. The gallery, the clips and the
comparison: [tools/crt-shaders](../../tools/crt-shaders/README.md) (`render.py`, `compare.py`).

## Shader presets (GPU only)

View > CRT Profile lists 19 libretro slang presets under the built-in profiles; the automation word is
`shader:<key>`:

| Key | libretro preset | Passes |
|-----|-----------------|--------|
| `crt-lottes` | crt/crt-lottes | 1 |
| `crt-lottes-glow` | crt/crt-lottes-multipass-glow | 7 |
| `crt-geom` | crt/crt-geom | 1 |
| `crt-easymode` | crt/crt-easymode | 1 |
| `crt-hyllian` | crt/crt-hyllian | 5 |
| `crt-gdv-mini` | crt/crt-gdv-mini | 1 |
| `guest-advanced` | crt/crt-guest-advanced | 12 |
| `guest-advanced-fast` | crt/crt-guest-advanced-fast | 9 |
| `guest-pal` | crt/crt-guest-advanced-pal | 19 |
| `guest-pal-composite` | pal/.../decoupled-guest-advanced-pal 2-Composite | 10 |
| `crt-beans-rgb` | crt/crt-beans-rgb | 12 |
| `megatron-v2-sdr` | hdr/crt-sony-megatron-v2-default-sdr (its mask is made for 4K; below it beats) | 2 |
| `crt-royale` | crt/crt-royale | 12 |
| `crt-yah` | crt/crt-yah | 13 |
| `crt-yo6-pvm` | crt/crt-yo6-KV-M1420B | 3 |
| `crt-consumer` | crt/crt-consumer | 1 |
| `crt-pi` | crt/crt-pi | 1 |
| `zfast-crt` | crt/zfast-crt | 1 |
| `newpixie` | crt/newpixie-crt | 4 |

`tools/crt-shaders/import.py` turns each preset into `data/shaders/crt/<key>/` with librashader-cli: `preset.json`
(the passes' scale, filter, wrap, framebuffer format, aliases, the LUTs, the parameters with the preset's values),
every pass as GLSL 330 (`pass<N>.vert` / `.frag`, the uniform blocks flattened to plain uniforms, arrays of arrays
flattened for macOS's GL 4.1) and the LUT images; `data/shaders/crt/SOURCE` names the slang-shaders revision. The
application carries them as resources (`:/crt/<key>/`). `SlangPresetRunner` (`unreal-qt/src/widgets/slangpresetrunner.{h,cpp}`)
runs them on the window's context with the slang semantics: `Original`, `Source`, `OriginalHistory#`, `PassOutput#`,
`PassFeedback#`, aliases and `<alias>Feedback`, LUTs, the `*Size` uniforms, `MVP`, `FrameCount` (with
`frame_count_mod`), `FrameDirection`, `FrameTimeDelta`, `OriginalFPS` and the parameters. A pass's output is sampled
with that pass's filter and wrap (librashader's rule); the last pass draws at the picture's device pixels into the
window, opaque, whatever its own framebuffer flags say (as librashader). The presets are arbitrary multi-pass code:
no software twin, greyed out without the GPU; switching the GPU off shows Basic.

On the same frame and size the window draws what librashader draws: `SlangPresetRunner_Test.MatchesLibrashader`
(`UNREAL_CRT_REFERENCE=<dir>` with `librashader-cli render` output) gives a mean difference of at most 0.08 / 255 on
macOS (Metal-backed GL 4.1) and 1.8 / 255 on Mesa (Intel); live window grabs of all 19 match `librashader-cli render
-r metal` of the same frame to 0.01 / 255.

## Custom shaders (GPU only)

A GLSL 1.20 or GLSL 330 fragment shader can replace the built-in effect: View > CRT Profile lists the shaders shipped with the app
(resources under `:/shaders`; none at the moment) and the user's own (`*.glsl`, `*.frag`, `*.fs` in the first existing of `<app>/../data/shaders`, `<app>/data/shaders`,
`<config>/shaders`; a user file replaces a shipped one of the same name). The shader gets `varying vec2 texCoord`
and the uniforms `tex`, `texSize`, `outputSize` (device pixels), `maskType` (0 none, 1 aperture, 2 shadow, 3 slot),
`curvature`, `cornerRadius`, `scanlineWeight`, `maskStrength`, `maskDotPitch`, `bloomStrength`, `saturation`,
`brightness`, `contrast`, `gamma` - the current profile's values, Basic's when the profile is None; the mask uniforms
are 0 (no profile has a mask). A GLSL 1.20 shader (or one without `#version`) gets a prelude for the core context:
`varying` becomes `in`, `texture2D` `texture`, `gl_FragColor` an output of the prelude (compiler messages count two
lines more); a `#version 330` or later shader declares `in vec2 texCoord` and its own output. A custom shader
is arbitrary code: it has no software twin, its entries are greyed out without the GPU, and switching the GPU off shows
its base profile.

## Automation

The window's display settings are on every surface (WebAPI `GET/PUT /api/v1/emulator/{id}/video/window`, CLI
`video window`, Python / Lua `window_display()`, MCP `capture_media` `window_display`): `crt`, `crt_profile` (the
words above, or `shader:<name>` for a shader preset or a custom shader with the GPU on), `gpu`. They are the window's, not the machine's:
changing them needs no control session. A screenshot with `source=window` shows the result; on the same window size
the GPU and the software grab are the same image.

## Verification

- `unreal-qt-tests --gtest_filter='CRTRenderer*'`: every profile shows the picture, full white stays bright with
  contrast, a profile without effects is a nearest scale, the shader arithmetic, the tables rebuilt only when needed,
  the tables' and the picture's hash (the same on every compiler).
- `unreal-qt-tests --gtest_filter='CRTBeamModel*'`: every model shows the picture, the tables rebuilt only when
  needed, the GPU picture is the software one bit for bit (two layouts, all seven models), the tables' and the
  picture's hash (the same on every compiler); with `UNREAL_CRT_REFERENCE` the comparison with librashader.
- `unreal-qt-tests --gtest_filter='SlangPresetRunner*'`: every preset compiles and shows the picture, the window's
  composition shader is the CPU picture; with `UNREAL_CRT_REFERENCE` the comparison with librashader.
- Live: grab `source=window` with `gpu` on and off for every profile and compare the images.
